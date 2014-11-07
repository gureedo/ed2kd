#ifndef ATOMIC_H
#define ATOMIC_H

#include <stdatomic.h>
#include <stdint.h>

typedef _Atomic uint16_tatomic_uint16_t;
typedef _Atomic uint32_t atomic_uint32_t;
typedef _Atomic uint64_t atomic_uint64_t;

#endif // ATOMIC_H
