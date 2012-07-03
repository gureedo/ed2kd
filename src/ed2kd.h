#ifndef ED2KD_H
#define ED2KD_H

/**
  @file ed2kd.h General server configuration variables and routines
*/

#define MAX_WELCOMEMSG_LEN		1024
#define MAX_SERVER_NAME_LEN		64
#define MAX_SERVER_DESCR_LEN	64
#define MAX_SEARCH_FILES        200
#define MAX_UNCOMPRESSED_PACKET_SIZE  300*1024

// runtime variables
struct ed2kd_rt {
	uint32_t user_count;
	uint32_t file_count;
};

/**
  @brief runtime variables access point
*/
struct ed2kd_rt *ed2kd_rt();

// variables loaded from config file or initialized at startup
struct ed2kd_cfg
{
    char listen_addr[15];
	uint32_t listen_addr_inaddr;
    uint16_t listen_port;
    int listen_backlog;
	unsigned char hash[16];
    uint32_t srv_tcp_flags;

	size_t welcome_msg_len;
    char welcome_msg[MAX_WELCOMEMSG_LEN+1];

	size_t server_name_len;
	char server_name[MAX_SERVER_NAME_LEN+1];

	size_t server_descr_len;
	char server_descr[MAX_SERVER_DESCR_LEN+1];

    //flags
    unsigned allow_lowid:1;
};

/**
  @brief configuration variables access point
*/
const struct ed2kd_cfg *ed2kd_cfg();

/**
  @brief common server initialization
  @return 0 on success, -1 on failure
*/
int ed2kd_init();

/**
  @brief main server loop
  @return EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int ed2kd_run();


#endif // ED2KD_H
