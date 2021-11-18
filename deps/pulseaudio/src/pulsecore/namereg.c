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

#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <stdio.h>

#include <pulse/xmalloc.h>

#include <pulsecore/source.h>
#include <pulsecore/sink.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "namereg.h"

struct namereg_entry {
    pa_namereg_type_t type;
    char *name;
    void *data;
};

static bool is_valid_char(char c) {
    return
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '.' ||
        c == '-' ||
        c == '_';
}

bool pa_namereg_is_valid_name(const char *name) {
    const char *c;

    pa_assert(name);

    if (*name == 0)
        return false;

    for (c = name; *c && (c-name < PA_NAME_MAX); c++)
        if (!is_valid_char(*c))
            return false;

    if (*c)
        return false;

    return true;
}

bool pa_namereg_is_valid_name_or_wildcard(const char *name, pa_namereg_type_t type) {

    pa_assert(name);

    if (pa_namereg_is_valid_name(name))
        return true;

    if (type == PA_NAMEREG_SINK &&
        pa_streq(name, "@DEFAULT_SINK@"))
        return true;

    if (type == PA_NAMEREG_SOURCE &&
        (pa_streq(name, "@DEFAULT_SOURCE@") ||
         pa_streq(name, "@DEFAULT_MONITOR@")))
        return true;

    return false;
}

char* pa_namereg_make_valid_name(const char *name) {
    const char *a;
    char *b, *n;

    if (*name == 0)
        return NULL;

    n = pa_xnew(char, strlen(name)+1);

    for (a = name, b = n; *a && (a-name < PA_NAME_MAX); a++, b++)
        *b = (char) (is_valid_char(*a) ? *a : '_');

    *b = 0;

    return n;
}

const char *pa_namereg_register(pa_core *c, const char *name, pa_namereg_type_t type, void *data, bool fail) {
    struct namereg_entry *e;
    char *n = NULL;

    pa_assert(c);
    pa_assert(name);
    pa_assert(data);

    if (!*name)
        return NULL;

    if ((type == PA_NAMEREG_SINK || type == PA_NAMEREG_SOURCE || type == PA_NAMEREG_CARD) &&
        !pa_namereg_is_valid_name(name)) {

        if (fail)
            return NULL;

        if (!(name = n = pa_namereg_make_valid_name(name)))
            return NULL;
    }

    if ((e = pa_hashmap_get(c->namereg, name)) && fail) {
        pa_xfree(n);
        return NULL;
    }

    if (e) {
        unsigned i;
        size_t l = strlen(name);
        char *k;

        if (l+4 > PA_NAME_MAX) {
            pa_xfree(n);
            return NULL;
        }

        k = pa_xmalloc(l+4);

        for (i = 2; i <= 99; i++) {
            pa_snprintf(k, l+4, "%s.%u", name, i);

            if (!(e = pa_hashmap_get(c->namereg, k)))
                break;
        }

        if (e) {
            pa_xfree(n);
            pa_xfree(k);
            return NULL;
        }

        pa_xfree(n);
        n = k;
    }

    e = pa_xnew(struct namereg_entry, 1);
    e->type = type;
    e->name = n ? n : pa_xstrdup(name);
    e->data = data;

    pa_assert_se(pa_hashmap_put(c->namereg, e->name, e) >= 0);

    return e->name;
}

void pa_namereg_unregister(pa_core *c, const char *name) {
    struct namereg_entry *e;

    pa_assert(c);
    pa_assert(name);

    pa_assert_se(e = pa_hashmap_remove(c->namereg, name));
    pa_xfree(e->name);
    pa_xfree(e);
}

void* pa_namereg_get(pa_core *c, const char *name, pa_namereg_type_t type) {
    struct namereg_entry *e;
    uint32_t idx;
    pa_assert(c);

    if (type == PA_NAMEREG_SOURCE && (!name || pa_streq(name, "@DEFAULT_SOURCE@"))) {
        return c->default_source;

    } else if (type == PA_NAMEREG_SINK && (!name || pa_streq(name, "@DEFAULT_SINK@"))) {
        return c->default_sink;

    } else if (type == PA_NAMEREG_SOURCE && name && pa_streq(name, "@DEFAULT_MONITOR@")) {
        if (c->default_sink)
            return c->default_sink->monitor_source;
        else
            return NULL;
    }

    if (!name)
        return NULL;

    if ((type == PA_NAMEREG_SINK || type == PA_NAMEREG_SOURCE || type == PA_NAMEREG_CARD) &&
        !pa_namereg_is_valid_name(name))
        return NULL;

    if ((e = pa_hashmap_get(c->namereg, name)))
        if (e->type == type)
            return e->data;

    if (pa_atou(name, &idx) < 0)
        return NULL;

    if (type == PA_NAMEREG_SINK)
        return pa_idxset_get_by_index(c->sinks, idx);
    else if (type == PA_NAMEREG_SOURCE)
        return pa_idxset_get_by_index(c->sources, idx);
    else if (type == PA_NAMEREG_SAMPLE && c->scache)
        return pa_idxset_get_by_index(c->scache, idx);
    else if (type == PA_NAMEREG_CARD)
        return pa_idxset_get_by_index(c->cards, idx);

    return NULL;
}
