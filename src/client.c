#include <stdint.h>
#include <math.h>
#include <malloc.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "client.h"
#include "ed2kd.h"
#include "config.h"
#include "ed2k_proto.h"
#include "version.h"
#include "log.h"
#include "db.h"

static uint32_t
get_next_lowid()
{
    static uint32_t id;

    if ( id > 0x1000000u ) {
        id = 1;
    } else {
        id++;
    }

    return id;
}

struct e_client *client_new()
{
    struct e_client *client = (struct e_client*)malloc(sizeof(struct e_client));
    memset(client, 0, sizeof(struct e_client));
	ed2kd_rt()->user_count++;

    return client;
}

void client_delete( struct e_client *client )
{
	ED2KD_LOGDBG("client removed (%s:%d)", client->dbg.ip_str, client->port);

    if( client->bev_cli ) bufferevent_free(client->bev_cli);
	if( client->bev_srv ) bufferevent_free(client->bev_srv);

	db_remove_source(client);

    free(client);

	ed2kd_rt()->user_count--;
}

void send_id_change( struct e_client *client )
{
	struct packet_id_change data;

	data.hdr.proto = PROTO_EDONKEY;
	data.hdr.length = sizeof data - sizeof(struct packet_header);
	data.opcode = OP_IDCHANGE;
   	data.user_id = client->id;
	data.tcp_flags = ed2kd_cfg()->srv_tcp_flags;

	bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_server_message( struct e_client *client, const char *msg, uint16_t len )
{
    struct packet_server_message data;

	data.hdr.proto = PROTO_EDONKEY;
	data.hdr.length = sizeof data - sizeof(struct packet_header) + len;
	data.opcode = OP_SERVERMESSAGE;
	data.msg_len = len;

    bufferevent_write(client->bev_srv, &data, sizeof data);
    bufferevent_write(client->bev_srv, msg, len);
}

void send_server_status( struct e_client *client )
{
    struct packet_server_status data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof(struct packet_header);
    data.opcode = OP_SERVERSTATUS;
    data.user_count = ed2kd_rt()->user_count;
    data.file_count = ed2kd_rt()->file_count;

    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_server_ident( struct e_client *client )
{
	struct packet_server_ident data;

	data.hdr.proto = PROTO_EDONKEY;
	data.hdr.length = sizeof data - sizeof(struct packet_header);
	data.opcode = OP_SERVERSTATUS;
	memcpy(data.hash, ed2kd_cfg()->hash, sizeof data.hash);
	data.ip = ed2kd_cfg()->listen_addr_int;
	data.port = ed2kd_cfg()->listen_port;
	data.tag_count = (ed2kd_cfg()->server_name_len>0) + (ed2kd_cfg()->server_descr_len>0);
	
    if ( data.tag_count > 0 ) {
        struct evbuffer *buf = evbuffer_new();
        evbuffer_add(buf, &data, sizeof data);

        if ( ed2kd_cfg()->server_name_len > 0 ) {
            struct tag_header th;
            size_t data_len = sizeof(uint16_t)+ed2kd_cfg()->server_name_len;
            unsigned char *data = (unsigned char*)alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_SERVERNAME;
            *(uint16_t*)data = ed2kd_cfg()->server_name_len;
            memcpy(data+sizeof(uint16_t), ed2kd_cfg()->server_name, ed2kd_cfg()->server_name_len);

            evbuffer_add(buf, &th, sizeof th);
            evbuffer_add(buf, data, data_len);
        }
    
        if ( ed2kd_cfg()->server_descr_len > 0 ) {
            struct tag_header th;
            size_t data_len = sizeof(uint16_t) + ed2kd_cfg()->server_descr_len;
            unsigned char *data = (unsigned char*)alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_DESCRIPTION;
            *(uint16_t*)data = ed2kd_cfg()->server_descr_len;
            memcpy(data+sizeof(uint16_t), ed2kd_cfg()->server_descr, ed2kd_cfg()->server_descr_len);

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

void send_server_list( struct e_client *client )
{
    // TODO: implement
}

void send_search_result( struct e_client *client, struct search_node *search_tree )
{
    size_t count = MAX_SEARCH_FILES;
    struct evbuffer *buf = evbuffer_new();
    struct packet_search_result data = {0};

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

void send_found_sources( struct e_client *client, const unsigned char *hash )
{
	uint8_t src_count = MAX_FOUND_SOURCES;
	struct e_source sources[MAX_FOUND_SOURCES];
	struct packet_found_sources data;

	db_get_sources(hash, sources, &src_count);

	data.hdr.proto = PROTO_EDONKEY;
	memcpy(data.hash, hash, sizeof data.hash);
	data.hdr.length = sizeof data - sizeof(struct packet_header) + src_count*sizeof(struct e_source);
	data.opcode = OP_FOUNDSOURCES;
	data.count = src_count;
	bufferevent_write(client->bev_srv, &data, sizeof data);
	bufferevent_write(client->bev_srv, sources, src_count*sizeof(struct e_source));
}

void send_reject( struct e_client *client )
{
	static const char data[] = { PROTO_EDONKEY, 1, 0, 0, 0, OP_REJECT };
    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_callback_fail( struct e_client *client )
{
    static const char data[] = { PROTO_EDONKEY, 1, 0, 0, 0, OP_CALLBACK_FAIL };
    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void client_portcheck_finish( struct e_client *client, enum portcheck_result result )
{
	if ( client->bev_cli ) {
        bufferevent_free(client->bev_cli);
	    client->bev_cli = NULL;
    }
	client->portcheck_finished = 1;
    client->lowid = (PORTCHECK_SUCCESS == result);

    if ( client->lowid ) {
        static const char msg_lowid[] = "WARNING : You have a lowid. Please review your network config and/or your settings.";
        ED2KD_LOGDBG("port check failed (%s:%d)", client->dbg.ip_str, client->port);
        send_server_message(client, msg_lowid, sizeof msg_lowid - 1);
        // todo: 
    } 
    
    if ( client->lowid ) {
        if ( ed2kd_cfg()->allow_lowid ) {
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
