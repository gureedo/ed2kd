#ifndef PORT_CHECK_H
#define PORT_CHECK_H

struct client;
typedef struct client client_t;

/**
  @brief start client port test
  @param client
  @return 0 on success, -1 on failure
*/
int client_portcheck_start( client_t *client );

void client_read( client_t *client );
void client_event( client_t *client, short events );

#endif // PORT_CHECK_H
