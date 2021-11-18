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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/xmalloc.h>

#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "modargs.h"

struct pa_modargs {
    pa_hashmap *raw;
    pa_hashmap *unescaped;
};

struct entry {
    char *key, *value;
};

static int add_key_value(pa_modargs *ma, char *key, char *value, const char* const valid_keys[], bool ignore_dupes) {
    struct entry *e;
    char *raw;

    pa_assert(ma);
    pa_assert(ma->raw);
    pa_assert(ma->unescaped);
    pa_assert(key);
    pa_assert(value);

    if (pa_hashmap_get(ma->unescaped, key)) {
        pa_xfree(key);
        pa_xfree(value);

        if (ignore_dupes)
            return 0;
        else
            return -1;
    }

    if (valid_keys) {
        const char*const* v;
        for (v = valid_keys; *v; v++)
            if (pa_streq(*v, key))
                break;

        if (!*v) {
            pa_xfree(key);
            pa_xfree(value);
            return -1;
        }
    }

    raw = pa_xstrdup(value);

    e = pa_xnew(struct entry, 1);
    e->key = key;
    e->value = pa_unescape(value);
    pa_hashmap_put(ma->unescaped, key, e);

    if (pa_streq(raw, value))
        pa_xfree(raw);
    else {
        e = pa_xnew(struct entry, 1);
        e->key = pa_xstrdup(key);
        e->value = raw;
        pa_hashmap_put(ma->raw, key, e);
    }

    return 0;
}

static void free_func(void *p) {
    struct entry *e = p;
    pa_assert(e);

    pa_xfree(e->key);
    pa_xfree(e->value);
    pa_xfree(e);
}

static int parse(pa_modargs *ma, const char *args, const char* const* valid_keys, bool ignore_dupes) {
    enum {
        WHITESPACE,
        KEY,
        VALUE_START,
        VALUE_SIMPLE,
        VALUE_SIMPLE_ESCAPED,
        VALUE_DOUBLE_QUOTES,
        VALUE_DOUBLE_QUOTES_ESCAPED,
        VALUE_TICKS,
        VALUE_TICKS_ESCAPED
    } state;

    const char *p, *key = NULL, *value = NULL;
    size_t key_len = 0, value_len = 0;

    state = WHITESPACE;

    for (p = args; *p; p++) {
        switch (state) {

            case WHITESPACE:
                if (*p == '=')
                    goto fail;
                else if (!isspace((unsigned char)*p)) {
                    key = p;
                    state = KEY;
                    key_len = 1;
                }
                break;

            case KEY:
                if (*p == '=')
                    state = VALUE_START;
                else if (isspace((unsigned char)*p))
                    goto fail;
                else
                    key_len++;
                break;

            case VALUE_START:
                if (*p == '\'') {
                    state = VALUE_TICKS;
                    value = p+1;
                    value_len = 0;
                } else if (*p == '"') {
                    state = VALUE_DOUBLE_QUOTES;
                    value = p+1;
                    value_len = 0;
                } else if (isspace((unsigned char)*p)) {
                    if (add_key_value(ma,
                                      pa_xstrndup(key, key_len),
                                      pa_xstrdup(""),
                                      valid_keys,
                                      ignore_dupes) < 0)
                        goto fail;
                    state = WHITESPACE;
                } else if (*p == '\\') {
                    state = VALUE_SIMPLE_ESCAPED;
                    value = p;
                    value_len = 1;
                } else {
                    state = VALUE_SIMPLE;
                    value = p;
                    value_len = 1;
                }
                break;

            case VALUE_SIMPLE:
                if (isspace((unsigned char)*p)) {
                    if (add_key_value(ma,
                                      pa_xstrndup(key, key_len),
                                      pa_xstrndup(value, value_len),
                                      valid_keys,
                                      ignore_dupes) < 0)
                        goto fail;
                    state = WHITESPACE;
                } else if (*p == '\\') {
                    state = VALUE_SIMPLE_ESCAPED;
                    value_len++;
                } else
                    value_len++;
                break;

            case VALUE_SIMPLE_ESCAPED:
                state = VALUE_SIMPLE;
                value_len++;
                break;

            case VALUE_DOUBLE_QUOTES:
                if (*p == '"') {
                    if (add_key_value(ma,
                                      pa_xstrndup(key, key_len),
                                      pa_xstrndup(value, value_len),
                                      valid_keys,
                                      ignore_dupes) < 0)
                        goto fail;
                    state = WHITESPACE;
                } else if (*p == '\\') {
                    state = VALUE_DOUBLE_QUOTES_ESCAPED;
                    value_len++;
                } else
                    value_len++;
                break;

            case VALUE_DOUBLE_QUOTES_ESCAPED:
                state = VALUE_DOUBLE_QUOTES;
                value_len++;
                break;

            case VALUE_TICKS:
                if (*p == '\'') {
                    if (add_key_value(ma,
                                      pa_xstrndup(key, key_len),
                                      pa_xstrndup(value, value_len),
                                      valid_keys,
                                      ignore_dupes) < 0)
                        goto fail;
                    state = WHITESPACE;
                } else if (*p == '\\') {
                    state = VALUE_TICKS_ESCAPED;
                    value_len++;
                } else
                    value_len++;
                break;

            case VALUE_TICKS_ESCAPED:
                state = VALUE_TICKS;
                value_len++;
                break;
        }
    }

    if (state == VALUE_START) {
        if (add_key_value(ma, pa_xstrndup(key, key_len), pa_xstrdup(""), valid_keys, ignore_dupes) < 0)
            goto fail;
    } else if (state == VALUE_SIMPLE) {
        if (add_key_value(ma, pa_xstrndup(key, key_len), pa_xstrdup(value), valid_keys, ignore_dupes) < 0)
            goto fail;
    } else if (state != WHITESPACE)
        goto fail;

    return 0;

fail:
    return -1;
}

