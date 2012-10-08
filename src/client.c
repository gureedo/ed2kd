#include "client.h"
#include <string.h>
#ifdef __GNUC__
#include <alloca.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include "ed2k_proto.h"
#include "server.h"
#include "packet.h"
#include "version.h"
#include "log.h"
#include "db.h"
#include "util.h"

struct shared_file_entry {
        /* key */
        unsigned char hash[ED2K_HASH_SIZE];
        /* makes this structure hashable */
        UT_hash_handle hh;
};

static uint32_t get_next_lowid()
{
        atomic32_t old_id, new_id;

        do {
                old_id = atomic_load(&g_srv.lowid_counter);
                new_id = old_id + 1;
                if ( new_id > MAX_LOWID )
                        new_id = 1;
        } while ( !atomic_cas(&g_srv.lowid_counter, new_id, old_id) );

        return new_id;
}

struct client *client_new()
{
        struct client *clnt = (struct client*)calloc(1, sizeof(*clnt));

        if ( atomic_inc(&g_srv.user_count)+1 >= g_srv.cfg->max_clients ) {
                evconnlistener_disable(g_srv.tcp_listener);
        }

        token_bucket_init(&clnt->limit_offer, g_srv.cfg->max_offers_limit);
        token_bucket_init(&clnt->limit_search, g_srv.cfg->max_searches_limit);

        return clnt;
}

void client_delete( struct client *clnt )
{
        struct shared_file_entry *she, *she_tmp;

        if ( 0 == atomic_store(&clnt->deleted, 1) ) {
                // disable all events
                if ( clnt->bev )
                        bufferevent_disable(clnt->bev, EV_READ|EV_WRITE);
                if ( clnt->bev_pc )
                        bufferevent_disable(clnt->bev_pc, EV_READ|EV_WRITE);
                if ( clnt->evtimer_status_notify )
                        event_del(clnt->evtimer_status_notify);
                if ( clnt->evtimer_portcheck )
                        event_del(clnt->evtimer_portcheck);

                // delete all events
                if ( clnt->evtimer_status_notify ) {
                        event_free(clnt->evtimer_status_notify);
                        clnt->evtimer_status_notify = NULL;
                }
                if ( clnt->evtimer_portcheck ) {
                        event_free(clnt->evtimer_portcheck);
                        clnt->evtimer_portcheck = NULL;
                }
                if ( clnt->bev_pc ) {
                        bufferevent_free(clnt->bev_pc);
                        clnt->bev_pc = NULL;
                }
                if ( clnt->bev ) {
                        bufferevent_free(clnt->bev);
                        clnt->bev = NULL;
                }

                ED2KD_LOGDBG("client removed (%s:%d)", clnt->dbg.ip_str, clnt->port);

                if ( clnt->file_count ) {
                        db_remove_source(clnt);
                        clnt->file_count = 0;
                }

                HASH_ITER(hh, clnt->shared_files, she, she_tmp) {
                        HASH_DEL(clnt->shared_files, she);
                        free(she);
                }

                atomic_sub(&g_srv.file_count, clnt->file_count);

                if ( atomic_dec(&g_srv.user_count)-1 < g_srv.cfg->max_clients ) {
                        evconnlistener_enable(g_srv.tcp_listener);
                }
        }

        if ( 0 == atomic_load(&clnt->ref_cnt) ) {
                free(clnt);
        }
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

        if ( db_search_files(search_tree, buf, &count) ) {
                struct packet_search_result *ph = (struct packet_search_result*)evbuffer_pullup(buf, sizeof(*ph));
                ph->hdr.length = evbuffer_get_length(buf) - sizeof(ph->hdr);
                ph->files_count = count;

                bufferevent_write_buffer(clnt->bev, buf);
        }

        evbuffer_free(buf);
}

void client_get_sources( struct client *clnt, const unsigned char *hash )
{
        struct file_source sources[MAX_FOUND_SOURCES];
        uint8_t src_count = ARRAY_SIZE(sources);

        if (  db_get_sources(hash, sources, &src_count) )
                send_found_sources(clnt->bev, hash, sources, src_count);
}

void client_portcheck_start( struct client *clnt )
{
        struct sockaddr_in client_sa;

        memset(&client_sa, 0, sizeof(client_sa));
        client_sa.sin_family = AF_INET;
        client_sa.sin_addr.s_addr = clnt->ip;
        client_sa.sin_port = htons(clnt->port);

        clnt->bev_pc = bufferevent_socket_new(g_srv.evbase_tcp, -1, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
        bufferevent_setcb(clnt->bev_pc, portcheck_read_cb, NULL, portcheck_event_cb, clnt);

        if ( bufferevent_socket_connect(clnt->bev_pc, (struct sockaddr*)&client_sa, sizeof(client_sa)) < 0 ) {
                bufferevent_free(clnt->bev_pc);
                clnt->bev_pc = NULL;
                client_portcheck_finish(clnt, PORTCHECK_FAILED);
        } else {
                clnt->evtimer_portcheck = evtimer_new(g_srv.evbase_tcp, portcheck_timeout_cb, clnt);
                evtimer_add(clnt->evtimer_portcheck, g_srv.portcheck_timeout_tv);
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
                static const char msg[] = "WARNING: You have a lowid. Please review your network config and/or your settings.";
                send_server_message(clnt->bev, msg, sizeof(msg) - 1);
                ED2KD_LOGDBG("port check failed (%s:%d)", clnt->dbg.ip_str, clnt->port);
        }

        if ( clnt->lowid ) {
                if ( g_srv.cfg->allow_lowid ) {
                        clnt->id = get_next_lowid();
                        clnt->port = 0;
                } else {
                        client_delete(clnt);
                        return;
                }
        } else {
                clnt->id = clnt->ip;
        }

        send_id_change(clnt->bev, clnt->id);

        clnt->evtimer_status_notify = evtimer_new(g_srv.evbase_tcp, server_status_notify_cb, clnt);
        event_add(clnt->evtimer_status_notify, g_srv.status_notify_tv);
}

void client_share_files( struct client *clnt, struct pub_file *files, size_t count )
{
        size_t i, real_count = 0;
        struct pub_file *f = files;

        if ( clnt->file_count > g_srv.cfg->max_files_per_client ) {
                static const char msg[] = "WARNING: You reached shared files limit";
                send_server_message(clnt->bev, msg, sizeof(msg) - 1);
                return;
        }

        if ( atomic_load(&g_srv.file_count) > g_srv.cfg->max_files ) {
                static const char msg[] = "WARNING: Server reached shared files limit";
                send_server_message(clnt->bev, msg, sizeof(msg) - 1);
                return;
        }

        for ( i=0; i<count; ++i ) {
                struct shared_file_entry *she = NULL;
                HASH_FIND(hh, clnt->shared_files, f->hash, sizeof(f->hash), she);
                if( !she ) {
                        she = (struct shared_file_entry *)malloc(sizeof(*she));
                        memcpy(she->hash, f->hash, sizeof(she->hash));
                        HASH_ADD(hh, clnt->shared_files, hash, sizeof(she->hash), she);
                        real_count++;
                } else {
                        // mark as invalid
                        f->name_len = 0;
                }

                f++;
        }

        if ( db_share_files(files, count, clnt) ) {
                ED2KD_LOGDBG("client %u: published %u files, %u duplicates", clnt->id, count, count-real_count);
                clnt->file_count += real_count;
                atomic_add(&g_srv.file_count, real_count);
        }
}
