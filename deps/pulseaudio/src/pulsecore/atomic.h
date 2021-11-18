#ifndef foopulseatomichfoo
#define foopulseatomichfoo

/***
  This file is part of PulseAudio.

  Copyright 2006-2008 Lennart Poettering
  Copyright 2008 Nokia Corporation

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#include <pulsecore/macro.h>

/*
 * atomic_ops guarantees us that sizeof(AO_t) == sizeof(void*).  It is
 * not guaranteed however, that sizeof(AO_t) == sizeof(size_t).
 * however very likely.
 *
 * For now we do only full memory barriers. Eventually we might want
 * to support more elaborate memory barriers, in which case we will add
 * suffixes to the function names.
 *
 * On gcc >= 4.1 we use the builtin atomic functions. otherwise we use
 * libatomic_ops
 */

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

#ifdef HAVE_ATOMIC_BUILTINS

/* __sync based implementation */

typedef struct pa_atomic {
    volatile int value;
} pa_atomic_t;

#define PA_ATOMIC_INIT(v) { .value = (v) }

#ifdef HAVE_ATOMIC_BUILTINS_MEMORY_MODEL

/* __atomic based implementation */

static inline int pa_atomic_load(const pa_atomic_t *a) {
    return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST);
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    __atomic_store_n(&a->value, i, __ATOMIC_SEQ_CST);
}

#else

static inline int pa_atomic_load(const pa_atomic_t *a) {
    __sync_synchronize();
    return a->value;
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    a->value = i;
    __sync_synchronize();
}

#endif


/* Returns the previously set value */
static inline int pa_atomic_add(pa_atomic_t *a, int i) {
    return __sync_fetch_and_add(&a->value, i);
}

/* Returns the previously set value */
static inline int pa_atomic_sub(pa_atomic_t *a, int i) {
    return __sync_fetch_and_sub(&a->value, i);
}

/* Returns the previously set value */
static inline int pa_atomic_inc(pa_atomic_t *a) {
    return pa_atomic_add(a, 1);
}

/* Returns the previously set value */
static inline int pa_atomic_dec(pa_atomic_t *a) {
    return pa_atomic_sub(a, 1);
}

/* Returns true when the operation was successful. */
static inline bool pa_atomic_cmpxchg(pa_atomic_t *a, int old_i, int new_i) {
    return __sync_bool_compare_and_swap(&a->value, old_i, new_i);
}

typedef struct pa_atomic_ptr {
    volatile unsigned long value;
} pa_atomic_ptr_t;

#define PA_ATOMIC_PTR_INIT(v) { .value = (long) (v) }

#ifdef HAVE_ATOMIC_BUILTINS_MEMORY_MODEL

/* __atomic based implementation */

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
    return (void*) __atomic_load_n(&a->value, __ATOMIC_SEQ_CST);
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void* p) {
    __atomic_store_n(&a->value, (unsigned long) p, __ATOMIC_SEQ_CST);
}

#else

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
    __sync_synchronize();
    return (void*) a->value;
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void *p) {
    a->value = (unsigned long) p;
    __sync_synchronize();
}

#endif

static inline bool pa_atomic_ptr_cmpxchg(pa_atomic_ptr_t *a, void *old_p, void* new_p) {
    return __sync_bool_compare_and_swap(&a->value, (long) old_p, (long) new_p);
}

#elif defined(__NetBSD__) && defined(HAVE_SYS_ATOMIC_H)

/* NetBSD 5.0+ atomic_ops(3) implementation */

#include <sys/atomic.h>

typedef struct pa_atomic {
    volatile unsigned int value;
} pa_atomic_t;

#define PA_ATOMIC_INIT(v) { .value = (unsigned int) (v) }

static inline int pa_atomic_load(const pa_atomic_t *a) {
    membar_sync();
    return (int) a->value;
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    a->value = (unsigned int) i;
    membar_sync();
}

/* Returns the previously set value */
static inline int pa_atomic_add(pa_atomic_t *a, int i) {
    int nv = (int) atomic_add_int_nv(&a->value, i);
    return nv - i;
}

