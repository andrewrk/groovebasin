#ifndef fooremapfoo
#define fooremapfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2009 Wim Taymans <wim.taymans@collabora.co.uk.com>

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

#include <pulse/sample.h>

typedef struct pa_remap pa_remap_t;

typedef void (*pa_do_remap_func_t) (pa_remap_t *m, void *d, const void *s, unsigned n);

struct pa_remap {
    pa_sample_format_t format;
    pa_sample_spec i_ss, o_ss;
    float map_table_f[PA_CHANNELS_MAX][PA_CHANNELS_MAX];
    int32_t map_table_i[PA_CHANNELS_MAX][PA_CHANNELS_MAX];
    pa_do_remap_func_t do_remap;
    void *state; /* optional state information for the remap function */
};

void pa_init_remap_func(pa_remap_t *m);

/* custom installation of init functions */
typedef void (*pa_init_remap_func_t) (pa_remap_t *m);

pa_init_remap_func_t pa_get_init_remap_func(void);
void pa_set_init_remap_func(pa_init_remap_func_t func);

/* Check if remapping can be performed by just copying some or all input
 * channels' data to output channels. Returns true and a table of input
 * channel indices, or false otherwise.
 *
 * The table contains an entry for each output channels. Each table entry given
 * either the input channel index to be copied, or -1 indicating that the
 * output channel is not used and hence zero.
 */
bool pa_setup_remap_arrange(const pa_remap_t *m, int8_t arrange[PA_CHANNELS_MAX]);

void pa_set_remap_func(pa_remap_t *m, pa_do_remap_func_t func_s16,
    pa_do_remap_func_t func_s32, pa_do_remap_func_t func_float);

#endif /* fooremapfoo */
