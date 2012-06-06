#ifndef PROTOFILTER_H
#define PROTOFILTER_H

enum bufferevent_filter_result
ed2k_input_filter_cb( struct evbuffer *src, struct evbuffer *dst,
    ev_ssize_t dst_limit, enum bufferevent_flush_mode mode, void *ctx );

#endif // PROTOFILTER_H
