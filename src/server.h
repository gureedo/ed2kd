#ifndef SERVER_H
#define SERVER_H

/**
  @file server.h General server configuration variables and routines
*/

#include <stdint.h>
#include "job.h"
#include <pthread.h>
#include <atomic_ops.h>

struct event_base;
struct bufferevent;
struct client;

#define MAX_WELCOMEMSG_LEN		1024
#define MAX_SERVER_NAME_LEN		64
#define MAX_SERVER_DESCR_LEN	64
#define MAX_SEARCH_FILES        200
#define MAX_UNCOMPRESSED_PACKET_SIZE  300*1024

// variables loaded from config file or initialized at startup
typedef struct server_config
{
    char listen_addr[15];
	uint32_t listen_addr_inaddr;
    uint16_t listen_port;
    int listen_backlog;
	unsigned char hash[16];
    uint32_t srv_tcp_flags;

	size_t welcome_msg_len;
    char welcome_msg[MAX_WELCOMEMSG_LEN+1];

	size_t server_name_len;
	char server_name[MAX_SERVER_NAME_LEN+1];

	size_t server_descr_len;
	char server_descr[MAX_SERVER_DESCR_LEN+1];

    //flags
    unsigned allow_lowid:1;
} server_config_t;

typedef struct server_instance {
    struct event_base *evbase;
    const server_config_t *cfg;
    size_t thread_count;
	volatile AO_t user_count;
	volatile AO_t file_count;
    volatile AO_t lowid_counter;

    volatile AO_t terminate;
    pthread_mutex_t job_mutex;
    pthread_cond_t job_cond;
    job_queue_t jqueue;
} server_instance_t;

extern server_instance_t g_instance;

void server_add_job( job_t *job );

void *server_job_worker( void *ctx );

void server_remove_client_jobs( const struct client *clnt );

#endif // SERVER_H