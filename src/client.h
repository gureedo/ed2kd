#ifndef CLIENT_H
#define CLIENT_H

#define MAX_NICK_LEN		255
// max sources per one packet
#define	MAX_FOUND_SOURCES	200


struct e_client {
    unsigned char hash[16];
    uint32_t ip;
    uint16_t port;
    char nick[MAX_NICK_LEN+1];
    uint16_t nick_len;
    uint32_t server_flags;
    uint32_t emule_ver;


	// flags
	unsigned portcheck_finished : 1;

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

void client_portcheck_finish( struct e_client *client );
void client_portcheck_failed( struct e_client *client );

void send_server_message( struct e_client *client, const char *msg, uint16_t msg_len );

void send_id_change( struct e_client *client );

void send_found_sources( struct e_client *client, const unsigned char *hash );

#endif // CLIENT_H
