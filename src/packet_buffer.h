#ifndef PACKET_BUFFER_H
#define PACKET_BUFFER_H

struct packet_buffer {
    const unsigned char *ptr;
    const unsigned char *end;
};

#define PB_INIT(pb, buf, len)  \
    (pb)->ptr = (buf);         \
    (pb)->end = (buf) + (len);

#define PB_END(pb) \
    ((pb)->ptr < (pb)->end)

#define PB_CHECK(stmt) \
    if ( !(stmt) ) goto malformed

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

#endif // PACKET_BUFFER_H
