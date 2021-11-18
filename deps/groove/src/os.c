/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "os.h"
#include "groove_internal.h"
#include "util.h"
#include "atomics.h"

#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#if defined(_WIN32)
#define GROOVE_OS_WINDOWS

#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#if !defined(VC_EXTRALEAN)
#define VC_EXTRALEAN
#endif

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#if !defined(UNICODE)
#define UNICODE
#endif

// require Windows 7 or later
#if WINVER < 0x0601
#undef WINVER
#define WINVER 0x0601
#endif
#if _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>

#else

#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

#endif

#if defined(__FreeBSD__) || defined(__MACH__)
#define GROOVE_OS_KQUEUE
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#if defined(__MACH__)
#include <mach/clock.h>
#include <mach/mach.h>
#endif

struct GrooveOsThread {
#if defined(GROOVE_OS_WINDOWS)
    HANDLE handle;
    DWORD id;
#else
    pthread_t id;
    bool running;
#endif
    void *arg;
    void (*run)(void *arg);
};

struct GrooveOsMutex {
#if defined(GROOVE_OS_WINDOWS)
    CRITICAL_SECTION id;
#else
    pthread_mutex_t id;
    bool id_init;
#endif
};

#if defined(GROOVE_OS_KQUEUE)
static const uintptr_t notify_ident = 1;
struct GrooveOsCond {
    int kq_id;
};
#elif defined(GROOVE_OS_WINDOWS)
struct GrooveOsCond {
    CONDITION_VARIABLE id;
    CRITICAL_SECTION default_cs_id;
};
#else
struct GrooveOsCond {
    pthread_cond_t id;
    bool id_init;

    pthread_condattr_t attr;
    bool attr_init;

    pthread_mutex_t default_mutex_id;
    bool default_mutex_init;
};
#endif

#if defined(GROOVE_OS_WINDOWS)
static INIT_ONCE win32_init_once = INIT_ONCE_STATIC_INIT;
static double win32_time_resolution;
static SYSTEM_INFO win32_system_info;
#else
static atomic_bool initialized = ATOMIC_VAR_INIT(false);
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
#if defined(__MACH__)
static clock_serv_t cclock;
#endif
#endif

double groove_os_get_time(void) {
#if defined(GROOVE_OS_WINDOWS)
    unsigned __int64 time;
    QueryPerformanceCounter((LARGE_INTEGER*) &time);
    return time * win32_time_resolution;
#elif defined(__MACH__)
    mach_timespec_t mts;

    kern_return_t err = clock_get_time(cclock, &mts);
    assert(!err);

    double seconds = (double)mts.tv_sec;
    seconds += ((double)mts.tv_nsec) / 1000000000.0;

    return seconds;
#else
    struct timespec tms;
    clock_gettime(CLOCK_MONOTONIC, &tms);
    double seconds = (double)tms.tv_sec;
    seconds += ((double)tms.tv_nsec) / 1000000000.0;
    return seconds;
#endif
}

#if defined(GROOVE_OS_WINDOWS)
static DWORD WINAPI run_win32_thread(LPVOID userdata) {
    struct GrooveOsThread *thread = (struct GrooveOsThread *)userdata;
    HRESULT err = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    assert(err == S_OK);
    thread->run(thread->arg);
    CoUninitialize();
    return 0;
}
#else
static void assert_no_err(int err) {
    assert(!err);
}

static void *run_pthread(void *userdata) {
    struct GrooveOsThread *thread = (struct GrooveOsThread *)userdata;
    thread->run(thread->arg);
    return NULL;
}
#endif

int groove_os_thread_create(
        void (*run)(void *arg), void *arg,
        struct GrooveOsThread ** out_thread)
{
    *out_thread = NULL;

    struct GrooveOsThread *thread = ALLOCATE(struct GrooveOsThread, 1);
    if (!thread) {
        groove_os_thread_destroy(thread);
        return GrooveErrorNoMem;
    }

