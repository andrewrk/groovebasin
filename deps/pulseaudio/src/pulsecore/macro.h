#ifndef foopulsemacrohfoo
#define foopulsemacrohfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

/* Rounds down */
static inline void* PA_ALIGN_PTR(const void *p) {
    return (void*) (((size_t) p) & ~(sizeof(void*) - 1));
}

/* Rounds up */
static inline size_t PA_ALIGN(size_t l) {
    return ((l + sizeof(void*) - 1) & ~(sizeof(void*) - 1));
}

#if defined(__GNUC__)
    #define PA_UNUSED __attribute__ ((unused))
#else
    #define PA_UNUSED
#endif

#define PA_ELEMENTSOF(x) (sizeof(x)/sizeof((x)[0]))

#if defined(__GNUC__)
    #define PA_DECLARE_ALIGNED(n,t,v)      t v __attribute__ ((aligned (n)))
#else
    #define PA_DECLARE_ALIGNED(n,t,v)      t v
#endif

#ifdef __GNUC__
#define typeof __typeof__
#endif

/* The users of PA_MIN and PA_MAX, PA_CLAMP, PA_ROUND_UP should be
 * aware that these macros on non-GCC executed code with side effects
 * twice. It is thus considered misuse to use code with side effects
 * as arguments to MIN and MAX. */

#ifdef __GNUC__
#define PA_MAX(a,b)                             \
    __extension__ ({                            \
            typeof(a) _a = (a);                 \
            typeof(b) _b = (b);                 \
            _a > _b ? _a : _b;                  \
        })
#else
#define PA_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifdef __GNUC__
#define PA_MIN(a,b)                             \
    __extension__ ({                            \
            typeof(a) _a = (a);                 \
            typeof(b) _b = (b);                 \
            _a < _b ? _a : _b;                  \
        })
#else
#define PA_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifdef __GNUC__
#define PA_ROUND_UP(a, b)                       \
    __extension__ ({                            \
            typeof(a) _a = (a);                 \
            typeof(b) _b = (b);                 \
            ((_a + _b - 1) / _b) * _b;          \
        })
#else
#define PA_ROUND_UP(a, b) ((((a) + (b) - 1) / (b)) * (b))
#endif

#ifdef __GNUC__
#define PA_ROUND_DOWN(a, b)                     \
    __extension__ ({                            \
            typeof(a) _a = (a);                 \
            typeof(b) _b = (b);                 \
            (_a / _b) * _b;                     \
        })
#else
#define PA_ROUND_DOWN(a, b) (((a) / (b)) * (b))
#endif

#ifdef __GNUC__
#define PA_CLIP_SUB(a, b)                       \
    __extension__ ({                            \
            typeof(a) _a = (a);                 \
            typeof(b) _b = (b);                 \
            _a > _b ? _a - _b : 0;              \
        })
#else
#define PA_CLIP_SUB(a, b) ((a) > (b) ? (a) - (b) : 0)
#endif

#ifdef __GNUC__
#define PA_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
#define PA_PRETTY_FUNCTION ""
#endif

