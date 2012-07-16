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
    } while ( AO_fetch_compare_and_swap_release(&g_instance.lowid_counter, old_id, new_id) == old_id );

    return new_id;
}

client_t *client_new()
{
    client_t *client = (client_t*)malloc(sizeof(client_t));
    memset(client, 0, sizeof(client_t));
    AO_fetch_and_add1(&g_instance.user_count);
    pthread_mutex_init(&client->job_mutex, NULL);
    STAILQ_INIT(&client->jqueue);

    return client;
}

void client_delete( client_t *client )
{
    ED2KD_LOGDBG("client removed (%s:%d)", client->dbg.ip_str, client->port);

    if( client->bev_cli ) bufferevent_free(client->bev_cli);
    if( client->bev_srv ) bufferevent_free(client->bev_srv);

    db_remove_source(client);

    free(client);

    AO_fetch_and_sub1(&g_instance.user_count);
}

void send_id_change( client_t *client )
{
    struct packet_id_change data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof(struct packet_header);
    data.opcode = OP_IDCHANGE;
    data.user_id = client->id;
    data.tcp_flags = g_instance.cfg->srv_tcp_flags;

    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_server_message( client_t *client, const char *msg, uint16_t len )
{
    struct packet_server_message data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof(struct packet_header) + len;
    data.opcode = OP_SERVERMESSAGE;
    data.msg_len = len;

    bufferevent_write(client->bev_srv, &data, sizeof data);
    bufferevent_write(client->bev_srv, msg, len);
}

void send_server_status( client_t *client )
{
    struct packet_server_status data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof(struct packet_header);
    data.opcode = OP_SERVERSTATUS;
    data.user_count = AO_load(&g_instance.user_count);
    data.file_count = AO_load(&g_instance.file_count);

    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_server_ident( client_t *client )
{
    struct packet_server_ident data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof(struct packet_header);
    data.opcode = OP_SERVERSTATUS;
    memcpy(data.hash, g_instance.cfg->hash, sizeof data.hash);
    data.ip = g_instance.cfg->listen_addr_inaddr;
    data.port = g_instance.cfg->listen_port;
    data.tag_count = (g_instance.cfg->server_name_len>0) + (g_instance.cfg->server_descr_len>0);

    if ( data.tag_count > 0 ) {
        struct evbuffer *buf = evbuffer_new();
        evbuffer_add(buf, &data, sizeof data);

        if ( g_instance.cfg->server_name_len > 0 ) {
            struct tag_header th;
            size_t data_len = sizeof(uint16_t) + g_instance.cfg->server_name_len;
            unsigned char *data = (unsigned char*)alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_SERVERNAME;
            *(uint16_t*)data = g_instance.cfg->server_name_len;
            memcpy(data+sizeof(uint16_t), g_instance.cfg->server_name, g_instance.cfg->server_name_len);

            evbuffer_add(buf, &th, sizeof th);
            evbuffer_add(buf, data, data_len);
        }

        if ( g_instance.cfg->server_descr_len > 0 ) {
            struct tag_header th;
            size_t data_len = sizeof(uint16_t) + g_instance.cfg->server_descr_len;
            unsigned char *data = (unsigned char*)alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_DESCRIPTION;
            *(uint16_t*)data = g_instance.cfg->server_descr_len;
            memcpy(data+sizeof(uint16_t), g_instance.cfg->server_descr, g_instance.cfg->server_descr_len);

            evbuffer_add(buf, &th, sizeof th);
            evbuffer_add(buf, data, data_len);
        }

        {
            struct packet_header *ph = (struct packet_header*)evbuffer_pullup(buf, sizeof(struct packet_header));
            ph->length = evbuffer_get_length(buf) - sizeof(struct packet_header);
        }

        bufferevent_write_buffer(client->bev_srv, buf);
        evbuffer_free(buf);
    } else {
        bufferevent_write(client->bev_srv, &data, sizeof data);
    }
}

void send_server_list( client_t *client )
{
    // TODO: implement
}

void send_search_result( client_t *client, search_node_t *search_tree )
{
    size_t count = MAX_SEARCH_FILES;
    struct evbuffer *buf = evbuffer_new();
    struct packet_search_result data;

    data.hdr.proto = PROTO_EDONKEY;
    //data.length = 0;
    data.opcode = OP_SEARCHRESULT;
    //data.files_count = 0;
    evbuffer_add(buf, &data, sizeof data);

    if ( db_search_file(search_tree, buf, &count) >= 0 ) {
        struct packet_search_result *ph = (struct packet_search_result*)evbuffer_pullup(buf, sizeof(struct packet_search_result));
        ph->hdr.length = evbuffer_get_length(buf) - sizeof(struct packet_header);
        ph->files_count = count;

        bufferevent_write_buffer(client->bev_srv, buf);
        evbuffer_free(buf);
    }
}

void write_search_file( struct evbuffer *buf, const struct search_file *file )
{
    struct search_file_entry sfe;

    memcpy(sfe.hash, file->hash, sizeof sfe.hash);
    sfe.id = file->client_id;
    sfe.port = file->client_port;
    sfe.tag_count = 1+ 1+ /*(0!=file->type)+*/ (file->ext_len>0)+ 1+ 1+ (file->rated_count>0)+ (file->media_length>0)+ (file->media_length>0)+ (file->media_codec_len>0);
    evbuffer_add(buf, &sfe, sizeof sfe);

    {
        struct tag_header th;
        size_t data_len = sizeof(uint16_t)+file->name_len;
        unsigned char *data = (unsigned char*)alloca(data_len);

        th.type = TT_STRING;
        th.name_len = 1;
        *th.name = TN_FILENAME;
        *(uint16_t*)data = file->name_len;
        memcpy(data+sizeof(uint16_t), file->name, file->name_len);

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, data, data_len);
    }

    {
        struct tag_header th;
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
        struct tag_header th;
        size_t data_len = sizeof(uint16_t)+file->ext_len;
        unsigned char *data = (unsigned char*)alloca(data_len);

        th.type = TT_STRING;
        th.name_len = 1;
        *th.name = TN_FILEFORMAT;
        *(uint16_t*)data = file->ext_len;
        memcpy(data+sizeof(uint16_t), file->ext, file->ext_len);

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, data, data_len);
    }

    {
        struct tag_header th;
        th.type = TT_UINT32;
        th.name_len = 1;
        *th.name = TN_SOURCES;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->srcavail, sizeof file->srcavail);
    }

    {
        struct tag_header th;
        th.type = TT_UINT32;
        th.name_len = 1;
        *th.name = TN_COMPLETE_SOURCES;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->srccomplete, sizeof file->srccomplete);
    }

    if ( file->rated_count > 0 ) {
        uint16_t data;
        struct tag_header th;
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
        uint16_t name_len = sizeof(TNS_MEDIA_LENGTH)-1;
        size_t th_len = sizeof(struct tag_header)-1+name_len;
        struct tag_header *th = (struct tag_header*)alloca(th_len);
        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_LENGTH, name_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, &file->media_length, sizeof file->media_length);
    }

    if ( file->media_bitrate ) {
        uint16_t name_len = sizeof(TNS_MEDIA_BITRATE)-1;
        size_t th_len = sizeof(struct tag_header)-1+name_len;
        struct tag_header *th = (struct tag_header*)alloca(th_len);
        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_BITRATE, name_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, &file->media_bitrate, sizeof file->media_bitrate);
    }

    if ( file->media_codec_len ) {
        uint16_t name_len = sizeof(TNS_MEDIA_CODEC)-1;
        size_t th_len = sizeof(struct tag_header)-1+name_len;
        struct tag_header *th = (struct tag_header*)alloca(th_len);

        size_t data_len = sizeof(uint16_t)+file->media_codec_len;
        unsigned char *data = (unsigned char*)alloca(data_len);

        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_CODEC, name_len);

        *(uint16_t*)data = file->media_codec_len;
        memcpy(data+sizeof(uint16_t), file->media_codec, file->media_codec_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, data, data_len);
    }
}

void send_found_sources( client_t *client, const unsigned char *hash )
{
    uint8_t src_count = MAX_FOUND_SOURCES;
    file_source_t sources[MAX_FOUND_SOURCES];
    struct packet_found_sources data;

    db_get_sources(hash, sources, &src_count);

    data.hdr.proto = PROTO_EDONKEY;
    memcpy(data.hash, hash, sizeof data.hash);
    data.hdr.length = sizeof data - sizeof(struct packet_header) + src_count*sizeof(sources[0]);
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
            client_delete(client);
            return;
        }
    } else {
        client->id = client->ip;
    }

    send_id_change(client);
}
