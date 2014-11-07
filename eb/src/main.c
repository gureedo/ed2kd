#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <assert.h>

#include <zlib.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "../../src/ed2k_proto.h"
#include "../../src/packet.h"

// stringize macro
#define _CSTR(x) #x
#define CSTR(x) _CSTR(x)

#define MAX_UNCOMPRESSED_PACKET_SIZE 300*1024
#define DEFAULT_SPAWN_PAUSE 100 // msecs
#define DEFAULT_ACTION_PAUSE 500 // msecs

#define EB_VERSION "0.01"

// definitions for fixed length fields in ed2k packets
#define NICK_LEN 5
#define FILENAME_LEN 5

enum actions {
    ACTION_OFFER = 0,
    ACTION_QUERY,
    ACTION_SOURCE,
    ACTION_COUNT
};

struct ebclient {
    /* Index */
    int idx;
    /* eDonkey2000 ID */
    uint32_t id;
    /* eDonkey2000 hash */
    unsigned char hash[ED2K_HASH_SIZE];
    /* connection bufferevent */
    struct bufferevent *bev;
    /* action timer */
    struct event *ev_action;
    /* connection established flag */
    unsigned connected:1;
};

struct ebinstance {
    /* event base */
    struct event_base *evbase;
    /* Client spawn timer */
    struct event *ev_spawn;
    /* Common pause between spawning clients */
    const struct timeval *spawn_pause;
    /* Server address to connect */
    struct sockaddr_in server_sa;
    /* Count of concurrently working clients */
    int client_cnt;
    /* Total count of actions to perform */
    int repeat_cnt;
    /* Count currently running clients */
    int running_cnt;
    /* Common pause between client actions */
    const struct timeval *action_pause;
    /* Number of selected actions */
    int action_cnt;
    /* Array of selected actions */
    int actions[ACTION_COUNT];
};

struct packet_login {
    struct packet_header hdr;
    uint8_t opcode;
    unsigned char hash[ED2K_HASH_SIZE];
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
} __attribute__((__packed__));

struct packet_offer_files {
    struct packet_header hdr;
    uint8_t opcode;
    uint32_t file_count;
} __attribute__((__packed__));

struct pub_file {
    unsigned char hash[ED2K_HASH_SIZE];
    uint32_t id;
    uint16_t port;
    uint32_t tag_count;
    struct {
        struct tag_header hdr;
        uint16_t len;
        unsigned char val[FILENAME_LEN];
    } tag_name;
    struct {
        struct tag_header hdr;
        uint32_t val;
    } tag_size;
    struct {
        struct tag_header hdr;
        uint32_t val;
    } tag_rating;
    struct {
        struct tag_header hdr;
        uint32_t val;
    } tag_type;
} __attribute__((__packed__));

struct ebinstance g_eb;