/* Returns the previously set value */
static inline int pa_atomic_sub(pa_atomic_t *a, int i) {
    int nv = (int) atomic_add_int_nv(&a->value, -i);
    return nv + i;
}

/* Returns the previously set value */
static inline int pa_atomic_inc(pa_atomic_t *a) {
    int nv = (int) atomic_inc_uint_nv(&a->value);
    return nv - 1;
}

/* Returns the previously set value */
static inline int pa_atomic_dec(pa_atomic_t *a) {
    int nv = (int) atomic_dec_uint_nv(&a->value);
    return nv + 1;
}

/* Returns true when the operation was successful. */
static inline bool pa_atomic_cmpxchg(pa_atomic_t *a, int old_i, int new_i) {
    unsigned int r = atomic_cas_uint(&a->value, (unsigned int) old_i, (unsigned int) new_i);
    return (int) r == old_i;
}

typedef struct pa_atomic_ptr {
    volatile void *value;
} pa_atomic_ptr_t;

#define PA_ATOMIC_PTR_INIT(v) { .value = (v) }

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
    membar_sync();
    return (void *) a->value;
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void *p) {
    a->value = p;
    membar_sync();
}

static inline bool pa_atomic_ptr_cmpxchg(pa_atomic_ptr_t *a, void *old_p, void* new_p) {
    void *r = atomic_cas_ptr(&a->value, old_p, new_p);
    return r == old_p;
}

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/param.h>
#include <machine/atomic.h>

#if __FreeBSD_version < 600000
#if defined(__i386__) || defined(__amd64__)
#if defined(__amd64__)
#define atomic_load_acq_64      atomic_load_acq_long
#endif
static inline u_int atomic_fetchadd_int(volatile u_int *p, u_int v) {
    __asm __volatile(
            "   " __XSTRING(MPLOCKED) "         "
            "   xaddl   %0, %1 ;        "
            "# atomic_fetchadd_int"
            : "+r" (v),
            "=m" (*p)
            : "m" (*p));

    return (v);
}
#elif defined(__sparc__) && defined(__arch64__)
#define atomic_load_acq_64      atomic_load_acq_long
#define atomic_fetchadd_int     atomic_add_int
#elif defined(__ia64__)
#define atomic_load_acq_64      atomic_load_acq_long
static inline uint32_t
atomic_fetchadd_int(volatile uint32_t *p, uint32_t v) {
    uint32_t value;

    do {
        value = *p;
    } while (!atomic_cmpset_32(p, value, value + v));
    return (value);
}
#endif
#endif

typedef struct pa_atomic {
    volatile unsigned long value;
} pa_atomic_t;

#define PA_ATOMIC_INIT(v) { .value = (v) }

static inline int pa_atomic_load(const pa_atomic_t *a) {
    return (int) atomic_load_acq_int((unsigned int *) &a->value);
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    atomic_store_rel_int((unsigned int *) &a->value, i);
}

static inline int pa_atomic_add(pa_atomic_t *a, int i) {
    return atomic_fetchadd_int((unsigned int *) &a->value, i);
}

static inline int pa_atomic_sub(pa_atomic_t *a, int i) {
    return atomic_fetchadd_int((unsigned int *) &a->value, -(i));
}

static inline int pa_atomic_inc(pa_atomic_t *a) {
    return atomic_fetchadd_int((unsigned int *) &a->value, 1);
}

static inline int pa_atomic_dec(pa_atomic_t *a) {
    return atomic_fetchadd_int((unsigned int *) &a->value, -1);
}

static inline int pa_atomic_cmpxchg(pa_atomic_t *a, int old_i, int new_i) {
    return atomic_cmpset_int((unsigned int *) &a->value, old_i, new_i);
}

typedef struct pa_atomic_ptr {
    volatile unsigned long value;
} pa_atomic_ptr_t;

#define PA_ATOMIC_PTR_INIT(v) { .value = (unsigned long) (v) }

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
#ifdef atomic_load_acq_64
    return (void*) atomic_load_acq_ptr((unsigned long *) &a->value);
#else
    return (void*) atomic_load_acq_ptr((unsigned int *) &a->value);
