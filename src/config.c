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

extern struct ed2kd_inst g_ed2kd;

int ed2kd_config_load( const char * path )
{
    config_t config;

    config_init(&config);

    if ( NULL == path ) {
        path = CFG_DEFAULT_PATH;
    }

    int ret = 1;

    if ( config_read_file(&config, path) ) {
        config_setting_t * root;
        const char * str_val;
        int int_val;

        root = config_root_setting(&config);

        // listen address
        if ( config_setting_lookup_string(root, CFG_LISTEN_ADDR, &str_val) ) {
            strncpy(g_ed2kd.listen_addr, str_val, sizeof(g_ed2kd.listen_addr));
        } else {
            ED2KD_LOGERR("config: " CFG_LISTEN_ADDR " missing");
            ret = -1;
        }

        // listen port
        if ( config_setting_lookup_int(root, CFG_LISTEN_PORT, &int_val) ) {
            g_ed2kd.listen_port = (uint16_t)int_val;
        } else {
            ED2KD_LOGERR("config: " CFG_LISTEN_PORT " missing");
            ret = -1;
        }

        // listen backlog
        if ( config_setting_lookup_int(root, CFG_LISTEN_BACKLOG, &int_val) ) {
            g_ed2kd.listen_backlog = int_val;
        } else {
            ED2KD_LOGERR("config: " CFG_LISTEN_BACKLOG " missing");
            ret = -1;
        }

        // (optional) welcome message + predefined server version
        const char srv_ver[] = "server version" ED2KD_VER_STR "(ed2kd)";
        if ( config_setting_lookup_string(root, CFG_WELCOME_MESSAGE, &str_val) ) {
            g_ed2kd.welcome_msg_len = sizeof(srv_ver) + strlen(str_val);
            evutil_snprintf(g_ed2kd.welcome_msg, sizeof(g_ed2kd.welcome_msg), "%s\n%s", srv_ver, str_val);
        } else {
            g_ed2kd.welcome_msg_len = sizeof(srv_ver) - sizeof(char);
            strcpy(g_ed2kd.welcome_msg, srv_ver);
            ED2KD_LOGWRN("config: " CFG_WELCOME_MESSAGE " missing");
        }

        // server hash
        if ( config_setting_lookup_string(root, CFG_SERVER_HASH, &str_val) ) {
            hex2bin(str_val, g_ed2kd.hash, HASH_SIZE);
        } else {
            ED2KD_LOGERR("config: " CFG_SERVER_HASH " missing");
            ret = -1;
        }

    } else {
        ED2KD_LOGWRN("config: failed to parse %s(error:%s at %d line)", config_error_file(&config),
            config_error_text(&config), config_error_line(&config));
        ret = -1;
    }

    config_destroy(&config);

    return ret;
}

void ed2kd_config_free()
{
}
