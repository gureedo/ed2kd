#include "packet.h"

#include <math.h>       /* floor */
#include <string.h>     /* memcpy */
#include <malloc.h>     /* alloca */
#include <alloca.h>     /* alloca */

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "ed2k_proto.h"
#include "server.h"

void send_id_change(struct bufferevent *bev, uint32_t id)
{
    struct packet_id_change data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof(data) - sizeof(data.hdr);
    data.opcode = OP_IDCHANGE;
    data.user_id = id;
    data.tcp_flags = g_srv.cfg->srv_tcp_flags;

    bufferevent_write(bev, &data, sizeof(data));
}

void send_server_message(struct bufferevent *bev, const char *msg, uint16_t len)
{
    struct packet_server_message data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof(data) - sizeof(data.hdr) + len;
    data.opcode = OP_SERVERMESSAGE;
    data.msg_len = len;

    bufferevent_write(bev, &data, sizeof(data));
    bufferevent_write(bev, msg, len);
}

void send_server_status(struct bufferevent *bev)
{
    struct packet_server_status data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof(data) - sizeof(data.hdr);
    data.opcode = OP_SERVERSTATUS;
    data.user_count = atomic_load(&g_srv.user_count);
    data.file_count = atomic_load(&g_srv.file_count);

    bufferevent_write(bev, &data, sizeof(data));
}

void send_server_ident(struct bufferevent *bev)
{
    struct packet_server_ident data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof(data) - sizeof(data.hdr);
    data.opcode = OP_SERVERSTATUS;
    memcpy(data.hash, g_srv.cfg->hash, sizeof(data.hash));
    data.ip = g_srv.cfg->listen_addr_inaddr;
    data.port = g_srv.cfg->listen_port;
    data.tag_count = (g_srv.cfg->server_name_len > 0) + (g_srv.cfg->server_descr_len > 0);

    if (data.tag_count > 0) {
        struct evbuffer *buf = evbuffer_new();
        evbuffer_add(buf, &data, sizeof(data));

        if (g_srv.cfg->server_name_len > 0) {
            struct tag_header th;
            struct tag_strval *tv;
            size_t data_len = sizeof(*tv) + g_srv.cfg->server_name_len - 1;
            tv = (struct tag_strval *) alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_SERVERNAME;
            tv->len = g_srv.cfg->server_name_len;
            memcpy(tv->str, g_srv.cfg->server_name, g_srv.cfg->server_name_len);

            evbuffer_add(buf, &th, sizeof(th));
            evbuffer_add(buf, tv, data_len);
        }

        if (g_srv.cfg->server_descr_len > 0) {
            struct tag_header th;
            struct tag_strval *tv;
            size_t data_len = sizeof(*tv) + g_srv.cfg->server_descr_len - 1;
            tv = (struct tag_strval *) alloca(data_len);

            th.type = TT_STRING;
            th.name_len = 1;
            *th.name = TN_DESCRIPTION;
            tv->len = g_srv.cfg->server_descr_len;
            memcpy(tv->str, g_srv.cfg->server_descr, g_srv.cfg->server_descr_len);

            evbuffer_add(buf, &th, sizeof(th));
            evbuffer_add(buf, tv, data_len);
        }

        {
            struct packet_header *ph = (struct packet_header *) evbuffer_pullup(buf, sizeof(*ph));
            ph->length = evbuffer_get_length(buf) - sizeof(*ph);
        }

        bufferevent_write_buffer(bev, buf);
        evbuffer_free(buf);
    } else {
        bufferevent_write(bev, &data, sizeof(data));
    }
}

void send_server_list(struct bufferevent *bev)
{
    (void) bev;
    // TODO: implement me :)
}

void send_reject(struct bufferevent *bev)
{
    static const char data[] = {PROTO_EDONKEY, 1, 0, 0, 0, OP_REJECT};
    bufferevent_write(bev, &data, sizeof(data));
}

void send_callback_fail(struct bufferevent *bev)
{
    static const char data[] = {PROTO_EDONKEY, 1, 0, 0, 0, OP_CALLBACK_FAIL};
    bufferevent_write(bev, &data, sizeof(data));
}

void send_found_sources(struct bufferevent *bev, const unsigned char *hash, struct file_source *sources, size_t count)
{
    struct packet_found_sources data;
    size_t srcs_len = count * sizeof(*sources);

    data.hdr.proto = PROTO_EDONKEY;
    memcpy(data.hash, hash, sizeof(data.hash));
    data.hdr.length = sizeof(data) - sizeof(data.hdr) + srcs_len;
    data.opcode = OP_FOUNDSOURCES;
    data.count = count;
    bufferevent_write(bev, &data, sizeof(data));
    if (count)
        bufferevent_write(bev, sources, srcs_len);
}

