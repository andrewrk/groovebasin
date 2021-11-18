/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdarg.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/socket.h>
#include <pulsecore/macro.h>
#include <pulsecore/flist.h>

#include "tagstruct.h"

#define MAX_TAG_SIZE (64*1024)
#define MAX_APPENDED_SIZE 128
#define GROW_TAG_SIZE 100

struct pa_tagstruct {
    uint8_t *data;
    size_t length, allocated;
    size_t rindex;

    enum {
        PA_TAGSTRUCT_FIXED, /* The tagstruct does not own the data, buffer was provided by caller. */
        PA_TAGSTRUCT_DYNAMIC, /* Buffer owned by tagstruct, data must be freed. */
        PA_TAGSTRUCT_APPENDED, /* Data points to appended buffer, used for small tagstructs. Will change to dynamic if needed. */
    } type;
    union {
        uint8_t appended[MAX_APPENDED_SIZE];
    } per_type;
};

PA_STATIC_FLIST_DECLARE(tagstructs, 0, pa_xfree);

pa_tagstruct *pa_tagstruct_new(void) {
    pa_tagstruct*t;

    if (!(t = pa_flist_pop(PA_STATIC_FLIST_GET(tagstructs))))
        t = pa_xnew(pa_tagstruct, 1);
    t->data = t->per_type.appended;
    t->allocated = MAX_APPENDED_SIZE;
    t->length = t->rindex = 0;
    t->type = PA_TAGSTRUCT_APPENDED;

    return t;
}

pa_tagstruct *pa_tagstruct_new_fixed(const uint8_t* data, size_t length) {
    pa_tagstruct*t;

    pa_assert(data && length);

    if (!(t = pa_flist_pop(PA_STATIC_FLIST_GET(tagstructs))))
        t = pa_xnew(pa_tagstruct, 1);
    t->data = (uint8_t*) data;
    t->allocated = t->length = length;
    t->rindex = 0;
    t->type = PA_TAGSTRUCT_FIXED;

    return t;
}

void pa_tagstruct_free(pa_tagstruct*t) {
    pa_assert(t);

    if (t->type == PA_TAGSTRUCT_DYNAMIC)
        pa_xfree(t->data);
    if (pa_flist_push(PA_STATIC_FLIST_GET(tagstructs), t) < 0)
        pa_xfree(t);
}

static inline void extend(pa_tagstruct*t, size_t l) {
    pa_assert(t);
    pa_assert(t->type != PA_TAGSTRUCT_FIXED);

    if (t->length+l <= t->allocated)
        return;

    if (t->type == PA_TAGSTRUCT_DYNAMIC)
        t->data = pa_xrealloc(t->data, t->allocated = t->length + l + GROW_TAG_SIZE);
    else if (t->type == PA_TAGSTRUCT_APPENDED) {
        t->type = PA_TAGSTRUCT_DYNAMIC;
        t->data = pa_xmalloc(t->allocated = t->length + l + GROW_TAG_SIZE);
        memcpy(t->data, t->per_type.appended, t->length);
    }
}

static void write_u8(pa_tagstruct *t, uint8_t u) {
    extend(t, 1);
    t->data[t->length++] = u;
}

static int read_u8(pa_tagstruct *t, uint8_t *u) {
    if (t->rindex + 1 > t->length)
        return -1;

    *u = t->data[t->rindex++];
    return 0;
}

static void write_u32(pa_tagstruct *t, uint32_t u) {
    extend(t, 4);
    u = htonl(u);
    memcpy(t->data + t->length, &u, 4);
    t->length += 4;
}

static int read_u32(pa_tagstruct *t, uint32_t *u) {
    if (t->rindex + 4 > t->length)
        return -1;

    memcpy(u, t->data + t->rindex, 4);
    *u = ntohl(*u);
    t->rindex += 4;

    return 0;
}

static void write_u64(pa_tagstruct *t, uint64_t u) {
    write_u32(t, u >> 32);
    write_u32(t, u);
}

static int read_u64(pa_tagstruct *t, uint64_t *u) {
    uint32_t tmp;

    if (read_u32(t, &tmp) < 0)
        return -1;

    *u = ((uint64_t) tmp) << 32;

    if (read_u32(t, &tmp) < 0)
        return -1;

    *u |= tmp;
    return 0;
}

