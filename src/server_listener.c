#include "server.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>

#include "log.h"
#include "client.h"
#include "packet.h"

static void accept_cb( struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *ctx )
{
        struct sockaddr_in *peer_sa = (struct sockaddr_in*)sa;
        struct client *clnt;
        struct bufferevent *bev;

        (void)listener;
        (void)ctx;

        assert(sizeof(struct sockaddr_in) == socklen);

        // todo: limit total client count
        // todo: limit connections from same ip
        // todo: block banned ips

        clnt = client_new();

        bev = bufferevent_socket_new(g_srv.evbase_tcp, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
        clnt->bev = bev;
        clnt->ip = peer_sa->sin_addr.s_addr;

#ifdef USE_DEBUG
        evutil_inet_ntop(AF_INET, &(clnt->ip), clnt->dbg.ip_str, sizeof(clnt->dbg.ip_str));
        ED2KD_LOGDBG("connected ip:%s", clnt->dbg.ip_str);
#endif

        bufferevent_setcb(clnt->bev, server_read_cb, NULL, server_event_cb, clnt);
        bufferevent_enable(clnt->bev, EV_READ|EV_WRITE);

        // todo: set timeout for op_login
}

static void accept_error_cb( struct evconnlistener *listener, void *ctx )
{
        int err = EVUTIL_SOCKET_ERROR();
        (void)listener;
        (void)ctx;

        ED2KD_LOGERR("error %d (%s) on the tcp listener, terminating...", \
                err, evutil_socket_error_to_string(err));

        server_stop();
}

int server_listen( void )
{
        int ret;
        struct sockaddr_in bind_sa;
        int bind_sa_len;

        bind_sa_len = sizeof(bind_sa);
        memset(&bind_sa, 0, sizeof(bind_sa));
        ret = evutil_parse_sockaddr_port(g_srv.cfg->listen_addr, (struct sockaddr*)&bind_sa, &bind_sa_len);
        if ( ret < 0 ) {
                ED2KD_LOGERR("failed to parse listen addr '%s'", g_srv.cfg->listen_addr);
                return -1;
        }
        bind_sa.sin_port = htons(g_srv.cfg->listen_port);
        bind_sa.sin_family = AF_INET;

        g_srv.tcp_listener = evconnlistener_new_bind(g_srv.evbase_main,
                accept_cb, NULL, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
                g_srv.cfg->listen_backlog, (struct sockaddr*)&bind_sa, sizeof(bind_sa) );
        if ( NULL == g_srv.tcp_listener ) {
                int err = EVUTIL_SOCKET_ERROR();
                ED2KD_LOGERR("failed to start listen on %s:%u, last error: %s", g_srv.cfg->listen_addr, g_srv.cfg->listen_port, evutil_socket_error_to_string(err));
                return -1;
        }

        evconnlistener_set_error_cb(g_srv.tcp_listener, accept_error_cb);

        ED2KD_LOGNFO("start listening on %s:%u", g_srv.cfg->listen_addr, g_srv.cfg->listen_port);

        ret = event_base_dispatch(g_srv.evbase_main);
        if ( ret < 0 )
                ED2KD_LOGERR("main loop finished with error");

        return 0;
}

