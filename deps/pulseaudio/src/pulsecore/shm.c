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
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <signal.h>

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

/* This is deprecated on glibc but is still used by FreeBSD */
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

#include <pulse/xmalloc.h>
#include <pulse/gccmacro.h>

#include <pulsecore/memfd-wrappers.h>
#include <pulsecore/core-error.h>
#include <pulsecore/log.h>
#include <pulsecore/random.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/atomic.h>
#include <pulsecore/mem.h>

#include "shm.h"

#if defined(__linux__) && !defined(MADV_REMOVE)
#define MADV_REMOVE 9
#endif

/* 1 GiB at max */
#define MAX_SHM_SIZE (PA_ALIGN(1024*1024*1024))

#ifdef __linux__
/* On Linux we know that the shared memory blocks are files in
 * /dev/shm. We can use that information to list all blocks and
 * cleanup unused ones */
#define SHM_PATH "/dev/shm/"
#define SHM_ID_LEN 10
#elif defined(__sun)
#define SHM_PATH "/tmp"
#define SHM_ID_LEN 15
#else
#undef SHM_PATH
#undef SHM_ID_LEN
#endif

#define SHM_MARKER ((int) 0xbeefcafe)

/* We now put this SHM marker at the end of each segment. It's
 * optional, to not require a reboot when upgrading, though. Note that
 * on multiarch systems 32bit and 64bit processes might access this
 * region simultaneously. The header fields need to be independent
 * from the process' word with */
struct shm_marker {
    pa_atomic_t marker; /* 0xbeefcafe */
    pa_atomic_t pid;
    uint64_t _reserved1;
    uint64_t _reserved2;
    uint64_t _reserved3;
    uint64_t _reserved4;
} PA_GCC_PACKED;

static inline size_t shm_marker_size(pa_mem_type_t type) {
    if (type == PA_MEM_TYPE_SHARED_POSIX)
        return PA_ALIGN(sizeof(struct shm_marker));

    return 0;
}

#ifdef HAVE_SHM_OPEN
static char *segment_name(char *fn, size_t l, unsigned id) {
    pa_snprintf(fn, l, "/pulse-shm-%u", id);
    return fn;
}
#endif

static int privatemem_create(pa_shm *m, size_t size) {
    pa_assert(m);
    pa_assert(size > 0);

    m->type = PA_MEM_TYPE_PRIVATE;
    m->id = 0;
    m->size = size;
    m->do_unlink = false;
    m->fd = -1;

#ifdef MAP_ANONYMOUS
    if ((m->ptr = mmap(NULL, m->size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, (off_t) 0)) == MAP_FAILED) {
        pa_log("mmap() failed: %s", pa_cstrerror(errno));
        return -1;
    }
#elif defined(HAVE_POSIX_MEMALIGN)
    {
        int r;

        if ((r = posix_memalign(&m->ptr, pa_page_size(), size)) < 0) {
            pa_log("posix_memalign() failed: %s", pa_cstrerror(r));
            return r;
        }
    }
#else
    m->ptr = pa_xmalloc(m->size);
#endif

    return 0;
}