static int read_s64(pa_tagstruct *t, int64_t *u) {
    uint32_t tmp;

    if (read_u32(t, &tmp) < 0)
        return -1;

    *u = (int64_t) (((uint64_t) tmp) << 32);

    if (read_u32(t, &tmp) < 0)
        return -1;

    *u |= (int64_t) tmp;
    return 0;
}

static void write_arbitrary(pa_tagstruct *t, const void *p, size_t len) {
    extend(t, len);

    if (len > 0)
        memcpy(t->data + t->length, p, len);

    t->length += len;
}

static int read_arbitrary(pa_tagstruct *t, const void **p, size_t length) {
    if (t->rindex + length > t->length)
        return -1;

    *p = t->data + t->rindex;
    t->rindex += length;
    return 0;
}

void pa_tagstruct_puts(pa_tagstruct*t, const char *s) {
    size_t l;
    pa_assert(t);

    if (s) {
        write_u8(t, PA_TAG_STRING);
        l = strlen(s)+1;
        write_arbitrary(t, s, l);
    } else
        write_u8(t, PA_TAG_STRING_NULL);
}

void pa_tagstruct_putu32(pa_tagstruct*t, uint32_t i) {
    pa_assert(t);

    write_u8(t, PA_TAG_U32);
    write_u32(t, i);
}

void pa_tagstruct_putu8(pa_tagstruct*t, uint8_t c) {
    pa_assert(t);

    write_u8(t, PA_TAG_U8);
    write_u8(t, c);
}

void pa_tagstruct_put_sample_spec(pa_tagstruct *t, const pa_sample_spec *ss) {
    pa_assert(t);
    pa_assert(ss);

    write_u8(t, PA_TAG_SAMPLE_SPEC);
    write_u8(t, ss->format);
    write_u8(t, ss->channels);
    write_u32(t, ss->rate);
}

void pa_tagstruct_put_arbitrary(pa_tagstruct *t, const void *p, size_t length) {
    pa_assert(t);
    pa_assert(p);

    write_u8(t, PA_TAG_ARBITRARY);
    write_u32(t, length);
    write_arbitrary(t, p, length);
}

void pa_tagstruct_put_boolean(pa_tagstruct*t, bool b) {
    pa_assert(t);

    write_u8(t, b ? PA_TAG_BOOLEAN_TRUE : PA_TAG_BOOLEAN_FALSE);
}

void pa_tagstruct_put_timeval(pa_tagstruct*t, const struct timeval *tv) {
    pa_assert(t);

    write_u8(t, PA_TAG_TIMEVAL);
    write_u32(t, tv->tv_sec);
    write_u32(t, tv->tv_usec);
}

void pa_tagstruct_put_usec(pa_tagstruct*t, pa_usec_t u) {
    pa_assert(t);

    write_u8(t, PA_TAG_USEC);
    write_u64(t, u);
}

void pa_tagstruct_putu64(pa_tagstruct*t, uint64_t u) {
    pa_assert(t);

    write_u8(t, PA_TAG_U64);
    write_u64(t, u);
}

void pa_tagstruct_puts64(pa_tagstruct*t, int64_t u) {
    pa_assert(t);

    write_u8(t, PA_TAG_S64);
    write_u64(t, u);
}

void pa_tagstruct_put_channel_map(pa_tagstruct *t, const pa_channel_map *map) {
    unsigned i;

    pa_assert(t);
    pa_assert(map);

    write_u8(t, PA_TAG_CHANNEL_MAP);
    write_u8(t, map->channels);

    for (i = 0; i < map->channels; i ++)
        write_u8(t, map->map[i]);
}

void pa_tagstruct_put_cvolume(pa_tagstruct *t, const pa_cvolume *cvolume) {
    unsigned i;

    pa_assert(t);
    pa_assert(cvolume);

    write_u8(t, PA_TAG_CVOLUME);
    write_u8(t, cvolume->channels);

    for (i = 0; i < cvolume->channels; i ++)
        write_u32(t, cvolume->values[i]);
}

void pa_tagstruct_put_volume(pa_tagstruct *t, pa_volume_t vol) {
    pa_assert(t);

    write_u8(t, PA_TAG_VOLUME);
    write_u32(t, vol);
}

