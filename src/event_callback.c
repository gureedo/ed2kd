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
        struct job *job = (struct job *)calloc(1, sizeof *job);
        (void)bev;

        job->type = JOB_SERVER_READ;
        job->client = (struct client*)ctx;

        server_add_job(job);
}

void server_event_cb( struct bufferevent *bev, short events, void *ctx )
{

        struct job_event *job = (struct job_event*)calloc(1, sizeof *job);
        (void)bev;

        job->hdr.type = JOB_SERVER_EVENT;
        job->hdr.client = (struct client*)ctx;
        job->events = events;

        server_add_job((struct job*)job);
}

void server_accept_cb( struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *ctx )
{
        struct job_server_accept *job = (struct job_server_accept*)calloc(1, sizeof *job);
        (void)listener;
        (void)ctx;

        job->hdr.type = JOB_SERVER_ACCEPT;
        job->fd =fd;
        memcpy(&job->sa, sa, socklen);
        job->socklen = socklen;

        server_add_job((struct job*)job);
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

void server_status_notify_cb( evutil_socket_t fd, short events, void *ctx )
{
        struct job *job = (struct job*)calloc(1, sizeof(*job));
        (void)fd;
        (void)events;

        job->type = JOB_SERVER_STATUS_NOTIFY;
        job->client = (struct client*)ctx;

        server_add_job(job);
}

void portcheck_read_cb( struct bufferevent *bev, void *ctx )
{
        struct job *job = (struct job *)calloc(1, sizeof(*job));
        (void)bev;

        job->type = JOB_PORTCHECK_READ;
        job->client = (struct client*)ctx;

        server_add_job((struct job*)job);
}

void portcheck_timeout_cb( evutil_socket_t fd, short events, void *ctx )
{
        struct job *job = (struct job*)calloc(1, sizeof(*job));
        (void)fd;
        (void)events;

        job->type = JOB_PORTCHECK_TIMEOUT;
        job->client = (struct client*)ctx;

        server_add_job(job);
}

void portcheck_event_cb( struct bufferevent *bev, short events, void *ctx )
{
        struct job_event *job = (struct job_event*)calloc(1, sizeof(*job));
        (void)bev;

        job->hdr.type = JOB_PORTCHECK_EVENT;
        job->hdr.client = (struct client*)ctx;
        job->events = events;

        server_add_job((struct job*)job);
}
