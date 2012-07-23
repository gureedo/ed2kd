#include "client.h"
#include <math.h>
#include <string.h>
#ifdef __GNUC__
#include <alloca.h>
#endif
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "server.h"
#include "config.h"
#include "ed2k_proto.h"
#include "version.h"
#include "log.h"
#include "db.h"

static uint32_t
get_next_lowid()
{
    AO_t old_id, new_id;

    do {
        old_id = AO_load_acquire(&g_instance.lowid_counter);
        new_id = old_id + 1;
        if ( new_id > 0x1000000 )
            new_id = 0;
    } while ( !AO_compare_and_swap_full(&g_instance.lowid_counter, old_id, new_id) );

    return new_id;
}

client_t *client_new()
{
    client_t *client = (client_t*)calloc(sizeof *client, 1);
    AO_fetch_and_add1(&g_instance.user_count);
    pthread_mutex_init(&client->job_mutex, NULL);
    STAILQ_INIT(&client->jqueue);

    return client;
}

void client_schedule_delete( client_t *clnt )
{
    clnt->sched_del = 1;
}

void client_delete( client_t *clnt )
{
    assert(clnt->sched_del);
    ED2KD_LOGDBG("client removed (%s:%d)", clnt->dbg.ip_str, clnt->port);

    if( clnt->bev_cli )
        bufferevent_free(clnt->bev_cli);
    if( clnt->bev_srv )
        bufferevent_free(clnt->bev_srv);
    if ( clnt->file_count )
        db_remove_source(clnt);

    server_remove_client_jobs(clnt);
    
    {
        job_t *j_tmp, *j = STAILQ_FIRST(&clnt->jqueue);
        while ( j != NULL ) {
            j_tmp = STAILQ_NEXT(j, qentry);
            free(j);
            j = j_tmp;
        }

    }
    
    free(clnt);

    AO_fetch_and_sub1(&g_instance.user_count);
}

void send_id_change( client_t *clnt )
{
    packet_id_change_t data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof data.hdr;
    data.opcode = OP_IDCHANGE;
    data.user_id = clnt->id;
    data.tcp_flags = g_instance.cfg->srv_tcp_flags;

    bufferevent_write(clnt->bev_srv, &data, sizeof data);
}

void send_server_message( client_t *clnt, const char *msg, size_t len )
{
    packet_server_message_t data;

    assert( len <= UINT16_MAX );

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof data.hdr + len;
    data.opcode = OP_SERVERMESSAGE;
    data.msg_len = (uint16_t)len;

    bufferevent_write(clnt->bev_srv, &data, sizeof data);
    bufferevent_write(clnt->bev_srv, msg, len);
}

void send_server_status( client_t *clnt )
{
    packet_server_status_t data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof data.hdr;
    data.opcode = OP_SERVERSTATUS;
    data.user_count = AO_load_acquire(&g_instance.user_count);
    data.file_count = AO_load_acquire(&g_instance.file_count);

    bufferevent_write(clnt->bev_srv, &data, sizeof data);
}

