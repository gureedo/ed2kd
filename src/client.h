#ifndef CLIENT_H
#define CLIENT_H

struct e_client {
    unsigned char hash[16];
    uint32_t ip;
    uint16_t port;
    char *nick;
    uint16_t nick_len;
    uint32_t server_flags;
    uint32_t emule_ver;

    struct bufferevent *bev_srv;
    struct bufferevent *bev_cli;
};

struct e_client *client_new();

void client_free( struct e_client *client );

void client_handshake_finish( struct e_client *client );
void client_handshake_failed( struct e_client *client );

void send_server_message( struct e_client *client, const char *msg, uint16_t msg_len );
void send_id_change( struct e_client *client );

#endif // CLIENT_H
