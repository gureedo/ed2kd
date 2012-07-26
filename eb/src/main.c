#include <stdint.h>
#include <signal.h>
#ifdef WIN32
#include <winsock2.h>
#endif

#include <zlib.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "../../src/ed2k_proto.h"
#include "../../src/packet_buffer.h"

#define MAX_UNCOMPRESSED_PACKET_SIZE 300*1024
#define NICK_LEN 5
#define CLIENT_COUNT 10

#define TCP_FLAGS (CLI_CAP_UNICODE|CLI_CAP_LARGEFILES|CLI_CAP_ZLIB)

struct eb_client {
    int idx;
    uint32_t id;
    unsigned char hash[HASH_SIZE];
    unsigned char nick[NICK_LEN];
    struct bufferevent *bev;
};

struct eb_instance {
    struct event_base *evbase;
    struct eb_client *clnts;
} g_instance;

PACKED_STRUCT(
struct packet_login {
    struct packet_header hdr;
    uint8_t opcode;
    unsigned char hash[HASH_SIZE];
    uint32_t id;
    uint16_t port;
    uint32_t tag_count;
    struct {
        struct tag_header hdr;
        uint16_t len;
        unsigned char val[NICK_LEN];
    } tag_nick;
    struct {
        struct tag_header hdr;
        uint16_t val;
    } tag_port;
    struct {
        struct tag_header hdr;
        uint32_t val;
    } tag_version;
    struct {
        struct tag_header hdr;
        uint32_t val;
    } tag_tcp_flags;
}
);

void client_free( struct eb_client *clnt )
{
    bufferevent_free(clnt->bev);
}

start_benchmark( struct eb_client *clnt )
{

}

void send_login_request( struct eb_client *clnt )
{
    struct packet_login data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof data - sizeof data.hdr;
    data.opcode = OP_LOGINREQUEST;

    // hash already initialized
    data.id = 0;
    data.port = 0; // port listening no implemented
    data.tag_count = 4;    
    
    // nick
    data.tag_nick.hdr.type = TT_STRING;
    data.tag_nick.hdr.name_len = 1;
    *data.tag_nick.hdr.name = TN_NAME;
    data.tag_nick.len = NICK_LEN;
    memcpy(data.tag_nick.val, clnt->nick, NICK_LEN);

    // port
    data.tag_port.hdr.type = TT_UINT16;
    data.tag_port.hdr.name_len = 1;
    *data.tag_port.hdr.name = TN_NAME;
    data.tag_port.val = 0; // // port listening no yet implemented

    // version
    data.tag_version.hdr.type = TT_UINT32;
    data.tag_version.hdr.name_len = 1;
    *data.tag_version.hdr.name = TN_VERSION;
    data.tag_version.val = EDONKEYVERSION;
    
    // tcp flags
    data.tag_tcp_flags.hdr.type = TT_UINT32;
    data.tag_tcp_flags.hdr.name_len = 1;
    *data.tag_tcp_flags.hdr.name = TN_SERVER_FLAGS;
    data.tag_tcp_flags.val = TCP_FLAGS;

    bufferevent_write(clnt->bev, &data, sizeof data);
}

int process_id_change( struct packet_buffer *pb, struct eb_client *clnt )
{
    uint32_t tcp_flags;

    PB_READ_UINT32(pb, clnt->id);
    PB_READ_UINT32(pb, tcp_flags);

    return 0;

malformed:
    printf("%d# malformed OP_IDCHANGE\n", clnt->idx);
    return -1;
}

int process_packet( struct packet_buffer *pb, uint8_t opcode, struct eb_client *clnt )
{
    switch ( opcode ) {
    case OP_IDCHANGE:
        PB_CHECK( process_id_change(pb, clnt) == 0 );
        start_benchmark(clnt);
        return 0;

    case OP_SERVERMESSAGE:
        return 0;

    case OP_SERVERSTATUS:
        return 0;
    
    case OP_SERVERIDENT:
        return 0;

    case OP_FOUNDSOURCES:

    case OP_SEARCHRESULT:
        return 0;
    
    case OP_DISCONNECT:
        return 0;

    case OP_REJECT:
        return 0;

    default:
        // skip all unknown packets
        return 0;
    }

malformed:
    return -1;
}

