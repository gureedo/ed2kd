#include "client.h"
#include <string.h>
#ifdef __GNUC__
#include <alloca.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "ed2k_proto.h"
#include "server.h"
#include "packet.h"
#include "event_callback.h"
#include "version.h"
#include "log.h"
#include "db.h"

static
uint32_t get_next_lowid()
{
    AO_t old_id, new_id;

    do {
        old_id = AO_load(&g_instance.lowid_counter);
        new_id = old_id + 1;
        if ( new_id > MAX_LOWID )
            new_id = 0;
    } while ( !AO_compare_and_swap(&g_instance.lowid_counter, old_id, new_id) );

    return new_id;
}

struct client *client_new()
{
    struct client *client = (struct client*)calloc(1, sizeof(*client));
    AO_fetch_and_add1(&g_instance.user_count);
    pthread_mutex_init(&client->job_mutex, NULL);
    STAILQ_INIT(&client->jqueue);

    return client;
}

void client_schedule_delete( struct client *clnt )
{
    clnt->sched_del = 1;
}

void client_delete( struct client *clnt )
{
    assert(clnt->sched_del);
    ED2KD_LOGDBG("client removed (%s:%d)", clnt->dbg.ip_str, clnt->port);

    if( clnt->bev_pc )
        bufferevent_free(clnt->bev_pc);
    if( clnt->bev_srv )
        bufferevent_free(clnt->bev_srv);
    if ( clnt->file_count )
        db_remove_source(clnt);

    server_remove_client_jobs(clnt);
    
    {
        struct job *j_tmp, *j = STAILQ_FIRST(&clnt->jqueue);
        while ( j != NULL ) {
            j_tmp = STAILQ_NEXT(j, qentry);
            free(j);
            j = j_tmp;
        }

    }
    
    free(clnt);

    AO_fetch_and_sub1(&g_instance.user_count);
}

void client_search_files( struct client *clnt, struct search_node *search_tree )
{
    size_t count = MAX_SEARCH_FILES;
    struct evbuffer *buf = evbuffer_new();
    struct packet_search_result data;

    data.hdr.proto = PROTO_EDONKEY;
    //data.length = 0;
    data.opcode = OP_SEARCHRESULT;
    //data.files_count = 0;
    evbuffer_add(buf, &data, sizeof(data));

    if ( db_search_file(search_tree, buf, &count) >= 0 ) {
        struct packet_search_result *ph = (struct packet_search_result*)evbuffer_pullup(buf, sizeof(*ph));
        ph->hdr.length = evbuffer_get_length(buf) - sizeof(ph->hdr);
        ph->files_count = count;

        bufferevent_write_buffer(clnt->bev_srv, buf);
    }

    evbuffer_free(buf);
}

void client_get_sources( struct client *clnt, const unsigned char *hash )
{
    struct file_source sources[MAX_FOUND_SOURCES];
    uint8_t src_count = sizeof(sources)/sizeof(sources[0]);

    db_get_sources(hash, sources, &src_count);

    send_found_sources(clnt->bev_srv, hash, sources, src_count);
}

void client_portcheck_start( struct client *clnt )
{
    struct sockaddr_in client_sa;

    memset(&client_sa, 0, sizeof(client_sa));
    client_sa.sin_family = AF_INET;
    client_sa.sin_addr.s_addr = clnt->ip;
    client_sa.sin_port = htons(clnt->port);

    clnt->bev_pc = bufferevent_socket_new(g_instance.evbase, -1, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    bufferevent_setcb(clnt->bev_pc, portcheck_read_cb, NULL, portcheck_event_cb, clnt);

    if ( bufferevent_socket_connect(clnt->bev_pc, (struct sockaddr*)&client_sa, sizeof(client_sa)) < 0 ) {
        bufferevent_free(clnt->bev_pc);
        clnt->bev_pc = NULL;
        client_portcheck_finish(clnt, PORTCHECK_FAILED);
    } else {
        clnt->evtimer_portcheck = evtimer_new(g_instance.evbase, portcheck_timeout_cb, clnt);
        evtimer_add(clnt->evtimer_portcheck, &g_instance.cfg->portcheck_timeout);
    }
}

void client_portcheck_finish( struct client *clnt, enum portcheck_result result )
{
    if ( clnt->bev_pc ) {
        bufferevent_free(clnt->bev_pc);
        clnt->bev_pc = NULL;
    }
    if ( clnt->evtimer_portcheck ) {
        event_free(clnt->evtimer_portcheck);
        clnt->evtimer_portcheck = NULL;
    }
    clnt->portcheck_finished = 1;
    clnt->lowid = (PORTCHECK_SUCCESS != result);

    if ( clnt->lowid ) {
        static const char msg_lowid[] = "WARNING : You have a lowid. Please review your network config and/or your settings.";
        ED2KD_LOGDBG("port check failed (%s:%d)", clnt->dbg.ip_str, clnt->port);
        send_server_message(clnt->bev_srv, msg_lowid, sizeof(msg_lowid) - 1);
    }

    if ( clnt->lowid ) {
        if ( g_instance.cfg->allow_lowid ) {
            clnt->id = get_next_lowid();
            clnt->port = 0;
        } else {
            client_schedule_delete(clnt);
            return;
        }
    } else {
        clnt->id = clnt->ip;
    }

    send_id_change(clnt->bev_srv, clnt->id);
}