#endif
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void *p) {
#ifdef atomic_load_acq_64
    atomic_store_rel_ptr(&a->value, (unsigned long) p);
#else
    atomic_store_rel_ptr((unsigned int *) &a->value, (unsigned int) p);
#endif
}

static inline int pa_atomic_ptr_cmpxchg(pa_atomic_ptr_t *a, void *old_p, void* new_p) {
#ifdef atomic_load_acq_64
    return atomic_cmpset_ptr(&a->value, (unsigned long) old_p, (unsigned long) new_p);
#else
    return atomic_cmpset_ptr((unsigned int *) &a->value, (unsigned int) old_p, (unsigned int) new_p);
#endif
}

#elif defined(__GNUC__) && (defined(__amd64__) || defined(__x86_64__))

#warn "The native atomic operations implementation for AMD64 has not been tested thoroughly. libatomic_ops is known to not work properly on AMD64 and your gcc version is too old for the gcc-builtin atomic ops support. You have three options now: test the native atomic operations implementation for AMD64, fix libatomic_ops, or upgrade your GCC."

/* Adapted from glibc */

typedef struct pa_atomic {
    volatile int value;
} pa_atomic_t;

#define PA_ATOMIC_INIT(v) { .value = (v) }

static inline int pa_atomic_load(const pa_atomic_t *a) {
    return a->value;
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    a->value = i;
}

static inline int pa_atomic_add(pa_atomic_t *a, int i) {
    int result;

    __asm __volatile ("lock; xaddl %0, %1"
                      : "=r" (result), "=m" (a->value)
                      : "0" (i), "m" (a->value));

    return result;
}

static inline int pa_atomic_sub(pa_atomic_t *a, int i) {
    return pa_atomic_add(a, -i);
}

static inline int pa_atomic_inc(pa_atomic_t *a) {
    return pa_atomic_add(a, 1);
}

static inline int pa_atomic_dec(pa_atomic_t *a) {
    return pa_atomic_sub(a, 1);
}

static inline bool pa_atomic_cmpxchg(pa_atomic_t *a, int old_i, int new_i) {
    int result;

    __asm__ __volatile__ ("lock; cmpxchgl %2, %1"
                          : "=a" (result), "=m" (a->value)
                          : "r" (new_i), "m" (a->value), "0" (old_i));

    return result == old_i;
}

typedef struct pa_atomic_ptr {
    volatile unsigned long value;
} pa_atomic_ptr_t;

#define PA_ATOMIC_PTR_INIT(v) { .value = (long) (v) }

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
    return (void*) a->value;
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void *p) {
    a->value = (unsigned long) p;
}

static inline bool pa_atomic_ptr_cmpxchg(pa_atomic_ptr_t *a, void *old_p, void* new_p) {
    void *result;

    __asm__ __volatile__ ("lock; cmpxchgq %q2, %1"
                          : "=a" (result), "=m" (a->value)
                          : "r" (new_p), "m" (a->value), "0" (old_p));

    return result == old_p;
}

#elif defined(ATOMIC_ARM_INLINE_ASM)

/*
   These should only be enabled if we have ARMv6 or better.
*/

typedef struct pa_atomic {
    volatile int value;
} pa_atomic_t;

#define PA_ATOMIC_INIT(v) { .value = (v) }

static inline void pa_memory_barrier(void) {
#ifdef ATOMIC_ARM_MEMORY_BARRIER_ENABLED
    asm volatile ("mcr  p15, 0, r0, c7, c10, 5  @ dmb");
#endif
}

static inline int pa_atomic_load(const pa_atomic_t *a) {
    pa_memory_barrier();
    return a->value;
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    a->value = i;
    pa_memory_barrier();
}

/* Returns the previously set value */
static inline int pa_atomic_add(pa_atomic_t *a, int i) {
    unsigned long not_exclusive;
    int new_val, old_val;

    pa_memory_barrier();
    do {
        asm volatile ("ldrex    %0, [%3]\n"
                      "add      %2, %0, %4\n"
                      "strex    %1, %2, [%3]\n"
                      : "=&r" (old_val), "=&r" (not_exclusive), "=&r" (new_val)
                      : "r" (&a->value), "Ir" (i)
                      : "cc");
    } while(not_exclusive);
    pa_memory_barrier();

    return old_val;
}

