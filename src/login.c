#include "login.h"
#include <assert.h>
#include <pthread.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>

#include "server.h"
#include "log.h"
#include "event_callback.h"
#include "client.h"
#include "packet.h"

static void accept_cb( struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *ctx )
{
        struct sockaddr_in *peer_sa = (struct sockaddr_in*)sa;
        struct client *clnt;
        struct bufferevent *bev;

        assert(sizeof(struct sockaddr_in) == socklen);

        // todo: limit total client count
        // todo: limit connections from same ip
        // todo: block banned ips

        clnt = client_new();

        bev = bufferevent_socket_new(g_instance.evbase_tcp, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
        clnt->bev = bev;
        clnt->ip = peer_sa->sin_addr.s_addr;

#ifdef DEBUG
        evutil_inet_ntop(AF_INET, &(clnt->ip), clnt->dbg.ip_str, sizeof clnt->dbg.ip_str);
        ED2KD_LOGDBG("client connected (%s)", clnt->dbg.ip_str);
#endif

        bufferevent_setcb(clnt->bev, tcp_read_cb, NULL, tcp_event_cb, clnt);
        bufferevent_enable(clnt->bev, EV_READ|EV_WRITE);

        send_server_message(clnt->bev, g_instance.cfg->welcome_msg, g_instance.cfg->welcome_msg_len);

        if ( !g_instance.cfg->allow_lowid ) {
                static const char msg_highid[] = "WARNING: Only HighID clients!";
                send_server_message(clnt->bev, msg_highid, sizeof(msg_highid) - 1);
        }

        // todo: set timeout for op_login
}

static void accept_error_cb( struct evconnlistener *listener, void *ctx )
{
        int err = EVUTIL_SOCKET_ERROR();
        (void)listener;
        (void)ctx;

        ED2KD_LOGERR("error %d (%s) on the tcp listener, terminating...", \
                err, evutil_socket_error_to_string(err));

        event_base_loopexit(g_instance.evbase_tcp, NULL);
}

static void *login_worker( void *ctx )
{
        int ret;

        ret = event_base_dispatch(g_instance.evbase_login);
        if ( ret < 0 ) {
                ED2KD_LOGERR("login loop finished with error");
        }
        else if ( 0 == ret ) {
                ED2KD_LOGWRN("no active events in login loop");
        }

        return NULL;
}

int start_login_thread( void )
{
        int ret;
        pthread_t thread;
        struct sockaddr_in bind_sa;
        int bind_sa_len;

        bind_sa_len = sizeof(bind_sa);
        memset(&bind_sa, 0, sizeof(bind_sa));
        ret = evutil_parse_sockaddr_port(g_instance.cfg->listen_addr, (struct sockaddr*)&bind_sa, &bind_sa_len);
        if ( ret < 0 ) {
                ED2KD_LOGERR("failed to parse listen addr '%s'", g_instance.cfg->listen_addr);
                return EXIT_FAILURE;
        }
        bind_sa.sin_port = htons(g_instance.cfg->listen_port);
        bind_sa.sin_family = AF_INET;

        g_instance.tcp_listener = evconnlistener_new_bind(g_instance.evbase_login,
                accept_cb, NULL, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
                g_instance.cfg->listen_backlog, (struct sockaddr*)&bind_sa, sizeof(bind_sa) );
        if ( NULL == g_instance.tcp_listener ) {
                int err = EVUTIL_SOCKET_ERROR();
                ED2KD_LOGERR("failed to start listen on %s:%u, last error: %s", g_instance.cfg->listen_addr, g_instance.cfg->listen_port, evutil_socket_error_to_string(err));
                return -1;
        }

        evconnlistener_set_error_cb(g_instance.tcp_listener, accept_error_cb);

        ED2KD_LOGNFO("start listening on %s:%u", g_instance.cfg->listen_addr, g_instance.cfg->listen_port);

        pthread_create(&thread, NULL, login_worker, NULL);

        return 0;
}

