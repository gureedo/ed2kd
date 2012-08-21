
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <event2/util.h>
#include "ed2k_proto.h"

int hex2bin( const char *src, unsigned char *dst, size_t dst_len )
{
        size_t i;
        for ( i=0; i<dst_len; ++i ) {
                dst[i] = 0;
                if ( src[i] >= '0' && src[i] <= '9')
                        dst[i] = 16*(src[i]-'0');
                else if ( src[i] >= 'a' && src[i] <= 'f' )
                        dst[i] = 16*(src[i] - 'a' + 10);
                else if ( src[i] >= 'A' && src[i] <= 'F' )
                        dst[i] = 16*(src[i] - 'A' + 10);
                else
                        return -1;

                if ( src[i+1] >= '0' && src[i+1] <= '9')
                        dst[i] += src[i+1]-'0';
                else if ( src[i+1] >= 'a' && src[i+1] <= 'f' )
                        dst[i] += src[i+1] - 'a' + 10;
                else if ( src[i+i] >= 'A' && src[i+1] <= 'F' )
                        dst[i] += src[i+1] - 'A' + 10;
                else
                        return -1;
        }

        return 0;
}

int bin2hex( const unsigned char *src, char *dst, size_t dst_len )
{
        size_t i,j;
        static unsigned char hex[] = "0123456789abcdef";

        if ( dst_len % 2 == 0 )
                return -1;

        for ( i=0,j=0; i < dst_len-1; i+=2,j++ ) {
                dst[i]   = hex[(src[j] >> 4) & 0xf];
                dst[i+1] = hex[src[j] & 0xf];
        }
        dst[dst_len-1] = '\0';

        return 0;
}

void get_random_user_hash( unsigned char *hash )
{
        evutil_secure_rng_get_bytes((char*)hash, ED2K_HASH_SIZE);
        hash[6] = 14;
        hash[15] = 111;
}

uint8_t get_ed2k_file_type( const char *type, size_t len )
{
        if ( strncmp(FTS_AUDIO, type, len) == 0 ) {
                return FT_AUDIO;
        } else if ( strncmp(FTS_VIDEO, type, len) == 0 ) {
                return FT_VIDEO;
        } else if ( strncmp(FTS_IMAGE, type, len) == 0 ) {
                return FT_IMAGE;
        } else if ( strncmp(FTS_DOCUMENT, type, len) == 0 ) {
                return FT_DOCUMENT;
        } else if ( strncmp(FTS_PROGRAM, type, len) == 0 ) {
                return FT_PROGRAM;
        } else if ( strncmp(FTS_ARCHIVE, type, len) == 0 ) {
                return FT_ARCHIVE;
        } else if ( strncmp(FTS_CDIMAGE, type, len) == 0 ) {
                return FT_CDIMAGE;
        } else if ( strncmp(FTS_EMULECOLLECTION, type, len) == 0 ) {
                return FT_EMULECOLLECTION;
        } else {
                return FT_ANY;
        }
}