/* Returns the previously set value */
static inline int pa_atomic_sub(pa_atomic_t *a, int i) {
    unsigned long not_exclusive;
    int new_val, old_val;

    pa_memory_barrier();
    do {
        asm volatile ("ldrex    %0, [%3]\n"
                      "sub      %2, %0, %4\n"
                      "strex    %1, %2, [%3]\n"
                      : "=&r" (old_val), "=&r" (not_exclusive), "=&r" (new_val)
                      : "r" (&a->value), "Ir" (i)
                      : "cc");
    } while(not_exclusive);
    pa_memory_barrier();

    return old_val;
}

static inline int pa_atomic_inc(pa_atomic_t *a) {
    return pa_atomic_add(a, 1);
}

static inline int pa_atomic_dec(pa_atomic_t *a) {
    return pa_atomic_sub(a, 1);
}

static inline bool pa_atomic_cmpxchg(pa_atomic_t *a, int old_i, int new_i) {
    unsigned long not_equal, not_exclusive;

    pa_memory_barrier();
    do {
        asm volatile ("ldrex    %0, [%2]\n"
                      "subs     %0, %0, %3\n"
                      "mov      %1, %0\n"
                      "strexeq %0, %4, [%2]\n"
                      : "=&r" (not_exclusive), "=&r" (not_equal)
                      : "r" (&a->value), "Ir" (old_i), "r" (new_i)
                      : "cc");
    } while(not_exclusive && !not_equal);
    pa_memory_barrier();

    return !not_equal;
}

typedef struct pa_atomic_ptr {
    volatile unsigned long value;
} pa_atomic_ptr_t;

#define PA_ATOMIC_PTR_INIT(v) { .value = (long) (v) }

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
    pa_memory_barrier();
    return (void*) a->value;
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void *p) {
    a->value = (unsigned long) p;
    pa_memory_barrier();
}

static inline bool pa_atomic_ptr_cmpxchg(pa_atomic_ptr_t *a, void *old_p, void* new_p) {
    unsigned long not_equal, not_exclusive;

    pa_memory_barrier();
    do {
        asm volatile ("ldrex    %0, [%2]\n"
                      "subs     %0, %0, %3\n"
                      "mov      %1, %0\n"
                      "strexeq %0, %4, [%2]\n"
                      : "=&r" (not_exclusive), "=&r" (not_equal)
                      : "r" (&a->value), "Ir" (old_p), "r" (new_p)
                      : "cc");
    } while(not_exclusive && !not_equal);
    pa_memory_barrier();

    return !not_equal;
}

#elif defined(ATOMIC_ARM_LINUX_HELPERS)

/* See file arch/arm/kernel/entry-armv.S in your kernel sources for more
   information about these functions. The arm kernel helper functions first
   appeared in 2.6.16.
   Apply --disable-atomic-arm-linux-helpers flag to configure if you prefer
   inline asm implementation or you have an obsolete Linux kernel.
*/
/* Memory barrier */
typedef void (__kernel_dmb_t)(void);
#define __kernel_dmb (*(__kernel_dmb_t *)0xffff0fa0)

static inline void pa_memory_barrier(void) {
#ifndef ATOMIC_ARM_MEMORY_BARRIER_ENABLED
    __kernel_dmb();
#endif
}

/* Atomic exchange (__kernel_cmpxchg_t contains memory barriers if needed) */
typedef int (__kernel_cmpxchg_t)(int oldval, int newval, volatile int *ptr);
#define __kernel_cmpxchg (*(__kernel_cmpxchg_t *)0xffff0fc0)

/* This is just to get rid of all warnings */
typedef int (__kernel_cmpxchg_u_t)(unsigned long oldval, unsigned long newval, volatile unsigned long *ptr);
#define __kernel_cmpxchg_u (*(__kernel_cmpxchg_u_t *)0xffff0fc0)

typedef struct pa_atomic {
    volatile int value;
} pa_atomic_t;

#define PA_ATOMIC_INIT(v) { .value = (v) }

