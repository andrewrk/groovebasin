/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_OS_H
#define GROOVE_OS_H

#include <stdbool.h>
#include <stddef.h>

// safe to call from any thread(s) multiple times, but
// must be called at least once before calling any other os functions
// groove_create calls this function.
int groove_os_init(int (*init_once)(void));

double groove_os_get_time(void);

struct GrooveOsThread;
int groove_os_thread_create(
        void (*run)(void *arg), void *arg,
        struct GrooveOsThread ** out_thread);

void groove_os_thread_destroy(struct GrooveOsThread *thread);


struct GrooveOsMutex;
struct GrooveOsMutex *groove_os_mutex_create(void);
void groove_os_mutex_destroy(struct GrooveOsMutex *mutex);
void groove_os_mutex_lock(struct GrooveOsMutex *mutex);
void groove_os_mutex_unlock(struct GrooveOsMutex *mutex);

struct GrooveOsCond;
struct GrooveOsCond *groove_os_cond_create(void);
void groove_os_cond_destroy(struct GrooveOsCond *cond);

// locked_mutex is optional. On systems that use mutexes for conditions, if you
// pass NULL, a mutex will be created and locked/unlocked for you. On systems
// that do not use mutexes for conditions, no mutex handling is necessary. If
// you already have a locked mutex available, pass it; this will be better on
// systems that use mutexes for conditions.
void groove_os_cond_signal(struct GrooveOsCond *cond,
        struct GrooveOsMutex *locked_mutex);
void groove_os_cond_timed_wait(struct GrooveOsCond *cond,
        struct GrooveOsMutex *locked_mutex, double seconds);
void groove_os_cond_wait(struct GrooveOsCond *cond,
        struct GrooveOsMutex *locked_mutex);

#endif
