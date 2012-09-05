#ifndef EVENT_CALLBACK_H
#define EVENT_CALLBACK_H

#include <event2/util.h>

struct sockaddr;
struct bufferevent;
struct evconnlistener;

void signal_cb( evutil_socket_t fd, short what, void *ctx );
void tcp_read_cb( struct bufferevent *bev, void *ctx );
void tcp_event_cb( struct bufferevent *bev, short events, void *ctx );

void tcp_status_notify_cb( evutil_socket_t fd, short events, void *ctx );

void portcheck_read_cb( struct bufferevent *bev, void *ctx );
void portcheck_timeout_cb( evutil_socket_t fd, short events, void *ctx );
void portcheck_event_cb( struct bufferevent *bev, short events, void *ctx );

#endif // EVENT_CALLBACK_H
