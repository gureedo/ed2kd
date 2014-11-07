#include "server.h"
#include <assert.h>
#include <malloc.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <zlib.h>

#include "ed2k_proto.h"
#include "packet.h"
#include "client.h"
#include "portcheck.h"
#include "db.h"
#include "log.h"
#include "util.h"


static void dummy_cb( evutil_socket_t fd, short what, void *ctx )
{
        (void)fd;
        (void)what;
        (void)ctx;
}

void *server_base_worker( void * arg )
{
        // todo: after moving to libevent 2.1.x replace timer below with EVLOOP_NO_EXIT_ON_EMPTY flag
        struct event_base *evbase = (struct event_base *)arg;
        struct timeval tv = {500, 0};
        struct event *ev_dummy = event_new(evbase, -1, EV_PERSIST, dummy_cb, 0);
        event_add(ev_dummy, &tv);

        if ( event_base_dispatch(evbase) < 0 )
                ED2KD_LOGERR("loop finished with error");

        event_free(ev_dummy);
        return NULL;
}

void server_add_job( struct job *job )
{
        pthread_mutex_lock(&g_srv.job_mutex);
        client_addref(job->clnt);
        TAILQ_INSERT_TAIL(&g_srv.jqueue, job, qentry);
        pthread_mutex_unlock(&g_srv.job_mutex);
        pthread_cond_signal(&g_srv.job_cond);
}

static int process_login_request( struct packet_buffer *pb, struct client *clnt )
{
        uint32_t tag_count;

        // user hash 16b
        PB_MEMCPY(pb, clnt->hash, sizeof clnt->hash);

        // user id 4b
        PB_SEEK(pb, sizeof(uint32_t));

        // user port 2b
        PB_READ_UINT16(pb, clnt->port);

        // tag count 4b
        PB_READ_UINT32(pb, tag_count);

        for ( ; tag_count>0; --tag_count ) {
                const struct tag_header *tag_hdr = (struct tag_header*)pb->ptr;

                // no new tags allowed here
                PB_CHECK( (tag_hdr->type & 0x80) == 0 );

                // only int-based tag names
                PB_CHECK( 1 == tag_hdr->name_len );

                PB_SKIP_TAGHDR(pb, tag_hdr);

                switch ( *tag_hdr->name ) {
                case TN_NAME: {
                        PB_CHECK(TT_STRING == tag_hdr->type);
                        clnt->nick_len = MAX_NICK_LEN;
                        PB_READ_STRING(pb, clnt->nick, clnt->nick_len);
                        clnt->nick[clnt->nick_len] = 0;
                        break;
                              }

                case TN_PORT:
                        PB_CHECK(TT_UINT16 == tag_hdr->type);
                        PB_READ_UINT16(pb, clnt->port);
                        break;

                case TN_VERSION: {
                        uint32_t ver;
                        PB_CHECK(TT_UINT32 == tag_hdr->type);
                        PB_READ_UINT32(pb, ver);
                        PB_CHECK(EDONKEYVERSION == ver);
                        break;
                                 }

                case TN_SERVER_FLAGS:
                        PB_CHECK(TT_UINT32 == tag_hdr->type);
                        PB_READ_UINT32(pb, clnt->tcp_flags);
                        break;

                case TN_EMULE_VERSION: {
                        uint32_t emule_ver;
                        (void)emule_ver;
                        PB_CHECK(TT_UINT32 == tag_hdr->type);
                        PB_READ_UINT32(pb, emule_ver);
                        break;
                                       }

                default:
                        PB_CHECK(0);
                }
        }

        // todo: search already connected with same ip:port

        client_portcheck_start(clnt);

        return 1;

malformed:
        return 0;
}

