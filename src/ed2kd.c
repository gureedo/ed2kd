//#include <stdlib.h>
//#include <signal.h>
//#include <string.h>
//#include <errno.h>
//#include <malloc.h>
#ifdef __GNUC__
//#include <alloca.h>
#endif
//#include <omp.h>

//#include <event2/event.h>
//#include <event2/thread.h>
//#include <event2/buffer.h>
//#include <event2/bufferevent.h>
//#include <event2/listener.h>

#include "ed2kd.h"
#include "log.h"
#include "config.h"
#include "db.h"
#include "server.h"



int ed2kd_run()
{
    int ret;
    struct event_base *base;
    struct event *sigint_event;
    struct sockaddr_in bind_sa;
    int bind_sa_len;
    struct evconnlistener *listener;




}
