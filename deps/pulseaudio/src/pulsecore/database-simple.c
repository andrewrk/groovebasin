/***
  This file is part of PulseAudio.

  Copyright 2009 Nokia Corporation
  Contact: Maemo Multimedia <multimedia@maemo.org>

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
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#include <pulse/xmalloc.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/core-error.h>
#include <pulsecore/hashmap.h>

#include "database.h"

typedef struct simple_data {
    char *filename;
    char *tmp_filename;
    pa_hashmap *map;
    bool read_only;
} simple_data;

typedef struct entry {
    pa_datum key;
    pa_datum data;
} entry;

void pa_datum_free(pa_datum *d) {
    pa_assert(d);

    pa_xfree(d->data);
    d->data = NULL;
    d->size = 0;
}

static int compare_func(const void *a, const void *b) {
    const pa_datum *aa, *bb;

    aa = (const pa_datum*)a;
    bb = (const pa_datum*)b;

    if (aa->size != bb->size)
        return aa->size > bb->size ? 1 : -1;

    return memcmp(aa->data, bb->data, aa->size);
}

/* pa_idxset_string_hash_func modified for our use */
static unsigned hash_func(const void *p) {
    const pa_datum *d;
    unsigned hash = 0;
    const char *c;
    unsigned i;

    d = (const pa_datum*)p;
    c = d->data;

    for (i = 0; i < d->size; i++) {
        hash = 31 * hash + (unsigned) *c;
        c++;
    }

    return hash;
}

static entry* new_entry(const pa_datum *key, const pa_datum *data) {
    entry *e;

    pa_assert(key);
    pa_assert(data);

    e = pa_xnew0(entry, 1);
    e->key.data = key->size > 0 ? pa_xmemdup(key->data, key->size) : NULL;
    e->key.size = key->size;
    e->data.data = data->size > 0 ? pa_xmemdup(data->data, data->size) : NULL;
    e->data.size = data->size;
    return e;
}

static void free_entry(entry *e) {
    if (e) {
        if (e->key.data)
            pa_xfree(e->key.data);
        if (e->data.data)
            pa_xfree(e->data.data);
        pa_xfree(e);
    }
}

static int read_uint(FILE *f, uint32_t *res) {
    size_t items = 0;
    uint8_t values[4];
    uint32_t tmp;
    int i;

    items = fread(&values, sizeof(values), sizeof(uint8_t), f);

    if (feof(f)) /* EOF */
        return 0;

    if (ferror(f))
        return -1;

    for (i = 0; i < 4; ++i) {
        tmp = values[i];
        *res += (tmp << (i*8));
    }

    return items;
}

static int read_data(FILE *f, void **data, ssize_t *length) {
    size_t items = 0;
    uint32_t data_len = 0;

    pa_assert(f);

    *data = NULL;
    *length = 0;

    if ((items = read_uint(f, &data_len)) <= 0)
        return -1;

    if (data_len > 0) {
        *data = pa_xmalloc0(data_len);
        items = fread(*data, data_len, 1, f);

        if (feof(f)) /* EOF */
            goto reset;

        if (ferror(f))
            goto reset;

        *length = data_len;

    } else { /* no data? */
        return -1;
    }

    return 0;

reset:
    pa_xfree(*data);
    *data = NULL;
    *length = 0;
    return -1;
}

static int fill_data(simple_data *db, FILE *f) {
    pa_datum key;
    pa_datum data;
    void *d = NULL;
    ssize_t l = 0;
    bool append = false;
    enum { FIELD_KEY = 0, FIELD_DATA } field = FIELD_KEY;

    pa_assert(db);
    pa_assert(db->map);

    errno = 0;

    key.size = 0;
    key.data = NULL;

    while (!read_data(f, &d, &l)) {

        switch (field) {
            case FIELD_KEY:
                key.data = d;
                key.size = l;
                field = FIELD_DATA;
                break;
            case FIELD_DATA:
                data.data = d;
                data.size = l;
                append = true;
                break;
        }

        if (append) {
            entry *e = pa_xnew0(entry, 1);
            e->key.data = key.data;
            e->key.size = key.size;
            e->data.data = data.data;
            e->data.size = data.size;
            pa_hashmap_put(db->map, &e->key, e);
            append = false;
            field = FIELD_KEY;
        }
    }

    if (ferror(f)) {
        pa_log_warn("read error. %s", pa_cstrerror(errno));
        pa_database_clear((pa_database*)db);
    }

    if (field == FIELD_DATA && d)
        pa_xfree(d);

    return pa_hashmap_size(db->map);
}

const char* pa_database_get_filename_suffix(void) {
    return ".simple";
}

pa_database* pa_database_open_internal(const char *path, bool for_write) {
    FILE *f;
    simple_data *db;

    pa_assert(path);

    errno = 0;

    f = pa_fopen_cloexec(path, "r");

    if (f || errno == ENOENT) { /* file not found is ok */
        db = pa_xnew0(simple_data, 1);
        db->map = pa_hashmap_new_full(hash_func, compare_func, NULL, (pa_free_cb_t) free_entry);
        db->filename = pa_xstrdup(path);
        db->tmp_filename = pa_sprintf_malloc(".%s.tmp", db->filename);
        db->read_only = !for_write;

        if (f) {
            fill_data(db, f);
            fclose(f);
        }
    } else {
        if (errno == 0)
            errno = EIO;
        db = NULL;
    }

    return (pa_database*) db;
}