static int process_offer_files( struct packet_buffer *pb, struct client *clnt )
{
        size_t i;
        uint32_t count;
        struct pub_file *files, *cur_file;

        PB_READ_UINT32(pb, count);
        PB_CHECK(count <= 200 );

        i = count;
        // todo: limit total files count on server

        cur_file = files = (struct pub_file*)calloc(count, sizeof(*files));

        while( i-- > 0 ) {
                uint32_t tag_count, id;
                uint16_t port;

                PB_MEMCPY(pb, cur_file->hash, sizeof(cur_file->hash));

                PB_READ_UINT32(pb, id);
                PB_READ_UINT16(pb, port);

                if ( (0xfbfbfbfb == id) && (0xfbfb == port) ) {
                        cur_file->complete = 1;
                }

                PB_READ_UINT32(pb, tag_count);

                for ( ; tag_count>0; --tag_count ) {
                        struct tag_header *tag_hdr = (struct tag_header*)pb->ptr;

                        // todo: new tags support
                        PB_CHECK( (tag_hdr->type & 0x80) == 0 );

                        PB_SKIP_TAGHDR(pb, tag_hdr);

                        // string-named tags
                        if (tag_hdr->name_len > 1 ) {
                                if( strncmp(TNS_MEDIA_LENGTH, (const char*)tag_hdr->name, tag_hdr->name_len) == 0 ) {
                                        if ( TT_UINT32 == tag_hdr->type ) {
                                                PB_READ_UINT32(pb, cur_file->media_length);
                                        } else if ( TT_STRING == tag_hdr->type ) {
                                                // todo: support string values ( hh:mm:ss )
                                                uint16_t len;
                                                PB_READ_UINT16(pb, len);
                                                PB_SEEK(pb, len);
                                        } else {
                                                PB_CHECK(0);
                                        }
                                } else if( strncmp(TNS_MEDIA_BITRATE, (const char*)tag_hdr->name, tag_hdr->name_len) == 0 ) {
                                        PB_CHECK( TT_UINT32 == tag_hdr->type );
                                        PB_READ_UINT32(pb, cur_file->media_bitrate);
                                } else if( strncmp(TNS_MEDIA_CODEC, (const char*)tag_hdr->name, tag_hdr->name_len) == 0 ) {
                                        uint16_t len = MAX_MCODEC_LEN;
                                        PB_CHECK(TT_STRING == tag_hdr->type);
                                        PB_READ_STRING(pb, cur_file->media_codec, len);
                                        cur_file->media_codec[len] = 0;
                                } else {
                                        PB_CHECK(0);
                                }
                        } else {
                                switch ( *tag_hdr->name ) {

                                case TN_FILENAME:
                                        PB_CHECK(TT_STRING == tag_hdr->type);
                                        PB_READ_UINT16(pb, cur_file->name_len);
                                        cur_file->name_len = cur_file->name_len > MAX_FILENAME_LEN ? MAX_FILENAME_LEN : cur_file->name_len;
                                        PB_MEMCPY(pb, cur_file->name, cur_file->name_len);
                                        cur_file->name[cur_file->name_len] = 0;
                                        break;

                                case TN_FILESIZE:
                                        PB_CHECK(TT_UINT32 == tag_hdr->type);
                                        PB_READ_UINT32(pb, cur_file->size);
                                        break;

                                case TN_FILESIZE_HI: {
                                        uint32_t size_hi;
                                        PB_CHECK(TT_UINT32 == tag_hdr->type);
                                        PB_READ_UINT32(pb, size_hi);
                                        cur_file->size += (uint64_t)size_hi << 32;
                                        break;
                                                     }

                                case TN_FILERATING:
                                        PB_CHECK(TT_UINT32 == tag_hdr->type);
                                        PB_READ_UINT32(pb, cur_file->rating);
                                        if ( cur_file->rating > 5 ) {
                                                cur_file->rating = 5;
                                        }
                                        break;

                                case TN_FILETYPE:
                                        if ( TT_UINT32 == tag_hdr->type ) {
                                                PB_READ_UINT32(pb, cur_file->type);
                                        } else if ( TT_STRING == tag_hdr->type ) {
                                                uint16_t len;
                                                PB_READ_UINT16(pb, len);
                                                cur_file->type = get_ed2k_file_type((const char*)pb->ptr, len);
                                                PB_SEEK(pb, len);
                                        } else {
                                                PB_CHECK(0);
                                        }
                                        break;

                                default:
                                        PB_CHECK(0);
                                }
                        }
                }

                cur_file++;
        }

        client_share_files(clnt, files, count);

        return 1;

malformed:
        return 0;
}

