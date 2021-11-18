#ifndef foopulsecoresndfileutilhfoo
#define foopulsecoresndfileutilhfoo

/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

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

#include <sndfile.h>

#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulse/proplist.h>

int pa_sndfile_read_sample_spec(SNDFILE *sf, pa_sample_spec *ss);
int pa_sndfile_read_channel_map(SNDFILE *sf, pa_channel_map *cm);

int pa_sndfile_write_sample_spec(SF_INFO *sfi, pa_sample_spec *ss);
int pa_sndfile_write_channel_map(SNDFILE *sf, pa_channel_map *cm);

void pa_sndfile_init_proplist(SNDFILE *sf, pa_proplist *p);

typedef sf_count_t (*pa_sndfile_readf_t)(SNDFILE *sndfile, void *ptr, sf_count_t frames);
typedef sf_count_t (*pa_sndfile_writef_t)(SNDFILE *sndfile, const void *ptr, sf_count_t frames);

/* Returns NULL if sf_read_raw() shall be used */
pa_sndfile_readf_t pa_sndfile_readf_function(const pa_sample_spec *ss);

/* Returns NULL if sf_write_raw() shall be used */
pa_sndfile_writef_t pa_sndfile_writef_function(const pa_sample_spec *ss);

int pa_sndfile_format_from_string(const char *extension);

void pa_sndfile_dump_formats(void);

#endif
