#ifndef EVENT_CALLBACK_H
#define EVENT_CALLBACK_H

#include <event2/util.h>

struct sockaddr;
struct bufferevent;
struct evconnlistener;

void signal_cb( evutil_socket_t fd, short what, void *ctx );
void server_read_cb( struct bufferevent *bev, void *ctx );
void server_event_cb( struct bufferevent *bev, short events, void *ctx );
void server_accept_cb( struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *ctx );
void server_accept_error_cb( struct evconnlistener *listener, void *ctx );

void client_read_cb( struct bufferevent *bev, void *ctx );
void client_event_cb( struct bufferevent *bev, short events, void *ctx );

#endif // EVENT_CALLBACK_H