static int process_search_request( struct packet_buffer *pb, struct client *clnt )
{
        struct search_node *n, root;
        n = &root;

        memset(&root, 0, sizeof(root));

        while ( n ) {
                if( (ST_AND <= n->type) && (ST_NOT >= n->type) ) {
                        if ( !n->left ) {
                                struct search_node *new_node = (struct search_node*)alloca(sizeof(struct search_node));
                                memset(new_node, 0, sizeof(struct search_node));
                                new_node->parent = n;
                                n->left = new_node;
                                n = new_node;
                                continue;
                        } else if ( !n->right ) {
                                struct search_node *new_node = (struct search_node*)alloca(sizeof(struct search_node));
                                memset(new_node, 0, sizeof(struct search_node));
                                new_node->parent = n;
                                n->right = new_node;
                                n = new_node;
                                continue;
                        } else if ( n->left->string_term && n->right->string_term ) {
                                n->string_term = 1;
                        }
                } else if ( ST_EMPTY == n->type ) {
                        if ( SO_AND == PB_PTR_UINT16(pb) ) {
                                n->type = ST_AND;
                                PB_SEEK(pb, sizeof(uint16_t));
                                continue;

                        } else if ( SO_OR == PB_PTR_UINT16(pb) ) {
                                n->type = ST_OR;
                                PB_SEEK(pb, sizeof(uint16_t));
                                continue;

                        } else if ( SO_NOT == PB_PTR_UINT16(pb) ) {
                                n->type = ST_NOT;
                                PB_SEEK(pb, sizeof(uint16_t));
                                continue;

                        } else if ( SO_STRING_TERM == PB_PTR_UINT8(pb) ) {
                                n->type = ST_STRING;
                                PB_SEEK(pb, 1);
                                PB_READ_UINT16(pb, n->str_len);
                                n->str_val = (const char*)pb->ptr;
                                PB_SEEK(pb, n->str_len);
                                n->string_term = 1;

                        } else if ( SO_STRING_CONSTR == PB_PTR_UINT8(pb) ) {
                                uint16_t tail1;
                                uint8_t tail2;
                                PB_SEEK(pb, 1);
                                PB_READ_UINT16(pb, n->str_len);
                                n->str_val = (const char*)pb->ptr;
                                PB_SEEK(pb, n->str_len);
                                PB_READ_UINT16(pb, tail1);
                                PB_READ_UINT8(pb, tail2);
                                // todo: add macro for this magic constants
                                if( (0x0001 == tail1) && (0x04 == tail2) ) {
                                        n->type = ST_EXTENSION;
                                } else if ( (0x0001 == tail1) && (0xd5 == tail2) ) {
                                        n->type = ST_CODEC;
                                } else if ( (0x0001 == tail1) && (0x03 == tail2) ) {
                                        n->type = ST_TYPE;
                                } else {
                                        PB_CHECK(0);
                                }

                        } else if ( (SO_UINT32 == PB_PTR_UINT8(pb)) || (SO_UINT64 == PB_PTR_UINT8(pb)) ) {
                                uint32_t constr;
                                uint8_t type;
                                PB_READ_UINT8(pb, type);

                                if ( SO_UINT32 == type ) {
                                        PB_READ_UINT32(pb, n->int_val);
                                } else {
                                        PB_READ_UINT64(pb, n->int_val);
                                }

                                PB_READ_UINT32(pb, constr);
                                if ( SC_MINSIZE == constr ) {
                                        n->type = ST_MINSIZE;
                                } else if ( SC_MAXSIZE == constr ) {
                                        n->type = ST_MAXSIZE;
                                } else if ( SC_SRCAVAIL == constr ) {
                                        n->type = ST_SRCAVAIL;
                                } else if ( SC_SRCCMPLETE == constr ) {
                                        n->type = ST_SRCCOMLETE;
                                } else if ( SC_MINBITRATE == constr ) {
                                        n->type = ST_MINBITRATE;
                                } else if ( SC_MINLENGTH == constr ) {
                                        n->type = ST_MINLENGTH;
                                } else {
                                        PB_CHECK(0);
                                }
                        }

                }

                n = n->parent;
        }

        client_search_files(clnt, &root);
        return 1;

malformed:
        return 0;
}

