#include <stdint.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
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
	int sa_len;

	// get local ip addr
    fd = bufferevent_getfd(client->bev_cli);
    sa_len = sizeof sa;
    getsockname(fd, (struct sockaddr*)&sa, &sa_len);
	
    data.proto = PROTO_EDONKEY;
    data.length = sizeof data - sizeof(struct packet_header);
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
process_packet( struct packet_buffer *pb, struct e_client *client )
{
    uint8_t opcode;

    PB_READ_UINT8(pb, opcode);

    switch ( opcode ) {
    case OP_HELLOANSWER:
        PB_CHECK( process_hello_answer(pb, client) == 0 );
		client_portcheck_finish(client);
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
		size_t packet_len;
		const unsigned char *data;
		struct packet_buffer pb;
		const struct packet_header *header =
			(struct packet_header*)evbuffer_pullup(input, sizeof(struct packet_header));

		// check protocol header
		if ( PROTO_EDONKEY != header->proto ) {
#ifdef DEBUG
			ED2KD_LOGDBG("unknown packet protocol %s:%u", client->dbg.ip_str, client->port);
#endif
			client_portcheck_failed(client);
			return;
		}

		// todo: add compression support

		// wait for full length packet
		// todo: max packet size limit
		packet_len = header->length + sizeof(struct packet_header);
		if ( packet_len > src_len )
			return;

		data = evbuffer_pullup(input, packet_len) + sizeof(struct packet_header);

		PB_INIT(&pb, data, header->length);
		if ( process_packet(&pb, client) < 0 ) {
#ifdef DEBUG
			ED2KD_LOGDBG("client packet parsing error %s:%u", client->dbg.ip_str, client->port);
#endif
			client_portcheck_failed(client);
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
		client_portcheck_failed(client);
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
