#ifndef fooclitexthfoo
#define fooclitexthfoo

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

/* Some functions to generate pretty formatted listings of
 * entities. The returned strings have to be freed manually. */

char *pa_sink_input_list_to_string(pa_core *c);
char *pa_source_output_list_to_string(pa_core *c);
char *pa_sink_list_to_string(pa_core *core);
char *pa_source_list_to_string(pa_core *c);
char *pa_card_list_to_string(pa_core *c);
char *pa_client_list_to_string(pa_core *c);
char *pa_module_list_to_string(pa_core *c);
char *pa_scache_list_to_string(pa_core *c);

char *pa_full_status_string(pa_core *c);

#endif
