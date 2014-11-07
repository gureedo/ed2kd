#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>
#include <pthread.h>
#include "uthash.h"
#include "atomic.h"
#include "util.h"

struct search_node;
struct shared_file_entry;
struct pub_file;

#define MAX_NICK_LEN		255
#define	MAX_FOUND_SOURCES	200 // move to config
#define	MAX_FOUND_FILES 	200 // move to config

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
        /* set of already shared files hashes */
        struct shared_file_entry *shared_files;

        /* connection bufferevent */
        struct bufferevent *bev;
        /* portcheck bufferevent */
        struct bufferevent *bev_pc;
        /* portcheck timeout timer */
        struct event *evtimer_portcheck;
        /* status notify timer */
        struct event *evtimer_status_notify;

        /* lock flag */
        volatile atomic32_t locked;
        /* references counter */
        volatile atomic32_t ref_cnt;
        /* marked for remove flag */
        volatile atomic32_t deleted;

        /* offer limit */
        struct token_bucket limit_offer;

        /* search limit */
        struct token_bucket limit_search;

#ifdef USE_DEBUG
        /* for debugging only */
        struct {
                /* ip address string */
                char ip_str[16];
        } dbg;
#endif
};

/**
  @brief allocates and initializes empty client structure
  @return pointer to new client structure
*/
struct client *client_new();

void client_delete( struct client *clnt );

static __inline void client_addref( struct client *clnt )
{
        atomic_inc(&clnt->ref_cnt);
}

static __inline void client_decref( struct client *clnt )
{
        if ( !atomic_dec(&clnt->ref_cnt) && atomic_load(&clnt->deleted) )
                client_delete(clnt);
}

void client_portcheck_start( struct client *client );

void client_portcheck_finish( struct client *clnt, enum portcheck_result result );

void client_search_files( struct client *clnt, struct search_node *search_tree );

void client_get_sources( struct client *clnt, const unsigned char *hash );

void client_share_files( struct client *clnt, struct pub_file *files, size_t count );

#endif // CLIENT_H