    thread->run = run;
    thread->arg = arg;

#if defined(GROOVE_OS_WINDOWS)
    thread->handle = CreateThread(NULL, 0, run_win32_thread, thread, 0, &thread->id);
    if (!thread->handle) {
        groove_os_thread_destroy(thread);
        return GrooveErrorSystemResources;
    }
#else
    int err;
    if ((err = pthread_create(&thread->id, NULL, run_pthread, thread))) {
        groove_os_thread_destroy(thread);
        return GrooveErrorNoMem;
    }
    thread->running = true;
#endif

    *out_thread = thread;
    return 0;
}

void groove_os_thread_destroy(struct GrooveOsThread *thread) {
    if (!thread)
        return;

#if defined(GROOVE_OS_WINDOWS)
    if (thread->handle) {
        DWORD err = WaitForSingleObject(thread->handle, INFINITE);
        assert(err != WAIT_FAILED);
        BOOL ok = CloseHandle(thread->handle);
        assert(ok);
    }
#else
    if (thread->running) {
        assert_no_err(pthread_join(thread->id, NULL));
    }
#endif

    free(thread);
}

struct GrooveOsMutex *groove_os_mutex_create(void) {
    struct GrooveOsMutex *mutex = ALLOCATE(struct GrooveOsMutex, 1);
    if (!mutex) {
        groove_os_mutex_destroy(mutex);
        return NULL;
    }

#if defined(GROOVE_OS_WINDOWS)
    InitializeCriticalSection(&mutex->id);
#else
    int err;
    if ((err = pthread_mutex_init(&mutex->id, NULL))) {
        groove_os_mutex_destroy(mutex);
        return NULL;
    }
    mutex->id_init = true;
#endif

    return mutex;
}

void groove_os_mutex_destroy(struct GrooveOsMutex *mutex) {
    if (!mutex)
        return;

#if defined(GROOVE_OS_WINDOWS)
    DeleteCriticalSection(&mutex->id);
#else
    if (mutex->id_init) {
        assert_no_err(pthread_mutex_destroy(&mutex->id));
    }
#endif

    free(mutex);
}

void groove_os_mutex_lock(struct GrooveOsMutex *mutex) {
#if defined(GROOVE_OS_WINDOWS)
    EnterCriticalSection(&mutex->id);
#else
    assert_no_err(pthread_mutex_lock(&mutex->id));
#endif
}

void groove_os_mutex_unlock(struct GrooveOsMutex *mutex) {
#if defined(GROOVE_OS_WINDOWS)
    LeaveCriticalSection(&mutex->id);
#else
    assert_no_err(pthread_mutex_unlock(&mutex->id));
#endif
}

struct GrooveOsCond * groove_os_cond_create(void) {
    struct GrooveOsCond *cond = ALLOCATE(struct GrooveOsCond, 1);

    if (!cond) {
        groove_os_cond_destroy(cond);
        return NULL;
    }

#if defined(GROOVE_OS_WINDOWS)
    InitializeConditionVariable(&cond->id);
    InitializeCriticalSection(&cond->default_cs_id);
#elif defined(GROOVE_OS_KQUEUE)
    cond->kq_id = kqueue();
    if (cond->kq_id == -1)
        return NULL;
#else
    if (pthread_condattr_init(&cond->attr)) {
        groove_os_cond_destroy(cond);
        return NULL;
    }
    cond->attr_init = true;

    if (pthread_condattr_setclock(&cond->attr, CLOCK_MONOTONIC)) {
        groove_os_cond_destroy(cond);
        return NULL;
    }

    if (pthread_cond_init(&cond->id, &cond->attr)) {
        groove_os_cond_destroy(cond);
        return NULL;
    }
    cond->id_init = true;

    if ((pthread_mutex_init(&cond->default_mutex_id, NULL))) {
        groove_os_cond_destroy(cond);
        return NULL;
    }
    cond->default_mutex_init = true;
#endif

    return cond;
}

