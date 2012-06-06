#include <stdint.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "client.h"
#include "ed2kd.h"
#include "config.h"
#include "ed2k_proto.h"
#include "version.h"

struct e_client *client_new()
{
    struct e_client *client = (struct e_client*)malloc(sizeof(struct e_client));
    memset(client, 0, sizeof(struct e_client));

    return client;
}

void client_free( struct e_client *client )
{
    if( client->nick )
        free(client->nick);
    free(client);
}

void send_server_message( struct e_client *client, const char *msg, uint16_t len )
{
    struct evbuffer *outbuf = bufferevent_get_output(client->bev_srv);
    struct packet_server_message data = {
        .proto = PROTO_EDONKEY,
        .length = 3 + len,
        .opcode = OP_SERVERMESSAGE,
        .msg_len = len
    };

    evbuffer_add(outbuf, &data, sizeof(data));
    evbuffer_add(outbuf, msg, len);
}

void send_id_change( struct e_client *client )
{
    struct evbuffer *outbuf = bufferevent_get_output(client->bev_srv);
    struct packet_id_change data = {
        .proto = PROTO_EDONKEY,
        .length = 9,
        .opcode = OP_IDCHANGE,
        .user_id = client->ip,
        .tcp_flags = ED2KD_SRV_TCP_FLAGS
    };

    evbuffer_add(outbuf, &data, sizeof(data));
}

void send_server_status( struct e_client *client )
{
    struct evbuffer *outbuf = bufferevent_get_output(client->bev_srv);
    struct packet_server_status data = {
        .proto = PROTO_EDONKEY,
        .length = 9,
        .opcode = OP_SERVERSTATUS,
        .user_count = ed2kd()->user_count,
        .file_count = ed2kd()->file_count
    };

    evbuffer_add(outbuf, &data, sizeof(data));
}

void send_server_ident( struct e_client *client )
{
    //struct evbuffer *outbuf = bufferevent_get_output(client->bev);
}

void send_search_result( struct e_client *client )
{
    //struct evbuffer *outbuf = bufferevent_get_output(client->bev);
}

void send_found_sources( struct e_client *client )
{
    //struct evbuffer *outbuf = bufferevent_get_output(client->bev);
}

void send_reject( struct e_client *client )
{
    struct evbuffer *outbuf = bufferevent_get_output(client->bev_srv);
    uint8_t opcode = OP_REJECT;
    evbuffer_add(outbuf, &opcode, sizeof(opcode));
}

void client_handshake_finish( struct e_client *client )
{
    send_id_change(client);
    send_server_message(client, ed2kd()->welcome_msg, ed2kd()->welcome_msg_len);
}
