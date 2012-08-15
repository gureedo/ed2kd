#include "server.h"
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
#include "event_callback.h"

void server_add_job( struct job *job )
{
    if ( job->client ) {
        pthread_mutex_lock(&job->client->job_mutex);
        if ( job->client->pending_evcnt ) {
            STAILQ_INSERT_TAIL(&job->client->jqueue, job, qentry);
        } else {
            pthread_mutex_lock(&g_instance.job_mutex);
            STAILQ_INSERT_TAIL(&g_instance.jqueue, job, qentry);
            pthread_mutex_unlock(&g_instance.job_mutex);
            pthread_cond_signal(&g_instance.job_cond);
        }
        job->client->pending_evcnt++;
        pthread_mutex_unlock(&job->client->job_mutex);
    } else {
        pthread_mutex_lock(&g_instance.job_mutex);
        STAILQ_INSERT_TAIL(&g_instance.jqueue, job, qentry);
        pthread_mutex_unlock(&g_instance.job_mutex);
        pthread_cond_signal(&g_instance.job_cond);
    }
}

void server_remove_client_jobs( const struct client *clnt )
{
    struct job *j, *j_tmp;
    pthread_mutex_lock(&g_instance.job_mutex);
    STAILQ_FOREACH_SAFE( j, &g_instance.jqueue, qentry, j_tmp ) {
        if ( clnt == j->client ) {
            STAILQ_REMOVE_AFTER(&g_instance.jqueue, j, qentry);
            free(j);
        }
    }
    pthread_mutex_unlock(&g_instance.job_mutex);
}

static
int process_login_request( struct packet_buffer *pb, struct client *clnt )
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

    return 0;

malformed:
    return -1;
}