static int process_packet( struct packet_buffer *pb, uint8_t opcode, struct client *clnt )
{
        PB_CHECK( clnt->portcheck_finished || (OP_LOGINREQUEST==opcode));

        switch ( opcode ) {
        case OP_LOGINREQUEST:
                /* client already logined */
                if ( clnt->id )
                        client_delete(clnt);
                        
                send_server_message(clnt->bev, g_srv.cfg->welcome_msg, g_srv.cfg->welcome_msg_len);
                if ( !g_srv.cfg->allow_lowid ) {
                        static const char msg_highid[] = "WARNING: Only HighID clients!";
                        send_server_message(clnt->bev, msg_highid, sizeof(msg_highid) - 1);
                }
                PB_CHECK( process_login_request(pb, clnt) );
                return 1;

        case OP_GETSERVERLIST:
                send_server_ident(clnt->bev);
                send_server_list(clnt->bev);
                return 1;

        case OP_SEARCHREQUEST:
                if ( !token_bucket_update(&clnt->limit_search, g_srv.cfg->max_searches_limit) ) {
                        ED2KD_LOGDBG("search limit reached for %u", clnt->id);
                        client_delete(clnt);
                        return 0;
                }
                process_search_request(pb, clnt);
                return 1;

        case OP_QUERY_MORE_RESULT:
                return 1;

        case OP_DISCONNECT:
                client_delete(clnt);
                return 1;

        case OP_GETSOURCES:
                PB_CHECK( PB_LEFT(pb) == ED2K_HASH_SIZE );
                client_get_sources(clnt, pb->ptr);
                return 1;

        case OP_OFFERFILES:
                if ( !token_bucket_update(&clnt->limit_offer, g_srv.cfg->max_offers_limit) ) {
                        ED2KD_LOGDBG("offer limit reached for %u", clnt->id);
                        client_delete(clnt);
                        return 0;
                }
                PB_CHECK( process_offer_files(pb, clnt) );
                return 1;

        case OP_CALLBACKREQUEST:
                send_callback_fail(clnt->bev);
                return 1;

        case OP_GETSOURCES_OBFU:
                return 1;

        case OP_REJECT:
                return 1;

        default:
                PB_CHECK(0);
        }

malformed:
        ED2KD_LOGDBG("malformed tcp packet (opcode:%u)", opcode);
        client_delete(clnt);
        return 0;
}

static void server_read( struct client *clnt )
{
        struct evbuffer *input = bufferevent_get_input(clnt->bev);
        size_t src_len = evbuffer_get_length(input);

        while( !clnt->deleted && src_len > sizeof(struct packet_header) ) {
                unsigned char *data;
                struct packet_buffer pb;
                size_t packet_len;
                int ret;
                const struct packet_header *header =
                        (struct packet_header*)evbuffer_pullup(input, sizeof(struct packet_header));

                if  ( (PROTO_PACKED != header->proto) && (PROTO_EDONKEY != header->proto) ) {
                        ED2KD_LOGDBG("unknown packet protocol from %s:%u", clnt->dbg.ip_str, clnt->port);
                        client_delete(clnt);
                        return;
                }

                // wait for full length packet
                packet_len = header->length + sizeof(struct packet_header);
                if ( packet_len > src_len )
                        return;

                data = evbuffer_pullup(input, packet_len);
                header = (struct packet_header*)data;
                data += sizeof(struct packet_header);

                if ( PROTO_PACKED == header->proto ) {
                        unsigned long unpacked_len = MAX_UNCOMPRESSED_PACKET_SIZE;
                        unsigned char *unpacked = (unsigned char*)malloc(unpacked_len);

                        ret = uncompress(unpacked, &unpacked_len, data+1, header->length-1);
                        if ( Z_OK == ret ) {
                                PB_INIT(&pb, unpacked, unpacked_len);
                                ret = process_packet(&pb, *data, clnt);
                        } else {
                                ED2KD_LOGDBG("failed to unpack packet from %s:%u", clnt->dbg.ip_str, clnt->port);
                                ret = 0;
                        }
                        free(unpacked);
                } else {
                        PB_INIT(&pb, data+1, header->length-1);
                        ret = process_packet(&pb, *data, clnt);
                }

                if ( !ret )
                        return;

                evbuffer_drain(input, packet_len);
                src_len = evbuffer_get_length(input);
        }
}

