#include "server.h"
#include <string.h>
#include <stdlib.h>

#include <libconfig.h>
#include <event2/util.h>

#include "log.h"

#include "ed2k_proto.h"
#include "version.h"
#include "util.h"

#define CFG_DEFAULT_PATH "ed2kd.conf"

#define CFG_LISTEN_ADDR                 "listen_addr"
#define CFG_LISTEN_PORT                 "listen_port"
#define CFG_LISTEN_BACKLOG              "listen_backlog"
#define CFG_WELCOME_MESSAGE             "welcome_message"
#define CFG_SERVER_HASH                 "server_hash"
#define CFG_SERVER_NAME                 "server_name"
#define CFG_SERVER_DESCR                "server_descr"
#define CFG_ALLOW_LOWID                 "allow_lowid"
#define CFG_PORTCHECK_TIMEOUT           "portcheck_timeout"
#define CFG_STATUS_NOTIFY_INTERVAL      "status_notify_interval"
#define CFG_MAX_CLIENTS                 "max_clients"
#define CFG_MAX_FILES                   "max_files"
#define CFG_MAX_FILES_PER_CLIENT        "max_files_per_client"
#define CFG_MAX_OFFERS_LIMIT            "max_offers_limit"
#define CFG_MAX_SEARCHES_LIMIT          "max_searches_limit"

