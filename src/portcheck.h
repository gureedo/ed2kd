#ifndef ED2KD_PORTCHECK_H
#define ED2KD_PORTCHECK_H

/*
  @file portcheck.h
*/

struct client;

void portcheck_read(struct client *client);

void portcheck_event(struct client *client, short events);

void portcheck_timeout(struct client *clnt);

#endif // ED2KD_PORTCHECK_H
