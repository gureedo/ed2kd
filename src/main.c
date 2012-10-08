#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#if defined(_WIN32)
#include <winsock2.h>
#endif
#include <omp.h>

#include <event2/event.h>
#include <event2/thread.h>
#include <event2/listener.h>

#include "version.h"
#include "util.h"
#include "log.h"
#include "ed2k_proto.h"
#include "server.h"
#include "db.h"

struct server_instance g_srv;

// command line options
static const char *optString = "vhg";
static const struct option longOpts[] = {
        { "version", no_argument, NULL, 'v'},
        { "help", no_argument, NULL, 'h' },
        { "genhash", no_argument, NULL, 'g' },
        { NULL, no_argument, NULL, 0 }
};

static void display_version( void )
{
        puts(
                "ed2kd v" ED2KD_VER_STR "\n"
                "Build on: "__DATE__ " " __TIME__
        );
}

static void display_usage( void )
{
        puts(
                "Options:\n"
                "--help, -h\tshow this help\n"
                "--version, -v\tprint version\n"
                "--genhash, -g\tgenerate random ed2k hash"
        );
}

static void sigint_cb( evutil_socket_t fd, short what, void *ctx )
{
        (void)fd;
        (void)what;
        (void)ctx;
        ED2KD_LOGNFO("caught SIGINT, terminating...");
        server_stop();
}

static void display_libevent_info( void )
{
        int i;
        const char **methods = event_get_supported_methods();
        if ( NULL == methods ) {
                ED2KD_LOGERR("failed to get supported libevent methods");
                return;
        }
        ED2KD_LOGNFO("using libevent %s. available methods are:", event_get_version());
        for ( i=0; methods[i] != NULL; ++i ) {
                ED2KD_LOGNFO("    %s", methods[i]);
        }
}

int main( int argc, char *argv[] )
{
        size_t i;
        int ret, opt, longIndex = 0;
        struct event *evsig_int;
#ifdef _WIN32
        WSADATA WSAData;
#endif
        pthread_t tcp_thread, *job_threads;

        if ( evutil_secure_rng_init() < 0 ) {
                ED2KD_LOGERR("failed to seed random number generator");
                return EXIT_FAILURE;
        }

        /* parse command line arguments */
        opt = getopt_long( argc, argv, optString, longOpts, &longIndex );
        while( opt != -1 ) {
                switch( opt ) {
                case 'v':
                        display_version();
                        return EXIT_SUCCESS;

                case 'g': {
                        unsigned char hash[ED2K_HASH_SIZE];
                        char hex_hash[sizeof(hash)*2+1];
                        get_random_user_hash(hash);
                        bin2hex(hash, hex_hash, sizeof(hex_hash));
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

#ifdef _WIN32
        if ( 0 != WSAStartup(0x0201, &WSAData) ) {
                ED2KD_LOGERR("WSAStartup failed!");
                return EXIT_FAILURE;
        }
#endif

        if ( !server_load_config(NULL) ) {
                ED2KD_LOGERR("failed to load configuration file");
                return EXIT_FAILURE;
        }

        display_libevent_info();

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

        g_srv.evbase_main = event_base_new();
        if ( NULL == g_srv.evbase_main ) {
                ED2KD_LOGERR("failed to create main event loop");
                return EXIT_FAILURE;
        }
        g_srv.evbase_tcp = event_base_new();
        if ( NULL == g_srv.evbase_tcp ) {
                ED2KD_LOGERR("failed to create tcp event loop");
                return EXIT_FAILURE;
        }

        evsig_int = evsignal_new(g_srv.evbase_main, SIGINT, sigint_cb, NULL);
        evsignal_add(evsig_int, NULL);

        // common timers timevals
        g_srv.portcheck_timeout_tv = event_base_init_common_timeout(g_srv.evbase_tcp, &g_srv.cfg->portcheck_timeout_tv);
        g_srv.status_notify_tv = event_base_init_common_timeout(g_srv.evbase_tcp, &g_srv.cfg->status_notify_tv);

        if ( db_create() < 0 ) {
                ED2KD_LOGERR("failed to create database");
                return EXIT_FAILURE;
        }

        g_srv.thread_count = omp_get_num_procs() + 1;

        pthread_cond_init(&g_srv.job_cond, NULL);
        pthread_mutex_init(&g_srv.job_mutex, NULL);
        TAILQ_INIT(&g_srv.jqueue);

        job_threads = (pthread_t *)malloc(g_srv.thread_count * sizeof(*job_threads));

        // start tcp worker threads
        for ( i=0; i<g_srv.thread_count; ++i ) {
                pthread_create(&job_threads[i], NULL, server_job_worker, NULL);
        }

        // start tcp dispatch thread
        pthread_create(&tcp_thread, NULL, server_base_worker, g_srv.evbase_tcp);

        // start tcp listen loop
        if ( !server_listen() ) {
                ED2KD_LOGERR("failed to start server listener");
                server_stop();
        }

        pthread_join(tcp_thread, NULL);

        while ( EBUSY == pthread_cond_destroy(&g_srv.job_cond) ) {
                pthread_cond_broadcast(&g_srv.job_cond);
                //pthread_yield();
        }

        pthread_mutex_destroy(&g_srv.job_mutex);

        for ( i=0; i<g_srv.thread_count; ++i ) {
                pthread_join(job_threads[i], NULL);
        }

        free(job_threads);

        // todo: free job queue items

        evconnlistener_free(g_srv.tcp_listener);
        event_free(evsig_int);
        event_base_free(g_srv.evbase_tcp);
        event_base_free(g_srv.evbase_main);

        if ( db_destroy() < 0 ) {
                ED2KD_LOGERR("failed to destroy database");
        }

        server_free_config();

        return EXIT_SUCCESS;
}
