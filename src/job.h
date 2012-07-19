#ifndef JOB_H
#define JOB_H

#include <sys/queue.h>
#include <event2/util.h>

struct client;

typedef enum job_type {
    JOB_SERVER_ACCEPT,
    JOB_SERVER_EVENT,
    JOB_SERVER_READ,
    JOB_CLIENT_EVENT,
    JOB_CLIENT_READ,
    JOB_CLIENT_DELETE
} job_type_t;

typedef struct job {
    job_type_t type;
    struct client *client;
    STAILQ_ENTRY(job) qentry;
} job_t;

typedef struct job_server_accept {
    job_t hdr;
    evutil_socket_t fd;
    struct sockaddr *sa;
    int socklen;
} job_server_accept_t;

typedef struct job_event {
    job_t hdr;
    short events;
} job_event_t;

STAILQ_HEAD(job_queue, job);
typedef struct job_queue job_queue_t;

#endif