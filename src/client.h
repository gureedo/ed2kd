#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>
#include "job.h"
#include <pthread.h>
#include <atomic_ops.h>

struct search_node;
struct search_file;
struct evbuffer;

#define MAX_NICK_LEN		255
#define	MAX_FOUND_SOURCES	200
#define	MAX_FOUND_FILES 	200

typedef enum portcheck_result {
    PORTCHECK_FAILED,
    PORTCHECK_SUCCESS
} portcheck_result_t;

typedef struct client {
    unsigned char hash[16];
    uint32_t ip;
    uint16_t port;
    uint32_t id;
    char nick[MAX_NICK_LEN+1];
    uint16_t nick_len;
    uint32_t tcp_flags;
    uint32_t file_count;

    // flags
    unsigned portcheck_finished:1;
    unsigned lowid:1;

    struct bufferevent *bev_srv;
    struct bufferevent *bev_cli;

    pthread_mutex_t job_mutex;
    volatile AO_t pending_evcnt;
    volatile AO_t sched_del;
    job_queue_t jqueue;

#ifdef DEBUG
    struct {
        char ip_str[16];
    } dbg;
#endif
} client_t;

client_t *client_new();

void client_schedule_delete( client_t *clnt );

void client_delete( client_t *clnt );

void client_portcheck_finish( client_t *clnt, portcheck_result_t result );

void send_id_change( client_t *clnt );

void send_server_message( client_t *clnt, const char *msg, size_t len );

void send_server_ident( client_t *clnt );

void send_server_list( client_t *clnt );

void send_search_result( client_t *clnt, struct search_node *search_tree );

void send_found_sources( client_t *clnt, const unsigned char *hash );

void send_reject( client_t *clnt );

void send_callback_fail( client_t *clnt );

void write_search_file( struct evbuffer *buf, const struct search_file *file );

#endif // CLIENT_H