void send_server_ident( client_t *clnt )
{
    packet_server_ident_t data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof data.hdr;
    data.opcode = OP_SERVERSTATUS;
    memcpy(data.hash, g_instance.cfg->hash, sizeof data.hash);
    data.ip = g_instance.cfg->listen_addr_inaddr;
    data.port = g_instance.cfg->listen_port;
    data.tag_count = (g_instance.cfg->server_name_len>0) + (g_instance.cfg->server_descr_len>0);

    if ( data.tag_count > 0 ) {
        struct evbuffer *buf = evbuffer_new();
        evbuffer_add(buf, &data, sizeof data);

        if ( g_instance.cfg->server_name_len > 0 ) {
            tag_header_t th;
            tag_strval_t *tv;
            size_t data_len = sizeof *tv + g_instance.cfg->server_name_len - 1;
            tv = (tag_strval_t *)alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_SERVERNAME;
            tv->len = g_instance.cfg->server_name_len;
            memcpy(tv->str, g_instance.cfg->server_name, g_instance.cfg->server_name_len);

            evbuffer_add(buf, &th, sizeof th);
            evbuffer_add(buf, tv, data_len);
        }

        if ( g_instance.cfg->server_descr_len > 0 ) {
            tag_header_t th;
            tag_strval_t *tv;
            size_t data_len = sizeof *tv + g_instance.cfg->server_descr_len - 1;
            tv = (tag_strval_t *)alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_DESCRIPTION;
            tv->len = g_instance.cfg->server_descr_len;
            memcpy(tv->str, g_instance.cfg->server_descr, g_instance.cfg->server_descr_len);

            evbuffer_add(buf, &th, sizeof th);
            evbuffer_add(buf, tv, data_len);
        }

        {
            packet_header_t *ph = (packet_header_t *)evbuffer_pullup(buf, sizeof *ph);
            ph->length = evbuffer_get_length(buf) - sizeof *ph;
        }

        bufferevent_write_buffer(clnt->bev_srv, buf);
        evbuffer_free(buf);
    } else {
        bufferevent_write(clnt->bev_srv, &data, sizeof data);
    }
}

void send_server_list( client_t *clnt )
{
    (void)clnt;
    // TODO: implement
}

void send_search_result( client_t *client, search_node_t *search_tree )
{
    size_t count = MAX_SEARCH_FILES;
    struct evbuffer *buf = evbuffer_new();
    packet_search_result_t data;

    data.hdr.proto = PROTO_EDONKEY;
    //data.length = 0;
    data.opcode = OP_SEARCHRESULT;
    //data.files_count = 0;
    evbuffer_add(buf, &data, sizeof data);

    if ( db_search_file(search_tree, buf, &count) >= 0 ) {
        packet_search_result_t *ph = (packet_search_result_t*)evbuffer_pullup(buf, sizeof *ph);
        ph->hdr.length = evbuffer_get_length(buf) - sizeof *ph;
        ph->files_count = count;

        bufferevent_write_buffer(client->bev_srv, buf);
    }

    evbuffer_free(buf);
}