#define pa_return_if_fail(expr)                                         \
    do {                                                                \
        if (PA_UNLIKELY(!(expr))) {                                     \
            pa_log_debug("Assertion '%s' failed at %s:%u, function %s.", #expr , __FILE__, __LINE__, PA_PRETTY_FUNCTION); \
            return;                                                     \
        }                                                               \
    } while(false)

#define pa_return_val_if_fail(expr, val)                                \
    do {                                                                \
        if (PA_UNLIKELY(!(expr))) {                                     \
            pa_log_debug("Assertion '%s' failed at %s:%u, function %s.", #expr , __FILE__, __LINE__, PA_PRETTY_FUNCTION); \
            return (val);                                               \
        }                                                               \
    } while(false)

#define pa_return_null_if_fail(expr) pa_return_val_if_fail(expr, NULL)

/* pa_assert_se() is an assert which guarantees side effects of x,
 * i.e. is never optimized away, regardless of NDEBUG or FASTPATH. */
#ifndef __COVERITY__
#define pa_assert_se(expr)                                              \
    do {                                                                \
        if (PA_UNLIKELY(!(expr))) {                                     \
            pa_log_error("Assertion '%s' failed at %s:%u, function %s(). Aborting.", #expr , __FILE__, __LINE__, PA_PRETTY_FUNCTION); \
            abort();                                                    \
        }                                                               \
    } while (false)
#else
#define pa_assert_se(expr)                                              \
    do {                                                                \
        int _unique_var = (expr);                                       \
        if (!_unique_var)                                               \
            abort();                                                    \
    } while (false)
#endif

/* Does exactly nothing */
#define pa_nop() do {} while (false)

/* pa_assert() is an assert that may be optimized away by defining
 * NDEBUG. pa_assert_fp() is an assert that may be optimized away by
 * defining FASTPATH. It is supposed to be used in inner loops. It's
 * there for extra paranoia checking and should probably be removed in
 * production builds. */
#ifdef NDEBUG
#define pa_assert(expr) pa_nop()
#define pa_assert_fp(expr) pa_nop()
#elif defined (FASTPATH)
#define pa_assert(expr) pa_assert_se(expr)
#define pa_assert_fp(expr) pa_nop()
#else
#define pa_assert(expr) pa_assert_se(expr)
#define pa_assert_fp(expr) pa_assert_se(expr)
#endif

#ifdef NDEBUG
#define pa_assert_not_reached() abort()
#else
#define pa_assert_not_reached()                                         \
    do {                                                                \
        pa_log_error("Code should not be reached at %s:%u, function %s(). Aborting.", __FILE__, __LINE__, PA_PRETTY_FUNCTION); \
        abort();                                                        \
    } while (false)
#endif

/* A compile time assertion */
#define pa_assert_cc(expr)                         \
    do {                                           \
        switch (0) {                               \
            case 0:                                \
            case !!(expr):                         \
                ;                                  \
        }                                          \
    } while (false)

#define PA_PTR_TO_UINT(p) ((unsigned int) ((uintptr_t) (p)))
#define PA_UINT_TO_PTR(u) ((void*) ((uintptr_t) (u)))

#define PA_PTR_TO_UINT32(p) ((uint32_t) ((uintptr_t) (p)))
#define PA_UINT32_TO_PTR(u) ((void*) ((uintptr_t) (u)))

#define PA_PTR_TO_INT(p) ((int) ((intptr_t) (p)))
#define PA_INT_TO_PTR(u) ((void*) ((intptr_t) (u)))

#define PA_PTR_TO_INT32(p) ((int32_t) ((intptr_t) (p)))
#define PA_INT32_TO_PTR(u) ((void*) ((intptr_t) (u)))

#ifdef OS_IS_WIN32
#define PA_PATH_SEP "\\"
#define PA_PATH_SEP_CHAR '\\'
#else
#define PA_PATH_SEP "/"
#define PA_PATH_SEP_CHAR '/'
#endif

#if defined(__GNUC__) && defined(__ELF__)

#define PA_WARN_REFERENCE(sym, msg)                  \
    __asm__(".section .gnu.warning." #sym);          \
    __asm__(".asciz \"" msg "\"");                   \
    __asm__(".previous")

#else

#define PA_WARN_REFERENCE(sym, msg)

#endif

#if defined(__i386__) || defined(__x86_64__)
#define PA_DEBUG_TRAP __asm__("int $3")
#else
#define PA_DEBUG_TRAP raise(SIGTRAP)
#endif

#define pa_memzero(x,l) (memset((x), 0, (l)))
#define pa_zero(x) (pa_memzero(&(x), sizeof(x)))

#define PA_INT_TYPE_SIGNED(type) (!!((type) 0 > (type) -1))

#define PA_INT_TYPE_HALF(type) ((type) 1 << (sizeof(type)*8 - 2))

#define PA_INT_TYPE_MAX(type)                                          \
    ((type) (PA_INT_TYPE_SIGNED(type)                                  \
             ? (PA_INT_TYPE_HALF(type) - 1 + PA_INT_TYPE_HALF(type))   \
             : (type) -1))

#define PA_INT_TYPE_MIN(type)                                          \
    ((type) (PA_INT_TYPE_SIGNED(type)                                  \
             ? (-1 - PA_INT_TYPE_MAX(type))                            \
             : (type) 0))

/* The '#' preprocessor operator doesn't expand any macros that are in the
 * parameter, which is why we need a separate macro for those cases where the
 * parameter contains a macro that needs expanding. */
#define PA_STRINGIZE(x) #x
#define PA_EXPAND_AND_STRINGIZE(x) PA_STRINGIZE(x)

/* We include this at the very last place */
#include <pulsecore/log.h>

#endif
