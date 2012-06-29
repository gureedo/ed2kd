#include <stdint.h>
#include <string.h>
#include <malloc.h>
#ifdef WIN32
#include <ws2tcpip.h>
#endif
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <zlib.h>
#include "client.h"
#include "portcheck.h"
#include "log.h"
#include "packet_buffer.h"
#include "ed2k_proto.h"
#include "ed2kd.h"

static void
send_hello( struct e_client *client )
{
    static const char name[] = {'e','d','2','k','d'};
    struct packet_hello data;
    evutil_socket_t fd;
    struct sockaddr_in sa;
    socklen_t sa_len;

    // get local ip addr
    fd = bufferevent_getfd(client->bev_cli);
    sa_len = sizeof sa;
    getsockname(fd, (struct sockaddr*)&sa, &sa_len);

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof(struct packet_header);
    data.opcode = OP_HELLO;
    data.hash_size = 16;
    memcpy(data.hash, ed2kd_cfg()->hash, sizeof data.hash);
    data.client_id = ntohl(sa.sin_addr.s_addr);
    data.client_port = 4662;
    data.tag_count = 2;
    data.tag_name.type = TT_STR5 | 0x80;
    data.tag_name.name = TN_NAME;
    memcpy(data.tag_name.value, name, sizeof data.tag_name.value);
    data.tag_version.type = TT_UINT8 | 0x80;
    data.tag_version.name = TN_VERSION;
    data.tag_version.value = EDONKEYVERSION;
    data.ip = 0;
    data.port = 0;

    bufferevent_write(client->bev_cli, &data, sizeof data);
}

static int
process_hello_answer( struct packet_buffer *pb, struct e_client *client )
{
    PB_CHECK( memcmp(client->hash, pb->ptr, HASH_SIZE) == 0 );
    PB_SEEK(pb, HASH_SIZE);

    return 0;

malformed:
    return -1;
}

static int
process_packet( struct packet_buffer *pb, uint8_t opcode, struct e_client *client )
{
    switch ( opcode ) {
    case OP_HELLOANSWER:
        PB_CHECK( process_hello_answer(pb, client) == 0 );
        client_portcheck_finish(client, PORTCHECK_SUCCESS);
        return 0;

    default:
        // skip all unknown packets
        return 0;
    }

malformed:
    return -1;
}

static void
read_cb( struct bufferevent *bev, void *ctx )
{
    struct e_client *client = (struct e_client*)ctx;
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t src_len = evbuffer_get_length(input);

    while( src_len > sizeof(struct packet_header) ) {
        unsigned char *data;
        struct packet_buffer pb;
        size_t packet_len;
        int ret;
        const struct packet_header *header =
            (struct packet_header*)evbuffer_pullup(input, sizeof(struct packet_header));

        if  ( (PROTO_PACKED != header->proto) && (PROTO_EDONKEY != header->proto) ) {
            ED2KD_LOGDBG("unknown packet protocol from %s:%u", client->dbg.ip_str, client->port);
            client_portcheck_finish(client, PORTCHECK_FAILED);
            return;
        }

        // wait for full length packet
        packet_len = header->length + sizeof(struct packet_header);
        if ( packet_len > src_len )
            return;

        data = evbuffer_pullup(input, packet_len);
        header = (struct packet_header*)data;
        data += sizeof(struct packet_header);

        if ( PROTO_PACKED == header->proto ) {
            unsigned long unpacked_len = MAX_UNCOMPRESSED_PACKET_SIZE;
            unsigned char *unpacked = (unsigned char*)malloc(unpacked_len);

            ret = uncompress(unpacked, &unpacked_len, data+1, header->length-1);
            if ( Z_OK == ret ) {
                PB_INIT(&pb, unpacked, unpacked_len);
                ret = process_packet(&pb, *data, client);
            } else {
                ED2KD_LOGDBG("failed to unpack packet from %s:%u", client->dbg.ip_str, client->port);
                ret = -1;
            }
            free(unpacked);
        } else {
            PB_INIT(&pb, data+1, header->length-1);
            ret = process_packet(&pb, *data, client);
        }

        if (  ret < 0 ) {
            ED2KD_LOGDBG("client packet parsing error (%s:%u)", client->dbg.ip_str, client->port);
            client_portcheck_finish(client, PORTCHECK_FAILED);
            return;
        }

        if ( client->portcheck_finished )
            return;

        evbuffer_drain(input, packet_len);
        src_len = evbuffer_get_length(input);
    }
}

static void
event_cb( struct bufferevent *bev, short events, void *ctx )
{
    struct e_client *client = (struct e_client *)ctx;

    if ( !client->portcheck_finished && (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) ) {
        client_portcheck_finish(client, PORTCHECK_FAILED);
    } else if ( events & BEV_EVENT_CONNECTED ) {
        send_hello(client);
    }
}

int client_portcheck_start( struct e_client *client )
{
    struct sockaddr_in client_sa;
    struct event_base *base;
    struct bufferevent *bev;

    memset(&client_sa, 0, sizeof client_sa);
    client_sa.sin_family = AF_INET;
    client_sa.sin_addr.s_addr = client->ip;
    client_sa.sin_port = htons(client->port);

    base = bufferevent_get_base(client->bev_srv);
    bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
    bufferevent_setcb(bev, read_cb, NULL, event_cb, client);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    if ( bufferevent_socket_connect(bev, (struct sockaddr*)&client_sa, sizeof client_sa) < 0 ) {
        bufferevent_free(bev);
        return -1;
    }

    // todo: timeout for handshake
    client->bev_cli = bev;

    return 0;
}
