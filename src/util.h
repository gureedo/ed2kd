#pragma once

#ifndef UTIL_H
#define UTIL_H

/**
  @file util.h utilities
*/

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef USE_DEBUG
#define DEBUG_ONLY(x) x
#else
#define DEBUG_ONLY(x)
#endif

#if defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__)
#define THREAD_LOCAL __thread
#else
#error "unknown compiler"
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/**
  @brief convert "DEADBEEF" -> {0xDE,0xAD,0xBE,0xEF}
  @param src       source string
  @param dst       destination buffer
  @param dst_len   dst buffer length
  @return 0 on success, -1 on failure
*/
int hex2bin( const char *src, unsigned char *dst, size_t dst_len );

/**
  @brief convert {0xDE,0xAD,0xBE,0xEF} -> "DEADBEEF\0"
  @param src       source binary buffer
  @param dst       destination string
  @param dst_len   destination string length, including null character
  @return 0 on success, -1 on failure
*/
int bin2hex( const unsigned char *src, char *dst, size_t dst_len );

/**
  @brief generate random ed2k user hash
  @param hash  destination buffer, at least HASH_SIZE bytes
*/
void get_random_user_hash( unsigned char *hash );

/**
  @brief get integer ed2k file type from string file type
  @param type   string type
  @param len    length of type without null byte
*/
uint8_t get_ed2k_file_type( const char *type, size_t len );

/**
  @brief search file extension
  @param name   file name
  @param len    file name length
  @return pointer where file extension begins or NULL
*/
const char *file_extension( const char *name, size_t len );

struct token_bucket {
        double tokens;
        time_t last_update;
};

/**
  @brief token bucket initialization
  @param bucket pinter to target bucket
  @param max_tokens initial number of tokens in bucket (tokens per second)
*/
__inline void token_bucket_init( struct token_bucket *bucket, double tokens )
{
        bucket->last_update = time(NULL);
        bucket->tokens = tokens;
}

/**
  @brief try to get one token from bucket
  @param bucket pinter to target bucket
  @param max_tokens maximum number of tokens per second
  @return non-zero on success
*/
int token_bucket_update ( struct token_bucket *bucket, double max_tokens );


#endif // UTIL_H
