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

#include <string.h>

#include <pulse/xmalloc.h>

#include <pulsecore/strbuf.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>

#include "strlist.h"

struct pa_strlist {
    pa_strlist *next;
};

#define ITEM_TO_TEXT(c) ((char*) (c) + PA_ALIGN(sizeof(pa_strlist)))

pa_strlist* pa_strlist_prepend(pa_strlist *l, const char *s) {
    pa_strlist *n;
    size_t size;

    pa_assert(s);
    size = strlen(s);
    n = pa_xmalloc(PA_ALIGN(sizeof(pa_strlist)) + size + 1);
    memcpy(ITEM_TO_TEXT(n), s, size + 1);
    n->next = l;

    return n;
}

char *pa_strlist_to_string(pa_strlist *l) {
    int first = 1;
    pa_strbuf *b;

    b = pa_strbuf_new();
    for (; l; l = l->next) {
        if (!first)
            pa_strbuf_puts(b, " ");
        first = 0;
        pa_strbuf_puts(b, ITEM_TO_TEXT(l));
    }

    return pa_strbuf_to_string_free(b);
}

pa_strlist* pa_strlist_remove(pa_strlist *l, const char *s) {
    pa_strlist *ret = l, *prev = NULL;

    pa_assert(l);
    pa_assert(s);

    while (l) {
        if (pa_streq(ITEM_TO_TEXT(l), s)) {
            pa_strlist *n = l->next;

            if (!prev) {
                pa_assert(ret == l);
                ret = n;
            } else
                prev->next = n;

            pa_xfree(l);

            l = n;

        } else {
            prev = l;
            l = l->next;
        }
    }

    return ret;
}

void pa_strlist_free(pa_strlist *l) {
    while (l) {
        pa_strlist *c = l;
        l = l->next;
        pa_xfree(c);
    }
}

pa_strlist* pa_strlist_pop(pa_strlist *l, char **s) {
    pa_strlist *r;

    pa_assert(s);

    if (!l) {
        *s = NULL;
        return NULL;
    }

    *s = pa_xstrdup(ITEM_TO_TEXT(l));
    r = l->next;
    pa_xfree(l);
    return r;
}

pa_strlist* pa_strlist_parse(const char *s) {
    pa_strlist *head = NULL, *p = NULL;
    const char *state = NULL;
    char *r;

    while ((r = pa_split_spaces(s, &state))) {
        pa_strlist *n;
        size_t size = strlen(r);

        n = pa_xmalloc(PA_ALIGN(sizeof(pa_strlist)) + size + 1);
        n->next = NULL;
        memcpy(ITEM_TO_TEXT(n), r, size+1);
        pa_xfree(r);

        if (p)
            p->next = n;
        else
            head = n;

        p = n;
    }

    return head;
}

pa_strlist *pa_strlist_reverse(pa_strlist *l) {
    pa_strlist *r = NULL;

    while (l) {
        pa_strlist *n;

        n = l->next;
        l->next = r;
        r = l;
        l = n;
    }

    return r;
}

pa_strlist *pa_strlist_next(pa_strlist *s) {
    pa_assert(s);

    return s->next;
}

const char *pa_strlist_data(pa_strlist *s) {
    pa_assert(s);

    return ITEM_TO_TEXT(s);
}
