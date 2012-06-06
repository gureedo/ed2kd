#ifndef PACKET_BUFFER_H
#define PACKET_BUFFER_H

struct packet_buffer {
    const unsigned char *ptr;
    const unsigned char *end;
};

#define PB_INIT(pkt, buf, len)  \
    (pkt)->ptr = (buf);         \
    (pkt)->end = (buf) + (len);

#define PB_CHECK(stmt) \
    if ( !(stmt) ) goto malformed

#define PB_SEEK(pkt, off)               \
    (pkt)->ptr += off;                  \
    PB_CHECK( (pkt)->ptr < (pkt)->end )

#define PB_MEMCPY(pkt, dst, len)      \
    memcpy((dst), (pkt)->ptr, (len)); \
    PB_SEEK((pkt), (len))

#define PB_READ_UINT8(pkt, val)     \
    (val) = *(uint8_t*)(pkt)->ptr;  \
    PB_SEEK((pkt), sizeof(uint8_t))

#define PB_READ_UINT16(pkt, val)     \
    (val) = *(uint16_t*)(pkt)->ptr;  \
    PB_SEEK((pkt), sizeof(uint16_t))

#define PB_READ_UINT32(pkt, val)     \
    (val) = *(uint32_t*)(pkt)->ptr;  \
    PB_SEEK((pkt), sizeof(uint32_t))

#define PB_READ_UINT64(pkt, val)     \
    (val) = *(uint64_t*)(pkt)->ptr;  \
    PB_SEEK((pkt), sizeof(uint64_t))

#define PB_SKIP_TAGHDR_INT(pkt) \
    PB_SEEK((pkt), sizeof(uint8_t)*2)

#define PB_SKIP_TAGHDR(pkt, hdr) \
    PB_SEEK((pkt), sizeof(uint8_t)+sizeof(uint16_t)+(hdr)->name_len)

#endif // PACKET_BUFFER_H