static int sharedmem_create(pa_shm *m, pa_mem_type_t type, size_t size, mode_t mode) {
#if defined(HAVE_SHM_OPEN) || defined(HAVE_MEMFD)
    char fn[32];
    int fd = -1;
    struct shm_marker *marker;
    bool do_unlink = false;

    /* Each time we create a new SHM area, let's first drop all stale
     * ones */
    pa_shm_cleanup();

    pa_random(&m->id, sizeof(m->id));

    switch (type) {
#ifdef HAVE_SHM_OPEN
    case PA_MEM_TYPE_SHARED_POSIX:
        segment_name(fn, sizeof(fn), m->id);
        fd = shm_open(fn, O_RDWR|O_CREAT|O_EXCL, mode);
        do_unlink = true;
        break;
#endif
#ifdef HAVE_MEMFD
    case PA_MEM_TYPE_SHARED_MEMFD:
        fd = memfd_create("pulseaudio", MFD_ALLOW_SEALING);
        break;
#endif
    default:
        goto fail;
    }

    if (fd < 0) {
        pa_log("%s open() failed: %s", pa_mem_type_to_string(type), pa_cstrerror(errno));
        goto fail;
    }

    m->type = type;
    m->size = size + shm_marker_size(type);
    m->do_unlink = do_unlink;

    if (ftruncate(fd, (off_t) m->size) < 0) {
        pa_log("ftruncate() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

    if ((m->ptr = mmap(NULL, PA_PAGE_ALIGN(m->size), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_NORESERVE, fd, (off_t) 0)) == MAP_FAILED) {
        pa_log("mmap() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    if (type == PA_MEM_TYPE_SHARED_POSIX) {
        /* We store our PID at the end of the shm block, so that we
         * can check for dead shm segments later */
        marker = (struct shm_marker*) ((uint8_t*) m->ptr + m->size - shm_marker_size(type));
        pa_atomic_store(&marker->pid, (int) getpid());
        pa_atomic_store(&marker->marker, SHM_MARKER);
    }

    /* For memfds, we keep the fd open until we pass it
     * to the other PA endpoint over unix domain socket. */
    if (type != PA_MEM_TYPE_SHARED_MEMFD) {
        pa_assert_se(pa_close(fd) == 0);
        m->fd = -1;
    }
#ifdef HAVE_MEMFD
    else
        m->fd = fd;
#endif

    return 0;

fail:
    if (fd >= 0) {
#ifdef HAVE_SHM_OPEN
        if (type == PA_MEM_TYPE_SHARED_POSIX)
            shm_unlink(fn);
#endif
        pa_close(fd);
    }
#endif /* defined(HAVE_SHM_OPEN) || defined(HAVE_MEMFD) */

    return -1;
}

int pa_shm_create_rw(pa_shm *m, pa_mem_type_t type, size_t size, mode_t mode) {
    pa_assert(m);
    pa_assert(size > 0);
    pa_assert(size <= MAX_SHM_SIZE);
    pa_assert(!(mode & ~0777));
    pa_assert(mode >= 0600);

    /* Round up to make it page aligned */
    size = PA_PAGE_ALIGN(size);

    if (type == PA_MEM_TYPE_PRIVATE)
        return privatemem_create(m, size);

    return sharedmem_create(m, type, size, mode);
}

static void privatemem_free(pa_shm *m) {
    pa_assert(m);
    pa_assert(m->ptr);
    pa_assert(m->size > 0);

#ifdef MAP_ANONYMOUS
    if (munmap(m->ptr, m->size) < 0)
        pa_log("munmap() failed: %s", pa_cstrerror(errno));
#elif defined(HAVE_POSIX_MEMALIGN)
    free(m->ptr);
#else
    pa_xfree(m->ptr);
#endif
}

void pa_shm_free(pa_shm *m) {
    pa_assert(m);
    pa_assert(m->ptr);
    pa_assert(m->size > 0);

#ifdef MAP_FAILED
    pa_assert(m->ptr != MAP_FAILED);
#endif

    if (m->type == PA_MEM_TYPE_PRIVATE) {
        privatemem_free(m);
        goto finish;
    }

#if defined(HAVE_SHM_OPEN) || defined(HAVE_MEMFD)
    if (munmap(m->ptr, PA_PAGE_ALIGN(m->size)) < 0)
        pa_log("munmap() failed: %s", pa_cstrerror(errno));

#ifdef HAVE_SHM_OPEN
    if (m->type == PA_MEM_TYPE_SHARED_POSIX && m->do_unlink) {
        char fn[32];

        segment_name(fn, sizeof(fn), m->id);
        if (shm_unlink(fn) < 0)
            pa_log(" shm_unlink(%s) failed: %s", fn, pa_cstrerror(errno));
    }
#endif
#ifdef HAVE_MEMFD
    if (m->type == PA_MEM_TYPE_SHARED_MEMFD && m->fd != -1)
        pa_assert_se(pa_close(m->fd) == 0);
#endif

#else
    /* We shouldn't be here without shm or memfd support */
    pa_assert_not_reached();
#endif

finish:
    pa_zero(*m);
}

void pa_shm_punch(pa_shm *m, size_t offset, size_t size) {
    void *ptr;
    size_t o;
    const size_t page_size = pa_page_size();

    pa_assert(m);
    pa_assert(m->ptr);
    pa_assert(m->size > 0);
    pa_assert(offset+size <= m->size);

#ifdef MAP_FAILED
    pa_assert(m->ptr != MAP_FAILED);
#endif

    /* You're welcome to implement this as NOOP on systems that don't
     * support it */

    /* Align the pointer up to multiples of the page size */
    ptr = (uint8_t*) m->ptr + offset;
    o = (size_t) ((uint8_t*) ptr - (uint8_t*) PA_PAGE_ALIGN_PTR(ptr));

    if (o > 0) {
        size_t delta = page_size - o;
        ptr = (uint8_t*) ptr + delta;
        size -= delta;
    }

    /* Align the size down to multiples of page size */
    size = (size / page_size) * page_size;

#ifdef MADV_REMOVE
    if (madvise(ptr, size, MADV_REMOVE) >= 0)
        return;
#endif

#ifdef MADV_FREE
    if (madvise(ptr, size, MADV_FREE) >= 0)
        return;
#endif

#ifdef MADV_DONTNEED
    madvise(ptr, size, MADV_DONTNEED);
#elif defined(POSIX_MADV_DONTNEED)
    posix_madvise(ptr, size, POSIX_MADV_DONTNEED);
#endif
}

static int shm_attach(pa_shm *m, pa_mem_type_t type, unsigned id, int memfd_fd, bool writable, bool for_cleanup) {
#if defined(HAVE_SHM_OPEN) || defined(HAVE_MEMFD)
    char fn[32];
    int fd = -1;
    int prot;
    struct stat st;

    pa_assert(m);

    switch (type) {
#ifdef HAVE_SHM_OPEN
    case PA_MEM_TYPE_SHARED_POSIX:
        pa_assert(memfd_fd == -1);
        segment_name(fn, sizeof(fn), id);
        if ((fd = shm_open(fn, writable ? O_RDWR : O_RDONLY, 0)) < 0) {
            if ((errno != EACCES && errno != ENOENT) || !for_cleanup)
                pa_log("shm_open() failed: %s", pa_cstrerror(errno));
            goto fail;
        }
        break;
#endif
#ifdef HAVE_MEMFD
    case PA_MEM_TYPE_SHARED_MEMFD:
        pa_assert(memfd_fd != -1);
        fd = memfd_fd;
        break;
#endif
    default:
        goto fail;
    }

    if (fstat(fd, &st) < 0) {
        pa_log("fstat() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    if (st.st_size <= 0 ||
        st.st_size > (off_t) MAX_SHM_SIZE + (off_t) shm_marker_size(type) ||
        PA_ALIGN((size_t) st.st_size) != (size_t) st.st_size) {
        pa_log("Invalid shared memory segment size");
        goto fail;
    }

    prot = writable ? PROT_READ | PROT_WRITE : PROT_READ;
    if ((m->ptr = mmap(NULL, PA_PAGE_ALIGN(st.st_size), prot, MAP_SHARED, fd, (off_t) 0)) == MAP_FAILED) {
        pa_log("mmap() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    /* In case of attaching to memfd areas, _the caller_ maintains
     * ownership of the passed fd and has the sole responsibility
     * of closing it down.. For other types, we're the code path
     * which created the fd in the first place and we're thus the
     * ones responsible for closing it down */
    if (type != PA_MEM_TYPE_SHARED_MEMFD)
        pa_assert_se(pa_close(fd) == 0);

    m->type = type;
    m->id = id;
    m->size = (size_t) st.st_size;
    m->do_unlink = false;
    m->fd = -1;

    return 0;

fail:
    /* In case of memfds, caller maintains fd ownership */
    if (fd >= 0 && type != PA_MEM_TYPE_SHARED_MEMFD)
        pa_close(fd);

#endif /* defined(HAVE_SHM_OPEN) || defined(HAVE_MEMFD) */

    return -1;
}

/* Caller owns passed @memfd_fd and must close it down when appropriate. */
int pa_shm_attach(pa_shm *m, pa_mem_type_t type, unsigned id, int memfd_fd, bool writable) {
    return shm_attach(m, type, id, memfd_fd, writable, false);
}

int pa_shm_cleanup(void) {

#ifdef HAVE_SHM_OPEN
#ifdef SHM_PATH
    DIR *d;
    struct dirent *de;

    if (!(d = opendir(SHM_PATH))) {
        pa_log_warn("Failed to read "SHM_PATH": %s", pa_cstrerror(errno));
        return -1;
    }

    while ((de = readdir(d))) {
        pa_shm seg;
        unsigned id;
        pid_t pid;
        char fn[128];
        struct shm_marker *m;

#if defined(__sun)
        if (strncmp(de->d_name, ".SHMDpulse-shm-", SHM_ID_LEN))
#else
        if (strncmp(de->d_name, "pulse-shm-", SHM_ID_LEN))
#endif
            continue;

        if (pa_atou(de->d_name + SHM_ID_LEN, &id) < 0)
            continue;

        if (shm_attach(&seg, PA_MEM_TYPE_SHARED_POSIX, id, -1, false, true) < 0)
            continue;

        if (seg.size < shm_marker_size(seg.type)) {
            pa_shm_free(&seg);
            continue;
        }

        m = (struct shm_marker*) ((uint8_t*) seg.ptr + seg.size - shm_marker_size(seg.type));

        if (pa_atomic_load(&m->marker) != SHM_MARKER) {
            pa_shm_free(&seg);
            continue;
        }

        if (!(pid = (pid_t) pa_atomic_load(&m->pid))) {
            pa_shm_free(&seg);
            continue;
        }

        if (kill(pid, 0) == 0 || errno != ESRCH) {
            pa_shm_free(&seg);
            continue;
        }

        pa_shm_free(&seg);

        /* Ok, the owner of this shms segment is dead, so, let's remove the segment */
        segment_name(fn, sizeof(fn), id);

        if (shm_unlink(fn) < 0 && errno != EACCES && errno != ENOENT)
            pa_log_warn("Failed to remove SHM segment %s: %s", fn, pa_cstrerror(errno));
    }

    closedir(d);
#endif /* SHM_PATH */
#endif /* HAVE_SHM_OPEN */

    return 0;
}
