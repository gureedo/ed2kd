#include "server.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <zlib.h>
#include "client.h"
#include "ed2kd.h"
#include "packet_buffer.h"
#include "portcheck.h"
#include "ed2k_proto.h"
#include "db.h"
#include "log.h"
#include "util.h"
#include "event_callback.h"

void server_add_job( job_t *job )
{
    if ( job->client ) {
        pthread_mutex_lock(&job->client->job_mutex);
        if ( AO_load_acquire(&job->client->busy) ) {
            STAILQ_INSERT_TAIL(&job->client->jqueue, job, qentry);
        } else {
            pthread_mutex_lock(&g_instance.job_mutex);
            STAILQ_INSERT_TAIL(&g_instance.jqueue, job, qentry);
            pthread_mutex_unlock(&g_instance.job_mutex);
            pthread_cond_signal(&g_instance.job_cond);
        }
        pthread_mutex_unlock(&job->client->job_mutex);
    } else {
        pthread_mutex_lock(&g_instance.job_mutex);
        STAILQ_INSERT_TAIL(&g_instance.jqueue, job, qentry);
        pthread_mutex_unlock(&g_instance.job_mutex);
        pthread_cond_signal(&g_instance.job_cond);
    }
}

static int
process_login_request( struct packet_buffer *pb, client_t *client )
{
    uint32_t tag_count;

    // user hash 16b
    PB_MEMCPY(pb, client->hash, sizeof client->hash);

    // user id 4b
    PB_SEEK(pb, sizeof(uint32_t));

    // user port 2b
    PB_READ_UINT16(pb, client->port);

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
            client->nick_len = MAX_NICK_LEN;
            PB_READ_STRING(pb, client->nick, client->nick_len);
            client->nick[client->nick_len] = 0;
            break;
        }

        case TN_PORT:
            PB_CHECK(TT_UINT16 == tag_hdr->type);
            PB_READ_UINT16(pb, client->port);
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
            PB_READ_UINT32(pb, client->tcp_flags);
            break;

        case TN_EMULE_VERSION: {
            UINT32 emule_ver;
            PB_CHECK(TT_UINT32 == tag_hdr->type);
            PB_READ_UINT32(pb, emule_ver);
            break;
        }

        default:
            PB_CHECK(0);
        }
    }

    // todo: search already connected with same ip:port

    if ( client_portcheck_start(client) < 0 ) {
        client_portcheck_finish(client, PORTCHECK_FAILED);
        return -1;
    }

    return 0;

malformed:
    return -1;
}

