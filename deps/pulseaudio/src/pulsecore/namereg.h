#ifndef foonamereghfoo
#define foonamereghfoo

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

#include <pulsecore/core.h>
#include <pulsecore/macro.h>

#define PA_NAME_MAX 128

typedef enum pa_namereg_type {
    PA_NAMEREG_SINK,
    PA_NAMEREG_SOURCE,
    PA_NAMEREG_SAMPLE,
    PA_NAMEREG_CARD
} pa_namereg_type_t;

const char *pa_namereg_register(pa_core *c, const char *name, pa_namereg_type_t type, void *data, bool fail);
void pa_namereg_unregister(pa_core *c, const char *name);
void* pa_namereg_get(pa_core *c, const char *name, pa_namereg_type_t type);

bool pa_namereg_is_valid_name(const char *name);
bool pa_namereg_is_valid_name_or_wildcard(const char *name, pa_namereg_type_t type);
char* pa_namereg_make_valid_name(const char *name);

#endif