static inline int pa_atomic_load(const pa_atomic_t *a) {
    pa_memory_barrier();
    return a->value;
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    a->value = i;
    pa_memory_barrier();
}

/* Returns the previously set value */
static inline int pa_atomic_add(pa_atomic_t *a, int i) {
    int old_val;
    do {
        old_val = a->value;
    } while(__kernel_cmpxchg(old_val, old_val + i, &a->value));
    return old_val;
}

/* Returns the previously set value */
static inline int pa_atomic_sub(pa_atomic_t *a, int i) {
    int old_val;
    do {
        old_val = a->value;
    } while(__kernel_cmpxchg(old_val, old_val - i, &a->value));
    return old_val;
}

/* Returns the previously set value */
static inline int pa_atomic_inc(pa_atomic_t *a) {
    return pa_atomic_add(a, 1);
}

/* Returns the previously set value */
static inline int pa_atomic_dec(pa_atomic_t *a) {
    return pa_atomic_sub(a, 1);
}

/* Returns true when the operation was successful. */
static inline bool pa_atomic_cmpxchg(pa_atomic_t *a, int old_i, int new_i) {
    bool failed;
    do {
      failed = !!__kernel_cmpxchg(old_i, new_i, &a->value);
    } while(failed && a->value == old_i);
    return !failed;
}

typedef struct pa_atomic_ptr {
    volatile unsigned long value;
} pa_atomic_ptr_t;

#define PA_ATOMIC_PTR_INIT(v) { .value = (unsigned long) (v) }

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
    pa_memory_barrier();
    return (void*) a->value;
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void *p) {
    a->value = (unsigned long) p;
    pa_memory_barrier();
}

static inline bool pa_atomic_ptr_cmpxchg(pa_atomic_ptr_t *a, void *old_p, void* new_p) {
    bool failed;
    do {
        failed = !!__kernel_cmpxchg_u((unsigned long) old_p, (unsigned long) new_p, &a->value);
    } while(failed && a->value == (unsigned long) old_p);
    return !failed;
}

#else

/* libatomic_ops based implementation */

#include <atomic_ops.h>

typedef struct pa_atomic {
    volatile AO_t value;
} pa_atomic_t;

#define PA_ATOMIC_INIT(v) { .value = (AO_t) (v) }

static inline int pa_atomic_load(const pa_atomic_t *a) {
    return (int) AO_load_full((AO_t*) &a->value);
}

static inline void pa_atomic_store(pa_atomic_t *a, int i) {
    AO_store_full(&a->value, (AO_t) i);
}

static inline int pa_atomic_add(pa_atomic_t *a, int i) {
    return (int) AO_fetch_and_add_full(&a->value, (AO_t) i);
}

static inline int pa_atomic_sub(pa_atomic_t *a, int i) {
    return (int) AO_fetch_and_add_full(&a->value, (AO_t) -i);
}

static inline int pa_atomic_inc(pa_atomic_t *a) {
    return (int) AO_fetch_and_add1_full(&a->value);
}

static inline int pa_atomic_dec(pa_atomic_t *a) {
    return (int) AO_fetch_and_sub1_full(&a->value);
}

static inline bool pa_atomic_cmpxchg(pa_atomic_t *a, int old_i, int new_i) {
    return AO_compare_and_swap_full(&a->value, (unsigned long) old_i, (unsigned long) new_i);
}

typedef struct pa_atomic_ptr {
    volatile AO_t value;
} pa_atomic_ptr_t;

#define PA_ATOMIC_PTR_INIT(v) { .value = (AO_t) (v) }

static inline void* pa_atomic_ptr_load(const pa_atomic_ptr_t *a) {
    return (void*) AO_load_full((AO_t*) &a->value);
}

static inline void pa_atomic_ptr_store(pa_atomic_ptr_t *a, void *p) {
    AO_store_full(&a->value, (AO_t) p);
}

static inline bool pa_atomic_ptr_cmpxchg(pa_atomic_ptr_t *a, void *old_p, void* new_p) {
    return AO_compare_and_swap_full(&a->value, (AO_t) old_p, (AO_t) new_p);
}

#endif

#endif
