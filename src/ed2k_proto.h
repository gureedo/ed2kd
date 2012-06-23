#ifndef PACKET_H
#define PACKET_H

#include "packed_struct.h"

// magic constant
enum  
{
	EDONKEYVERSION = 0x3c
};

#define HASH_SIZE 16

enum packet_proto
{
    PROTO_EDONKEY = 0xE3,
    PROTO_EMULE   = 0xC5,
    PROTO_PACKED  = 0xD4
};

enum packet_opcode {
    //client2client
    OP_HELLO                    = 0x01, // 0x10<HASH 16><ID 4><PORT 2><1 Tag_set>
    OP_HELLOANSWER              = 0x4C, // <HASH 16><ID 4><PORT 2><1 Tag_set><SERVER_IP 4><SERVER_PORT 2>

    //client2server
    OP_LOGINREQUEST             = 0x01, // <HASH 16><ID 4><PORT 2><TAG_COUNT 4><TAGS...>
    OP_REJECT                   = 0x05, // (null)
    OP_GETSERVERLIST			= 0x14,	// (null)client->server
    OP_OFFERFILES				= 0x15,	// <count 4>(<HASH 16><ID 4><PORT 2><1 Tag_set>)[count]
    OP_SEARCHREQUEST			= 0x16,	// <Query_Tree>
    OP_DISCONNECT				= 0x18,	// (not verified)
    OP_GETSOURCES				= 0x19,	// <HASH 16>
                                        // v2 <HASH 16><SIZE_4> (17.3) (mandatory on 17.8)
                                        // v2large <HASH 16><FILESIZE 4(0)><FILESIZE 8> (17.9) (large files only)
    //OP_SEARCH_USER			= 0x1A,	// <Query_Tree>
    //OP_CALLBACKREQUEST		= 0x1C,	// <ID 4>
    //OP_QUERY_CHATS			= 0x1D,	// (deprecated, not supported by server any longer)
    //OP_CHAT_MESSAGE			= 0x1E,	// (deprecated, not supported by server any longer)
    //OP_JOIN_ROOM				= 0x1F,	// (deprecated, not supported by server any longer)
    OP_QUERY_MORE_RESULT		= 0x21,	// (null)
    //OP_GETSOURCES_OBFU        = 0x23,
    //OP_SERVERLIST				= 0x32,	// <count 1>(<IP 4><PORT 2>)[count] server->client
    //OP_SEARCHRESULT			= 0x33,	// <count 4>(<HASH 16><ID 4><PORT 2><1 Tag_set>)[count]
    OP_SERVERSTATUS             = 0x34, // <USERS 4><FILES 4>
    //OP_CALLBACKREQUESTED		= 0x35,	// <IP 4><PORT 2>
    //OP_CALLBACK_FAIL			= 0x36,	// (null notverified)
    OP_SERVERMESSAGE            = 0x38, // <len 2><Message len>
    //OP_CHAT_ROOM_REQUEST		= 0x39,	// (deprecated, not supported by server any longer)
    //OP_CHAT_BROADCAST			= 0x3A,	// (deprecated, not supported by server any longer)
    //OP_CHAT_USER_JOIN			= 0x3B,	// (deprecated, not supported by server any longer)
    //OP_CHAT_USER_LEAVE		= 0x3C,	// (deprecated, not supported by server any longer)
    //OP_CHAT_USER				= 0x3D,	// (deprecated, not supported by server any longer)
    OP_IDCHANGE                 = 0x40, // <NEW_ID 4>
    OP_SERVERIDENT              = 0x41, // <HASH 16><IP 4><PORT 2>{1 TAG_SET}
    OP_FOUNDSOURCES             = 0x42 // <HASH 16><count 1>(<ID 4><PORT 2>)[count]
    //OP_USERS_LIST				= 0x43, // <count 4>(<HASH 16><ID 4><PORT 2><1 Tag_set>)[count]
    //OP_FOUNDSOURCES_OBFU      = 0x44  // <HASH 16><count 1>(<ID 4><PORT 2><obf settings 1>(UserHash16 if obf&0x08))[count]
};

PACKED_STRUCT(
struct packet_header {
    uint8_t proto;
    uint32_t length;
};
)

PACKED_STRUCT(
struct packet_server_message {
    uint8_t proto;
    uint32_t length;
    uint8_t opcode;
    uint16_t msg_len;
};
)

PACKED_STRUCT(
struct packet_id_change {
    uint8_t proto;
    uint32_t length;
    uint8_t opcode;
    uint32_t user_id;
    uint32_t tcp_flags;
};
)

PACKED_STRUCT(
struct packet_server_status {
    uint8_t proto;
    uint32_t length;
    uint8_t opcode;
    uint32_t user_count;
    uint32_t file_count;
};
)

PACKED_STRUCT(
struct packet_server_ident {
	uint8_t proto;
	uint32_t length;
	uint8_t opcode;
	unsigned char hash[HASH_SIZE];
	uint32_t ip;
	uint16_t port;
	uint32_t tag_count;
	// tag set
};
)

PACKED_STRUCT(
struct packet_found_sources {
	uint8_t proto;
	uint32_t length;
	uint8_t opcode;
	unsigned char hash[HASH_SIZE];
	uint32_t count;
};
)

PACKED_STRUCT(
struct packet_hello {
    uint8_t proto;
    uint32_t length;
    uint8_t opcode;
    uint8_t hash_size; // 16
    unsigned char hash[16]; // server hash
    uint32_t client_id; // get local sock ip
    uint16_t client_port; // 4662
    uint32_t tag_count; // 2
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
};
)

PACKED_STRUCT(
struct tag_header
{
    uint8_t type;
    uint16_t name_len;
    unsigned char name;
};
)

PACKED_STRUCT(
struct tag_value {
    union {
        char val_hash16[16];
        struct {
            uint16_t val_str_len;
            char val_str;
        };
        uint32_t val_uint32;
        //float32_t val_float32;
        //uint8_t value_bool;
        uint8_t val_uint8;
        uint16_t val_uint16;
        uint64_t val_uint64;
    };
};
)

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

	// OP_OFFERFILES
	TN_FILENAME					= 0x01,
	TN_FILESIZE					= 0x02,
	TN_FILETYPE					= 0x03,
	FT_FILESIZE_HI				= 0x3A,
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
	FT_ANY				= 0,
	FT_AUDIO			= 1,
	FT_VIDEO			= 2,
	FT_IMAGE			= 3,
	FT_PROGRAM			= 4,
	FT_DOCUMENT			= 5,
	FT_ARCHIVE			= 6,
	FT_CDIMAGE			= 7,
	FT_EMULECOLLECTION	= 8
};

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
    SC_MINSIZE      = 0x01000101,
    SC_MAXSIZE      = 0x02000101,
    SC_SRCAVAIL     = 0x15000101,
    SC_SRCCMPLETE   = 0x30000101,
    SC_MINBITRATE   = 0xd4000101,
    SC_MINLENGTH    = 0xd3000101,
};

#endif // PACKET_H
