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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* Some versions of tdb lack inclusion of signal.h in the header files but use sigatomic_t */
#include <signal.h>
#include <tdb.h>

#include <pulse/xmalloc.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>

#include "database.h"

#define MAKE_TDB_CONTEXT(x) ((struct tdb_context*) (x))

static inline TDB_DATA* datum_to_tdb(TDB_DATA *to, const pa_datum *from) {
    pa_assert(from);
    pa_assert(to);

    to->dptr = from->data;
    to->dsize = from->size;

    return to;
}

static inline pa_datum* datum_from_tdb(pa_datum *to, const TDB_DATA *from) {
    pa_assert(from);
    pa_assert(to);

    to->data = from->dptr;
    to->size = from->dsize;

    return to;
}

void pa_datum_free(pa_datum *d) {
    pa_assert(d);

    free(d->data); /* tdb uses raw malloc/free hence we should do that here, too */
    pa_zero(d);
}

static struct tdb_context *tdb_open_cloexec(
        const char *name,
        int hash_size,
        int tdb_flags,
        int open_flags,
        mode_t mode) {

    /* Mimics pa_open_cloexec() */

    struct tdb_context *c;

#ifdef O_NOCTTY
    open_flags |= O_NOCTTY;
#endif

#ifdef O_CLOEXEC
    errno = 0;
    if ((c = tdb_open(name, hash_size, tdb_flags, open_flags | O_CLOEXEC, mode)))
        goto finish;

    if (errno != EINVAL)
        return NULL;
#endif

    errno = 0;
    if (!(c = tdb_open(name, hash_size, tdb_flags, open_flags, mode)))
        return NULL;

finish:
    pa_make_fd_cloexec(tdb_fd(c));
    return c;
}

const char* pa_database_get_filename_suffix(void) {
    return ".tdb";
}

pa_database* pa_database_open_internal(const char *path, bool for_write) {
    struct tdb_context *c;

    pa_assert(path);

    if ((c = tdb_open_cloexec(path, 0, TDB_NOSYNC|TDB_NOLOCK, (for_write ? O_RDWR|O_CREAT : O_RDONLY), 0644)))
        pa_log_debug("Opened TDB database '%s'", path);

    if (!c) {
        if (errno == 0)
            errno = EIO;
        return NULL;
    }

    return (pa_database*) c;
}

void pa_database_close(pa_database *db) {
    pa_assert(db);

    tdb_close(MAKE_TDB_CONTEXT(db));
}

pa_datum* pa_database_get(pa_database *db, const pa_datum *key, pa_datum* data) {
    TDB_DATA tdb_key, tdb_data;

    pa_assert(db);
    pa_assert(key);
    pa_assert(data);

    tdb_data = tdb_fetch(MAKE_TDB_CONTEXT(db), *datum_to_tdb(&tdb_key, key));

    return tdb_data.dptr ?
        datum_from_tdb(data, &tdb_data) :
        NULL;
}

int pa_database_set(pa_database *db, const pa_datum *key, const pa_datum* data, bool overwrite) {
    TDB_DATA tdb_key, tdb_data;

    pa_assert(db);
    pa_assert(key);
    pa_assert(data);

    return tdb_store(MAKE_TDB_CONTEXT(db),
                      *datum_to_tdb(&tdb_key, key),
                      *datum_to_tdb(&tdb_data, data),
                     overwrite ? TDB_REPLACE : TDB_INSERT) != 0 ? -1 : 0;
}

int pa_database_unset(pa_database *db, const pa_datum *key) {
    TDB_DATA tdb_key;

    pa_assert(db);
    pa_assert(key);

    return tdb_delete(MAKE_TDB_CONTEXT(db), *datum_to_tdb(&tdb_key, key)) != 0 ? -1 : 0;
}

int pa_database_clear(pa_database *db) {
    pa_assert(db);

    return tdb_wipe_all(MAKE_TDB_CONTEXT(db)) != 0 ? -1 : 0;
}

signed pa_database_size(pa_database *db) {
    TDB_DATA tdb_key;
    unsigned n = 0;

    pa_assert(db);

    /* This sucks */

    tdb_key = tdb_firstkey(MAKE_TDB_CONTEXT(db));

    while (tdb_key.dptr) {
        TDB_DATA next;

        n++;

        next = tdb_nextkey(MAKE_TDB_CONTEXT(db), tdb_key);
        free(tdb_key.dptr);
        tdb_key = next;
    }

    return (signed) n;
}

pa_datum* pa_database_first(pa_database *db, pa_datum *key, pa_datum *data) {
    TDB_DATA tdb_key, tdb_data;

    pa_assert(db);
    pa_assert(key);

    tdb_key = tdb_firstkey(MAKE_TDB_CONTEXT(db));

    if (!tdb_key.dptr)
        return NULL;

    if (data) {
        tdb_data = tdb_fetch(MAKE_TDB_CONTEXT(db), tdb_key);

        if (!tdb_data.dptr) {
            free(tdb_key.dptr);
            return NULL;
        }

        datum_from_tdb(data, &tdb_data);
    }

    datum_from_tdb(key, &tdb_key);

    return key;
}

pa_datum* pa_database_next(pa_database *db, const pa_datum *key, pa_datum *next, pa_datum *data) {
    TDB_DATA tdb_key, tdb_data;

    pa_assert(db);
    pa_assert(key);

    tdb_key = tdb_nextkey(MAKE_TDB_CONTEXT(db), *datum_to_tdb(&tdb_key, key));

    if (!tdb_key.dptr)
        return NULL;

    if (data) {
        tdb_data = tdb_fetch(MAKE_TDB_CONTEXT(db), tdb_key);

        if (!tdb_data.dptr) {
            free(tdb_key.dptr);
            return NULL;
        }

        datum_from_tdb(data, &tdb_data);
    }

    datum_from_tdb(next, &tdb_key);

    return next;
}

int pa_database_sync(pa_database *db) {
    pa_assert(db);

    return 0;
}
