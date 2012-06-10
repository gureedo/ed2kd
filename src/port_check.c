#include <stdint.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "client.h"
#include "port_check.h"
#include "log.h"
#include "protofilter.h"
#include "packet_buffer.h"
#include "ed2k_proto.h"
#include "ed2kd.h"

static void
send_hello( struct e_client *client )
{
	static const char name[] = {'e','d','2','k','d'};
	struct evbuffer *outbuf;
	struct packet_hello data;
	evutil_socket_t fd;
	struct sockaddr_in sa;
	int sa_len;

	// get local ip addr
    outbuf = bufferevent_get_output(client->bev_cli);
    fd = bufferevent_getfd(client->bev_cli);
    sa_len = sizeof(sa);
    getsockname(fd, (struct sockaddr*)&sa, &sa_len);
	
    data.proto = PROTO_EDONKEY;
    data.length = sizeof data - sizeof(struct packet_header);
    data.opcode = OP_HELLO;
    data.hash_size = 16;
    //.hash // server hash
    data.client_id = ntohl(sa.sin_addr.s_addr); // get local sock ip
    data.client_port = 4662;
    data.tag_count = 2;
    data.tag_name.type = TT_STR5;
    data.tag_name.name = TN_NAME;
    //.tag_name.value // ed2kd
    data.tag_version.type = TT_UINT8 | 0x80;
    data.tag_version.name = TN_VERSION;
    data.tag_version.value = EDONKEYVERSION;
    data.ip = 0;
    data.port = 0;

    memcpy(data.hash, ed2kd()->hash, sizeof(data.hash));
    memcpy(data.tag_name.value, name, sizeof(name));

    evbuffer_add(outbuf, &data, sizeof(data));
}

static int
process_hello_answer( struct packet_buffer *pb, struct e_client *client )
{
    uint8_t hash_size;
    PB_READ_UINT8(pb, hash_size);
    PB_CHECK(HASH_SIZE == hash_size);

    PB_CHECK( memcmp(client->hash, pb->ptr, HASH_SIZE) == 0 );
    PB_SEEK(pb, HASH_SIZE);

    return 1;

malformed:
    return -1;
}

static int
process_packet( struct packet_buffer *pb, struct e_client *client )
{
    uint8_t opcode;

    PB_READ_UINT8(pb, opcode);

    switch ( opcode ) {
    case OP_HELLOANSWER:
        PB_CHECK( process_hello_answer(pb, client) );
        return 1;

    default:
        // unknown opcode
        return -1;
    }

malformed:
    // todo: log
    return -1;
}

static void
read_cb( struct bufferevent *bev, void *ctx )
{
    size_t input_len;
    struct e_client *client = (struct e_client*)ctx;
    struct evbuffer *input = bufferevent_get_input(bev);

    do {
        struct packet_header * header =
            (struct packet_header*)evbuffer_pullup(input, sizeof(struct packet_header));
        size_t packet_len = header->length + sizeof(struct packet_header);
        unsigned char * data = evbuffer_pullup(input, packet_len) + sizeof(struct packet_header);

        struct packet_buffer pb;
        PB_INIT(&pb, data, packet_len);
        process_packet(&pb, client);

        evbuffer_drain(input, packet_len);
        input_len = evbuffer_get_length(input);
    } while ( input_len > 0 );
}

static void
event_cb( struct bufferevent *bev, short events, void *ctx )
{
    struct e_client *client = (struct e_client *)ctx;

    if ( events & BEV_EVENT_CONNECTED ) {
        send_hello(client);
    }
    if ( events & BEV_EVENT_ERROR )
        perror("Error from bufferevent");
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        bufferevent_free(bev);
    }
}

int client_handshake_start( struct e_client *client )
{
    struct sockaddr_in client_sa;
	struct event_base *base;
	struct bufferevent *tcp_bev, *ed2k_bev;

    memset(&client_sa, 0, sizeof(client_sa));
    client_sa.sin_family = AF_INET;
    client_sa.sin_addr.s_addr = client->ip;
    client_sa.sin_port = htons(client->port);

    base = bufferevent_get_base(client->bev_srv);

    tcp_bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
    ed2k_bev = bufferevent_filter_new(tcp_bev, ed2k_input_filter_cb, NULL, BEV_OPT_CLOSE_ON_FREE, NULL, NULL);

    bufferevent_setcb(ed2k_bev, read_cb, NULL, event_cb, client);

    if (bufferevent_socket_connect(ed2k_bev, (struct sockaddr *)&client_sa, sizeof(client_sa)) < 0) {
        bufferevent_free(ed2k_bev);
        return 0;
    }

    client->bev_cli = ed2k_bev;

    return 1;
}