int server_load_config( const char *path )
{
        static const char srv_ver[] = "server version" ED2KD_VER_STR " (ed2kd)";
        config_t config;
        int ret = 1;
        struct server_config *server_cfg = (struct server_config *)malloc(sizeof(*server_cfg));

        config_init(&config);
        memset(server_cfg, 0, sizeof(*server_cfg));

        if ( NULL == path ) {
                path = CFG_DEFAULT_PATH;
        }

        if ( config_read_file(&config, path) ) {
                config_setting_t *root;
                const char *str_val;
                int int_val;

                root = config_root_setting(&config);

                /* listen address */
                if ( config_setting_lookup_string(root, CFG_LISTEN_ADDR, &str_val) ) {
                        server_cfg->listen_addr = strdup(str_val);
                } else {
                        ED2KD_LOGERR("config: " CFG_LISTEN_ADDR " missing");
                        ret = 0;
                }

                /* listen port */
                if ( config_setting_lookup_int(root, CFG_LISTEN_PORT, &int_val) ) {
                        server_cfg->listen_port = (uint16_t)int_val;
                } else {
                        ED2KD_LOGERR("config: " CFG_LISTEN_PORT " missing");
                        ret = 0;
                }

                /* listen backlog */
                if ( config_setting_lookup_int(root, CFG_LISTEN_BACKLOG, &int_val) ) {
                        server_cfg->listen_backlog = int_val;
                } else {
                        ED2KD_LOGERR("config: " CFG_LISTEN_BACKLOG " missing");
                        ret = 0;
                }

                /* (optional) welcome message + predefined server version */
                if ( config_setting_lookup_string(root, CFG_WELCOME_MESSAGE, &str_val) ) {
                        server_cfg->welcome_msg_len = sizeof(srv_ver) + strlen(str_val) + 1;
                        evutil_snprintf(server_cfg->welcome_msg, sizeof(server_cfg->welcome_msg), "%s\n%s", srv_ver, str_val);
                } else {
                        server_cfg->welcome_msg_len = sizeof(srv_ver) - sizeof(char);
                        strcpy(server_cfg->welcome_msg, srv_ver);
                }

                /* server hash */
                if ( config_setting_lookup_string(root, CFG_SERVER_HASH, &str_val) ) {
                        hex2bin(str_val, server_cfg->hash, ED2K_HASH_SIZE);
                } else {
                        ED2KD_LOGERR("config: " CFG_SERVER_HASH " missing");
                        ret = 0;
                }

                /* server name (optional) */
                if ( config_setting_lookup_string(root, CFG_SERVER_NAME, &str_val) ) {
                        size_t len = strlen(str_val)-1;
                        server_cfg->server_name_len = MAX_SERVER_NAME_LEN > len ? len: MAX_SERVER_NAME_LEN;
                        strncpy(server_cfg->server_name, str_val, server_cfg->server_name_len+1);
                }

                /* server description (optional) */
                if ( config_setting_lookup_string(root, CFG_SERVER_DESCR, &str_val) ) {
                        size_t len = strlen(str_val)-1;
                        server_cfg->server_descr_len = MAX_SERVER_DESCR_LEN > len ? len: MAX_SERVER_DESCR_LEN;
                        strncpy(server_cfg->server_descr, str_val, server_cfg->server_descr_len+1);
                }

                /* allow lowid */
                if ( config_setting_lookup_int(root, CFG_ALLOW_LOWID, &int_val) ) {
                        server_cfg->allow_lowid = (int_val != 0);
                } else {
                        ED2KD_LOGERR("config: " CFG_ALLOW_LOWID " missing");
                        ret = 0;
                }

                /* port check timeout */
                if ( config_setting_lookup_int(root, CFG_PORTCHECK_TIMEOUT, &int_val) ) {
                        server_cfg->portcheck_timeout_tv.tv_sec = int_val / 1000;
                        server_cfg->portcheck_timeout_tv.tv_usec = (int_val % 1000) * 1000;
                }  else {
                        ED2KD_LOGERR("config: " CFG_PORTCHECK_TIMEOUT " missing");
                        ret = 0;
                }

                /* status notify interval */
                if ( config_setting_lookup_int(root, CFG_STATUS_NOTIFY_INTERVAL, &int_val) ) {
                        server_cfg->status_notify_tv.tv_sec = int_val / 1000;
                        server_cfg->status_notify_tv.tv_usec = (int_val % 1000) * 1000;
                }  else {
                        ED2KD_LOGERR("config: " CFG_STATUS_NOTIFY_INTERVAL " missing");
                        ret = 0;
                }

                /* max clients */
                if ( config_setting_lookup_int(root, CFG_MAX_CLIENTS, &int_val) ) {
                        server_cfg->max_clients = int_val;
                } else {
                        ED2KD_LOGERR("config: " CFG_MAX_CLIENTS " missing");
                        ret = 0;
                }

                /* max files */
                if ( config_setting_lookup_int(root, CFG_MAX_FILES, &int_val) ) {
                        server_cfg->max_files = int_val;
                } else {
                        ED2KD_LOGERR("config: " CFG_MAX_FILES " missing");
                        ret = 0;
                }

                /* max files per client */
                if ( config_setting_lookup_int(root, CFG_MAX_FILES_PER_CLIENT, &int_val) ) {
                        server_cfg->max_files_per_client = int_val;
                } else {
                        ED2KD_LOGERR("config: " CFG_MAX_FILES_PER_CLIENT " missing");
                        ret = 0;
                }

                /* max offers limit */
                if ( config_setting_lookup_int(root, CFG_MAX_OFFERS_LIMIT, &int_val) ) {
                        server_cfg->max_offers_limit = int_val;
                } else {
                        ED2KD_LOGERR("config: " CFG_MAX_OFFERS_LIMIT " missing");
                        ret = 0;
                }

                /* max searches limit */
                if ( config_setting_lookup_int(root, CFG_MAX_SEARCHES_LIMIT, &int_val) ) {
                        server_cfg->max_searches_limit = int_val;
                } else {
                        ED2KD_LOGERR("config: " CFG_MAX_SEARCHES_LIMIT " missing");
                        ret = 0;
                }


        } else {
                ED2KD_LOGWRN("config: failed to parse %s(error:%s at %d line)", path,
                        config_error_text(&config), config_error_line(&config));
                ret = 0;
        }

        config_destroy(&config);

        if ( ret < 0 ) {
                free(server_cfg);
        } else {
                server_cfg->srv_tcp_flags = SRV_TCPFLG_COMPRESSION | SRV_TCPFLG_TYPETAGINTEGER | SRV_TCPFLG_LARGEFILES;
                evutil_inet_pton(AF_INET, server_cfg->listen_addr, &server_cfg->listen_addr_inaddr);
                g_srv.cfg = server_cfg;
        }

        return ret;
}

void server_free_config()
{
        struct server_config *cfg = (struct server_config *)g_srv.cfg;
        g_srv.cfg = NULL;
        free(cfg->listen_addr);
        free(cfg);
}
