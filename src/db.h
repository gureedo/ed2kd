#ifndef DB_H
#define DB_H

/*
  @file db.h Database stuff
*/

#include <stdint.h>
#include "packed_struct.h"

struct client;
struct evbuffer;

#define MAX_FILENAME_LEN    255
#define MAX_MCODEC_LEN      64
#define MAX_FILEEXT_LEN     16

PACKED_STRUCT(
struct file_source {
    uint32_t ip;
    uint16_t port;
};
)
typedef struct file_source file_source_t;

typedef struct pub_file {
    unsigned char hash[16];
    uint16_t name_len;
    char name[MAX_FILENAME_LEN+1];
    uint64_t size;
    uint32_t rating;
    uint32_t type;
    uint32_t media_length;
    uint32_t media_bitrate;
    uint16_t media_codec_len;
    char media_codec[MAX_MCODEC_LEN+1];
    unsigned complete:1;
} pub_file_t;

typedef struct search_file {
    const unsigned char *hash;
    uint32_t client_id;
    uint16_t client_port;
    uint16_t name_len;
    const char *name;
    uint64_t size;
    uint32_t type;
    uint32_t rating;
    uint32_t rated_count;
    uint16_t ext_len;
    const char *ext;
    uint32_t media_length;
    uint32_t media_bitrate;
    uint16_t media_codec_len;
    const char *media_codec;
    uint32_t srcavail;
    uint32_t srccomplete;
} search_file_t;

typedef enum search_node_type {
    ST_EMPTY,
    // logical nodes
    ST_AND,
    ST_OR,
    ST_NOT,
    // string nodes
    ST_STRING,
    ST_EXTENSION,
    ST_CODEC,
    ST_TYPE,
    // int nodes
    ST_MINSIZE,
    ST_MAXSIZE,
    ST_SRCAVAIL,
    ST_SRCCOMLETE,
    ST_MINBITRATE,
    ST_MINLENGTH
} search_node_type_t;

typedef struct search_node {
    search_node_type_t type;
    struct search_node *parent;
    unsigned left_visited:1;
    unsigned right_visited:1;
    unsigned string_term:1;
    union {
        struct {
            uint16_t str_len;
            const char *str_val;
        };
        uint64_t int_val;
        struct {
            struct search_node *left;
            struct search_node *right;
        };
    };
} search_node_t;

int db_open();
int db_close();

int db_add_file( const pub_file_t *file, const struct client *owner );
int db_remove_source( const struct client *owner );
int db_search_file( search_node_t *root, struct evbuffer *buf, size_t *count );
int db_get_sources( const unsigned char *hash, file_source_t *out_sources, uint8_t *size );


#endif // DB_H