void pa_tagstruct_put_proplist(pa_tagstruct *t, const pa_proplist *p) {
    void *state = NULL;
    pa_assert(t);
    pa_assert(p);

    write_u8(t, PA_TAG_PROPLIST);

    for (;;) {
        const char *k;
        const void *d;
        size_t l;

        if (!(k = pa_proplist_iterate(p, &state)))
            break;

        pa_tagstruct_puts(t, k);
        pa_assert_se(pa_proplist_get(p, k, &d, &l) >= 0);
        pa_tagstruct_putu32(t, (uint32_t) l);
        pa_tagstruct_put_arbitrary(t, d, l);
    }

    pa_tagstruct_puts(t, NULL);
}

void pa_tagstruct_put_format_info(pa_tagstruct *t, const pa_format_info *f) {
    pa_assert(t);
    pa_assert(f);

    write_u8(t, PA_TAG_FORMAT_INFO);
    pa_tagstruct_putu8(t, (uint8_t) f->encoding);
    pa_tagstruct_put_proplist(t, f->plist);
}

static int read_tag(pa_tagstruct *t, uint8_t type) {
    if (t->rindex + 1 > t->length)
        return -1;

    if (t->data[t->rindex] != type)
        return -1;

    t->rindex++;

    return 0;
}

int pa_tagstruct_gets(pa_tagstruct*t, const char **s) {
    int error = 0;
    size_t n;
    char *c;

    pa_assert(t);
    pa_assert(s);

    if (t->rindex+1 > t->length)
        return -1;

    if (t->data[t->rindex] == PA_TAG_STRING_NULL) {
        t->rindex++;
        *s = NULL;
        return 0;
    }

    if (read_tag(t, PA_TAG_STRING) < 0)
        return -1;

    if (t->rindex + 1 > t->length)
        return -1;

    error = 1;
    for (n = 0, c = (char*) (t->data + t->rindex); t->rindex + n < t->length; n++, c++)
        if (!*c) {
            error = 0;
            break;
        }

    if (error)
        return -1;

    *s = (char*) (t->data + t->rindex);

    t->rindex += n + 1;
    return 0;
}

int pa_tagstruct_getu32(pa_tagstruct*t, uint32_t *i) {
    pa_assert(t);
    pa_assert(i);

    if (read_tag(t, PA_TAG_U32) < 0)
        return -1;

    return read_u32(t, i);
}

int pa_tagstruct_getu8(pa_tagstruct*t, uint8_t *c) {
    pa_assert(t);
    pa_assert(c);

    if (read_tag(t, PA_TAG_U8) < 0)
        return -1;

    return read_u8(t, c);
}

int pa_tagstruct_get_sample_spec(pa_tagstruct *t, pa_sample_spec *ss) {
    uint8_t tmp;

    pa_assert(t);
    pa_assert(ss);

    if (read_tag(t, PA_TAG_SAMPLE_SPEC) < 0)
        return -1;

    if (read_u8(t, &tmp) < 0)
        return -1;

    ss->format = tmp;

    if (read_u8(t, &ss->channels) < 0)
        return -1;

    return read_u32(t, &ss->rate);
}

int pa_tagstruct_get_arbitrary(pa_tagstruct *t, const void **p, size_t length) {
    uint32_t len;

    pa_assert(t);
    pa_assert(p);

    if (read_tag(t, PA_TAG_ARBITRARY) < 0)
        return -1;

    if (read_u32(t, &len) < 0 || len != length)
        return -1;

    return read_arbitrary(t, p, length);
}

int pa_tagstruct_eof(pa_tagstruct*t) {
    pa_assert(t);

    return t->rindex >= t->length;
}

const uint8_t* pa_tagstruct_data(pa_tagstruct*t, size_t *l) {
    pa_assert(t);
    pa_assert(l);

    *l = t->length;
    return t->data;
}

int pa_tagstruct_get_boolean(pa_tagstruct*t, bool *b) {
    pa_assert(t);
    pa_assert(b);

    if (t->rindex+1 > t->length)
        return -1;

    if (t->data[t->rindex] == PA_TAG_BOOLEAN_TRUE)
        *b = true;
    else if (t->data[t->rindex] == PA_TAG_BOOLEAN_FALSE)
        *b = false;
    else
        return -1;

    t->rindex +=1;
    return 0;
}

