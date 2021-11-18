#ifndef foopulsedeviceporthfoo
#define foopulsedeviceporthfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB
  Copyright 2011 David Henningsson, Canonical Ltd.

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

#include <inttypes.h>

#include <pulsecore/typedefs.h>
#include <pulse/def.h>
#include <pulsecore/object.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/core.h>
#include <pulsecore/card.h>

struct pa_device_port {
    pa_object parent; /* Needed for reference counting */
    pa_core *core;
    pa_card *card;

    char *name;
    char *description;
    char *preferred_profile;
    pa_device_port_type_t type;

    unsigned priority;
    pa_available_t available;         /* PA_AVAILABLE_UNKNOWN, PA_AVAILABLE_NO or PA_AVAILABLE_YES */
    char *availability_group;         /* a string indentifier which determine the group of devices handling the available state simulteneously */

    pa_proplist *proplist;
    pa_hashmap *profiles; /* Does not own the profiles */
    pa_direction_t direction;
    int64_t latency_offset;

    /* Free the extra implementation specific data. Called before other members are freed. */
    void (*impl_free)(pa_device_port *port);

    /* .. followed by some implementation specific data */
};

PA_DECLARE_PUBLIC_CLASS(pa_device_port);
#define PA_DEVICE_PORT(s) (pa_device_port_cast(s))

#define PA_DEVICE_PORT_DATA(d) ((void*) ((uint8_t*) d + PA_ALIGN(sizeof(pa_device_port))))

typedef struct pa_device_port_new_data {
    char *name;
    char *description;
    pa_available_t available;
    char *availability_group;
    pa_direction_t direction;
    pa_device_port_type_t type;
} pa_device_port_new_data;

pa_device_port_new_data *pa_device_port_new_data_init(pa_device_port_new_data *data);
void pa_device_port_new_data_set_name(pa_device_port_new_data *data, const char *name);
void pa_device_port_new_data_set_description(pa_device_port_new_data *data, const char *description);
void pa_device_port_new_data_set_available(pa_device_port_new_data *data, pa_available_t available);
void pa_device_port_new_data_set_availability_group(pa_device_port_new_data *data, const char *group);
void pa_device_port_new_data_set_direction(pa_device_port_new_data *data, pa_direction_t direction);
void pa_device_port_new_data_set_type(pa_device_port_new_data *data, pa_device_port_type_t type);
void pa_device_port_new_data_done(pa_device_port_new_data *data);

pa_device_port *pa_device_port_new(pa_core *c, pa_device_port_new_data *data, size_t extra);

/* The port's available status has changed */
void pa_device_port_set_available(pa_device_port *p, pa_available_t available);

void pa_device_port_set_latency_offset(pa_device_port *p, int64_t offset);
void pa_device_port_set_preferred_profile(pa_device_port *p, const char *new_pp);

pa_device_port *pa_device_port_find_best(pa_hashmap *ports);

pa_sink *pa_device_port_get_sink(pa_device_port *p);

pa_source *pa_device_port_get_source(pa_device_port *p);

#endif
