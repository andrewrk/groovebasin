#ifndef foomodargshfoo
#define foomodargshfoo

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

#include <inttypes.h>
#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulse/proplist.h>
#include <pulse/volume.h>
#include <pulsecore/macro.h>
#include <pulsecore/resampler.h>

typedef struct pa_modargs pa_modargs;

/* A generic parser for module arguments */

/* Parse the string args. The NULL-terminated array keys contains all valid arguments. */
pa_modargs *pa_modargs_new(const char *args, const char* const keys[]);
/* Parse the string args, and add any keys that are not already present. */
int pa_modargs_append(pa_modargs *ma, const char *args, const char* const* valid_keys);
void pa_modargs_free(pa_modargs*ma);

/* Return the module argument for the specified name as a string. If
 * the argument was not specified, return def instead.*/
const char *pa_modargs_get_value(pa_modargs *ma, const char *key, const char *def);

/* Return a module argument as unsigned 32bit value in *value. If the argument
 * was not specified, *value remains unchanged. */
int pa_modargs_get_value_u32(pa_modargs *ma, const char *key, uint32_t *value);
int pa_modargs_get_value_s32(pa_modargs *ma, const char *key, int32_t *value);
int pa_modargs_get_value_boolean(pa_modargs *ma, const char *key, bool *value);

/* Return a module argument as double value in *value. If the argument was not
 * specified, *value remains unchanged. */
int pa_modargs_get_value_double(pa_modargs *ma, const char *key, double *value);

/* Return a module argument as pa_volume_t value in *value. If the argument
 * was not specified, *value remains unchanged. */
int pa_modargs_get_value_volume(pa_modargs *ma, const char *key, pa_volume_t *value);

/* Return sample rate from the "rate" argument. If the argument was not
 * specified, *rate remains unchanged. */
int pa_modargs_get_sample_rate(pa_modargs *ma, uint32_t *rate);

/* Return sample spec data from the three arguments "rate", "format" and
 * "channels". If the argument was not specified, *ss remains unchanged. */
int pa_modargs_get_sample_spec(pa_modargs *ma, pa_sample_spec *ss);

/* Return channel map data from the argument "channel_map" if name is NULL,
 * otherwise read from the specified argument. If the argument was not
 * specified, *map remains unchanged. */
int pa_modargs_get_channel_map(pa_modargs *ma, const char *name, pa_channel_map *map);

/* Return resample method from the argument "resample_method". If the argument
 * was not specified, *method remains unchanged. */
int pa_modargs_get_resample_method(pa_modargs *ma, pa_resample_method_t *method);

/* Combination of pa_modargs_get_sample_spec() and
pa_modargs_get_channel_map(). Not always suitable, since this routine
initializes the map parameter based on the channels field of the ss
structure if no channel_map is found, using pa_channel_map_init_auto() */

int pa_modargs_get_sample_spec_and_channel_map(pa_modargs *ma, pa_sample_spec *ss, pa_channel_map *map, pa_channel_map_def_t def);

/* Return alternate sample rate from "alternate_sample_rate" parameter. If the
 * argument was not specified, *alternate_rate remains unchanged. */
int pa_modargs_get_alternate_sample_rate(pa_modargs *ma, uint32_t *alternate_rate);

int pa_modargs_get_proplist(pa_modargs *ma, const char *name, pa_proplist *p, pa_update_mode_t m);

/* Iterate through the module argument list. The user should allocate a
 * state variable of type void* and initialize it with NULL. A pointer
 * to this variable should then be passed to pa_modargs_iterate()
 * which should be called in a loop until it returns NULL which
 * signifies EOL. On each invocation this function will return the
 * key string for the next entry. The keys in the argument list do not
 * have any particular order. */
const char *pa_modargs_iterate(pa_modargs *ma, void **state);

#endif
