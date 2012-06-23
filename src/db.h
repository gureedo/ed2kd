#ifndef DB_H
#define DB_H

#include "packed_struct.h"

#define MAX_FILENAME_LEN	255
#define MAX_FILETAG_LEN		64

PACKED_STRUCT(
struct e_source {
	uint32_t ip;
	uint16_t port;
};
)

struct e_file {
	unsigned char hash[16];
	uint16_t name_len;
	char name[MAX_FILENAME_LEN+1];
	uint64_t size;
	uint8_t rating;
	uint8_t type;
	uint32_t media_length;
	uint32_t media_bitrate;
    uint16_t media_codec_len;
	char media_codec[MAX_FILETAG_LEN+1];
    // flags
    unsigned complete:1; 
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
    ST_MINLENGTH,
};

struct search_node {
    int type;
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

int db_open();
int db_close();

int db_add_file( const struct e_file *file, const struct e_client *owner );
int db_remove_source( const struct e_client *owner );
int db_search_file(struct search_node *tree, struct e_file *files, size_t *count );
int db_get_sources( const unsigned char *hash, struct e_source *buf, size_t *size );


#endif // DB_H 