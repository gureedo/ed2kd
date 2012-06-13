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

#define ED2KD_SRV_TCP_FLAGS \
    SRV_TCPFLG_TYPETAGINTEGER | SRV_TCPFLG_LARGEFILES

// runtime stuff
struct ed2kd_rt {
	// runtime variables
	uint32_t user_count;
	uint32_t file_count;
};

struct ed2kd_rt *ed2kd_rt();

// variables loaded from config file
struct ed2kd_cfg
{
    // configuration variables
    char listen_addr[15];
    uint16_t listen_port;
    int listen_backlog;
    size_t welcome_msg_len;
    char welcome_msg[1024];
    unsigned char hash[16];
};

const struct ed2kd_cfg *ed2kd_cfg();

int ed2kd_init();
int ed2kd_run();

#endif // SERVER_H