static
int process_offer_files( struct packet_buffer *pb, struct client *clnt )
{
    size_t i;
    uint32_t count;
    struct pub_file *files, *file;

    PB_READ_UINT32(pb, count);
    PB_CHECK(count <= 200 );

    i = count;
    // todo: limit total files count on server

    file = files = (struct pub_file*)calloc(count, sizeof(*files));

    while( i-- > 0 ) {
        uint32_t tag_count, id;
        uint16_t port;
        
        PB_MEMCPY(pb, file->hash, sizeof(files->hash));

        PB_READ_UINT32(pb, id);
        PB_READ_UINT16(pb, port);

        if ( (0xfbfbfbfb == id) && (0xfbfb == port) ) {
            file->complete = 1;
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
                        PB_READ_UINT32(pb, file->media_length);
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
                    PB_READ_UINT32(pb, file->media_bitrate);
                } else if( strncmp(TNS_MEDIA_CODEC, (const char*)tag_hdr->name, tag_hdr->name_len) == 0 ) {
                    uint16_t len = MAX_MCODEC_LEN;
                    PB_CHECK(TT_STRING == tag_hdr->type);
                    PB_READ_STRING(pb, file->media_codec, len);
                    file->media_codec[len] = 0;
                } else {
                    PB_CHECK(0);
                }
            } else {
                switch ( *tag_hdr->name ) {

                case TN_FILENAME:
                    PB_CHECK(TT_STRING == tag_hdr->type);
                    PB_READ_UINT16(pb, file->name_len);
                    file->name_len = file->name_len > MAX_FILENAME_LEN ? MAX_FILENAME_LEN : file->name_len;
                    PB_MEMCPY(pb, file->name, file->name_len);
                    file->name[file->name_len] = 0;
                    break;

                case TN_FILESIZE:
                    PB_CHECK(TT_UINT32 == tag_hdr->type);
                    PB_READ_UINT32(pb, file->size);
                    break;

                case TN_FILESIZE_HI: {
                    uint32_t size_hi;
                    PB_CHECK(TT_UINT32 == tag_hdr->type);
                    PB_READ_UINT32(pb, size_hi);
                    file->size += (uint64_t)size_hi << 32;
                    break;
                                     }

                case TN_FILERATING:
                    PB_CHECK(TT_UINT32 == tag_hdr->type);
                    PB_READ_UINT32(pb, file->rating);
                    if ( file->rating > 5 ) {
                        file->rating = 5;
                    }
                    break;

                case TN_FILETYPE:
                    if ( TT_UINT32 == tag_hdr->type ) {
                        PB_READ_UINT32(pb, file->type);
                    } else if ( TT_STRING == tag_hdr->type ) {
                        uint16_t len;
                        PB_READ_UINT16(pb, len);
                        file->type = get_ed2k_file_type((const char*)pb->ptr, len);
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



        // todo: filter duplicates

        file++;
    }

    if ( db_share_files(files, count, clnt) < 0 ) {
        // todo: something gone wrong
    }

    ED2KD_LOGDBG("client %u: published %u files", clnt->id, count);

    clnt->file_count += count;
    AO_fetch_and_add(&g_instance.file_count, count);

    return 0;

malformed:
    return -1;
}

static
int process_search_request( struct packet_buffer *pb, struct client *clnt )
{
    struct search_node *n, root;
    n = &root;

    memset(&root, 0, sizeof root);

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
    return 0;

malformed:
    return -1;
}

static
int process_packet( struct packet_buffer *pb, uint8_t opcode, struct client *clnt )
{
    PB_CHECK( clnt->portcheck_finished || (OP_LOGINREQUEST==opcode));

    switch ( opcode ) {
    case OP_LOGINREQUEST:
        PB_CHECK( process_login_request(pb, clnt) >= 0 );
        return 0;

    case OP_GETSERVERLIST:
        send_server_ident(clnt->bev_srv);
        send_server_list(clnt->bev_srv);
        return 0;

    case OP_SEARCHREQUEST:
        process_search_request(pb, clnt);
        return 0;

    case OP_QUERY_MORE_RESULT:
        send_reject(clnt->bev_srv);
        return 0;

    case OP_DISCONNECT:
        client_schedule_delete(clnt);
        return 0;

    case OP_GETSOURCES:
        client_get_sources(clnt, pb->ptr);
        return 0;

    case OP_OFFERFILES:
        PB_CHECK( process_offer_files(pb, clnt) >= 0 );
        return 0;

    case OP_CALLBACKREQUEST:
        // todo: send OP_CALLBACK_FAIL
        return 0;

    case OP_GETSOURCES_OBFU:
    case OP_REJECT:
    default:
        PB_CHECK(0);
    }

malformed:
    ED2KD_LOGDBG("malformed packet (opcode:%u)", opcode);
    return -1;
}

static
void server_read( struct client *clnt )
{
    struct evbuffer *input = bufferevent_get_input(clnt->bev_srv);
    size_t src_len = evbuffer_get_length(input);

    while( !clnt->sched_del && src_len > sizeof(struct packet_header) ) {
        unsigned char *data;
        struct packet_buffer pb;
        size_t packet_len;
        int ret;
        const struct packet_header *header =
            (struct packet_header*)evbuffer_pullup(input, sizeof(struct packet_header));

        if  ( (PROTO_PACKED != header->proto) && (PROTO_EDONKEY != header->proto) ) {
            ED2KD_LOGDBG("unknown packet protocol from %s:%u", clnt->dbg.ip_str, clnt->port);
            client_schedule_delete(clnt);
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
                ret = -1;
            }
            free(unpacked);
        } else {
            PB_INIT(&pb, data+1, header->length-1);
            ret = process_packet(&pb, *data, clnt);
        }


        if (  ret < 0 ) {
            ED2KD_LOGDBG("server packet parsing error (%s:%u)", clnt->dbg.ip_str, clnt->port);
            client_schedule_delete(clnt);
            return;
        }

        evbuffer_drain(input, packet_len);
        src_len = evbuffer_get_length(input);
    }
}

static
void server_event( struct client *clnt, short events )
{
    if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) ) {
        ED2KD_LOGDBG("got EOF or error from %s:%u", clnt->dbg.ip_str, clnt->port);
        client_schedule_delete(clnt);
    }
}

