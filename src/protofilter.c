#include <stdint.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "protofilter.h"
#include "ed2k_proto.h"

enum bufferevent_filter_result
ed2k_input_filter_cb( struct evbuffer *src, struct evbuffer *dst,
    ev_ssize_t dst_limit, enum bufferevent_flush_mode mode, void *ctx )
{
    // todo: add compression support

    size_t src_len = evbuffer_get_length(src);

    // wait for ed2k_header_t
    if ( src_len > sizeof(struct packet_header) ) {
        const struct packet_header * header =
            (struct packet_header*)evbuffer_pullup(src, sizeof(struct packet_header));

        // check header protocol
        if ( PROTO_EDONKEY != header->proto )
            return BEV_ERROR;

        // wait fo full length packet
        size_t packet_len = header->length + sizeof(struct packet_header);
        if ( packet_len > src_len )
            return BEV_NEED_MORE;

        evbuffer_remove_buffer(src, dst, packet_len);
        return BEV_OK;
    } else {
        return BEV_NEED_MORE;
    }
}
