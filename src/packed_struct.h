#ifndef PACKED_STRUCT_H
#define PACKED_STRUCT_H

#if defined(_MSC_VER)
#define PACKED_STRUCT( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop) )
#elif defined(__GNUC__)
#define PACKED_STRUCT( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#else
#error "Don't know how to declare packed structures"
#endif

#endif // PACKED_STRUCT_H