int pa_tagstruct_get_timeval(pa_tagstruct*t, struct timeval *tv) {
    uint32_t tmp;

    pa_assert(t);
    pa_assert(tv);

    if (read_tag(t, PA_TAG_TIMEVAL) < 0)
        return -1;

    if (read_u32(t, &tmp) < 0)
        return -1;

    tv->tv_sec = tmp;

    if (read_u32(t, &tmp) < 0)
        return -1;

    tv->tv_usec = tmp;

    return 0;
}

int pa_tagstruct_get_usec(pa_tagstruct*t, pa_usec_t *u) {
    pa_assert(t);
    pa_assert(u);

    if (read_tag(t, PA_TAG_USEC) < 0)
        return -1;

    return read_u64(t, u);
}

int pa_tagstruct_getu64(pa_tagstruct*t, uint64_t *u) {
    pa_assert(t);
    pa_assert(u);

    if (read_tag(t, PA_TAG_U64) < 0)
        return -1;

    return read_u64(t, u);
}

int pa_tagstruct_gets64(pa_tagstruct*t, int64_t *u) {
    pa_assert(t);
    pa_assert(u);

    if (read_tag(t, PA_TAG_S64) < 0)
        return -1;

    return read_s64(t, u);
}

int pa_tagstruct_get_channel_map(pa_tagstruct *t, pa_channel_map *map) {
    unsigned i;

    pa_assert(t);
    pa_assert(map);

    if (read_tag(t, PA_TAG_CHANNEL_MAP) < 0)
        return -1;

    if (read_u8(t, &map->channels) < 0 || map->channels > PA_CHANNELS_MAX)
        return -1;

    for (i = 0; i < map->channels; i ++) {
        uint8_t tmp;

        if (read_u8(t, &tmp) < 0)
            return -1;

        map->map[i] = tmp;
    }

    return 0;
}

int pa_tagstruct_get_cvolume(pa_tagstruct *t, pa_cvolume *cvolume) {
    unsigned i;

    pa_assert(t);
    pa_assert(cvolume);

    if (read_tag(t, PA_TAG_CVOLUME) < 0)
        return -1;

    if (read_u8(t, &cvolume->channels) < 0 || cvolume->channels > PA_CHANNELS_MAX)
        return -1;

    for (i = 0; i < cvolume->channels; i ++) {
        if (read_u32(t, &cvolume->values[i]) < 0)
            return -1;
    }

    return 0;
}

int pa_tagstruct_get_volume(pa_tagstruct*t, pa_volume_t *vol) {
    pa_assert(t);
    pa_assert(vol);

    if (read_tag(t, PA_TAG_VOLUME) < 0)
        return -1;

    return read_u32(t, vol);
}

int pa_tagstruct_get_proplist(pa_tagstruct *t, pa_proplist *p) {
    pa_assert(t);

    if (read_tag(t, PA_TAG_PROPLIST) < 0)
        return -1;

    for (;;) {
        const char *k;
        const void *d;
        uint32_t length;

        if (pa_tagstruct_gets(t, &k) < 0)
            return -1;

        if (!k)
            break;

        if (!pa_proplist_key_valid(k))
            return -1;

        if (pa_tagstruct_getu32(t, &length) < 0)
            return -1;

        if (length > MAX_TAG_SIZE)
            return -1;

        if (pa_tagstruct_get_arbitrary(t, &d, length) < 0)
            return -1;

        if (p)
            pa_assert_se(pa_proplist_set(p, k, d, length) >= 0);
    }

    return 0;
}

int pa_tagstruct_get_format_info(pa_tagstruct *t, pa_format_info *f) {
    uint8_t encoding;

    pa_assert(t);
    pa_assert(f);

    if (read_tag(t, PA_TAG_FORMAT_INFO) < 0)
        return -1;

    if (pa_tagstruct_getu8(t, &encoding) < 0)
        return -1;

    f->encoding = encoding;

    return pa_tagstruct_get_proplist(t, f->plist);
}

