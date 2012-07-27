#include <stdint.h>
#include <signal.h>
#include <getopt.h>
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

#define ED2K_BENCH_VER "0.01"

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
    int repeat_cnt;
};

struct eb_instance {
    struct event_base *evbase;

    int repeat_cnt;
    int publish_cnt;
    int search_cnt;
    int source_cnt;
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

// command line options
const char *optString = "vhr:p:s:S:";
const struct option longOpts[] = {
    { "version", no_argument, NULL, 'v'},
    { "help", no_argument, NULL, 'h' },
    { "repeat", required_argument, NULL, 'r'},
    { "publish", required_argument, NULL, 'p'},
    { "search", required_argument, NULL, 's'},
    { "source", required_argument, NULL, 'S'},
    { NULL, no_argument, NULL, 0 }
};

void display_version( void )
{
    puts("ed2kd benchmark (eb) v" ED2K_BENCH_VER);
    puts("Build on: "__DATE__ " " __TIME__);
}

void display_usage( void )
{
    puts("Options:");
    puts("--help, -h\tshow this help");
    puts("--version, -v\tprint version");
    puts("--repeat, -r\t<count>\trepeat task <count> times");
    puts("--publish, -p\t<count>\tpublish <count> random files");
    puts("--search, -s\t<count>\tsearch random query <count> times");
    puts("--source, -S\t<count>\tsearch random file sources <count> times");
}


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
    struct eb_client *clnt = (struct eb_client *)ctx;
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t src_len = evbuffer_get_length(input);

    while( src_len > sizeof(struct packet_header) ) {
        unsigned char *data;
        struct packet_buffer pb;
        size_t packet_len;
        int ret;
        const struct packet_header *ph =
            (struct packet_header*)evbuffer_pullup(input, sizeof *ph);

        if  ( (PROTO_PACKED != ph->proto) && (PROTO_EDONKEY != ph->proto) ) {
            printf("%d# unknown packet protocol %c\n", clnt->idx, ph->proto);
            // close and remove client
        }

        // wait for full length packet
        packet_len = ph->length + sizeof *ph;
        if ( packet_len > src_len )
            return;

        data = evbuffer_pullup(input, packet_len);
        ph = (struct packet_header*)data;
        data += sizeof *ph;

        if ( PROTO_PACKED == ph->proto ) {
            unsigned long unpacked_len = MAX_UNCOMPRESSED_PACKET_SIZE;
            unsigned char *unpacked = (unsigned char*)malloc(unpacked_len);

            ret = uncompress(unpacked, &unpacked_len, data+1, ph->length-1);
            if ( Z_OK == ret ) {
                PB_INIT(&pb, unpacked, unpacked_len);
                ret = process_packet(&pb, *data, clnt);
            } else {
                printf("%d# failed to unpack packet\n", clnt->idx);
                ret = -1;
            }
            free(unpacked);
        } else {
            PB_INIT(&pb, data+1, ph->length-1);
            ret = process_packet(&pb, *data, clnt);
        }

        if (  ret < 0 ) {
            printf("%d# packet parsing error (opcode:%c)\n", clnt->idx, data+1);
            // close and remove client
        }

        evbuffer_drain(input, packet_len);
        src_len = evbuffer_get_length(input);
    }
}

void event_cb( struct bufferevent *bev, short events, void *ctx )
{
    struct eb_client *clnt = (struct eb_client*)ctx;

    if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) ) {
        printf("%d# got error/EOF!\n", clnt->idx);
    } else if ( events & BEV_EVENT_CONNECTED ) {
        send_login_request(clnt);
    }
}

void signal_cb( evutil_socket_t fd, short what, void *ctx )
{
    printf("caught SIGINT, terminating...");
    event_base_loopexit(g_instance.evbase, NULL);
}

struct eb_client *start_instance( int idx, struct sockaddr *sa, int sa_len )
{
    struct bufferevent *bev;
    struct eb_client *clnt;

    bev = bufferevent_socket_new(g_instance.evbase, -1, BEV_OPT_CLOSE_ON_FREE);
    clnt = (struct eb_client*)calloc(1, sizeof *clnt);
    clnt->idx = idx;
    clnt->bev = bev;

    bufferevent_setcb(bev, read_cb, NULL, event_cb, (void*)clnt);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    if ( bufferevent_socket_connect(bev, sa, sa_len) < 0 ) {
        printf("%d# failed to connect\n", clnt->idx);
        bufferevent_free(bev);
        free(clnt);
        return NULL;
    }

    return clnt;
}

int main( int argc, char *argv[] )
{
#ifdef WIN32
    WSADATA WSAData;
#endif
    int ret, i, opt, longIndex = 0;
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

    // parse command line arguments
    opt = getopt_long( argc, argv, optString, longOpts, &longIndex );
    while( opt != -1 ) {
        switch( opt ) {
        case 'v':
            display_version();
            return EXIT_SUCCESS;

        case 'h':
            display_usage();
            return EXIT_SUCCESS;

        case 'r':
            g_instance.repeat_cnt = atoi(optarg);
            break;

        case 'p':
            g_instance.publish_cnt = atoi(optarg);
            break;

        case 's':
            g_instance.search_cnt = atoi(optarg);
            break; 

        case 'S':
            g_instance.source_cnt = atoi(optarg);
            break;

        default:
            return EXIT_FAILURE;
        }
        opt = getopt_long( argc, argv, optString, longOpts, &longIndex );
    }

    g_instance.evbase = event_base_new();
    if ( NULL == g_instance.evbase ) {
        printf("failed to create main event loop");
        return EXIT_FAILURE;
    }

    // setup signals
    sigint_event = evsignal_new(g_instance.evbase, SIGINT, signal_cb, NULL);
    evsignal_add(sigint_event, NULL);

    memset(&server_sa, 0, sizeof server_sa);
    server_sa.sin_family = AF_INET;
    server_sa.sin_addr.s_addr = inet_addr("78.29.10.18");
    server_sa.sin_port = htons(4662);
    
    for ( i=0; i<CLIENT_COUNT; ++i ) {
        start_instance(i, (struct sockaddr*)&server_sa, sizeof server_sa);
    }
    
    ret = event_base_dispatch(g_instance.evbase);
    if ( ret < 0 ) {
        printf("main dispatch loop finished with error\n");
    }
    else if ( 0 == ret ) {
        printf("no active events in main loop\n");
    }

    event_free(sigint_event);
    event_base_free(g_instance.evbase);

    return EXIT_SUCCESS;
}