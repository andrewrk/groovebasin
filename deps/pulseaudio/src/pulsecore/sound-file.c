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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sndfile.h>

#include <pulse/sample.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-scache.h>
#include <pulsecore/sndfile-util.h>

#include "sound-file.h"

int pa_sound_file_load(
        pa_mempool *pool,
        const char *fname,
        pa_sample_spec *ss,
        pa_channel_map *map,
        pa_memchunk *chunk,
        pa_proplist *p) {

    SNDFILE *sf = NULL;
    SF_INFO sfi;
    int ret = -1;
    size_t l;
    sf_count_t (*readf_function)(SNDFILE *sndfile, void *ptr, sf_count_t frames) = NULL;
    void *ptr = NULL;
    int fd;

    pa_assert(fname);
    pa_assert(ss);
    pa_assert(chunk);

    pa_memchunk_reset(chunk);

    if ((fd = pa_open_cloexec(fname, O_RDONLY, 0)) < 0) {
        pa_log("Failed to open file %s: %s", fname, pa_cstrerror(errno));
        goto finish;
    }

#ifdef HAVE_POSIX_FADVISE
    if (posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL) < 0) {
        pa_log_warn("POSIX_FADV_SEQUENTIAL failed: %s", pa_cstrerror(errno));
        goto finish;
    } else
        pa_log_debug("POSIX_FADV_SEQUENTIAL succeeded.");
#endif

    pa_zero(sfi);
    if (!(sf = sf_open_fd(fd, SFM_READ, &sfi, 1))) {
        pa_log("Failed to open file %s", fname);
        goto finish;
    }

    fd = -1;

    if (pa_sndfile_read_sample_spec(sf, ss) < 0) {
        pa_log("Failed to determine file sample format.");
        goto finish;
    }

    if ((map && pa_sndfile_read_channel_map(sf, map) < 0)) {
        if (ss->channels > 2)
            pa_log("Failed to determine file channel map, synthesizing one.");
        pa_channel_map_init_extend(map, ss->channels, PA_CHANNEL_MAP_DEFAULT);
    }

    if (p)
        pa_sndfile_init_proplist(sf, p);

    if ((l = pa_frame_size(ss) * (size_t) sfi.frames) > PA_SCACHE_ENTRY_SIZE_MAX) {
        pa_log("File too large");
        goto finish;
    }

    chunk->memblock = pa_memblock_new(pool, l);
    chunk->index = 0;
    chunk->length = l;

    readf_function = pa_sndfile_readf_function(ss);

    ptr = pa_memblock_acquire(chunk->memblock);

    if ((readf_function && readf_function(sf, ptr, sfi.frames) != sfi.frames) ||
        (!readf_function && sf_read_raw(sf, ptr, (sf_count_t) l) != (sf_count_t) l)) {
        pa_log("Premature file end");
        goto finish;
    }

    ret = 0;

finish:

    if (sf)
        sf_close(sf);

    if (ptr)
        pa_memblock_release(chunk->memblock);

    if (ret != 0 && chunk->memblock)
        pa_memblock_unref(chunk->memblock);

    if (fd >= 0)
        pa_close(fd);

    return ret;
}

int pa_sound_file_too_big_to_cache(const char *fname) {

    SNDFILE*sf = NULL;
    SF_INFO sfi;
    pa_sample_spec ss;

    pa_assert(fname);

    pa_zero(sfi);
    if (!(sf = sf_open(fname, SFM_READ, &sfi))) {
        pa_log("Failed to open file %s", fname);
        return -1;
    }

    if (pa_sndfile_read_sample_spec(sf, &ss) < 0) {
        pa_log("Failed to determine file sample format.");
        sf_close(sf);
        return -1;
    }

    sf_close(sf);

    if ((pa_frame_size(&ss) * (size_t) sfi.frames) > PA_SCACHE_ENTRY_SIZE_MAX) {
        pa_log("File too large: %s", fname);
        return 1;
    }

    return 0;
}