static void server_event( struct client *clnt, short events )
{
        if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) ) {
                ED2KD_LOGDBG("got EOF or error from %s:%u", clnt->dbg.ip_str, clnt->port);
                client_delete(clnt);
        }
}

void* server_job_worker( void *ctx )
{
        (void)ctx;

        if ( !db_open() ) {
                ED2KD_LOGERR("failed to open database");
                return NULL;
        }

        for(;;) {
                struct job *job = 0;

                pthread_mutex_lock(&g_srv.job_mutex);
                for(;;) {
                        struct job *j, *jtmp;

                        if ( atomic_load(&g_srv.terminate) ) {
                                pthread_mutex_unlock(&g_srv.job_mutex);
                                goto exit;
                        }

                        TAILQ_FOREACH_SAFE( j, &g_srv.jqueue, qentry, jtmp ) {
                                uint32_t old_val = 0;
                                if (atomic_compare_exchange_strong(&j->clnt->locked, &old_val, 1) ) {
                                        TAILQ_REMOVE(&g_srv.jqueue, j, qentry);
                                        job = j;
                                        break;
                                }
                        }

                        if ( job )
                                break;

                        pthread_cond_wait(&g_srv.job_cond, &g_srv.job_mutex);
                }

                pthread_mutex_unlock(&g_srv.job_mutex);

                if ( !atomic_load(&job->clnt->deleted) ) {
                        switch( job->type ) {

                        case JOB_SERVER_EVENT: {
                                struct job_event *j = (struct job_event*)job;
                                //ED2KD_LOGDBG("JOB_SERVER_EVENT event");
                                server_event(j->hdr.clnt, j->events);
                                break;
                        }

                        case JOB_SERVER_READ:
                                //ED2KD_LOGDBG("JOB_SERVER_READ event");
                                server_read(job->clnt);
                                break;

                        case JOB_SERVER_STATUS_NOTIFY:
                                //ED2KD_LOGDBG("JOB_SERVER_STATUS_NOTIFY event");
                                send_server_status(job->clnt->bev);
                                event_add(job->clnt->evtimer_status_notify, g_srv.status_notify_tv);
                                break;

                        case JOB_PORTCHECK_EVENT: {
                                struct job_event *j = (struct job_event*)job;
                                //ED2KD_LOGDBG("JOB_PORTCHECK_EVENT event");
                                portcheck_event(job->clnt, j->events);
                                break;
                        }

                        case JOB_PORTCHECK_READ:
                                //ED2KD_LOGDBG("JOB_PORTCHECK_READ event");
                                portcheck_read(job->clnt);
                                break;

                        case JOB_PORTCHECK_TIMEOUT:
                                portcheck_timeout(job->clnt);
                                break;

                        default:
                                assert(0);
                                break;
                        }
                }

                atomic_store(&job->clnt->locked, 0);
                client_decref(job->clnt);
                free(job);
        }

exit:
        if ( !db_close() )
                ED2KD_LOGERR("failed to close database");

        return NULL;
}

void server_stop()
{
        event_base_loopbreak(g_srv.evbase_main);
        event_base_loopbreak(g_srv.evbase_tcp);
        atomic_store(&g_srv.terminate, 1);
}
