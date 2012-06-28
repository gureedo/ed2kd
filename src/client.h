#ifndef CLIENT_H
#define CLIENT_H

#define MAX_NICK_LEN		255
#define	MAX_FOUND_SOURCES	200
#define	MAX_FOUND_FILES 	200

struct e_client {
    unsigned char hash[16];
    uint32_t ip;
    uint16_t port;
    uint32_t id;
    char nick[MAX_NICK_LEN+1];
    uint16_t nick_len;
    uint32_t server_flags;
    uint32_t emule_ver;

	// flags
	unsigned portcheck_finished:1;
    unsigned lowid:1;

	struct bufferevent *bev_srv;
	struct bufferevent *bev_cli;

#ifdef DEBUG
	struct {
		char ip_str[16];
	} dbg;
#endif
};

struct e_client *client_new();

void client_delete( struct e_client *client );

void client_portcheck_finish( struct e_client *client, unsigned success );

void send_id_change( struct e_client *client );

void send_server_message( struct e_client *client, const char *msg, uint16_t msg_len );

void send_server_ident( struct e_client *client );

void send_server_list( struct e_client *client );

void send_search_result( struct e_client *client, struct search_node *search_tree );

void send_found_sources( struct e_client *client, const unsigned char *hash );

void send_reject( struct e_client *client );

void send_callback_fail( struct e_client *client );

void write_search_file( struct evbuffer *buf, const struct search_file *file );

#endif // CLIENT_H
