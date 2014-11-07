#ifndef ATOMIC_H
#define ATOMIC_H

typedef unsigned long atomic32_t;

/**
  @brief
  @param ptr pointer to atomic variable
  @return initial value
*/
static __inline atomic32_t atomic_store( volatile atomic32_t *ptr, atomic32_t val )
{
        __sync_synchronize();
        return __sync_lock_test_and_set(ptr, val);
}

/**
  @return initial value
*/
static __inline atomic32_t atomic_add( volatile atomic32_t *ptr, atomic32_t val )
{
        return __sync_fetch_and_add(ptr, val);
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
        return (__sync_val_compare_and_swap(ptr, old_val, new_val) == old_val) ? 1 : 0;
}

#endif // ATOMIC_H
