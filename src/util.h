#ifndef UTIL_H
#define UTIL_H

/**
  @file util.h Some utilities collection
*/

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#ifdef DEBUG
#define DEBUG_ONLY(x) x
#else
#define DEBUG_ONLY(x)
#endif

/**
  @brief convert "DEADBEEF" -> {0xDE,0xAD,0xBE,0xEF"}
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
  @param hash  destination buffer
*/
void rnd_user_hash( unsigned char *hash );

#endif // UTIL_H
