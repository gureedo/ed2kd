#pragma once

#ifndef PORTCHECK_H
#define PORTCHECK_H

/*
  @file portcheck.h
*/

struct client;

void portcheck_read( struct client *client );
void portcheck_event( struct client *client, short events );

#endif // PORTCHECK_H
