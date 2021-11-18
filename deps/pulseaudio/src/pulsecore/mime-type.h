#ifndef foopulsecoremimetypehfoo
#define foopulsecoremimetypehfoo
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

#include <pulsecore/macro.h>
#include <pulse/sample.h>
#include <pulse/channelmap.h>

bool pa_sample_spec_is_mime(const pa_sample_spec *ss, const pa_channel_map *cm);
void pa_sample_spec_mimefy(pa_sample_spec *ss, pa_channel_map *cm);
char *pa_sample_spec_to_mime_type(const pa_sample_spec *ss, const pa_channel_map *cm);
char *pa_sample_spec_to_mime_type_mimefy(const pa_sample_spec *_ss, const pa_channel_map *_cm);

#endif
