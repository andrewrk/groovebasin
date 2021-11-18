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
#include <stdlib.h>

#include <pulse/xmalloc.h>

#include <pulsecore/dynarray.h>
#include <pulsecore/macro.h>

#include "tokenizer.h"

static void parse(pa_dynarray*a, const char *s, unsigned args) {
    int infty = 0;
    const char delimiter[] = " \t\n\r";
    const char *p;

    pa_assert(a);
    pa_assert(s);

    if (args == 0)
        infty = 1;

    p = s+strspn(s, delimiter);
    while (*p && (infty || args >= 2)) {
        size_t l = strcspn(p, delimiter);
        char *n = pa_xstrndup(p, l);
        pa_dynarray_append(a, n);
        p += l;
        p += strspn(p, delimiter);
        args--;
    }

    if (args && *p) {
        char *n = pa_xstrdup(p);
        pa_dynarray_append(a, n);
    }
}

pa_tokenizer* pa_tokenizer_new(const char *s, unsigned args) {
    pa_dynarray *a;

    a = pa_dynarray_new(pa_xfree);
    parse(a, s, args);
    return (pa_tokenizer*) a;
}

void pa_tokenizer_free(pa_tokenizer *t) {
    pa_dynarray *a = (pa_dynarray*) t;

    pa_assert(a);
    pa_dynarray_free(a);
}

const char *pa_tokenizer_get(pa_tokenizer *t, unsigned i) {
    pa_dynarray *a = (pa_dynarray*) t;

    pa_assert(a);

    return pa_dynarray_get(a, i);
}
