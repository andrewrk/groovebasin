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

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>

#include "shared.h"

typedef struct pa_shared {
    char *name;  /* Points to memory allocated by the shared property system */
    void *data;  /* Points to memory maintained by the caller */
} pa_shared;

/* Allocate a new shared property object */
static pa_shared* shared_new(const char *name, void *data) {
    pa_shared* p;

    pa_assert(name);
    pa_assert(data);

    p = pa_xnew(pa_shared, 1);
    p->name = pa_xstrdup(name);
    p->data = data;

    return p;
}

/* Free a shared property object */
static void shared_free(pa_shared *p) {
    pa_assert(p);

    pa_xfree(p->name);
    pa_xfree(p);
}

void* pa_shared_get(pa_core *c, const char *name) {
    pa_shared *p;

    pa_assert(c);
    pa_assert(name);
    pa_assert(c->shared);

    if (!(p = pa_hashmap_get(c->shared, name)))
        return NULL;

    return p->data;
}

int pa_shared_set(pa_core *c, const char *name, void *data) {
    pa_shared *p;

    pa_assert(c);
    pa_assert(name);
    pa_assert(data);
    pa_assert(c->shared);

    if (pa_hashmap_get(c->shared, name))
        return -1;

    p = shared_new(name, data);
    pa_hashmap_put(c->shared, p->name, p);
    return 0;
}

int pa_shared_remove(pa_core *c, const char *name) {
    pa_shared *p;

    pa_assert(c);
    pa_assert(name);
    pa_assert(c->shared);

    if (!(p = pa_hashmap_remove(c->shared, name)))
        return -1;

    shared_free(p);
    return 0;
}

void pa_shared_dump(pa_core *c, pa_strbuf *s) {
    void *state = NULL;
    pa_shared *p;

    pa_assert(c);
    pa_assert(s);

    while ((p = pa_hashmap_iterate(c->shared, &state, NULL)))
        pa_strbuf_printf(s, "[%s] -> [%p]\n", p->name, p->data);
}

int pa_shared_replace(pa_core *c, const char *name, void *data) {
    pa_assert(c);
    pa_assert(name);

    (void) pa_shared_remove(c, name);
    return pa_shared_set(c, name, data);
}