static
void server_accept( evutil_socket_t fd, struct sockaddr *sa, int socklen )
{
    struct sockaddr_in *peer_sa = (struct sockaddr_in*)sa;
    struct client *clnt;
    struct bufferevent *bev;

    assert(sizeof(struct sockaddr_in) == socklen);

    // todo: limit total client count
    // todo: limit connections from same ip
    // todo: block banned ips

    clnt = client_new();

    bev = bufferevent_socket_new(g_instance.evbase, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    clnt->bev_srv = bev;
    clnt->ip = peer_sa->sin_addr.s_addr;
    
#ifdef DEBUG
    evutil_inet_ntop(AF_INET, &(clnt->ip), clnt->dbg.ip_str, sizeof clnt->dbg.ip_str);
    ED2KD_LOGDBG("client connected (%s)", clnt->dbg.ip_str);
#endif

    bufferevent_setcb(clnt->bev_srv, server_read_cb, NULL, server_event_cb, clnt);
    bufferevent_enable(clnt->bev_srv, EV_READ|EV_WRITE);

    send_server_message(clnt->bev_srv, g_instance.cfg->welcome_msg, g_instance.cfg->welcome_msg_len);

    if ( !g_instance.cfg->allow_lowid ) {
        static const char msg_highid[] = "WARNING: Only HighID clients!";
        send_server_message(clnt->bev_srv, msg_highid, sizeof(msg_highid) - 1);
    }

    // todo: set timeout for op_login
}

void* server_job_worker( void *ctx )
{
    (void)ctx;

    if ( db_open() < 0 ) {
        ED2KD_LOGERR("failed to open database");
        return NULL;
    }


    for(;;) {
        struct client *clnt;
        struct job *job;

        pthread_mutex_lock(&g_instance.job_mutex);
        while ( !AO_load(&g_instance.terminate) && STAILQ_EMPTY(&g_instance.jqueue) ) {
            pthread_cond_wait(&g_instance.job_cond, &g_instance.job_mutex);
        }

        if ( AO_load(&g_instance.terminate) ) {
            pthread_mutex_unlock(&g_instance.job_mutex);
            break;
        } else {
            job = STAILQ_FIRST(&g_instance.jqueue);
            STAILQ_REMOVE_HEAD(&g_instance.jqueue, qentry);
        }
        
        clnt = job->client;

        pthread_mutex_unlock(&g_instance.job_mutex);

        while ( job ) {
            switch( job->type ) {

            case JOB_SERVER_ACCEPT: {
                struct job_server_accept *j = (struct job_server_accept*)job;
                //ED2KD_LOGDBG("JOB_SERVER_ACCEPT event");
                server_accept(j->fd, &j->sa, j->socklen);
                break;
            }

            case JOB_SERVER_EVENT: {
                struct job_event *j = (struct job_event*)job;
                //ED2KD_LOGDBG("JOB_SERVER_EVENT event");
                server_event(clnt, j->events);
                break;
            }

            case JOB_SERVER_READ:
                //ED2KD_LOGDBG("JOB_SERVER_READ event");
                server_read(clnt);
                break;

            case JOB_PORTCHECK_EVENT: {
                struct job_event *j = (struct job_event*)job;
                //ED2KD_LOGDBG("JOB_PORTCHECK_EVENT event");
                portcheck_event(clnt, j->events);
                break;
            }

            case JOB_PORTCHECK_READ:
                //ED2KD_LOGDBG("JOB_PORTCHECK_READ event");
                portcheck_read(clnt);
                break;

            case JOB_PORTCHECK_TIMEOUT:
                portcheck_timeout(clnt);
                break;
            default:
                assert(0);
                break;
            }

            free(job);
            job = NULL;

            if ( clnt ) {
                if ( clnt->sched_del ) {
                    client_delete(clnt);
                } else {
                    pthread_mutex_lock(&clnt->job_mutex);
                    clnt->pending_evcnt--;
                    if ( !STAILQ_EMPTY(&clnt->jqueue) ) {
                        job = STAILQ_FIRST(&clnt->jqueue);
                        STAILQ_REMOVE_HEAD(&clnt->jqueue, qentry);
                    }
                    pthread_mutex_unlock(&clnt->job_mutex);
                }
            }
        }
    }

    if ( db_close() < 0 ) {
        ED2KD_LOGERR("failed to close database");
    }

    return NULL;
}
