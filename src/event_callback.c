#include "event_callback.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <event2/event.h>
#include "server.h"
#include "client.h"
#include "log.h"

void signal_cb( evutil_socket_t fd, short what, void *ctx )
{
    (void)fd;
    (void)what;
    (void)ctx;
    ED2KD_LOGNFO("caught SIGINT, terminating...");
    event_base_loopexit(g_instance.evbase, NULL);
}

void server_read_cb( struct bufferevent *bev, void *ctx )
{
    job_t *job = (job_t *)calloc(sizeof *job, 1);
    (void)bev;

    job->type = JOB_SERVER_READ;
    job->client = (client_t*)ctx;

    server_add_job(job);
}

void server_event_cb( struct bufferevent *bev, short events, void *ctx )
{

    job_event_t *job = (job_event_t *)calloc(sizeof *job, 1);
    (void)bev;

    job->hdr.type = JOB_SERVER_EVENT;
    job->hdr.client = (client_t*)ctx;
    job->events = events;

    server_add_job((job_t*)job);
}

void server_accept_cb( struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *ctx )
{
    job_server_accept_t *job = (job_server_accept_t*)calloc(sizeof *job, 1);
    (void)listener;
    (void)ctx;

    job->hdr.type = JOB_SERVER_ACCEPT;
    job->fd =fd;
    job->sa = sa;
    job->socklen = socklen;

    server_add_job((job_t*)job);
}

void server_accept_error_cb( struct evconnlistener *listener, void *ctx )
{
    int err = EVUTIL_SOCKET_ERROR();
    (void)listener;
    (void)ctx;

    ED2KD_LOGERR("error %d (%s) on the listener. terminating...", \
        err, evutil_socket_error_to_string(err));

    event_base_loopexit(g_instance.evbase, NULL);
}

void client_read_cb( struct bufferevent *bev, void *ctx )
{
    job_t *job = (job_t *)calloc(sizeof *job, 1);
    (void)bev;

    job->type = JOB_CLIENT_READ;
    job->client = (client_t*)ctx;

    server_add_job((job_t*)job);
}

void client_event_cb( struct bufferevent *bev, short events, void *ctx )
{
    job_event_t *job = (job_event_t *)calloc(sizeof *job, 1);
    (void)bev;

    job->hdr.type = JOB_CLIENT_EVENT;
    job->hdr.client = (client_t*)ctx;
    job->events = events;

    server_add_job((job_t*)job);
}