pa_modargs *pa_modargs_new(const char *args, const char* const* valid_keys) {
    pa_modargs *ma = pa_xnew(pa_modargs, 1);

    ma->raw = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, free_func);
    ma->unescaped = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, free_func);

    if (args && parse(ma, args, valid_keys, false) < 0)
        goto fail;

    return ma;

fail:
    pa_modargs_free(ma);
    return NULL;
}

int pa_modargs_append(pa_modargs *ma, const char *args, const char* const* valid_keys) {
    return parse(ma, args, valid_keys, true);
}

void pa_modargs_free(pa_modargs*ma) {
    pa_assert(ma);

    pa_hashmap_free(ma->raw);
    pa_hashmap_free(ma->unescaped);
    pa_xfree(ma);
}

const char *pa_modargs_get_value(pa_modargs *ma, const char *key, const char *def) {
    struct entry*e;

    pa_assert(ma);
    pa_assert(key);

    if (!(e = pa_hashmap_get(ma->unescaped, key)))
        return def;

    return e->value;
}

static const char *modargs_get_value_raw(pa_modargs *ma, const char *key, const char *def) {
    struct entry*e;

    pa_assert(ma);
    pa_assert(key);

    if (!(e = pa_hashmap_get(ma->raw, key)))
        if (!(e = pa_hashmap_get(ma->unescaped, key)))
            return def;

    return e->value;
}

int pa_modargs_get_value_u32(pa_modargs *ma, const char *key, uint32_t *value) {
    const char *v;

    pa_assert(value);

    if (!(v = pa_modargs_get_value(ma, key, NULL)))
        return 0;

    if (pa_atou(v, value) < 0)
        return -1;

    return 0;
}

int pa_modargs_get_value_s32(pa_modargs *ma, const char *key, int32_t *value) {
    const char *v;

    pa_assert(value);

    if (!(v = pa_modargs_get_value(ma, key, NULL)))
        return 0;

    if (pa_atoi(v, value) < 0)
        return -1;

    return 0;
}

int pa_modargs_get_value_boolean(pa_modargs *ma, const char *key, bool *value) {
    const char *v;
    int r;

    pa_assert(value);

    if (!(v = pa_modargs_get_value(ma, key, NULL)))
        return 0;

    if (!*v)
        return -1;

    if ((r = pa_parse_boolean(v)) < 0)
        return -1;

    *value = r;
    return 0;
}

int pa_modargs_get_value_double(pa_modargs *ma, const char *key, double *value) {
    const char *v;

    pa_assert(value);

    if (!(v = pa_modargs_get_value(ma, key, NULL)))
        return 0;

    if (pa_atod(v, value) < 0)
        return -1;

    return 0;
}

int pa_modargs_get_value_volume(pa_modargs *ma, const char *key, pa_volume_t *value) {
    const char *v;

    pa_assert(value);

    if (!(v = pa_modargs_get_value(ma, key, NULL)))
        return 0;

    if (pa_parse_volume(v, value) < 0)
        return -1;

    return 0;
}