void pa_tagstruct_put(pa_tagstruct *t, ...) {
    va_list va;
    pa_assert(t);

    va_start(va, t);

    for (;;) {
        int tag = va_arg(va, int);

        if (tag == PA_TAG_INVALID)
            break;

        switch (tag) {
            case PA_TAG_STRING:
            case PA_TAG_STRING_NULL:
                pa_tagstruct_puts(t, va_arg(va, char*));
                break;

            case PA_TAG_U32:
                pa_tagstruct_putu32(t, va_arg(va, uint32_t));
                break;

            case PA_TAG_U8:
                pa_tagstruct_putu8(t, (uint8_t) va_arg(va, int));
                break;

            case PA_TAG_U64:
                pa_tagstruct_putu64(t, va_arg(va, uint64_t));
                break;

            case PA_TAG_SAMPLE_SPEC:
                pa_tagstruct_put_sample_spec(t, va_arg(va, pa_sample_spec*));
                break;

            case PA_TAG_ARBITRARY: {
                void *p = va_arg(va, void*);
                size_t size = va_arg(va, size_t);
                pa_tagstruct_put_arbitrary(t, p, size);
                break;
            }

            case PA_TAG_BOOLEAN_TRUE:
            case PA_TAG_BOOLEAN_FALSE:
                pa_tagstruct_put_boolean(t, va_arg(va, int));
                break;

            case PA_TAG_TIMEVAL:
                pa_tagstruct_put_timeval(t, va_arg(va, struct timeval*));
                break;

            case PA_TAG_USEC:
                pa_tagstruct_put_usec(t, va_arg(va, pa_usec_t));
                break;

            case PA_TAG_CHANNEL_MAP:
                pa_tagstruct_put_channel_map(t, va_arg(va, pa_channel_map *));
                break;

            case PA_TAG_CVOLUME:
                pa_tagstruct_put_cvolume(t, va_arg(va, pa_cvolume *));
                break;

            case PA_TAG_VOLUME:
                pa_tagstruct_put_volume(t, va_arg(va, pa_volume_t));
                break;

            case PA_TAG_PROPLIST:
                pa_tagstruct_put_proplist(t, va_arg(va, pa_proplist *));
                break;

            default:
                pa_assert_not_reached();
        }
    }

    va_end(va);
}

int pa_tagstruct_get(pa_tagstruct *t, ...) {
    va_list va;
    int ret = 0;

    pa_assert(t);

    va_start(va, t);
    while (ret == 0) {
        int tag = va_arg(va, int);

        if (tag == PA_TAG_INVALID)
            break;

        switch (tag) {
            case PA_TAG_STRING:
            case PA_TAG_STRING_NULL:
                ret = pa_tagstruct_gets(t, va_arg(va, const char**));
                break;

            case PA_TAG_U32:
                ret = pa_tagstruct_getu32(t, va_arg(va, uint32_t*));
                break;

            case PA_TAG_U8:
                ret = pa_tagstruct_getu8(t, va_arg(va, uint8_t*));
                break;

            case PA_TAG_U64:
                ret = pa_tagstruct_getu64(t, va_arg(va, uint64_t*));
                break;

            case PA_TAG_SAMPLE_SPEC:
                ret = pa_tagstruct_get_sample_spec(t, va_arg(va, pa_sample_spec*));
                break;

            case PA_TAG_ARBITRARY: {
                const void **p = va_arg(va, const void**);
                size_t size = va_arg(va, size_t);
                ret = pa_tagstruct_get_arbitrary(t, p, size);
                break;
            }

            case PA_TAG_BOOLEAN_TRUE:
            case PA_TAG_BOOLEAN_FALSE:
                ret = pa_tagstruct_get_boolean(t, va_arg(va, bool*));
                break;

            case PA_TAG_TIMEVAL:
                ret = pa_tagstruct_get_timeval(t, va_arg(va, struct timeval*));
                break;

            case PA_TAG_USEC:
                ret = pa_tagstruct_get_usec(t, va_arg(va, pa_usec_t*));
                break;

            case PA_TAG_CHANNEL_MAP:
                ret = pa_tagstruct_get_channel_map(t, va_arg(va, pa_channel_map *));
                break;

            case PA_TAG_CVOLUME:
                ret = pa_tagstruct_get_cvolume(t, va_arg(va, pa_cvolume *));
                break;

            case PA_TAG_VOLUME:
                ret = pa_tagstruct_get_volume(t, va_arg(va, pa_volume_t *));
                break;

            case PA_TAG_PROPLIST:
                ret = pa_tagstruct_get_proplist(t, va_arg(va, pa_proplist *));
                break;

            default:
                pa_assert_not_reached();
        }

    }

    va_end(va);
    return ret;
}
