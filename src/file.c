#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tree.h>
#include "file.h"
#include "ed2kd.h"

struct src_node {
	union {
		struct e_source val;
		uint64_t key;
	};
	RB_ENTRY(src_node) rb_entry;
};

struct file_node {
	struct e_file val;
	RB_ENTRY(file_node) rb_entry;
	RB_HEAD(source_set, src_node) sources; 
};

int src_node_cmp( struct src_node *e1, struct src_node *e2 )
{
	return (e1->key < e2->key ? -1 : e1->key > e2->key);
}

int file_node_cmp( struct file_node *e1, struct file_node *e2 )
{
	// todo: implement it in another way
	return memcmp(e1->val.hash, e2->val.hash, sizeof e1->val.hash);
}

RB_GENERATE_STATIC(source_set, src_node, rb_entry, src_node_cmp)

RB_HEAD(file_map, file_node) g_files = RB_INITIALIZER(&g_files);
RB_GENERATE_STATIC(file_map, file_node, rb_entry, file_node_cmp)

struct e_file *file_new()
{
	struct file_node *fnode = (struct file_node *)malloc(sizeof(struct file_node));
	RB_INIT(&fnode->sources);

	return (struct e_file*)fnode;
}

struct e_file *file_add( struct e_file *file, uint32_t client_ip, uint16_t client_port )
{
	struct src_node *snode;
	struct file_node *fnode;
	fnode = RB_INSERT(file_map, &g_files, (struct file_node*)file);

	if( fnode ) {
		memcpy(&fnode->val, file, sizeof fnode->val);
	} else {
		ed2kd_rt()->file_count++;
		fnode = (struct file_node*)file;
	}

	snode = (struct src_node*)malloc(sizeof(struct src_node));
	snode->key = 0;
	snode->val.ip = client_ip;
	snode->val.port = client_port;
	RB_INSERT(source_set, &fnode->sources, snode);

	fnode->val.ref++;
	return (struct e_file*)fnode;
}

void file_delete( struct e_file *file, uint32_t client_ip, uint16_t client_port )
{
	struct file_node *fnode = (struct file_node*)file;
	fnode->val.ref--;

	if ( !fnode->val.ref ) {
		struct src_node *snode, *nxt;

		RB_REMOVE(file_map, &g_files, fnode);

		RB_FOREACH_SAFE( snode, source_set, &fnode->sources, nxt ) {
			RB_REMOVE(source_set, &fnode->sources, snode);
			free(snode);
		}

		ed2kd_rt()->file_count--;
		free(file);
	} else {
		struct src_node snode = {0};
		snode.key = 0;
		snode.val.ip = client_ip;
		snode.val.port = client_port;
		RB_REMOVE(source_set, &fnode->sources, &snode);
	}
}

uint32_t file_get_sources( const unsigned char *hash, struct e_source *sources, uint32_t size )
{
	struct file_node find, *fnode;
	memcpy(find.val.hash, hash, sizeof find.val.hash);
	fnode = RB_FIND(file_map, &g_files, &find);
	if ( fnode ) {
		uint32_t i = 0;
		struct src_node *snode;
		RB_FOREACH( snode, source_set, &fnode->sources ) {
			sources[i].ip = snode->val.ip;
			sources[i].port = snode->val.port;
			i++;
			if ( i >= size )
				break;
		}
		return i;
	}

	return 0;
}

/*
struct e_file *file_find( unsigned char *hash )
{
	struct e_file find;
	memcpy(find.hash, hash, sizeof find.hash);
	
	return RB_FIND(file_map, &g_files, &find);
}
*/
