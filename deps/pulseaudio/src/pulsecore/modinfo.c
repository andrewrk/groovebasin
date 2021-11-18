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
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ltdl.h>

#include <pulse/xmalloc.h>

#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/ltdl-helper.h>

#include "modinfo.h"

#define PA_SYMBOL_AUTHOR "pa__get_author"
#define PA_SYMBOL_DESCRIPTION "pa__get_description"
#define PA_SYMBOL_USAGE "pa__get_usage"
#define PA_SYMBOL_VERSION "pa__get_version"
#define PA_SYMBOL_DEPRECATED "pa__get_deprecated"
#define PA_SYMBOL_LOAD_ONCE "pa__load_once"

pa_modinfo *pa_modinfo_get_by_handle(lt_dlhandle dl, const char *module_name) {
    pa_modinfo *i;
    const char* (*func)(void);
    bool (*func2) (void);

    pa_assert(dl);

    i = pa_xnew0(pa_modinfo, 1);

    if ((func = (const char* (*)(void)) pa_load_sym(dl, module_name, PA_SYMBOL_AUTHOR)))
        i->author = pa_xstrdup(func());

    if ((func = (const char* (*)(void)) pa_load_sym(dl, module_name, PA_SYMBOL_DESCRIPTION)))
        i->description = pa_xstrdup(func());

    if ((func = (const char* (*)(void)) pa_load_sym(dl, module_name, PA_SYMBOL_USAGE)))
        i->usage = pa_xstrdup(func());

    if ((func = (const char* (*)(void)) pa_load_sym(dl, module_name, PA_SYMBOL_VERSION)))
        i->version = pa_xstrdup(func());

    if ((func = (const char* (*)(void)) pa_load_sym(dl, module_name, PA_SYMBOL_DEPRECATED)))
        i->deprecated = pa_xstrdup(func());

    if ((func2 = (bool (*)(void)) pa_load_sym(dl, module_name, PA_SYMBOL_LOAD_ONCE)))
        i->load_once = func2();

    return i;
}

pa_modinfo *pa_modinfo_get_by_name(const char *name) {
    lt_dlhandle dl;
    pa_modinfo *i;

    pa_assert(name);

    if (!(dl = lt_dlopenext(name))) {
        pa_log("Failed to open module \"%s\": %s", name, lt_dlerror());
        return NULL;
    }

    i = pa_modinfo_get_by_handle(dl, name);
    lt_dlclose(dl);

    return i;
}

void pa_modinfo_free(pa_modinfo *i) {
    pa_assert(i);

    pa_xfree(i->author);
    pa_xfree(i->description);
    pa_xfree(i->usage);
    pa_xfree(i->version);
    pa_xfree(i->deprecated);
    pa_xfree(i);
}
