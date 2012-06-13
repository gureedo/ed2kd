#ifndef FILE_H
#define FILE_H

#include "packed_struct.h"

PACKED_STRUCT(
struct e_source {
	uint32_t ip;
	uint16_t port;
};
)

struct e_file {
	uint32_t ref;
	unsigned char hash[16];
	char name[256];
	uint64_t size;
};

struct e_file *file_new();
struct e_file *file_add( struct e_file *file, uint32_t client_ip, uint16_t client_port );
void file_delete( struct e_file *file, uint32_t client_ip, uint16_t client_port );
uint32_t file_get_sources( const unsigned char *hash, struct e_source *buf, uint32_t size );


#endif // FILE_H 