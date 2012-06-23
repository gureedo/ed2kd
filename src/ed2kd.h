#ifndef SERVER_H
#define SERVER_H

// Server TCP flags
#define SRV_TCPFLG_COMPRESSION      0x00000001
#define SRV_TCPFLG_NEWTAGS          0x00000008
#define SRV_TCPFLG_UNICODE          0x00000010
#define SRV_TCPFLG_RELATEDSEARCH    0x00000040
#define SRV_TCPFLG_TYPETAGINTEGER   0x00000080
#define SRV_TCPFLG_LARGEFILES       0x00000100
#define SRV_TCPFLG_TCPOBFUSCATION   0x00000400

#define MAX_WELCOMEMSG_LEN		1024
#define MAX_SERVER_NAME_LEN		64
#define MAX_SERVER_DESCR_LEN	64
#define MAX_SEARCH_FILES        200

#define ED2KD_SRV_TCP_FLAGS \
    SRV_TCPFLG_COMPRESSION | SRV_TCPFLG_TYPETAGINTEGER | SRV_TCPFLG_LARGEFILES

// runtime variables
struct ed2kd_rt {
	uint32_t user_count;
	uint32_t file_count;
};

struct ed2kd_rt *ed2kd_rt();

// variables loaded from config file
struct ed2kd_cfg
{
    char listen_addr[15];
	uint32_t listen_addr_int;
    uint16_t listen_port;
    int listen_backlog;
	unsigned char hash[16];
	
	size_t welcome_msg_len;
    char welcome_msg[MAX_WELCOMEMSG_LEN+1];

	size_t server_name_len;
	char server_name[MAX_SERVER_NAME_LEN+1];

	size_t server_descr_len;
	char server_descr[MAX_SERVER_DESCR_LEN+1];
};

const struct ed2kd_cfg *ed2kd_cfg();

int ed2kd_init();
int ed2kd_run();


#endif // SERVER_H
