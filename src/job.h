#ifndef JOB_H
#define JOB_H

#include "queue.h"
#include <event2/util.h>

struct sockaddr;
struct bufferevent;
struct evconnlistener;
struct client;

enum job_type {
        JOB_SERVER_EVENT,
        JOB_SERVER_READ,
        JOB_SERVER_STATUS_NOTIFY,
        JOB_PORTCHECK_EVENT,
        JOB_PORTCHECK_READ,
        JOB_PORTCHECK_TIMEOUT
};

struct job {
        enum job_type type;
        struct client *clnt;
        TAILQ_ENTRY(job) qentry;
};

struct job_event {
        struct job hdr;
        short events;
};

TAILQ_HEAD(job_queue, job);

void server_read_cb( struct bufferevent *bev, void *ctx );
void server_event_cb( struct bufferevent *bev, short events, void *ctx );
void server_status_notify_cb( evutil_socket_t fd, short events, void *ctx );

void portcheck_read_cb( struct bufferevent *bev, void *ctx );
void portcheck_event_cb( struct bufferevent *bev, short events, void *ctx );
void portcheck_timeout_cb( evutil_socket_t fd, short events, void *ctx );

#endif // JOB_H
