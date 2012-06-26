#include <stdint.h>
#include <stdio.h>
#include <getopt.h>
#ifdef WIN32
#include <winsock2.h>
#endif
#include "ed2kd.h"
#include "config.h"
#include "ed2k_proto.h"
#include "version.h"
#include "util.h"
#include "log.h"

// command line options
static const char *optString = "vhg";
static const struct option longOpts[] = {
    { "version", no_argument, NULL, 'v'},
    { "help", no_argument, NULL, 'h' },
    { "gen-hash", no_argument, NULL, 'g' },
    { NULL, no_argument, NULL, 0 }
};

void display_version( void )
{
    puts("ed2kd v" ED2KD_VER_STR);
    puts("Build on: "__DATE__ " " __TIME__);
}

void display_usage( void )
{
    puts("Options:");
    puts("--help, -h\tshow this help");
    puts("--version, -v\tprint version");
    puts("--gen-hash, -G\tgenerate random user hash");
}

int main( int argc, char *argv[] )
{
	int ret, opt, longIndex = 0;
#ifdef WIN32
	WSADATA WSAData;
#endif

    ed2kd_init();

    // parse command line arguments
    opt = getopt_long( argc, argv, optString, longOpts, &longIndex );
    while( opt != -1 ) {
        switch( opt ) {
            case 'v':
                display_version();
                return EXIT_SUCCESS;

            case 'g': {
                unsigned char hash[HASH_SIZE];
                char hex_hash[sizeof(hash)*2+1];
                rnd_user_hash(hash);
                bin2hex(hash, hex_hash, sizeof(hex_hash));
                puts(hex_hash);
                return EXIT_SUCCESS;
            }

            case 'h':
                display_usage();
                return EXIT_SUCCESS;

            default:
                return EXIT_FAILURE;
        }
        opt = getopt_long( argc, argv, optString, longOpts, &longIndex );
    }

    // load config
    if ( ed2kd_config_load(0) < 0 ) {
        ED2KD_LOGWRN("failed to load configuration file");
        return EXIT_FAILURE;
    }

#ifdef WIN32
    WSAStartup(0x0201, &WSAData);
#endif

    ret = ed2kd_run();

    ed2kd_config_free();

    return ret;
}
