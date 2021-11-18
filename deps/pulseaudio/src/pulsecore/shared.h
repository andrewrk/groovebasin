#ifndef foosharedshfoo
#define foosharedshfoo

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
#include <pulsecore/strbuf.h>

/* The shared property subsystem is to be used to share data between
 * modules. Consider them to be kind of "global" variables for a
 * core. Why not use the hashmap functions directly? The hashmap
 * functions copy neither the key nor value, while this property
 * system copies the key. Users of this system have to think about
 * reference counting themselves. */

/* Note: please don't confuse this with the proplist framework in
 * pulse/proplist.[ch]! */

/* Return a pointer to the value of the specified shared property. */
void* pa_shared_get(pa_core *c, const char *name);

/* Set the shared property 'name' to 'data'. This function fails in
 * case a property by this name already exists. The property data is
 * not copied or reference counted. This is the caller's job. */
int pa_shared_set(pa_core *c, const char *name, void *data);

/* Remove the specified shared property. Return non-zero on failure */
int pa_shared_remove(pa_core *c, const char *name);

/* A combination of pa_shared_remove() and pa_shared_set(); this function
 * first tries to remove the property by this name and then sets the
 * property. Return non-zero on failure. */
int pa_shared_replace(pa_core *c, const char *name, void *data);

/* Dump the current set of shared properties */
void pa_shared_dump(pa_core *c, pa_strbuf *s);

#endif
