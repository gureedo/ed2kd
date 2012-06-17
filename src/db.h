#ifndef DB_H
#define DB_H

#include "packed_struct.h"

#define MAX_FILENAME_LEN	255

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
};

int db_open();
int db_add_file( const struct e_file *file, const struct e_client *owner );
int db_remove_source( const struct e_client *owner );
int db_get_sources( const unsigned char *hash, struct e_source *buf, size_t *size );


#endif // DB_H 