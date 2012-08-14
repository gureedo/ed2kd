#pragma once

#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>
#include "job.h"
#include <pthread.h>
#include <atomic_ops.h>

struct search_node;

#define MAX_NICK_LEN		255
#define	MAX_FOUND_SOURCES	200
#define	MAX_FOUND_FILES 	200
#define PORTCHECK_TIMEOUT   5 // ms

enum portcheck_result {
    PORTCHECK_FAILED,
    PORTCHECK_SUCCESS
};

struct client {
    /* ed2k hash */
    unsigned char hash[16];
    /* ip address (network order) */
    uint32_t ip;
    /* port (from OP_LOGIN) */
    uint16_t port;
    /* ed2k id */
    uint32_t id;
    /* nick */
    char nick[MAX_NICK_LEN+1];
    /* nick length */
    uint16_t nick_len;
    /* tcp flags */
    uint32_t tcp_flags;
    /* shared files counter */
    uint32_t file_count;
    /* remote port check status flag */
    unsigned portcheck_finished:1;
    /* lowid flag */
    unsigned lowid:1;

    /* server bufferevent */
    struct bufferevent *bev_srv;
    /* portcheck bufferevent */
    struct bufferevent *bev_pc;
    /* portcheck timeout timer */
    struct event *evtimer_portcheck;

    /* local job queue access mutex */
    pthread_mutex_t job_mutex;
    /* pending events count */
    volatile AO_t pending_evcnt;
    /* scheduled deletion flag */
    volatile AO_t sched_del;
    /* local job queue */
    struct job_queue jqueue;

#ifdef DEBUG
    /* for debugging only */
    struct {
        /* ip address string */
        char ip_str[16];
    } dbg;
#endif
};

struct client *client_new();

void client_schedule_delete( struct client *clnt );

void client_delete( struct client *clnt );

void client_portcheck_start( struct client *client );

void client_portcheck_finish( struct client *clnt, enum portcheck_result result );

void client_search_files( struct client *clnt, struct search_node *search_tree );

void client_get_sources( struct client *clnt, const unsigned char *hash );

#endif // CLIENT_H