static int
process_offer_files( packet_buffer_t *pb, client_t *client )
{
    size_t i;
    uint32_t count;

    PB_READ_UINT32(pb, count);
    PB_CHECK(count <= 200 );

    // todo: limit total files count on server

    for( i=0; i<count; ++i ) {
        uint32_t tag_count, id;
        uint16_t port;
        pub_file_t file;

        memset(&file, 0, sizeof file);

        PB_MEMCPY(pb, file.hash, sizeof file.hash);

        PB_READ_UINT32(pb, id);
        PB_READ_UINT16(pb, port);

        if ( (0xfbfbfbfb == id) && (0xfbfb == port) ) {
            file.complete = 1;
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
                        PB_READ_UINT32(pb, file.media_length);
                    } else if ( TT_STRING == tag_hdr->type ) {
                        // todo: support string values ( hh:mm:ss)
                        uint16_t len;
                        PB_READ_UINT16(pb, len);
                        PB_SEEK(pb, len);
                    } else {
                        PB_CHECK(0);
                    }
                } else if( strncmp(TNS_MEDIA_BITRATE, (const char*)tag_hdr->name, tag_hdr->name_len) == 0 ) {
                    PB_CHECK( TT_UINT32 == tag_hdr->type );
                    PB_READ_UINT32(pb, file.media_bitrate);
                } else if( strncmp(TNS_MEDIA_CODEC, (const char*)tag_hdr->name, tag_hdr->name_len) == 0 ) {
                    uint16_t len = MAX_MCODEC_LEN;
                    PB_CHECK(TT_STRING == tag_hdr->type);
                    PB_READ_STRING(pb, file.media_codec, len);
                    file.media_codec[len] = 0;
                } else {
                    PB_CHECK(0);
                }
            } else {
                switch ( *tag_hdr->name ) {

                case TN_FILENAME:
                    PB_CHECK(TT_STRING == tag_hdr->type);
                    PB_READ_UINT16(pb, file.name_len);
                    file.name_len = file.name_len > MAX_FILENAME_LEN ? MAX_FILENAME_LEN : file.name_len;
                    PB_MEMCPY(pb, file.name, file.name_len);
                    file.name[file.name_len] = 0;
                    break;

                case TN_FILESIZE:
                    PB_CHECK(TT_UINT32 == tag_hdr->type);
                    PB_READ_UINT32(pb, file.size);
                    break;

                case TN_FILESIZE_HI: {
                    uint32_t size_hi;
                    PB_CHECK(TT_UINT32 == tag_hdr->type);
                    PB_READ_UINT32(pb, size_hi);
                    file.size += (uint64_t)size_hi << 32;
                    break;
                                     }

                case TN_FILERATING:
                    PB_CHECK(TT_UINT32 == tag_hdr->type);
                    PB_READ_UINT32(pb, file.rating);
                    if ( file.rating > 5 ) {
                        file.rating = 5;
                    }
                    break;

                case TN_FILETYPE:
                    if ( TT_UINT32 == tag_hdr->type ) {
                        PB_READ_UINT32(pb, file.type);
                    } else if ( TT_STRING == tag_hdr->type ) {
                        uint16_t len;
                        PB_READ_UINT16(pb, len);
                        file.type = get_ed2k_file_type((const char*)pb->ptr, len);
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

        if ( db_add_file( &file, client ) < 0 ) {
            // todo: something gone wrong
        } else {
            ED2KD_LOGDBG("published file(name:'%s',size:%u)", file.name, file.size);
        }
    }

    client->file_count += count;
    AO_fetch_and_add(&g_instance.file_count, count);

    return 0;

malformed:
    return -1;
}

static int
process_search_request( packet_buffer_t *pb, client_t *client )
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

    send_search_result(client, &root);
    return 0;

malformed:
    return -1;
}

static int
process_packet( packet_buffer_t *pb, uint8_t opcode, client_t *client )
{
    PB_CHECK( client->portcheck_finished || (OP_LOGINREQUEST==opcode));

    switch ( opcode ) {
    case OP_LOGINREQUEST:
        PB_CHECK( process_login_request(pb, client) >= 0 );
        return 0;

    case OP_GETSERVERLIST:
        send_server_ident(client);
        send_server_list(client);
        return 0;

    case OP_SEARCHREQUEST:
        process_search_request(pb, client);
        return 0;

    case OP_QUERY_MORE_RESULT:
        // todo: may be send op_reject?
        return 0;

    case OP_DISCONNECT:
        // todo: remove client
        return 0;

    case OP_GETSOURCES:
        send_found_sources(client, pb->ptr);
        return 0;

    case OP_OFFERFILES:
        PB_CHECK( process_offer_files(pb, client) >= 0 );
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

static void
server_read( client_t *client )
{
    struct evbuffer *input = bufferevent_get_input(client->bev_srv);
    size_t src_len = evbuffer_get_length(input);

    while( src_len > sizeof(struct packet_header) ) {
        unsigned char *data;
        struct packet_buffer pb;
        size_t packet_len;
        int ret;
        const struct packet_header *header =
            (struct packet_header*)evbuffer_pullup(input, sizeof(struct packet_header));

        if  ( (PROTO_PACKED != header->proto) && (PROTO_EDONKEY != header->proto) ) {
            ED2KD_LOGDBG("unknown packet protocol from %s:%u", client->dbg.ip_str, client->port);
            client_delete(client);
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
                ret = process_packet(&pb, *data, client);
            } else {
                ED2KD_LOGDBG("failed to unpack packet from %s:%u", client->dbg.ip_str, client->port);
                ret = -1;
            }
            free(unpacked);
        } else {
            PB_INIT(&pb, data+1, header->length-1);
            ret = process_packet(&pb, *data, client);
        }


        if (  ret < 0 ) {
            ED2KD_LOGDBG("server packet parsing error (%s:%u)", client->dbg.ip_str, client->port);
            client_delete(client);
            return;
        }

        evbuffer_drain(input, packet_len);
        src_len = evbuffer_get_length(input);
    }
}

static void
server_event( client_t *client, short events )
{
    if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) ) {
        ED2KD_LOGDBG("got EOF or error from %s:%u", client->dbg.ip_str, client->port);
        client_delete(client);
    }
}

