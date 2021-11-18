#ifndef foosconvhfoo
#define foosconvhfoo

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

#include <pulse/gccmacro.h>
#include <pulse/sample.h>

typedef void (*pa_convert_func_t)(unsigned n, const void *a, void *b);

pa_convert_func_t pa_get_convert_to_float32ne_function(pa_sample_format_t f) PA_GCC_PURE;
pa_convert_func_t pa_get_convert_from_float32ne_function(pa_sample_format_t f) PA_GCC_PURE;

pa_convert_func_t pa_get_convert_to_s16ne_function(pa_sample_format_t f) PA_GCC_PURE;
pa_convert_func_t pa_get_convert_from_s16ne_function(pa_sample_format_t f) PA_GCC_PURE;

void pa_set_convert_to_float32ne_function(pa_sample_format_t f, pa_convert_func_t func);
void pa_set_convert_from_float32ne_function(pa_sample_format_t f, pa_convert_func_t func);

void pa_set_convert_to_s16ne_function(pa_sample_format_t f, pa_convert_func_t func);
void pa_set_convert_from_s16ne_function(pa_sample_format_t f, pa_convert_func_t func);

#endif
