#ifndef PACKED_STRUCT_H
#define PACKED_STRUCT_H

#ifdef _MSC_VER
#define PACKED_STRUCT( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop) )
#else
#define PACKED_STRUCT( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#endif // PACKED_STRUCT_H