void read_cb( struct bufferevent *bev, void *ctx )
{
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
            printf("%d# unknown packet protocol %c\n", (int)ctx, header->proto);
            // close and remove client
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
                ret = process_packet(&pb, *data, &g_instance.clnts[(int)ctx]);
            } else {
                printf("%d# failed to unpack packet\n", (int)ctx);
                ret = -1;
            }
            free(unpacked);
        } else {
            PB_INIT(&pb, data+1, header->length-1);
            ret = process_packet(&pb, *data, &g_instance.clnts[(int)ctx]);
        }

        if (  ret < 0 ) {
            printf("packet parsing error (opcode:%c)", (int)ctx, data+1);
            // close and remove client
        }

        evbuffer_drain(input, packet_len);
        src_len = evbuffer_get_length(input);
    }
}

void event_cb( struct bufferevent *bev, short events, void *ctx )
{
    if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) ) {
        printf("%d# got error/EOF!\n", (int)ctx);
    } else if ( events & BEV_EVENT_CONNECTED ) {
        send_login_request(&g_instance.clnts[(int)ctx]);
    }
}

void signal_cb( evutil_socket_t fd, short what, void *ctx )
{
    printf("caught SIGINT, terminating...");
    event_base_loopexit(g_instance.evbase, NULL);
}

int main( int argc, char *argv[] )
{
#ifdef WIN32
    WSADATA WSAData;
#endif
    int ret,i;
    struct event *sigint_event;
    struct sockaddr_in server_sa;

#ifdef WIN32
    if ( 0 != WSAStartup(0x0201, &WSAData) ) {
        printf("WSAStartup failed!\n");
        return EXIT_FAILURE;
    }
#endif

    if ( evutil_secure_rng_init() < 0 ) {
        printf("Failed to seed random number generator\n");
        return EXIT_FAILURE;
    }

    g_instance.evbase = event_base_new();
    if ( NULL == g_instance.evbase ) {
        printf("failed to create main event loop");
        return EXIT_FAILURE;
    }

    memset(&server_sa, 0, sizeof server_sa);
    server_sa.sin_family = AF_INET;
    server_sa.sin_addr.s_addr = inet_addr("78.29.10.18");
    server_sa.sin_port = htons(4662);
    
    // setup signals
    sigint_event = evsignal_new(g_instance.evbase, SIGINT, signal_cb, NULL);
    evsignal_add(sigint_event, NULL);

    g_instance.clnts = (struct eb_client*)calloc(CLIENT_COUNT, sizeof(g_instance.clnts));

    for ( i=0; i<CLIENT_COUNT; ++i ) {
        struct bufferevent *bev;
        struct eb_client *clnt;

        bev = bufferevent_socket_new(g_instance.evbase, -1, BEV_OPT_CLOSE_ON_FREE);
        
        bufferevent_setcb(bev, read_cb, NULL, event_cb, (void*)i);
        bufferevent_enable(bev, EV_READ|EV_WRITE);

        if ( bufferevent_socket_connect(bev, (struct sockaddr*)&server_sa, sizeof server_sa) < 0 ) {
            bufferevent_free(bev);
            printf("%d# failed to connect\n", i);
        }

        clnt = &g_instance.clnts[i];
        clnt->idx = i;
        evutil_secure_rng_get_bytes(clnt->hash, sizeof clnt->hash);
    }
    
    ret = event_base_dispatch(g_instance.evbase);
    if ( ret < 0 ) {
        printf("main dispatch loop finished with error\n");
    }
    else if ( 0 == ret ) {
        printf("no active events in main loop\n");
    }

    for( i=0; i<CLIENT_COUNT; ++i ) {
        client_free(&g_instance.clnts[i]);
    }
    free(g_instance.clnts);
    
    event_free(sigint_event);
    event_base_free(g_instance.evbase);

    return EXIT_SUCCESS;
}