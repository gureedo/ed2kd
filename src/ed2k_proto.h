#ifndef ED2K_PROTO_H
#define ED2K_PROTO_H

/*
  @file ed2k_proto.h eDonkey2000 protocol details
*/

#include <stdint.h>
#include "packed_struct.h"

// potocol version
enum
{
    EDONKEYVERSION = 0x3c
};

#define HASH_SIZE 16

// Server TCP flags
#define SRV_TCPFLG_COMPRESSION      0x00000001
#define SRV_TCPFLG_NEWTAGS          0x00000008
#define SRV_TCPFLG_UNICODE          0x00000010
#define SRV_TCPFLG_RELATEDSEARCH    0x00000040
#define SRV_TCPFLG_TYPETAGINTEGER   0x00000080
#define SRV_TCPFLG_LARGEFILES       0x00000100
#define SRV_TCPFLG_TCPOBFUSCATION   0x00000400

enum packet_proto
{
    PROTO_EDONKEY = 0xE3,
    PROTO_EMULE   = 0xC5,
    PROTO_PACKED  = 0xD4
};

enum packet_opcode {
    //client2client
    OP_HELLO                    = 0x01, // <hash_size1><hash16><id4><port2><tag_count4>[tags...]
    OP_HELLOANSWER              = 0x4C, // <hash16><id4><port2><tag_count4>[tags...]<server_ip4><server_port2>

    //client2server
    OP_LOGINREQUEST             = 0x01, // <hash16><id4><port2><tag_count4>[tags...]
    OP_REJECT                   = 0x05, // empty
    OP_GETSERVERLIST			= 0x14,	// empty
    OP_OFFERFILES				= 0x15,	// <count4>[<hash16><id4><port2><tag_count4>[tags...]...]
    OP_SEARCHREQUEST			= 0x16,	// <query_tree>
    OP_DISCONNECT				= 0x18,	// empty
    OP_GETSOURCES				= 0x19,	// <hash16>
                                        // //v2 <HASH 16><SIZE_4> (17.3) (mandatory on 17.8)
                                        // //v2large <HASH 16><FILESIZE 4(0)><FILESIZE 8> (17.9) (large files only)
    //OP_SEARCH_USER			= 0x1A,	//
    OP_CALLBACKREQUEST          = 0x1C,	//
    //OP_QUERY_CHATS			= 0x1D,	//
    //OP_CHAT_MESSAGE			= 0x1E,	//
    //OP_JOIN_ROOM				= 0x1F,	//
    OP_QUERY_MORE_RESULT		= 0x21,	// empty
    OP_GETSOURCES_OBFU          = 0x23, //
    //OP_SERVERLIST				= 0x32,	//
    OP_SEARCHRESULT             = 0x33,	// <count4>[<hash16><id4><port2><tag_count4>[tags...]...]
    OP_SERVERSTATUS             = 0x34, // <users_count4><files_count4>
    //OP_CALLBACKREQUESTED		= 0x35,	//
    OP_CALLBACK_FAIL			= 0x36,	//
    OP_SERVERMESSAGE            = 0x38, // <msg_len2><message>
    //OP_CHAT_ROOM_REQUEST		= 0x39,	//
    //OP_CHAT_BROADCAST			= 0x3A,	//
    //OP_CHAT_USER_JOIN			= 0x3B,	//
    //OP_CHAT_USER_LEAVE		= 0x3C,	//
    //OP_CHAT_USER				= 0x3D,	//
    OP_IDCHANGE                 = 0x40, // <id4>
    OP_SERVERIDENT              = 0x41, // <hash16><ip4><port2><tag_count4>[tags...]
    OP_FOUNDSOURCES             = 0x42  // <HASH 16><count 1>(<ID 4><PORT 2>)[count]
    //OP_USERS_LIST				= 0x43,
    //OP_FOUNDSOURCES_OBFU      = 0x44
};

PACKED_STRUCT(
struct packet_header {
    uint8_t proto;
    uint32_t length;
}
);

PACKED_STRUCT(
struct packet_server_message {
    struct packet_header hdr;
    uint8_t opcode;
    uint16_t msg_len;
}
);

PACKED_STRUCT(
struct packet_id_change {
    struct packet_header hdr;
    uint8_t opcode;
    uint32_t user_id;
    uint32_t tcp_flags;
}
);

PACKED_STRUCT(
struct packet_server_status {
    struct packet_header hdr;
    uint8_t opcode;
    uint32_t user_count;
    uint32_t file_count;
}
);

PACKED_STRUCT(
struct packet_server_ident {
    struct packet_header hdr;
    uint8_t opcode;
    unsigned char hash[HASH_SIZE];
    uint32_t ip;
    uint16_t port;
    uint32_t tag_count;
}
);

PACKED_STRUCT(
struct packet_search_result {
    struct packet_header hdr;
    uint8_t opcode;
    uint32_t files_count;
}
);

