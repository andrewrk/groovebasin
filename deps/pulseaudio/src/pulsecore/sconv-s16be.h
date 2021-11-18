#ifndef foosconv_s16befoo
#define foosconv_s16befoo

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

void pa_sconv_s16be_to_float32ne(unsigned n, const int16_t *a, float *b);
void pa_sconv_s16be_from_float32ne(unsigned n, const float *a, int16_t *b);
void pa_sconv_s16be_to_float32re(unsigned n, const int16_t *a, float *b);
void pa_sconv_s16be_from_float32re(unsigned n, const float *a, int16_t *b);

void pa_sconv_s32be_to_float32ne(unsigned n, const int32_t *a, float *b);
void pa_sconv_s32be_from_float32ne(unsigned n, const float *a, int32_t *b);
void pa_sconv_s32be_to_float32re(unsigned n, const int32_t *a, float *b);
void pa_sconv_s32be_from_float32re(unsigned n, const float *a, int32_t *b);

void pa_sconv_s24be_to_float32ne(unsigned n, const uint8_t *a, float *b);
void pa_sconv_s24be_from_float32ne(unsigned n, const float *a, uint8_t *b);
void pa_sconv_s24be_to_float32re(unsigned n, const uint8_t *a, float *b);
void pa_sconv_s24be_from_float32re(unsigned n, const float *a, uint8_t *b);

void pa_sconv_s24_32be_to_float32ne(unsigned n, const uint32_t *a, float *b);
void pa_sconv_s24_32be_from_float32ne(unsigned n, const float *a, uint32_t *b);
void pa_sconv_s24_32be_to_float32re(unsigned n, const uint8_t *a, float *b);
void pa_sconv_s24_32be_from_float32re(unsigned n, const float *a, uint8_t *b);

void pa_sconv_s32be_to_s16ne(unsigned n, const int32_t *a, int16_t *b);
void pa_sconv_s32be_from_s16ne(unsigned n, const int16_t *a, int32_t *b);
void pa_sconv_s32be_to_s16re(unsigned n, const int32_t *a, int16_t *b);
void pa_sconv_s32be_from_s16re(unsigned n, const int16_t *a, int32_t *b);

void pa_sconv_s24be_to_s16ne(unsigned n, const uint8_t *a, int16_t *b);
void pa_sconv_s24be_from_s16ne(unsigned n, const int16_t *a, uint8_t *b);
void pa_sconv_s24be_to_s16re(unsigned n, const uint8_t *a, int16_t *b);
void pa_sconv_s24be_from_s16re(unsigned n, const int16_t *a, uint8_t *b);

void pa_sconv_s24_32be_to_s16ne(unsigned n, const uint32_t *a, int16_t *b);
void pa_sconv_s24_32be_from_s16ne(unsigned n, const int16_t *a, uint32_t *b);
void pa_sconv_s24_32be_to_s16re(unsigned n, const uint8_t *a, int16_t *b);
void pa_sconv_s24_32be_from_s16re(unsigned n, const int16_t *a, uint8_t *b);

#ifdef WORDS_BIGENDIAN
#define pa_sconv_float32be_to_s16ne pa_sconv_s16be_from_float32ne
#define pa_sconv_float32be_from_s16ne pa_sconv_s16be_to_float32ne
#define pa_sconv_float32le_to_s16ne pa_sconv_s16be_from_float32re
#define pa_sconv_float32le_from_s16ne pa_sconv_s16be_to_float32re
#endif

#endif
