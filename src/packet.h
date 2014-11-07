#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

struct bufferevent;
struct evbuffer;
struct file_source;

struct search_file {
        const unsigned char *hash;
        uint32_t        client_id;
        uint16_t        client_port;
        uint16_t        name_len;
        const char      *name;
        uint64_t        size;
        uint32_t        type;
        uint32_t        rating;
        uint32_t        rated_count;
        uint16_t        ext_len;
        const char      *ext;
        uint32_t        media_length;
        uint32_t        media_bitrate;
        uint16_t        media_codec_len;
        const char      *media_codec;
        uint32_t        srcavail;
        uint32_t        srccomplete;
};

void send_id_change( struct bufferevent *bev, uint32_t id );

void send_server_message( struct bufferevent *bev, const char *msg, uint16_t len );

void send_server_status( struct bufferevent *bev );

void send_server_ident( struct bufferevent *bev );

void send_server_list( struct bufferevent *bev );

void send_reject( struct bufferevent *bev );

void send_callback_fail( struct bufferevent *bev );

void send_found_sources( struct bufferevent *bev, const unsigned char *hash, struct file_source *sources, size_t count );

void send_search_result( struct bufferevent *bev, struct evbuffer *result, size_t count );

void write_search_file( struct evbuffer *buf, const struct search_file *file );

struct packet_buffer {
        const unsigned char *ptr; /**< current location pointer */
        const unsigned char *end; /**< buffer end pinter */
};

#define PB_INIT(pb, buf, len)  \
        (pb)->ptr = (buf);         \
        (pb)->end = (buf) + (len);

#define PB_END(pb) \
        ((pb)->ptr < (pb)->end)

#define PB_CHECK(stmt) \
        if ( !(stmt) ) goto malformed

#define PB_LEFT(pb) \
        ((pb)->end - (pb)->ptr)

#define PB_SEEK(pb, off)               \
        (pb)->ptr += off;                  \
        PB_CHECK( (pb)->ptr <= (pb)->end )

#define PB_MEMCPY(pb, dst, len)      \
        memcpy((dst), (pb)->ptr, (len)); \
        PB_SEEK((pb), (len))

#define PB_READ_UINT8(pb, val)     \
        (val) = *(uint8_t*)(pb)->ptr;  \
        PB_SEEK((pb), sizeof(uint8_t))

#define PB_READ_UINT16(pb, val)     \
        (val) = *(uint16_t*)(pb)->ptr;  \
        PB_SEEK((pb), sizeof(uint16_t))

#define PB_READ_UINT32(pb, val)     \
        (val) = *(uint32_t*)(pb)->ptr;  \
        PB_SEEK((pb), sizeof(uint32_t))

#define PB_READ_UINT64(pb, val)     \
        (val) = *(uint64_t*)(pb)->ptr;  \
        PB_SEEK((pb), sizeof(uint64_t))

#define PB_PTR_UINT8(pb)        *(uint8_t*)(pb)->ptr
#define PB_PTR_UINT16(pb)       *(uint16_t*)(pb)->ptr
#define PB_PTR_UINT32(pb)       *(uint32_t*)(pb)->ptr
#define PB_PTR_UINT64(pb)       *(uint64_t*)(pb)->ptr

#define PB_READ_STRING(pb, dst, max_len) \
{	\
        uint16_t _pb_len;	\
        PB_READ_UINT16((pb), _pb_len);	\
        (max_len) = _pb_len > (max_len) ? (max_len) : _pb_len;	\
        memcpy((dst), pb->ptr, (max_len));	\
        PB_SEEK(pb, _pb_len);	\
}

#define PB_SKIP_TAGHDR_INT(pb) \
        PB_SEEK((pb), sizeof(uint8_t)*2)

#define PB_SKIP_TAGHDR(pb, hdr) \
        PB_SEEK((pb), sizeof(uint8_t)+sizeof(uint16_t)+(hdr)->name_len)

#endif // PACKET_H
