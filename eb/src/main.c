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

#define MAX_UNCOMPRESSED_PACKET_SIZE 500000
#define CLIENT_COUNT 10

struct eb_client {
    struct bufferevent *bev;
};

struct eb_instance {
    struct event_base *evbase;
    struct eb_client *clnts;
} g_instance;

void send_login( struct eb_client *clnt )
{

}

int process_packet( struct packet_buffer *pb, uint8_t opcode, struct eb_client *clnt )
{
    switch ( opcode ) {
    case OP_IDCHANGE:
        //PB_CHECK( process_id_change(pb, clnt) == 0 );
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
        send_login(&g_instance.clnts[(int)ctx]);
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

        bev = bufferevent_socket_new(g_instance.evbase, -1, BEV_OPT_CLOSE_ON_FREE);
        g_instance.clnts[i].bev = bev;
        bufferevent_setcb(bev, read_cb, NULL, event_cb, (void*)i);
        bufferevent_enable(bev, EV_READ|EV_WRITE);

        if ( bufferevent_socket_connect(bev, (struct sockaddr*)&server_sa, sizeof server_sa) < 0 ) {
            bufferevent_free(bev);
            printf("%d# failed to connect\n", i);
            g_instance.clnts[i].bev = NULL;
        }
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
    free(g_instance.clnts);

    return EXIT_SUCCESS;
}