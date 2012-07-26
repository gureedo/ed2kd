#ifndef PORT_CHECK_H
#define PORT_CHECK_H

/*
  @file portcheck.h
*/

struct client;

/**
  @brief start client port test
  @param client
  @return 0 on success, -1 on failure
*/
int client_portcheck_start( struct client *client );

void client_read( struct client *client );
void client_event( struct client *client, short events );

#endif // PORT_CHECK_H
