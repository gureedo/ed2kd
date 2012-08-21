#ifndef JOB_H
#define JOB_H

#include <event2/util.h>
#include "queue.h"

struct client;

enum job_type {
        JOB_SERVER_ACCEPT,
        JOB_SERVER_EVENT,
        JOB_SERVER_READ,
        JOB_SERVER_STATUS_NOTIFY,
        JOB_PORTCHECK_EVENT,
        JOB_PORTCHECK_READ,
        JOB_PORTCHECK_TIMEOUT
};

struct job {
        enum job_type type;
        struct client *client;
        STAILQ_ENTRY(job) qentry;
};

struct job_server_accept {
        struct job hdr;
        evutil_socket_t fd;
        struct sockaddr sa;
        int socklen;
};

struct job_event {
        struct job hdr;
        short events;
};

STAILQ_HEAD(job_queue, job);

#endif // JOB_H