static void
server_accept( evutil_socket_t fd, struct sockaddr *sa, int socklen )
{
    struct sockaddr_in *peer_sa = (struct sockaddr_in*)sa;
    client_t *client;
    struct bufferevent *bev;

    assert(sizeof(struct sockaddr_in) == socklen);

    // todo: limit total client count
    // todo: limit connections from same ip
    // todo: block banned ips

    client = client_new();

    bev = bufferevent_socket_new(g_instance.evbase, fd, BEV_OPT_CLOSE_ON_FREE);

    client->ip = peer_sa->sin_addr.s_addr;
    client->bev_srv = bev;
#ifdef DEBUG
    evutil_inet_ntop(AF_INET, &(client->ip), client->dbg.ip_str, sizeof client->dbg.ip_str);
    ED2KD_LOGNFO("client connected (%s)", client->dbg.ip_str);
#endif

    bufferevent_setcb(bev, server_read_cb, NULL, server_event_cb, client);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    send_server_message(client, g_instance.cfg->welcome_msg, g_instance.cfg->welcome_msg_len);

    if ( !g_instance.cfg->allow_lowid ) {
        static const char msg_highid[] = "WARNING: Only HighID clients!";
        send_server_message(client, msg_highid, sizeof msg_highid - 1);
    }

    // todo: set timeout for op_login
}

void *server_job_worker( void *ctx )
{
    for(;;) {
        client_t *client;
        job_t *job;

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
        
        client = job->client;

        if ( client ) {
            AO_store_release(&client->busy, 1);
        }

        pthread_mutex_unlock(&g_instance.job_mutex);

        while ( job ) {
            switch( job->type ) {

            case JOB_SERVER_ACCEPT: {
                job_server_accept_t *j = (job_server_accept_t *)job;
                server_accept(j->fd, j->sa, j->socklen);
                break;
            }

            case JOB_SERVER_EVENT: {
                job_event_t *j = (job_event_t *)job;
                client_event(j->hdr.client, j->events);
                break;
            }

            case JOB_CLIENT_EVENT: {
                job_event_t *j = (job_event_t *)job;
                server_event(j->hdr.client, j->events);
                break;
            }

            case JOB_SERVER_READ: {
                server_read(job->client);
                break;
            }

            case JOB_CLIENT_READ: {
                client_read(job->client);
                break;
            }

            default:
                // todo: unknown job
                break;
            }

            free(job);
            job = NULL;

            if ( client ) {
                pthread_mutex_lock(&client->job_mutex);
                if ( STAILQ_EMPTY(&client->jqueue) ) {
                    AO_store_release(&client->busy, 0);
                } else {
                    job = STAILQ_FIRST(&client->jqueue);
                    STAILQ_REMOVE_HEAD(&client->jqueue, qentry);
                }
                pthread_mutex_unlock(&client->job_mutex);
            }
        }
    }

    return NULL;
}