int pa_modargs_get_sample_rate(pa_modargs *ma, uint32_t *rate) {
    uint32_t rate_local;

    pa_assert(rate);

    rate_local = *rate;
    if ((pa_modargs_get_value_u32(ma, "rate", &rate_local)) < 0 ||
        !pa_sample_rate_valid(rate_local))
        return -1;

    *rate = rate_local;

    return 0;
}

int pa_modargs_get_sample_spec(pa_modargs *ma, pa_sample_spec *rss) {
    const char *format;
    uint32_t channels;
    pa_sample_spec ss;

    pa_assert(rss);

    ss = *rss;
    if ((pa_modargs_get_sample_rate(ma, &ss.rate)) < 0)
        return -1;

    channels = ss.channels;
    if ((pa_modargs_get_value_u32(ma, "channels", &channels)) < 0 ||
        !pa_channels_valid(channels))
        return -1;
    ss.channels = (uint8_t) channels;

    if ((format = pa_modargs_get_value(ma, "format", NULL)))
        if ((ss.format = pa_parse_sample_format(format)) < 0)
            return -1;

    if (!pa_sample_spec_valid(&ss))
        return -1;

    *rss = ss;

    return 0;
}

int pa_modargs_get_alternate_sample_rate(pa_modargs *ma, uint32_t *alternate_rate) {
    uint32_t rate_local;

    pa_assert(alternate_rate);

    rate_local = *alternate_rate;
    if ((pa_modargs_get_value_u32(ma, "alternate_rate", &rate_local)) < 0 ||
        !pa_sample_rate_valid(*alternate_rate))
        return -1;

    *alternate_rate = rate_local;

    return 0;
}

int pa_modargs_get_channel_map(pa_modargs *ma, const char *name, pa_channel_map *rmap) {
    pa_channel_map map;
    const char *cm;

    pa_assert(rmap);

    map = *rmap;

    if ((cm = pa_modargs_get_value(ma, name ? name : "channel_map", NULL)))
        if (!pa_channel_map_parse(&map, cm))
            return -1;

    if (!pa_channel_map_valid(&map))
        return -1;

    *rmap = map;
    return 0;
}

int pa_modargs_get_resample_method(pa_modargs *ma, pa_resample_method_t *rmethod) {
    const char *m;

    pa_assert(ma);
    pa_assert(rmethod);

    if ((m = pa_modargs_get_value(ma, "resample_method", NULL))) {
        pa_resample_method_t method = pa_parse_resample_method(m);

        if (method == PA_RESAMPLER_INVALID)
            return -1;

        *rmethod = method;
    }

    return 0;
}

int pa_modargs_get_sample_spec_and_channel_map(
        pa_modargs *ma,
        pa_sample_spec *rss,
        pa_channel_map *rmap,
        pa_channel_map_def_t def) {

    pa_sample_spec ss;
    pa_channel_map map;

    pa_assert(rss);
    pa_assert(rmap);

    ss = *rss;

    if (pa_modargs_get_sample_spec(ma, &ss) < 0)
        return -1;

    map = *rmap;

    if (ss.channels != map.channels)
        pa_channel_map_init_extend(&map, ss.channels, def);

    if (pa_modargs_get_channel_map(ma, NULL, &map) < 0)
        return -1;

    if (map.channels != ss.channels) {
        if (!pa_modargs_get_value(ma, "channels", NULL))
            ss.channels = map.channels;
        else
            return -1;
    }

    *rmap = map;
    *rss = ss;

    return 0;
}

int pa_modargs_get_proplist(pa_modargs *ma, const char *name, pa_proplist *p, pa_update_mode_t m) {
    const char *v;
    pa_proplist *n;

    pa_assert(ma);
    pa_assert(name);
    pa_assert(p);

    if (!(v = modargs_get_value_raw(ma, name, NULL)))
        return 0;

    if (!(n = pa_proplist_from_string(v)))
        return -1;

    pa_proplist_update(p, m, n);
    pa_proplist_free(n);

    return 0;
}

const char *pa_modargs_iterate(pa_modargs *ma, void **state) {
    struct entry *e;

    pa_assert(ma);

    if (!(e = pa_hashmap_iterate(ma->unescaped, state, NULL)))
        return NULL;

    return e->key;
}