void send_search_result(struct bufferevent *bev, struct evbuffer *result, size_t count)
{
    struct packet_search_result data;

    data.hdr.proto = PROTO_EDONKEY;
    data.hdr.length = sizeof(data) - sizeof(data.hdr) + evbuffer_get_length(result);
    data.opcode = OP_SEARCHRESULT;
    data.files_count = count;
    evbuffer_prepend(result, &data, sizeof(data));

    bufferevent_write_buffer(bev, result);
}

void write_search_file(struct evbuffer *buf, const struct search_file *file)
{
    struct search_file_entry sfe;

    memcpy(sfe.hash, file->hash, sizeof sfe.hash);
    sfe.id = file->client_id;
    sfe.port = file->client_port;
    sfe.tag_count = 1 + 1 + /*(0!=file->type)+*/ (file->ext_len > 0) + 1 + 1 + (file->rated_count > 0) + (file->media_length > 0) + (file->media_length > 0) + (file->media_codec_len > 0);
    evbuffer_add(buf, &sfe, sizeof sfe);

    {
        struct tag_header th;
        struct tag_strval *tv;
        size_t data_len = sizeof *tv + file->name_len - 1;
        tv = (struct tag_strval *) alloca(data_len);

        th.type = TT_STRING;
        th.name_len = 1;
        *th.name = TN_FILENAME;
        tv->len = file->name_len;
        memcpy(tv->str, file->name, file->name_len);

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, tv, data_len);
    }

    {
        struct tag_header th;
        th.type = TT_UINT64;
        th.name_len = 1;
        *th.name = TN_FILESIZE;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->size, sizeof file->size);
    }

    /*if ( files[i].type ) {
    struct tag_header th;
    th.type = TT_STRING;
    th.name_len = 1;
    *th.name = TN_FILETYPE;
    //len
    //type
    }*/

    if (file->ext_len) {
        struct tag_header th;
        struct tag_strval *tv;
        size_t data_len = sizeof *tv + file->ext_len - 1;
        tv = (struct tag_strval *) alloca(data_len);

        th.type = TT_STRING;
        th.name_len = 1;
        *th.name = TN_FILEFORMAT;
        tv->len = file->ext_len;
        memcpy(tv->str, file->ext, file->ext_len);

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, tv, data_len);
    }

    {
        struct tag_header th;
        th.type = TT_UINT32;
        th.name_len = 1;
        *th.name = TN_SOURCES;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->srcavail, sizeof file->srcavail);
    }

    {
        struct tag_header th;
        th.type = TT_UINT32;
        th.name_len = 1;
        *th.name = TN_COMPLETE_SOURCES;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &file->srccomplete, sizeof file->srccomplete);
    }

    if (file->rated_count > 0) {
        uint16_t data;
        struct tag_header th;
        th.type = TT_UINT16;
        th.name_len = 1;
        *th.name = TN_FILERATING;

        // lo-byte: percentage rated this file
        data = (100 * (uint8_t) ((float) file->srcavail / (float) file->rated_count)) << 8;
        // hi-byte: average rating
        data += ((uint16_t) floor((double) file->rating / (double) file->rated_count + 0.5f) * 51) & 0xFF;

        evbuffer_add(buf, &th, sizeof th);
        evbuffer_add(buf, &data, sizeof data);
    }

    if (file->media_length) {
        struct tag_header *th;
        uint16_t name_len = sizeof(TNS_MEDIA_LENGTH) - 1;

        size_t th_len = sizeof *th + name_len - 1;
        th = (struct tag_header *) alloca(th_len);
        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_LENGTH, name_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, &file->media_length, sizeof file->media_length);
    }

    if (file->media_bitrate) {
        uint16_t name_len = sizeof(TNS_MEDIA_BITRATE) - 1;
        struct tag_header *th;
        size_t th_len = sizeof *th + name_len - 1;
        th = (struct tag_header *) alloca(th_len);
        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_BITRATE, name_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, &file->media_bitrate, sizeof(file->media_bitrate));
    }

    if (file->media_codec_len) {
        struct tag_header *th;
        struct tag_strval *tv;
        uint16_t name_len = sizeof(TNS_MEDIA_CODEC) - 1;
        size_t th_len = sizeof(*th) + name_len - 1;
        size_t tv_len = sizeof(*tv) + file->media_codec_len - 1;
        th = (struct tag_header *) alloca(th_len);
        tv = (struct tag_strval *) alloca(tv_len);

        th->type = TT_UINT32;
        th->name_len = name_len;
        memcpy(th->name, TNS_MEDIA_CODEC, name_len);

        tv->len = file->media_codec_len;
        memcpy(tv->str, file->media_codec, file->media_codec_len);

        evbuffer_add(buf, th, th_len);
        evbuffer_add(buf, tv, tv_len);
    }
}
