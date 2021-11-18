/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

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

#include <errno.h>
#include <gdbm.h>

#include <pulse/xmalloc.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>

#include "database.h"

#define MAKE_GDBM_FILE(x) ((GDBM_FILE) (x))

static inline datum* datum_to_gdbm(datum *to, const pa_datum *from) {
    pa_assert(from);
    pa_assert(to);

    to->dptr = from->data;
    to->dsize = from->size;

    return to;
}

static inline pa_datum* datum_from_gdbm(pa_datum *to, const datum *from) {
    pa_assert(from);
    pa_assert(to);

    to->data = from->dptr;
    to->size = from->dsize;

    return to;
}

void pa_datum_free(pa_datum *d) {
    pa_assert(d);

    free(d->data); /* gdbm uses raw malloc/free hence we should do that here, too */
    pa_zero(d);
}

const char* pa_database_get_filename_suffix(void) {
    return ".gdbm";
}

pa_database* pa_database_open_internal(const char *path, bool for_write) {
    GDBM_FILE f;
    int gdbm_cache_size;

    pa_assert(path);

    errno = 0;

    /* We need to set the block size explicitly here, since otherwise
     * gdbm takes the native block size of the underlying file system
     * which might be incredibly large. */
    f = gdbm_open((char*) path, 1024, GDBM_NOLOCK | (for_write ? GDBM_WRCREAT : GDBM_READER), 0644, NULL);

    if (f)
        pa_log_debug("Opened GDBM database '%s'", path);

    if (!f) {
        if (errno == 0)
            errno = EIO;
        return NULL;
    }

    /* By default the cache of gdbm is rather large, let's reduce it a bit to save memory */
    gdbm_cache_size = 10;
    gdbm_setopt(f, GDBM_CACHESIZE, &gdbm_cache_size, sizeof(gdbm_cache_size));

    return (pa_database*) f;
}

void pa_database_close(pa_database *db) {
    pa_assert(db);

    gdbm_close(MAKE_GDBM_FILE(db));
}

pa_datum* pa_database_get(pa_database *db, const pa_datum *key, pa_datum* data) {
    datum gdbm_key, gdbm_data;

    pa_assert(db);
    pa_assert(key);
    pa_assert(data);

    gdbm_data = gdbm_fetch(MAKE_GDBM_FILE(db), *datum_to_gdbm(&gdbm_key, key));

    return gdbm_data.dptr ?
        datum_from_gdbm(data, &gdbm_data) :
        NULL;
}

int pa_database_set(pa_database *db, const pa_datum *key, const pa_datum* data, bool overwrite) {
    datum gdbm_key, gdbm_data;

    pa_assert(db);
    pa_assert(key);
    pa_assert(data);

    return gdbm_store(MAKE_GDBM_FILE(db),
                      *datum_to_gdbm(&gdbm_key, key),
                      *datum_to_gdbm(&gdbm_data, data),
                      overwrite ? GDBM_REPLACE : GDBM_INSERT) != 0 ? -1 : 0;
}

int pa_database_unset(pa_database *db, const pa_datum *key) {
    datum gdbm_key;

    pa_assert(db);
    pa_assert(key);

    return gdbm_delete(MAKE_GDBM_FILE(db), *datum_to_gdbm(&gdbm_key, key)) != 0 ? -1 : 0;
}

int pa_database_clear(pa_database *db) {
    datum gdbm_key;

    pa_assert(db);

    gdbm_key = gdbm_firstkey(MAKE_GDBM_FILE(db));

    while (gdbm_key.dptr) {
        datum next;

        next = gdbm_nextkey(MAKE_GDBM_FILE(db), gdbm_key);

        gdbm_delete(MAKE_GDBM_FILE(db), gdbm_key);

        free(gdbm_key.dptr);
        gdbm_key = next;
    }

    return gdbm_reorganize(MAKE_GDBM_FILE(db)) == 0 ? 0 : -1;
}

signed pa_database_size(pa_database *db) {
    datum gdbm_key;
    unsigned n = 0;

    pa_assert(db);

    /* This sucks */

    gdbm_key = gdbm_firstkey(MAKE_GDBM_FILE(db));

    while (gdbm_key.dptr) {
        datum next;

        n++;

        next = gdbm_nextkey(MAKE_GDBM_FILE(db), gdbm_key);
        free(gdbm_key.dptr);
        gdbm_key = next;
    }

    return (signed) n;
}

pa_datum* pa_database_first(pa_database *db, pa_datum *key, pa_datum *data) {
    datum gdbm_key, gdbm_data;

    pa_assert(db);
    pa_assert(key);

    gdbm_key = gdbm_firstkey(MAKE_GDBM_FILE(db));

    if (!gdbm_key.dptr)
        return NULL;

    if (data) {
        gdbm_data = gdbm_fetch(MAKE_GDBM_FILE(db), gdbm_key);

        if (!gdbm_data.dptr) {
            free(gdbm_key.dptr);
            return NULL;
        }

        datum_from_gdbm(data, &gdbm_data);
    }

    datum_from_gdbm(key, &gdbm_key);

    return key;
}

pa_datum* pa_database_next(pa_database *db, const pa_datum *key, pa_datum *next, pa_datum *data) {
    datum gdbm_key, gdbm_data;

    pa_assert(db);
    pa_assert(key);
    pa_assert(next);

    if (!key)
        return pa_database_first(db, next, data);

    gdbm_key = gdbm_nextkey(MAKE_GDBM_FILE(db), *datum_to_gdbm(&gdbm_key, key));

    if (!gdbm_key.dptr)
        return NULL;

    if (data) {
        gdbm_data = gdbm_fetch(MAKE_GDBM_FILE(db), gdbm_key);

        if (!gdbm_data.dptr) {
            free(gdbm_key.dptr);
            return NULL;
        }

        datum_from_gdbm(data, &gdbm_data);
    }

    datum_from_gdbm(next, &gdbm_key);

    return next;
}

int pa_database_sync(pa_database *db) {
    pa_assert(db);

    gdbm_sync(MAKE_GDBM_FILE(db));
    return 0;
}
