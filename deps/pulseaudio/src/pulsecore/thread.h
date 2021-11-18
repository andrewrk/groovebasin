#ifndef foopulsethreadhfoo
#define foopulsethreadhfoo

/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <pulse/def.h>
#include <pulse/gccmacro.h>

#include <pulsecore/once.h>
#include <pulsecore/core-util.h>

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

typedef struct pa_thread pa_thread;

typedef void (*pa_thread_func_t) (void *userdata);

pa_thread* pa_thread_new(const char *name, pa_thread_func_t thread_func, void *userdata);
void pa_thread_free(pa_thread *t);
void pa_thread_free_nojoin(pa_thread *t);
int pa_thread_join(pa_thread *t);
int pa_thread_is_running(pa_thread *t);
pa_thread *pa_thread_self(void);
void pa_thread_yield(void);

void* pa_thread_get_data(pa_thread *t);
void pa_thread_set_data(pa_thread *t, void *userdata);

const char *pa_thread_get_name(pa_thread *t);
void pa_thread_set_name(pa_thread *t, const char *name);

typedef struct pa_tls pa_tls;

pa_tls* pa_tls_new(pa_free_cb_t free_cb);
void pa_tls_free(pa_tls *t);
void * pa_tls_get(pa_tls *t);
void *pa_tls_set(pa_tls *t, void *userdata);

#define PA_STATIC_TLS_DECLARE(name, free_cb)                            \
    static struct {                                                     \
        pa_once once;                                                   \
        pa_tls *volatile tls;                                           \
    } name##_tls = {                                                    \
        .once = PA_ONCE_INIT,                                           \
        .tls = NULL                                                     \
    };                                                                  \
    static void name##_tls_init(void) {                                 \
        name##_tls.tls = pa_tls_new(free_cb);                           \
    }                                                                   \
    static inline pa_tls* name##_tls_obj(void) {                        \
        pa_run_once(&name##_tls.once, name##_tls_init);                 \
        return name##_tls.tls;                                          \
    }                                                                   \
    static void name##_tls_destructor(void) PA_GCC_DESTRUCTOR;          \
    static void name##_tls_destructor(void) {                           \
        static void (*_free_cb)(void*) = free_cb;                       \
        if (!pa_in_valgrind())                                          \
            return;                                                     \
        if (!name##_tls.tls)                                            \
            return;                                                     \
        if (_free_cb) {                                                 \
            void *p;                                                    \
            if ((p = pa_tls_get(name##_tls.tls)))                       \
                _free_cb(p);                                            \
        }                                                               \
        pa_tls_free(name##_tls.tls);                                    \
    }                                                                   \
    static inline void* name##_tls_get(void) {                          \
        return pa_tls_get(name##_tls_obj());                            \
    }                                                                   \
    static inline void* name##_tls_set(void *p) {                       \
        return pa_tls_set(name##_tls_obj(), p);                         \
    }                                                                   \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#if defined(SUPPORT_TLS___THREAD) && !defined(OS_IS_WIN32)
/* An optimized version of the above that requires no dynamic
 * allocation if the compiler supports __thread */
#define PA_STATIC_TLS_DECLARE_NO_FREE(name)                             \
    static __thread void *name##_tls = NULL;                            \
    static inline void* name##_tls_get(void) {                          \
        return name##_tls;                                              \
    }                                                                   \
    static inline void* name##_tls_set(void *p) {                       \
        void *r = name##_tls;                                           \
        name##_tls = p;                                                 \
        return r;                                                       \
    }                                                                   \
    struct __stupid_useless_struct_to_allow_trailing_semicolon
#else
#define PA_STATIC_TLS_DECLARE_NO_FREE(name) PA_STATIC_TLS_DECLARE(name, NULL)
#endif

#define PA_STATIC_TLS_GET(name) (name##_tls_get())
#define PA_STATIC_TLS_SET(name, p) (name##_tls_set(p))

#endif
