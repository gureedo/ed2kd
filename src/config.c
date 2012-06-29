#include <string.h>
#include <stdint.h>
#include <libconfig.h>
#include <event2/util.h>
#include "config.h"
#include "log.h"
#include "ed2kd.h"
#include "ed2k_proto.h"
#include "version.h"
#include "util.h"

#define CFG_DEFAULT_PATH "ed2kd.conf"

#define CFG_LISTEN_ADDR             "listen_addr"
#define CFG_LISTEN_PORT             "listen_port"
#define CFG_LISTEN_BACKLOG          "listen_backlog"
#define CFG_WELCOME_MESSAGE         "welcome_message"
#define CFG_SERVER_HASH             "server_hash"
#define CFG_SERVER_NAME             "server_name"
#define CFG_SERVER_DESCR            "server_descr"
#define CFG_ALLOW_LOWID             "allow_lowid"

extern struct ed2kd_cfg g_ed2kd_cfg;

int ed2kd_config_load( const char * path )
{
    static const char srv_ver[] = "server version" ED2KD_VER_STR " (ed2kd)";
    config_t config;
    int ret = 1;

    config_init(&config);

    if ( NULL == path ) {
        path = CFG_DEFAULT_PATH;
    }

    if ( config_read_file(&config, path) ) {
        config_setting_t * root;
        const char * str_val;
        int int_val;

        root = config_root_setting(&config);

        // listen address
        if ( config_setting_lookup_string(root, CFG_LISTEN_ADDR, &str_val) ) {
            strncpy(g_ed2kd_cfg.listen_addr, str_val, sizeof(g_ed2kd_cfg.listen_addr));
            // in_addr value of listen_addr
            evutil_inet_pton(AF_INET, g_ed2kd_cfg.listen_addr, &g_ed2kd_cfg.listen_addr_int);
        } else {
            ED2KD_LOGERR("config: " CFG_LISTEN_ADDR " missing");
            ret = -1;
        }

        // listen port
        if ( config_setting_lookup_int(root, CFG_LISTEN_PORT, &int_val) ) {
            g_ed2kd_cfg.listen_port = (uint16_t)int_val;
        } else {
            ED2KD_LOGERR("config: " CFG_LISTEN_PORT " missing");
            ret = -1;
        }

        // listen backlog
        if ( config_setting_lookup_int(root, CFG_LISTEN_BACKLOG, &int_val) ) {
            g_ed2kd_cfg.listen_backlog = int_val;
        } else {
            ED2KD_LOGERR("config: " CFG_LISTEN_BACKLOG " missing");
            ret = -1;
        }

        // (optional) welcome message + predefined server version
        if ( config_setting_lookup_string(root, CFG_WELCOME_MESSAGE, &str_val) ) {
            g_ed2kd_cfg.welcome_msg_len = sizeof srv_ver + strlen(str_val)+1;
            evutil_snprintf(g_ed2kd_cfg.welcome_msg, sizeof(g_ed2kd_cfg.welcome_msg), "%s\n%s", srv_ver, str_val);
        } else {
            g_ed2kd_cfg.welcome_msg_len = sizeof srv_ver - sizeof(char);
            strcpy(g_ed2kd_cfg.welcome_msg, srv_ver);
            ED2KD_LOGWRN("config: " CFG_WELCOME_MESSAGE " missing");
        }

        // server hash
        if ( config_setting_lookup_string(root, CFG_SERVER_HASH, &str_val) ) {
            hex2bin(str_val, g_ed2kd_cfg.hash, HASH_SIZE);
        } else {
            ED2KD_LOGERR("config: " CFG_SERVER_HASH " missing");
            ret = -1;
        }

        // server name (optional)
        if ( config_setting_lookup_string(root, CFG_SERVER_NAME, &str_val) ) {
            size_t len = strlen(str_val)-1;
            g_ed2kd_cfg.server_name_len = MAX_SERVER_NAME_LEN > len ? len: MAX_SERVER_NAME_LEN;
            strncpy(g_ed2kd_cfg.server_name, str_val, g_ed2kd_cfg.server_name_len+1);
        }

        // server description (optional)
        if ( config_setting_lookup_string(root, CFG_SERVER_DESCR, &str_val) ) {
            size_t len = strlen(str_val)-1;
            g_ed2kd_cfg.server_descr_len = MAX_SERVER_DESCR_LEN > len ? len: MAX_SERVER_DESCR_LEN;
            strncpy(g_ed2kd_cfg.server_descr, str_val, g_ed2kd_cfg.server_descr_len+1);
        }

        // allow lowid
        if ( config_setting_lookup_int(root, CFG_ALLOW_LOWID, &int_val) ) {
            g_ed2kd_cfg.allow_lowid = (int_val != 0);
        } else {
            ED2KD_LOGERR("config: " CFG_ALLOW_LOWID " missing");
            ret = -1;
        }

    } else {
        ED2KD_LOGWRN("config: failed to parse %s(error:%s at %d line)", path,
            config_error_text(&config), config_error_line(&config));
        ret = -1;
    }

    config_destroy(&config);

    return ret;
}

void ed2kd_config_free()
{
}
