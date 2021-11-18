/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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
#include <ctype.h>

#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "ltdl-helper.h"

pa_void_func_t pa_load_sym(lt_dlhandle handle, const char *module, const char *symbol) {
    char *sn, *c;
    pa_void_func_t f;

    pa_assert(handle);
    pa_assert(symbol);

    f = (pa_void_func_t) lt_dlsym(handle, symbol);

    if (f)
        return f;

    if (!module)
        return NULL;

    /* As the .la files might have been cleansed from the system, we should
     * try with the ltdl prefix as well. */

    sn = pa_sprintf_malloc("%s_LTX_%s", module, symbol);

    for (c = sn; *c; c++)
        if (!isalnum((unsigned char)*c))
            *c = '_';

    f = (pa_void_func_t) lt_dlsym(handle, sn);
    pa_xfree(sn);

    return f;
}
