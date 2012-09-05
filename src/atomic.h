#pragma once

#ifndef ATOMIC_H
#define ATOMIC_H

#ifdef _WIN32
#include <windows.h>
#elif !defined(__GNUC__) 
#error "dont know how to implement atomic operations"
#endif

typedef unsigned long atomic32_t;

/**
  @brief 
  @param ptr pointer to atomic variable
  @return initial value
*/
static __inline atomic32_t atomic_store( volatile atomic32_t *ptr, atomic32_t val )
{
#ifdef _WIN32
        return InterlockedExchange(ptr, val);
#else
#endif
}

/**
  @return initial value
*/
static __inline atomic32_t atomic_add( volatile atomic32_t *ptr, atomic32_t val )
{
#ifdef _WIN32
        return InterlockedExchangeAdd(ptr, val);
#else
        return __sync_fetch_and_add(ptr, val);
#endif
}

/**
  @return variable value
*/
#define atomic_load(ptr) atomic_add((ptr), 0)

/**
  @return initial value
*/
#define atomic_sub(ptr,val) atomic_add((ptr), -(val))

/**
  @return initial value
*/
#define atomic_inc(ptr) atomic_add((ptr), 1)

/**
  @return initial value
*/
#define atomic_dec(ptr) atomic_add((ptr), -1)

/**
  @brief perform atomic compare and swap operation
  @return non-zero value on success
*/
static __inline int atomic_cas( volatile atomic32_t *ptr, atomic32_t new_val, atomic32_t old_val )
{
#ifdef _WIN32
        return (InterlockedCompareExchange(ptr, new_val, old_val) == old_val) ? 1 : 0;
#else
        return (__sync_val_compare_and_swap(ptr, old_val, new_val) == old_val) ? 1 : 0;
#endif
}

#endif // ATOMIC_H