void write_search_file( struct evbuffer *buf, const struct search_file *file )
{
    search_file_entry_t sfe;

    memcpy(sfe.hash, file->hash, sizeof sfe.hash);
    sfe.id = file->client_id;
    sfe.port = file->client_port;
    sfe.tag_count = 1+ 1+ /*(0!=file->type)+*/ (file->ext_len>0)+ 1+ 1+ (file->rated_count>0)+ (file->media_length>0)+ (file->media_length>0)+ (file->media_codec_len>0);
    evbuffer_add(buf, &sfe, sizeof sfe);

    {
        tag_header_t th;
        tag_strval_t *tv;
        size_t data_len = sizeof *tv + file->name_len - 1;
        tv = (tag_strval_t *)alloca(data_len);

        th.type = TT_STRING;
        th.name_len = 1;
        *th.name = TN_FILENAME;
        tv->len = file->name_len;
        memcpy(tv->str, file->name, file->name_len);

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, tv, data_len);
    }

    {
        tag_header_t th;
        th.type = TT_UINT64;
        th.name_len = 1;
        *th.name = TN_FILESIZE;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->size, sizeof file->size);
    }

    /*if ( files[i].type ) {
        struct tag_header th;
        th.type = TT_STRING;
        th.name_len = 1;
        *th.name = TN_FILETYPE;
        //len
        //type
    }*/

    if ( file->ext_len ) {
        tag_header_t th;
        tag_strval_t *tv;
        size_t data_len = sizeof *tv + file->ext_len - 1;
        tv = (tag_strval_t *)alloca(data_len);

        th.type = TT_STRING;
        th.name_len = 1;
        *th.name = TN_FILEFORMAT;
        tv->len = file->ext_len;
        memcpy(tv->str, file->ext, file->ext_len);

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, tv, data_len);
    }

    {
        tag_header_t th;
        th.type = TT_UINT32;
        th.name_len = 1;
        *th.name = TN_SOURCES;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->srcavail, sizeof file->srcavail);
    }

    {
        tag_header_t th;
        th.type = TT_UINT32;
        th.name_len = 1;
        *th.name = TN_COMPLETE_SOURCES;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->srccomplete, sizeof file->srccomplete);
    }

    if ( file->rated_count > 0 ) {
        uint16_t data;
        tag_header_t th;
        th.type = TT_UINT16;
        th.name_len = 1;
        *th.name = TN_FILERATING;

        // lo-byte: percentage rated this file
        data = (100 * (uint8_t)((float)file->srcavail/(float)file->rated_count)) << 8;
        // hi-byte: average rating
        data += ((uint16_t)floor((double)file->rating/(double)file->rated_count + 0.5f) * 51) & 0xFF;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &data, sizeof data);
    }

    if ( file->media_length ) {
        tag_header_t *th;
        uint16_t name_len = sizeof(TNS_MEDIA_LENGTH)-1;

        size_t th_len = sizeof *th + name_len - 1;
        th = (tag_header_t *)alloca(th_len);
        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_LENGTH, name_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, &file->media_length, sizeof file->media_length);
    }

    if ( file->media_bitrate ) {
        uint16_t name_len = sizeof(TNS_MEDIA_BITRATE)-1;
        tag_header_t *th;
        size_t th_len = sizeof *th + name_len - 1;
        th = (tag_header_t *)alloca(th_len);
        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_BITRATE, name_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, &file->media_bitrate, sizeof file->media_bitrate);
    }

    if ( file->media_codec_len ) {
        tag_header_t *th;
        tag_strval_t *tv;
        uint16_t name_len = sizeof(TNS_MEDIA_CODEC)-1;
        size_t th_len = sizeof *th + name_len - 1;
        size_t tv_len = sizeof *tv + file->media_codec_len - 1;
        th = (tag_header_t*)alloca(th_len);
        tv = (tag_strval_t*)alloca(tv_len);

        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_CODEC, name_len);

        tv->len = file->media_codec_len;
        memcpy(tv->str, file->media_codec, file->media_codec_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, tv, tv_len);
    }
}

void send_found_sources( client_t *client, const unsigned char *hash )
{
    uint8_t src_count = MAX_FOUND_SOURCES;
    file_source_t sources[MAX_FOUND_SOURCES];
    packet_found_sources_t data;

    db_get_sources(hash, sources, &src_count);

    data.hdr.proto = PROTO_EDONKEY;
    memcpy(data.hash, hash, sizeof data.hash);
    data.hdr.length = sizeof data - sizeof data.hdr + src_count*sizeof(sources[0]);
    data.opcode = OP_FOUNDSOURCES;
    data.count = src_count;
    bufferevent_write(client->bev_srv, &data, sizeof data);
    bufferevent_write(client->bev_srv, sources, src_count*sizeof(sources[0]));
}

void send_reject( client_t *client )
{
    static const char data[] = { PROTO_EDONKEY, 1, 0, 0, 0, OP_REJECT };
    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_callback_fail( client_t *client )
{
    static const char data[] = { PROTO_EDONKEY, 1, 0, 0, 0, OP_CALLBACK_FAIL };
    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void client_portcheck_finish( client_t *client, portcheck_result_t result )
{
    if ( client->bev_cli ) {
        bufferevent_free(client->bev_cli);
        client->bev_cli = NULL;
    }
    client->portcheck_finished = 1;
    client->lowid = (PORTCHECK_SUCCESS != result);

    if ( client->lowid ) {
        static const char msg_lowid[] = "WARNING : You have a lowid. Please review your network config and/or your settings.";
        ED2KD_LOGDBG("port check failed (%s:%d)", client->dbg.ip_str, client->port);
        send_server_message(client, msg_lowid, sizeof msg_lowid - 1);
        // todo:
    }

    if ( client->lowid ) {
        if ( g_instance.cfg->allow_lowid ) {
            client->id = get_next_lowid();
            client->port = 0;
        } else {
            client_schedule_delete(client);
            return;
        }
    } else {
        client->id = client->ip;
    }

    send_id_change(client);
}