void groove_os_cond_destroy(struct GrooveOsCond *cond) {
    if (!cond)
        return;

#if defined(GROOVE_OS_WINDOWS)
    DeleteCriticalSection(&cond->default_cs_id);
#elif defined(GROOVE_OS_KQUEUE)
    close(cond->kq_id);
#else
    if (cond->id_init) {
        assert_no_err(pthread_cond_destroy(&cond->id));
    }

    if (cond->attr_init) {
        assert_no_err(pthread_condattr_destroy(&cond->attr));
    }
    if (cond->default_mutex_init) {
        assert_no_err(pthread_mutex_destroy(&cond->default_mutex_id));
    }
#endif

    free(cond);
}

void groove_os_cond_signal(struct GrooveOsCond *cond,
        struct GrooveOsMutex *locked_mutex)
{
#if defined(GROOVE_OS_WINDOWS)
    if (locked_mutex) {
        WakeConditionVariable(&cond->id);
    } else {
        EnterCriticalSection(&cond->default_cs_id);
        WakeConditionVariable(&cond->id);
        LeaveCriticalSection(&cond->default_cs_id);
    }
#elif defined(GROOVE_OS_KQUEUE)
    struct kevent kev;
    struct timespec timeout = { 0, 0 };

    memset(&kev, 0, sizeof(kev));
    kev.ident = notify_ident;
    kev.filter = EVFILT_USER;
    kev.fflags = NOTE_TRIGGER;

    if (kevent(cond->kq_id, &kev, 1, NULL, 0, &timeout) == -1) {
        if (errno == EINTR)
            return;
        if (errno == ENOENT)
            return;
        assert(0); // kevent signal error
    }
#else
    if (locked_mutex) {
        assert_no_err(pthread_cond_signal(&cond->id));
    } else {
        assert_no_err(pthread_mutex_lock(&cond->default_mutex_id));
        assert_no_err(pthread_cond_signal(&cond->id));
        assert_no_err(pthread_mutex_unlock(&cond->default_mutex_id));
    }
#endif
}

void groove_os_cond_timed_wait(struct GrooveOsCond *cond,
        struct GrooveOsMutex *locked_mutex, double seconds)
{
#if defined(GROOVE_OS_WINDOWS)
    CRITICAL_SECTION *target_cs;
    if (locked_mutex) {
        target_cs = &locked_mutex->id;
    } else {
        target_cs = &cond->default_cs_id;
        EnterCriticalSection(&cond->default_cs_id);
    }
    DWORD ms = seconds * 1000.0;
    SleepConditionVariableCS(&cond->id, target_cs, ms);
    if (!locked_mutex)
        LeaveCriticalSection(&cond->default_cs_id);
#elif defined(GROOVE_OS_KQUEUE)
    struct kevent kev;
    struct kevent out_kev;

    if (locked_mutex)
        assert_no_err(pthread_mutex_unlock(&locked_mutex->id));

    memset(&kev, 0, sizeof(kev));
    kev.ident = notify_ident;
    kev.filter = EVFILT_USER;
    kev.flags = EV_ADD | EV_CLEAR;

    // this time is relative
    struct timespec timeout;
    timeout.tv_nsec = (seconds * 1000000000L);
    timeout.tv_sec  = timeout.tv_nsec / 1000000000L;
    timeout.tv_nsec = timeout.tv_nsec % 1000000000L;

    if (kevent(cond->kq_id, &kev, 1, &out_kev, 1, &timeout) == -1) {
        if (errno == EINTR)
            return;
        assert(0); // kevent wait error
    }
    if (locked_mutex)
        assert_no_err(pthread_mutex_lock(&locked_mutex->id));
#else
    pthread_mutex_t *target_mutex;
    if (locked_mutex) {
        target_mutex = &locked_mutex->id;
    } else {
        target_mutex = &cond->default_mutex_id;
        assert_no_err(pthread_mutex_lock(target_mutex));
    }
    // this time is absolute
    struct timespec tms;
    clock_gettime(CLOCK_MONOTONIC, &tms);
    tms.tv_nsec += (seconds * 1000000000L);
    tms.tv_sec += tms.tv_nsec / 1000000000L;
    tms.tv_nsec = tms.tv_nsec % 1000000000L;
    int err;
    if ((err = pthread_cond_timedwait(&cond->id, target_mutex, &tms))) {
        assert(err != EPERM);
        assert(err != EINVAL);
    }
    if (!locked_mutex)
        assert_no_err(pthread_mutex_unlock(target_mutex));
#endif
}

