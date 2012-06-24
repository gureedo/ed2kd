#include <stdint.h>
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

struct e_client *client_new()
{
    struct e_client *client = (struct e_client*)malloc(sizeof(struct e_client));
    memset(client, 0, sizeof(struct e_client));
	ed2kd_rt()->user_count++;

    return client;
}

void client_delete( struct e_client *client )
{
#ifdef DEBUG
	ED2KD_LOGDBG("client removed (%s:%d)", client->dbg.ip_str, client->port);
#endif
	if( client->bev_cli ) bufferevent_free(client->bev_cli);
	if( client->bev_srv ) bufferevent_free(client->bev_srv);

	db_remove_source(client);

    free(client);

	ed2kd_rt()->user_count--;
}

void send_id_change( struct e_client *client )
{
	struct packet_id_change data;
	data.proto = PROTO_EDONKEY;
	data.length = sizeof data - sizeof(struct packet_header);
	data.opcode = OP_IDCHANGE;
	data.user_id = client->ip;
	data.tcp_flags = ED2KD_SRV_TCP_FLAGS;

	bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_server_message( struct e_client *client, const char *msg, uint16_t len )
{
    struct packet_server_message data;
	data.proto = PROTO_EDONKEY;
	data.length = sizeof data - sizeof(struct packet_header) + len;
	data.opcode = OP_SERVERMESSAGE;
	data.msg_len = len;

    bufferevent_write(client->bev_srv, &data, sizeof data);
    bufferevent_write(client->bev_srv, msg, len);
}

void send_server_status( struct e_client *client )
{
    struct packet_server_status data;
    data.proto = PROTO_EDONKEY;
    data.length = sizeof data - sizeof(struct packet_header);
    data.opcode = OP_SERVERSTATUS;
    data.user_count = ed2kd_rt()->user_count;
    data.file_count = ed2kd_rt()->file_count;

    bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_server_ident( struct e_client *client )
{
	struct packet_server_ident data;
	data.proto = PROTO_EDONKEY;
	data.length = sizeof data - sizeof(struct packet_header);
	data.opcode = OP_SERVERSTATUS;
	memcpy(data.hash, ed2kd_cfg()->hash, sizeof data.hash);
	data.ip = ed2kd_cfg()->listen_addr_int;
	data.port = ed2kd_cfg()->listen_port;
	data.tag_count = 2;
	
	//ST_SERVERNAME
	//ST_DESCRIPTION
	
	bufferevent_write(client->bev_srv, &data, sizeof data);
}

void send_server_list( struct e_client *client )
{
    // todo: implement
}

// todo: implement data callback
void send_search_result( struct e_client *client, struct search_node *search_tree )
{
    size_t count = MAX_SEARCH_FILES;
    struct e_file *files = (struct e_file*)malloc(sizeof(struct e_file)*count);
    
    if ( db_search_file(search_tree, files, &count) >= 0 ) {
        size_t i;
        struct packet_search_result header = {0};
        struct evbuffer *buf = evbuffer_new();

        header.proto = PROTO_EDONKEY;
        //header.length = 0;
        header.opcode = OP_SEARCHRESULT;
        header.files_count = count;
        evbuffer_add(buf, &header, sizeof header);

        for ( i=0; i<count; ++i ) {
            struct search_file_entry sfe;
            
            memcpy(sfe.hash, files[i].hash, sizeof sfe.hash);
            sfe.id = client->ip;
            sfe.port = client->port;
            sfe.tag_count = 1+ 1+ (0!=files[i].type)+ (files[i].ext_len>0)+ 1+ 1+ (files[i].media_length>0)+ (files[i].media_length>0)+ (files[i].media_codec_len>0);
            evbuffer_add(buf, &sfe, sizeof sfe);

            {
                struct tag_header th;
                size_t data_len = sizeof(uint16_t)+files[i].name_len;
                unsigned char *data = (unsigned char*)alloca(data_len);

                th.type = TT_STRING;
                th.name_len = 1;
                *th.name = TN_FILENAME;
                *(uint16_t*)data = files[i].name_len;
                memcpy(data+sizeof(uint16_t), files[i].name, files[i].name_len);
             
                evbuffer_add(buf, &th, sizeof th);
                evbuffer_add(buf, data, data_len);
            }

            {
                struct tag_header th;
                th.type = TT_UINT64;
                th.name_len = 1;
                *th.name = TN_FILESIZE;

                evbuffer_add(buf, &th, sizeof th);
                evbuffer_add(buf, &files[i].size, sizeof files[i].size);
            }

            /*if ( files[i].type ) {
                struct tag_header th;
                th.type = TT_STRING;
                th.name_len = 1;
                *th.name = TN_FILETYPE;
                //len
                //type
            }*/

            if ( files[i].ext_len ) {
                struct tag_header th;
                size_t data_len = sizeof(uint16_t)+files[i].ext_len;
                unsigned char *data = (unsigned char*)alloca(data_len);

                th.type = TT_STRING;
                th.name_len = 1;
                *th.name = TN_FILEFORMAT;
                *(uint16_t*)data = files[i].name_len;
                memcpy(data+sizeof(uint16_t), files[i].ext, files[i].ext_len);

                evbuffer_add(buf, &th, sizeof th);
                evbuffer_add(buf, data, data_len);
            }

            {
                struct tag_header th;
                th.type = TT_UINT32;
                th.name_len = 1;
                *th.name = TN_SOURCES;

                evbuffer_add(buf, &th, sizeof th);
                evbuffer_add(buf, &files[i].srcavail, sizeof files[i].srcavail);
            }

            {
                struct tag_header th;
                th.type = TT_UINT32;
                th.name_len = 1;
                *th.name = TN_COMPLETE_SOURCES;

                evbuffer_add(buf, &th, sizeof th);
                evbuffer_add(buf, &files[i].srccomplete, sizeof files[i].srccomplete);
            }

            if ( files[i].media_length ) {
                uint16_t name_len = sizeof(TNS_MEDIA_LENGTH)-1;
                size_t th_len = sizeof(struct tag_header)-1+name_len;
                struct tag_header *th = (struct tag_header*)alloca(th_len);
                th->type = TT_UINT32;
                th->name_len = name_len;
                memcpy(th->name, TNS_MEDIA_LENGTH, name_len);

                evbuffer_add(buf, th, th_len);
                evbuffer_add(buf, &files[i].media_length, sizeof files[i].media_length);
            }
            
            if ( files[i].media_bitrate ) {
                uint16_t name_len = sizeof(TNS_MEDIA_BITRATE)-1;
                size_t th_len = sizeof(struct tag_header)-1+name_len;
                struct tag_header *th = (struct tag_header*)alloca(th_len);
                th->type = TT_UINT32;
                th->name_len = name_len;
                memcpy(th->name, TNS_MEDIA_BITRATE, name_len);

                evbuffer_add(buf, th, th_len);
                evbuffer_add(buf, &files[i].media_bitrate, sizeof files[i].media_bitrate);
            }
            
            if ( files[i].media_codec_len ) {
                uint16_t name_len = sizeof(TNS_MEDIA_CODEC)-1;
                size_t th_len = sizeof(struct tag_header)-1+name_len;
                struct tag_header *th = (struct tag_header*)alloca(th_len);

                size_t data_len = sizeof(uint16_t)+files[i].media_codec_len;
                unsigned char *data = (unsigned char*)alloca(data_len);

                th->type = TT_UINT32;
                th->name_len = name_len;
                memcpy(th->name, TNS_MEDIA_CODEC, name_len);

                *(uint16_t*)data = files[i].media_codec_len;
                memcpy(data+sizeof(uint16_t), files[i].media_codec, files[i].media_codec_len);

                evbuffer_add(buf, th, th_len);
                evbuffer_add(buf, data, data_len);
            }
        }

        {
            struct packet_header *ph = (struct packet_header*)evbuffer_pullup(buf, sizeof(struct packet_header));
            ph->length = evbuffer_get_length(buf) - sizeof(struct packet_header);
        }

        bufferevent_write_buffer(client->bev_srv, buf);
        evbuffer_free(buf);
    }

    free(files);
}

void send_found_sources( struct e_client *client, const unsigned char *hash )
{
	uint32_t src_count = MAX_FOUND_SOURCES;
	struct e_source sources[MAX_FOUND_SOURCES];
	struct packet_found_sources data;

	db_get_sources(hash, sources, &src_count);

	data.proto = PROTO_EDONKEY;
	memcpy(data.hash, hash, sizeof data.hash);
	data.length = sizeof data - sizeof(struct packet_header) + src_count*sizeof(struct e_source);
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

void client_portcheck_finish( struct e_client *client )
{
	bufferevent_free(client->bev_cli);
	client->bev_cli = NULL;
	client->portcheck_finished = 1;

    send_id_change(client);
    send_server_message(client, ed2kd_cfg()->welcome_msg, ed2kd_cfg()->welcome_msg_len);
}

void client_portcheck_failed( struct e_client *client )
{
#ifdef DEBUG
	ED2KD_LOGDBG("port check failed (%s:%d)", client->dbg.ip_str, client->port);
#endif
	client_delete(client);
}