PACKED_STRUCT(
struct search_file_entry {
    unsigned char hash[HASH_SIZE];
    uint32_t id;
    uint16_t port;
    uint32_t tag_count;
}
);

PACKED_STRUCT(
struct packet_found_sources {
    struct packet_header hdr;
    uint8_t opcode;
    unsigned char hash[HASH_SIZE];
    uint8_t count;
}
);

PACKED_STRUCT(
struct packet_hello {
    struct packet_header hdr;
    uint8_t opcode;
    uint8_t hash_size;
    unsigned char hash[16];
    uint32_t client_id;
    uint16_t client_port;
    uint32_t tag_count;
    struct {
        uint8_t type;  // TT_STR5
        uint8_t name;  // TN_NAME
        char value[5]; //ed2kd
    } tag_name;
    struct {
        uint8_t type; // TT_UINT8 & 0x7f
        uint8_t name; // TN_VERSION
        uint8_t value; // EDONKEYPROTO
    } tag_version;
    uint32_t ip; // 0
    uint16_t port; //0
}
);

PACKED_STRUCT(
struct tag_header
{
    uint8_t type;
    uint16_t name_len;
    unsigned char name[1];
}
);

PACKED_STRUCT(
struct short_tag {
    uint8_t type;
    uint8_t name;
    unsigned char data[1];
}
);

enum tag_type {
    TT_HASH16       = 0x01,
    TT_STRING       = 0x02,
    TT_UINT32       = 0x03,
    TT_FLOAT32      = 0x04,
    //TAGTYPE_BOOL		= 0x05,
    //TAGTYPE_BOOLARRAY	= 0x06,
    //TAGTYPE_BLOB		= 0x07,
    TT_UINT16       = 0x08,
    TT_UINT8        = 0x09,
    //TAGTYPE_BSOB		= 0x0A,
    TT_UINT64       = 0x0B,

    // Compressed string types
    TT_STR1         = 0x11,
    TT_STR2,
    TT_STR3,
    TT_STR4,
    TT_STR5,
    TT_STR6,
    TT_STR7,
    TT_STR8,
    TT_STR9,
    TT_STR10,
    TT_STR11,
    TT_STR12,
    TT_STR13,
    TT_STR14,
    TT_STR15,
    TT_STR16
};

enum tag_name
{
    // OP_LOGINREQUEST
    TN_NAME                     = 0x01,
    TN_PORT                     = 0x0F,
    TN_VERSION                  = 0x11,
    TN_SERVER_FLAGS             = 0x20,
    TN_EMULE_VERSION            = 0xFB,

    // OP_OFFERFILES, OP_SEARCHRESULT
    TN_FILENAME					= 0x01,
    TN_FILESIZE					= 0x02,
    TN_FILETYPE					= 0x03,
    TN_FILEFORMAT               = 0x04,
    TN_SOURCES                  = 0x15,
    TN_COMPLETE_SOURCES         = 0x30,
    TN_FILESIZE_HI				= 0x3A,
    TN_FILERATING				= 0xF7,

    // OP_SERVERIDENT
    TN_SERVERNAME				= 0x01,
    TN_DESCRIPTION				= 0x0B
};

// string tag names
#define	TNS_MEDIA_LENGTH	"length"
#define	TNS_MEDIA_BITRATE	"bitrate"
#define	TNS_MEDIA_CODEC		"codec"

enum file_type
{
    FT_ANY              = 0,
    FT_AUDIO			= 1,
    FT_VIDEO			= 2,
    FT_IMAGE			= 3,
    FT_PROGRAM			= 4,
    FT_DOCUMENT			= 5,
    FT_ARCHIVE			= 6,
    FT_CDIMAGE			= 7,
    FT_EMULECOLLECTION	= 8
};

// string file types
#define	FTS_AUDIO	        "Audio"
#define	FTS_VIDEO           "Video"
#define	FTS_IMAGE           "Image"
#define	FTS_DOCUMENT		"Doc"
#define	FTS_PROGRAM		    "Pro"
#define	FTS_ARCHIVE		    "Arc"
#define	FTS_CDIMAGE		    "Iso"
#define FTS_EMULECOLLECTION	"EmuleCollection"

enum search_operator
{
    SO_AND            = 0x0000, // uint16
    SO_OR             = 0x0100, // uint16
    SO_NOT            = 0x0200, // uint16
    SO_STRING_TERM    = 0x01, // uint8
    SO_STRING_CONSTR  = 0x02, // uint8
    SO_UINT32         = 0x03, // uint8
    SO_UINT64         = 0x08  // uint8
};

enum search_constraint {
    SC_MINSIZE      = 0x02000101,
    SC_MAXSIZE      = 0x02000102,
    SC_SRCAVAIL     = 0x15000101,
    SC_SRCCMPLETE   = 0x30000101,
    SC_MINBITRATE   = 0xd4000101,
    SC_MINLENGTH    = 0xd3000101
};

#endif // ED2K_PROTO_H
