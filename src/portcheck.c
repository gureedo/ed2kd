#include "portcheck.h"
#include <stdlib.h>
#ifdef WIN32
#include <ws2tcpip.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <zlib.h>

#include "server.h"
#include "client.h"
#include "log.h"
#include "packet.h"
#include "ed2k_proto.h"
#include "event_callback.h"

static void send_hello( struct client *clnt )
{
        static const char name[] = {'e','d','2','k','d'};
        struct packet_hello data;
        evutil_socket_t fd;
        struct sockaddr_in sa;
        socklen_t sa_len;

        // get local ip addr
        fd = bufferevent_getfd(clnt->bev_pc);
        sa_len = sizeof(sa);
        getsockname(fd, (struct sockaddr*)&sa, &sa_len);

        data.hdr.proto = PROTO_EDONKEY;
        data.hdr.length = sizeof(data) - sizeof(data.hdr);
        data.opcode = OP_HELLO;
        data.hash_size = 16;
        memcpy(data.hash, g_instance.cfg->hash, sizeof data.hash);
        data.client_id = ntohl(sa.sin_addr.s_addr);
        data.client_port = 4662;
        data.tag_count = 2;
        data.tag_name.type = TT_STR5 | 0x80;
        data.tag_name.name = TN_NAME;
        memcpy(data.tag_name.value, name, sizeof(data.tag_name.value));
        data.tag_version.type = TT_UINT8 | 0x80;
        data.tag_version.name = TN_VERSION;
        data.tag_version.value = EDONKEYVERSION;
        data.ip = 0;
        data.port = 0;

        bufferevent_write(clnt->bev_pc, &data, sizeof data);
}

static int process_hello_answer( struct packet_buffer *pb, struct client *clnt )
{
        PB_CHECK( memcmp(clnt->hash, pb->ptr, ED2K_HASH_SIZE) == 0 );
        PB_SEEK(pb, ED2K_HASH_SIZE);

        return 0;

malformed:
        return -1;
}

static int process_packet( struct packet_buffer *pb, uint8_t opcode, struct client *clnt )
{
        switch ( opcode ) {
        case OP_HELLOANSWER:
                PB_CHECK( process_hello_answer(pb, clnt) == 0 );
                client_portcheck_finish(clnt, PORTCHECK_SUCCESS);
                return 0;

        default:
                // skip all unknown packets
                return 0;
        }

malformed:
        return -1;
}

void portcheck_read( struct client *clnt )
{
        struct evbuffer *input;
        size_t src_len;

        if ( clnt->portcheck_finished )
                return;

        input = bufferevent_get_input(clnt->bev_pc);
        src_len = evbuffer_get_length(input);

        while( !clnt->sched_del && src_len > sizeof(struct packet_header) ) {
                unsigned char *data;
                struct packet_buffer pb;
                size_t packet_len;
                int ret;
                const struct packet_header *header =
                        (struct packet_header*)evbuffer_pullup(input, sizeof(struct packet_header));

                if  ( (PROTO_PACKED != header->proto) && (PROTO_EDONKEY != header->proto) ) {
                        ED2KD_LOGDBG("unknown packet protocol from %s:%u", clnt->dbg.ip_str, clnt->port);
                        client_portcheck_finish(clnt, PORTCHECK_FAILED);
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
                                ret = process_packet(&pb, *data, clnt);
                        } else {
                                ED2KD_LOGDBG("failed to unpack packet from %s:%u", clnt->dbg.ip_str, clnt->port);
                                ret = -1;
                        }
                        free(unpacked);
                } else {
                        PB_INIT(&pb, data+1, header->length-1);
                        ret = process_packet(&pb, *data, clnt);
                }

                if (  ret < 0 ) {
                        ED2KD_LOGDBG("client packet parsing error (%s:%u)", clnt->dbg.ip_str, clnt->port);
                        client_portcheck_finish(clnt, PORTCHECK_FAILED);
                        return;
                }

                if ( clnt->portcheck_finished )
                        return;

                evbuffer_drain(input, packet_len);
                src_len = evbuffer_get_length(input);
        }
}

void portcheck_timeout( struct client *clnt )
{
        if ( clnt->portcheck_finished )
                return;

        ED2KD_LOGDBG("port check timeout for %s", clnt->dbg.ip_str);
        client_portcheck_finish(clnt, PORTCHECK_FAILED);
}

void portcheck_event( struct client *clnt, short events )
{
        if ( clnt->portcheck_finished )
                return;

        if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) ) {
                client_portcheck_finish(clnt, PORTCHECK_FAILED);
        } else if ( events & BEV_EVENT_CONNECTED ) {
                bufferevent_enable(clnt->bev_pc, EV_READ|EV_WRITE);
                send_hello(clnt);
        }
}