void pa_database_close(pa_database *database) {
    simple_data *db = (simple_data*)database;
    pa_assert(db);

    pa_database_sync(database);
    pa_xfree(db->filename);
    pa_xfree(db->tmp_filename);
    pa_hashmap_free(db->map);
    pa_xfree(db);
}

pa_datum* pa_database_get(pa_database *database, const pa_datum *key, pa_datum* data) {
    simple_data *db = (simple_data*)database;
    entry *e;

    pa_assert(db);
    pa_assert(key);
    pa_assert(data);

    e = pa_hashmap_get(db->map, key);

    if (!e)
        return NULL;

    data->data = e->data.size > 0 ? pa_xmemdup(e->data.data, e->data.size) : NULL;
    data->size = e->data.size;

    return data;
}

int pa_database_set(pa_database *database, const pa_datum *key, const pa_datum* data, bool overwrite) {
    simple_data *db = (simple_data*)database;
    entry *e;
    int ret = 0;

    pa_assert(db);
    pa_assert(key);
    pa_assert(data);

    if (db->read_only)
        return -1;

    e = new_entry(key, data);

    if (pa_hashmap_put(db->map, &e->key, e) < 0) {
        /* entry with same key exists in hashmap */
        entry *r;
        if (overwrite) {
            r = pa_hashmap_remove(db->map, key);
            pa_hashmap_put(db->map, &e->key, e);
        } else {
            /* won't overwrite, so clean new entry */
            r = e;
            ret = -1;
        }

        free_entry(r);
    }

    return ret;
}

int pa_database_unset(pa_database *database, const pa_datum *key) {
    simple_data *db = (simple_data*)database;

    pa_assert(db);
    pa_assert(key);

    return pa_hashmap_remove_and_free(db->map, key);
}

int pa_database_clear(pa_database *database) {
    simple_data *db = (simple_data*)database;

    pa_assert(db);

    pa_hashmap_remove_all(db->map);

    return 0;
}

signed pa_database_size(pa_database *database) {
    simple_data *db = (simple_data*)database;
    pa_assert(db);

    return (signed) pa_hashmap_size(db->map);
}

pa_datum* pa_database_first(pa_database *database, pa_datum *key, pa_datum *data) {
    simple_data *db = (simple_data*)database;
    entry *e;

    pa_assert(db);
    pa_assert(key);

    e = pa_hashmap_first(db->map);

    if (!e)
        return NULL;

    key->data = e->key.size > 0 ? pa_xmemdup(e->key.data, e->key.size) : NULL;
    key->size = e->key.size;

    if (data) {
        data->data = e->data.size > 0 ? pa_xmemdup(e->data.data, e->data.size) : NULL;
        data->size = e->data.size;
    }

    return key;
}

pa_datum* pa_database_next(pa_database *database, const pa_datum *key, pa_datum *next, pa_datum *data) {
    simple_data *db = (simple_data*)database;
    entry *e;
    entry *search;
    void *state;
    bool pick_now;

    pa_assert(db);
    pa_assert(next);

    if (!key)
        return pa_database_first(database, next, data);

    search = pa_hashmap_get(db->map, key);

    state = NULL;
    pick_now = false;

    while ((e = pa_hashmap_iterate(db->map, &state, NULL))) {
        if (pick_now)
            break;

        if (search == e)
            pick_now = true;
    }

    if (!pick_now || !e)
        return NULL;

    next->data = e->key.size > 0 ? pa_xmemdup(e->key.data, e->key.size) : NULL;
    next->size = e->key.size;

    if (data) {
        data->data = e->data.size > 0 ? pa_xmemdup(e->data.data, e->data.size) : NULL;
        data->size = e->data.size;
    }

    return next;
}

static int write_uint(FILE *f, const uint32_t num) {
    size_t items;
    uint8_t values[4];
    int i;
    errno = 0;

    for (i = 0; i < 4; i++)
        values[i] = (num >> (i*8)) & 0xFF;

    items = fwrite(&values, sizeof(values), sizeof(uint8_t), f);

    if (ferror(f))
        return -1;

    return items;
}

static int write_data(FILE *f, void *data, const size_t length) {
    size_t items;
    uint32_t len;

    len = length;
    if ((items = write_uint(f, len)) <= 0)
        return -1;

    items = fwrite(data, length, 1, f);

    if (ferror(f) || items != 1)
        return -1;

    return 0;
}

static int write_entry(FILE *f, const entry *e) {
    pa_assert(f);
    pa_assert(e);

    if (write_data(f, e->key.data, e->key.size) < 0)
        return -1;
    if (write_data(f, e->data.data, e->data.size) < 0)
        return -1;

    return 0;
}

int pa_database_sync(pa_database *database) {
    simple_data *db = (simple_data*)database;
    FILE *f;
    void *state;
    entry *e;

    pa_assert(db);

    if (db->read_only)
        return 0;

    errno = 0;

    f = pa_fopen_cloexec(db->tmp_filename, "w");

    if (!f)
        goto fail;

    state = NULL;
    while((e = pa_hashmap_iterate(db->map, &state, NULL))) {
        if (write_entry(f, e) < 0) {
            pa_log_warn("error while writing to file. %s", pa_cstrerror(errno));
            goto fail;
        }
    }

    fclose(f);
    f = NULL;

    if (rename(db->tmp_filename, db->filename) < 0) {
        pa_log_warn("error while renaming file. %s", pa_cstrerror(errno));
        goto fail;
    }

    return 0;

fail:
    if (f)
        fclose(f);
    return -1;
}
