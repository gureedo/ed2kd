#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#ifdef WIN32
#include <winsock2.h>
#endif
#ifdef __GNUC__
#include <alloca.h>
#endif
#include <omp.h>

#include <event2/event.h>
#include <event2/thread.h>
#include <event2/listener.h>
#include <event2/util.h>

#include "server.h"
#include "config.h"
#include "ed2k_proto.h"
#include "version.h"
#include "util.h"
#include "log.h"
#include "db.h"
#include "event_callback.h"

// command line options
static const char *optString = "vhg";
static const struct option longOpts[] = {
    { "version", no_argument, NULL, 'v'},
    { "help", no_argument, NULL, 'h' },
    { "gen-hash", no_argument, NULL, 'g' },
    { NULL, no_argument, NULL, 0 }
};

void display_version( void )
{
    puts("ed2kd v" ED2KD_VER_STR);
    puts("Build on: "__DATE__ " " __TIME__);
}

void display_usage( void )
{
    puts("Options:");
    puts("--help, -h\tshow this help");
    puts("--version, -v\tprint version");
    puts("--gen-hash, -G\tgenerate random user hash");
}

server_instance_t g_instance;

int main( int argc, char *argv[] )
{
    int ret, opt, longIndex = 0;
#ifdef WIN32
    WSADATA WSAData;
#endif

    struct event *sigint_event;
    struct sockaddr_in bind_sa;
    int bind_sa_len;
    struct evconnlistener *listener;

    if ( evutil_secure_rng_init() < 0 ) {
        ED2KD_LOGERR("Failed to seed random number generator");
        return EXIT_FAILURE;
    }

    // parse command line arguments
    opt = getopt_long( argc, argv, optString, longOpts, &longIndex );
    while( opt != -1 ) {
        switch( opt ) {
            case 'v':
                display_version();
                return EXIT_SUCCESS;

            case 'g': {
                unsigned char hash[HASH_SIZE];
                char hex_hash[(sizeof hash)*2+1];
                get_random_user_hash(hash);
                bin2hex(hash, hex_hash, sizeof hex_hash);
                puts(hex_hash);
                return EXIT_SUCCESS;
            }

            case 'h':
                display_usage();
                return EXIT_SUCCESS;

            default:
                return EXIT_FAILURE;
        }
        opt = getopt_long( argc, argv, optString, longOpts, &longIndex );
    }

    // load config
    if ( config_load(0) < 0 ) {
        ED2KD_LOGWRN("failed to load configuration file");
        return EXIT_FAILURE;
    }

#ifdef WIN32
    if ( 0 != WSAStartup(0x0201, &WSAData) ) {
        ED2KD_LOGWRN("WSAStartup failed!");
        return EXIT_FAILURE;
    }
#endif

    {
        int i;
        const char **methods = event_get_supported_methods();
        if ( NULL == methods ) {
            ED2KD_LOGERR("failed to get supported libevent methods");
            return EXIT_FAILURE;
        }
        ED2KD_LOGDBG("using libevent %s. available methods are:", event_get_version());
        for ( i=0; methods[i] != NULL; ++i ) {
            ED2KD_LOGDBG("    %s", methods[i]);
        }
    }

#ifdef EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED
    ret = evthread_use_windows_threads();
#elif EVTHREAD_USE_PTHREADS_IMPLEMENTED
    ret = evthread_use_pthreads();
#else
#error "unable to determine threading model"
#endif
    if ( ret < 0 ) {
        ED2KD_LOGERR("failed to init libevent threading model");
        return EXIT_FAILURE;
    }

    g_instance.evbase = event_base_new();
    if ( NULL == g_instance.evbase ) {
        ED2KD_LOGERR("failed to create main event loop");
        return EXIT_FAILURE;
    }

    // setup signals
    sigint_event = evsignal_new(g_instance.evbase, SIGINT, signal_cb, NULL);
    evsignal_add(sigint_event, NULL);

    bind_sa_len = sizeof(bind_sa);
    memset(&bind_sa, 0, sizeof(bind_sa));
    ret = evutil_parse_sockaddr_port(g_instance.cfg->listen_addr, (struct sockaddr*)&bind_sa, &bind_sa_len);
    bind_sa.sin_port = htons(g_instance.cfg->listen_port);
    bind_sa.sin_family = AF_INET;

    listener = evconnlistener_new_bind(g_instance.evbase,
        server_accept_cb, NULL, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
        g_instance.cfg->listen_backlog, (struct sockaddr*)&bind_sa, sizeof(bind_sa) );
    if ( NULL == listener ) {
        int err = EVUTIL_SOCKET_ERROR();
        ED2KD_LOGERR("failed to start listen on %s:%u, last error: %s", g_instance.cfg->listen_addr, g_instance.cfg->listen_port, evutil_socket_error_to_string(err));
        return EXIT_FAILURE;
    }

    evconnlistener_set_error_cb(listener, server_accept_error_cb);

    ED2KD_LOGNFO("start listening on %s:%u", g_instance.cfg->listen_addr, g_instance.cfg->listen_port);

    if ( db_open() < 0 ) {
        ED2KD_LOGERR("failed to open database");
        return EXIT_FAILURE;
    }

    {
        size_t i;
        pthread_t *threads;

        g_instance.thread_count = omp_get_num_procs() + 1;
        threads = (pthread_t*)alloca((sizeof *threads) * g_instance.thread_count);

        pthread_cond_init(&g_instance.job_cond, NULL);
        pthread_mutex_init(&g_instance.job_mutex, NULL);
        STAILQ_INIT(&g_instance.jqueue);

        for ( i=0; i<g_instance.thread_count; ++i ) {
            pthread_create(&threads[i], NULL, server_job_worker, NULL);
        }

        ret = event_base_dispatch(g_instance.evbase);
        if ( ret < 0 ) {
            ED2KD_LOGERR("main dispatch loop finished with error");
        }
        else if ( 0 == ret ) {
            ED2KD_LOGWRN("no active events in main loop");
        }

        if ( db_close() < 0 ) {
            ED2KD_LOGERR("failed to close database");
        }

        AO_store(&g_instance.terminate, 1);

        while ( EBUSY == pthread_cond_destroy(&g_instance.job_cond) ) {
            pthread_cond_broadcast(&g_instance.job_cond);
            //pthread_yield();
        }

        pthread_mutex_destroy(&g_instance.job_mutex);

        for ( i=0; i<g_instance.thread_count; ++i ) {
            pthread_join(threads[i], NULL);
        }
    }

    evconnlistener_free(listener);
    event_free(sigint_event);
    event_base_free(g_instance.evbase);

    config_free();

    return EXIT_SUCCESS;
}
