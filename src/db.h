#ifndef DB_H
#define DB_H

/*
@file db.h Database stuff
*/

#include <stdint.h>
#include <stddef.h>

struct evbuffer;
struct client;
struct file_source;

#define MAX_FILENAME_LEN    255
#define MAX_MCODEC_LEN      64
#define MAX_FILEEXT_LEN     16

struct pub_file {
    unsigned char hash[16];
    uint16_t name_len;
    char name[MAX_FILENAME_LEN + 1];
    uint64_t size;
    uint32_t rating;
    uint32_t type;
    uint32_t media_length;
    uint32_t media_bitrate;
    uint16_t media_codec_len;
    char media_codec[MAX_MCODEC_LEN + 1];
    unsigned char complete;
};

enum search_node_type {
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
};

struct search_node {
    enum search_node_type type;
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
};

/**
@return non-zero on success
*/
int db_create();

/**
@return non-zero on success
*/
int db_destroy();

/**
@return non-zero on success
*/
int db_open();

/**
@return non-zero on success
*/
int db_close();

/**
@return non-zero on success
*/
int db_share_files(const struct pub_file *files, size_t count, const struct client *owner);

/**
@return non-zero on success
*/
int db_remove_source(const struct client *owner);

/**
@return non-zero on success
*/
int db_search_files(struct search_node *root, struct evbuffer *buf, size_t *count);

/**
@return non-zero on success
*/
int db_get_sources(const unsigned char *hash, struct file_source *out_sources, uint8_t *size);

#endif // DB_H
