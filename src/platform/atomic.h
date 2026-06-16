#ifndef ATOMIC_H
#define ATOMIC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    #include <windows.h>
    typedef volatile LONG atomic_int_t;
    typedef volatile LONG atomic_bool_t;

    #define atomic_init(ptr, val) (*(ptr) = (val))
    #define atomic_load(ptr) InterlockedCompareExchange(ptr, 0, 0)
    #define atomic_store(ptr, val) InterlockedExchange(ptr, val)
    #define atomic_fetch_add(ptr, val) InterlockedExchangeAdd(ptr, val)
    #define atomic_fetch_sub(ptr, val) InterlockedExchangeAdd(ptr, -(val))
    #define atomic_compare_exchange(ptr, expected, desired) \
        (InterlockedCompareExchange(ptr, desired, *(expected)) == *(expected))
#else
    #include <stdatomic.h>
    typedef atomic_int atomic_int_t;
    typedef atomic_bool atomic_bool_t;
#endif

#endif /* ATOMIC_H */