// command line options
static const char *optString = "vhs:c:w:r:p:OQS";
static const struct option longOpts[] = {
        {"version", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {"server", required_argument, NULL, 's'},
        {"concurrency", required_argument, NULL, 'c'},
        {"spawn-pause", required_argument, NULL, 'w'},
        {"repeat", required_argument, NULL, 'r'},
        {"action-pause", required_argument, NULL, 'p'},
        {"offer", no_argument, NULL, 'O'},
        {"query", no_argument, NULL, 'Q'},
        {"source", no_argument, NULL, 'S'},
        {NULL, no_argument, NULL, 0}
};

void get_rnd_str(unsigned char *str, size_t len)
{
    size_t i;
    static const unsigned char alphanum[] =
            "0123456789"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz";

    for (i = 0; i < len; ++i) {
        str[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }
}

void client_free(struct ebclient *clnt)
{
    bufferevent_free(clnt->bev);
    if (clnt->ev_action)
        event_free(clnt->ev_action);
    free(clnt);

    g_eb.running_cnt--;
    if (!g_eb.running_cnt && !g_eb.client_cnt) {
        event_base_loopbreak(g_eb.evbase);
    }
}

void send_login_request(struct ebclient *clnt)
{
    struct packet_login data;
    unsigned char nick[NICK_LEN] = {'e', 'b', 't', 's', 't'};

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof(data) - sizeof(data.hdr);
    data.opcode = OP_LOGINREQUEST;

    memcpy(data.hash, clnt->hash, sizeof(data.hash));
    data.id = 0;
    data.port = 0; // port listening no implemented
    data.tag_count = 4;

    // nick
    data.tag_nick.hdr.type = TT_STRING;
    data.tag_nick.hdr.name_len = 1;
    *data.tag_nick.hdr.name = TN_NAME;
    data.tag_nick.len = NICK_LEN;
    memcpy(data.tag_nick.val, nick, NICK_LEN);

    // port
    data.tag_port.hdr.type = TT_UINT16;
    data.tag_port.hdr.name_len = 1;
    *data.tag_port.hdr.name = TN_PORT;
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
    data.tag_tcp_flags.val = CLI_CAP_UNICODE | CLI_CAP_LARGEFILES | CLI_CAP_ZLIB;

    bufferevent_write(clnt->bev, &data, sizeof data);
}

void send_offer_files(struct ebclient *clnt)
{
    size_t i;
    struct packet_offer_files data;
    struct packet_header *ph;
    struct pub_file pf;
    struct evbuffer *buf = evbuffer_new();

    data.hdr.proto = PROTO_EDONKEY;
    //data.hdr.length = 0;
    data.opcode = OP_OFFERFILES;
    data.file_count = 200;

    evbuffer_add(buf, &data, sizeof(data));

    // complete source
    pf.id = 0xfdfdfdfdu;
    pf.port = 0xfdfdu;

    pf.tag_count = 4;

    // file name
    pf.tag_name.hdr.type = TT_STRING;
    pf.tag_name.hdr.name_len = 1;
    *pf.tag_name.hdr.name = TN_FILENAME;
    pf.tag_name.len = 5;

    // file size
    pf.tag_size.hdr.type = TT_UINT32;
    pf.tag_size.hdr.name_len = 1;
    *pf.tag_size.hdr.name = TN_FILESIZE;
    pf.tag_size.val = 10240;

    // file type
    pf.tag_type.hdr.type = TT_UINT32;
    pf.tag_type.hdr.name_len = 1;
    *pf.tag_type.hdr.name = TN_FILETYPE;
    pf.tag_type.val = FT_PROGRAM;

    // file rating
    pf.tag_rating.hdr.type = TT_UINT32;
    pf.tag_rating.hdr.name_len = 1;
    *pf.tag_rating.hdr.name = TN_FILERATING;
    pf.tag_rating.val = 0;

    for (i = 0; i < data.file_count; ++i) {
        evutil_secure_rng_get_bytes(pf.hash, sizeof(pf.hash));
        get_rnd_str(pf.tag_name.val, sizeof(pf.tag_name.val));

        evbuffer_add(buf, &pf, sizeof(pf));
    }

    ph = (struct packet_header *) evbuffer_pullup(buf, sizeof(*ph));
    ph->length = evbuffer_get_length(buf) - sizeof(*ph);

    bufferevent_write_buffer(clnt->bev, buf);
    evbuffer_free(buf);
}

void timer_cb(evutil_socket_t fd, short what, void *ctx)
{
    struct ebclient *clnt = (struct ebclient *) ctx;
    (void) fd;
    (void) what;

    if (g_eb.action_cnt && g_eb.repeat_cnt) {
        switch (g_eb.actions[rand() % g_eb.action_cnt]) {
            case ACTION_OFFER:
                send_offer_files(clnt);
                break;

            case ACTION_QUERY:
                break;

            case ACTION_SOURCE:
                break;

            default:
                assert(0);
                break;
        }

        g_eb.repeat_cnt--;
        evtimer_add(clnt->ev_action, g_eb.action_pause);
    } else {
        client_free(clnt);
    }
}

int process_id_change(struct packet_buffer *pb, struct ebclient *clnt)
{
    uint32_t tcp_flags;
    (void) tcp_flags;

    PB_READ_UINT32(pb, clnt->id);
    PB_READ_UINT32(pb, tcp_flags);

    return 0;

    malformed:
    printf("%d# malformed OP_IDCHANGE\n", clnt->idx);
    return -1;
}

int process_packet(struct packet_buffer *pb, uint8_t opcode, struct ebclient *clnt)
{
    switch (opcode) {
        case OP_IDCHANGE:
            PB_CHECK(process_id_change(pb, clnt) == 0);
            clnt->ev_action = evtimer_new(g_eb.evbase, timer_cb, clnt);
            evtimer_add(clnt->ev_action, g_eb.action_pause);
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

void read_cb(struct bufferevent *bev, void *ctx)
{
    struct ebclient *clnt = (struct ebclient *) ctx;
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t src_len = evbuffer_get_length(input);

    while (src_len > sizeof(struct packet_header)) {
        unsigned char *data;
        struct packet_buffer pb;
        size_t packet_len;
        int ret;
        const struct packet_header *ph =
                (struct packet_header *) evbuffer_pullup(input, sizeof *ph);

        if ((PROTO_PACKED != ph->proto) && (PROTO_EDONKEY != ph->proto)) {
            printf("%d# unknown packet protocol %c\n", clnt->idx, ph->proto);
            // close and remove client
        }

        // wait for full length packet
        packet_len = ph->length + sizeof(*ph);
        if (packet_len > src_len)
            return;

        data = evbuffer_pullup(input, packet_len);
        ph = (struct packet_header *) data;
        data += sizeof(*ph);

        if (PROTO_PACKED == ph->proto) {
            unsigned long unpacked_len = MAX_UNCOMPRESSED_PACKET_SIZE;
            unsigned char *unpacked = (unsigned char *) malloc(unpacked_len);

            ret = uncompress(unpacked, &unpacked_len, data + 1, ph->length - 1);
            if (Z_OK == ret) {
                PB_INIT(&pb, unpacked, unpacked_len);
                ret = process_packet(&pb, *data, clnt);
            } else {
                printf("%d# failed to unpack packet\n", clnt->idx);
                ret = -1;
            }
            free(unpacked);
        } else {
            PB_INIT(&pb, data + 1, ph->length - 1);
            ret = process_packet(&pb, *data, clnt);
        }

        if (ret < 0) {
            printf("%d# packet parsing error (opcode:%c)\n", clnt->idx, *(data + 1));
            // close and remove client
        }

        evbuffer_drain(input, packet_len);
        src_len = evbuffer_get_length(input);
    }
}

void event_cb(struct bufferevent *bev, short events, void *ctx)
{
    struct ebclient *clnt = (struct ebclient *) ctx;
    (void) bev;

    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        if (!clnt->connected) {
            printf("%d# failed to connect!\n", clnt->idx);
        } else {
            printf("%d# got error/EOF!\n", clnt->idx);
        }
        client_free(clnt);
    } else if (events & BEV_EVENT_CONNECTED) {
        clnt->connected = 1;
        send_login_request(clnt);
    }
}

void spawn_cb(evutil_socket_t fd, short what, void *ctx)
{
    static int idx = 0;
    (void) fd;
    (void) what;
    (void) ctx;

    if (g_eb.client_cnt > 0) {
        struct bufferevent *bev;
        struct ebclient *clnt;

        bev = bufferevent_socket_new(g_eb.evbase, -1, BEV_OPT_CLOSE_ON_FREE);
        clnt = (struct ebclient *) calloc(1, sizeof *clnt);
        clnt->idx = idx++;
        clnt->bev = bev;

        bufferevent_setcb(bev, read_cb, NULL, event_cb, (void *) clnt);
        bufferevent_enable(bev, EV_READ | EV_WRITE);

        if (bufferevent_socket_connect(bev, (struct sockaddr *) &g_eb.server_sa, sizeof(g_eb.server_sa)) < 0) {
            printf("%d# failed to connect\n", clnt->idx);
            bufferevent_free(bev);
            free(clnt);
        }

        g_eb.running_cnt++;
        g_eb.client_cnt--;
        if (g_eb.client_cnt) {
            event_add(g_eb.ev_spawn, g_eb.spawn_pause);
        }
    }
}

void signal_cb(evutil_socket_t fd, short what, void *ctx)
{
    (void) fd;
    (void) what;
    (void) ctx;

    printf("caught SIGINT, terminating...\n");
    event_base_loopexit(g_eb.evbase, NULL);
}

void display_version()
{
    puts(
            "ed2kd benchmark (eb) v" EB_VERSION "\n"
                    "Build on: "__DATE__ " " __TIME__
    );
}

void display_usage()
{
    puts(
            "Options:\n"
                    "--help, -h\tshow this help\n"
                    "--version, -v\tprint version\n"
                    "--server,-s <addr>\tserver address(ipv4:[port])\n"
                    "--concurrency, -c <count>\tconcurrent working clients\n"
                    "--spawn-pause, -w <msecs>\t pause between client spawning(default:" CSTR(DEFAULT_SPAWN_PAUSE) "ms)\n"
                    "--repeat, -r <count>\trepeat <count> times\n"
                    "--action-pause, -p <msecs>\t pause between activities (default:" CSTR(DEFAULT_ACTION_PAUSE) "ms)\n"
                    "--offer, -O\toffer files\n"
                    "--query, -Q\tsearch queries\n"
                    "--source, -S\tsources requests"
    );
}

int main(int argc, char *argv[])
{
    int ret, opt, longIndex = 0;
    struct event *ev_sigint;
    unsigned offer_flag = 0, query_flag = 0, source_flag = 0;
    struct timeval tv_action = {0, 0}, tv_spawn = {0, 0};
    char *server_addr = NULL;

    // parse command line arguments
    opt = getopt_long(argc, argv, optString, longOpts, &longIndex);
    while (opt != -1) {
        switch (opt) {
            case 'v':
                display_version();
                return EXIT_SUCCESS;

            case 'h':
                display_usage();
                return EXIT_SUCCESS;

            case 's':
                server_addr = optarg;
                break;

            case 'p': {
                int val = atoi(optarg);
                tv_action.tv_sec = val / 1000;
                tv_action.tv_usec = (val % 1000) * 1000;
                break;
            }

            case 'c':
                g_eb.client_cnt = atoi(optarg);
                break;

            case 'w': {
                int val = atoi(optarg);
                tv_spawn.tv_sec = val / 1000;
                tv_spawn.tv_usec = (val % 1000) * 1000;
                break;
            }

            case 'r':
                g_eb.repeat_cnt = atoi(optarg);
                break;

            case 'O':
                offer_flag = 1;
                break;

            case 'Q':
                query_flag = 1;
                break;

            case 'S':
                source_flag = 1;
                break;

            default:
                display_usage();
                return EXIT_FAILURE;
        }
        opt = getopt_long(argc, argv, optString, longOpts, &longIndex);
    }

    ret = 0;
    if (server_addr) {
        int sa_len = sizeof(g_eb.server_sa);
        ret = evutil_parse_sockaddr_port(server_addr, (struct sockaddr *) &g_eb.server_sa, &sa_len);
    }
    if (!server_addr || (ret < 0) || (g_eb.repeat_cnt <= 0) || (g_eb.client_cnt <= 0)) {
        display_usage();
        return EXIT_FAILURE;
    }

    // prepare array with user selected actions
    if (offer_flag) {
        g_eb.actions[g_eb.action_cnt++] = ACTION_OFFER;
    }
    if (query_flag) {
        g_eb.actions[g_eb.action_cnt++] = ACTION_QUERY;
    }
    if (source_flag) {
        g_eb.actions[g_eb.action_cnt] = ACTION_SOURCE;
    }

    if (!g_eb.action_cnt) {
        g_eb.repeat_cnt = 0;
    }

    if (tv_spawn.tv_sec == 0 && tv_spawn.tv_usec == 0) {
        tv_spawn.tv_sec = DEFAULT_SPAWN_PAUSE / 1000;
        tv_spawn.tv_usec = (DEFAULT_SPAWN_PAUSE % 1000) * 1000;
    }

    if (tv_action.tv_sec == 0 && tv_action.tv_usec == 0) {
        tv_action.tv_sec = DEFAULT_ACTION_PAUSE / 1000;
        tv_action.tv_usec = (DEFAULT_ACTION_PAUSE % 1000) * 1000;
    }

    if (evutil_secure_rng_init() < 0) {
        printf("Failed to seed random number generator\n");
        return EXIT_FAILURE;
    }
    srand((unsigned int) time(NULL));

#ifdef WIN32
    {
        WSADATA WSAData;
        if ( 0 != WSAStartup(0x0201, &WSAData) ) {
            printf("WSAStartup failed!\n");
            return EXIT_FAILURE;
        }
    }
#endif

    g_eb.evbase = event_base_new();
    if (NULL == g_eb.evbase) {
        printf("Failed to create main event loop\n");
        return EXIT_FAILURE;
    }

    // setup signals
    ev_sigint = evsignal_new(g_eb.evbase, SIGINT, signal_cb, NULL);
    evsignal_add(ev_sigint, NULL);

    // setup common timers timeouts
    g_eb.action_pause = event_base_init_common_timeout(g_eb.evbase, &tv_action);
    g_eb.spawn_pause = event_base_init_common_timeout(g_eb.evbase, &tv_spawn);

    // setup spawn timer
    g_eb.ev_spawn = evtimer_new(g_eb.evbase, spawn_cb, NULL);
    event_add(g_eb.ev_spawn, g_eb.spawn_pause);

    ret = event_base_dispatch(g_eb.evbase);
    if (ret < 0) {
        printf("Main dispatch loop finished with error\n");
    }
    else if (0 == ret) {
        printf("No active events in main loop\n");
    }

    event_free(g_eb.ev_spawn);
    event_free(ev_sigint);
    event_base_free(g_eb.evbase);

    return EXIT_SUCCESS;
}