void groove_os_cond_wait(struct GrooveOsCond *cond,
        struct GrooveOsMutex *locked_mutex)
{
#if defined(GROOVE_OS_WINDOWS)
    CRITICAL_SECTION *target_cs;
    if (locked_mutex) {
        target_cs = &locked_mutex->id;
    } else {
        target_cs = &cond->default_cs_id;
        EnterCriticalSection(&cond->default_cs_id);
    }
    SleepConditionVariableCS(&cond->id, target_cs, INFINITE);
    if (!locked_mutex)
        LeaveCriticalSection(&cond->default_cs_id);
#elif defined(GROOVE_OS_KQUEUE)
    struct kevent kev;
    struct kevent out_kev;

    if (locked_mutex)
        assert_no_err(pthread_mutex_unlock(&locked_mutex->id));

    memset(&kev, 0, sizeof(kev));
    kev.ident = notify_ident;
    kev.filter = EVFILT_USER;
    kev.flags = EV_ADD | EV_CLEAR;

    if (kevent(cond->kq_id, &kev, 1, &out_kev, 1, NULL) == -1) {
        if (errno == EINTR)
            return;
        assert(0); // kevent wait error
    }
    if (locked_mutex)
        assert_no_err(pthread_mutex_lock(&locked_mutex->id));
#else
    pthread_mutex_t *target_mutex;
    if (locked_mutex) {
        target_mutex = &locked_mutex->id;
    } else {
        target_mutex = &cond->default_mutex_id;
        assert_no_err(pthread_mutex_lock(&cond->default_mutex_id));
    }
    int err;
    if ((err = pthread_cond_wait(&cond->id, target_mutex))) {
        assert(err != EPERM);
        assert(err != EINVAL);
    }
    if (!locked_mutex)
        assert_no_err(pthread_mutex_unlock(&cond->default_mutex_id));
#endif
}

static int get_random_seed(uint32_t *seed) {
    int fd = open("/dev/random", O_RDONLY|O_NONBLOCK);
    if (fd == -1)
        return GrooveErrorSystemResources;

    int amt = read(fd, seed, 4);
    if (amt != 4) {
        close(fd);
        return GrooveErrorSystemResources;
    }

    close(fd);
    return 0;
}

static int internal_init(void) {
#if defined(GROOVE_OS_WINDOWS)
    unsigned __int64 frequency;
    if (QueryPerformanceFrequency((LARGE_INTEGER*) &frequency)) {
        win32_time_resolution = 1.0 / (double) frequency;
    } else {
        return GrooveErrorSystemResources;
    }
    GetSystemInfo(&win32_system_info);
#elif defined(__MACH__)
    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
#endif
    uint32_t seed;
    int err;
    if ((err = get_random_seed(&seed))) {
        return err;
    }
    srand(seed);
    return 0;
}

int groove_os_init(int (*init_once)(void)) {
    int err;
#if defined(GROOVE_OS_WINDOWS)
    PVOID lpContext;
    BOOL pending;

    if (!InitOnceBeginInitialize(&win32_init_once, INIT_ONCE_ASYNC, &pending, &lpContext))
        return GrooveErrorSystemResources;

    if (!pending)
        return 0;

    if ((err = internal_init()))
        return err;

    if ((err = init_once()))
        return err;

    if (!InitOnceComplete(&win32_init_once, INIT_ONCE_ASYNC, NULL))
        return GrooveErrorSystemResources;
#else
    if (atomic_load(&initialized))
        return 0;

    assert_no_err(pthread_mutex_lock(&init_mutex));
    if (atomic_load(&initialized)) {
        assert_no_err(pthread_mutex_unlock(&init_mutex));
        return 0;
    }
    atomic_store(&initialized, true);
    if ((err = internal_init()))
        return err;
    if ((err = init_once()))
        return err;
    assert_no_err(pthread_mutex_unlock(&init_mutex));
#endif

    return 0;
}
