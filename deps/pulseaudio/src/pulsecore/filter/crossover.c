/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/macro.h>

#include "crossover.h"

void lr4_set(struct lr4 *lr4, enum biquad_type type, float freq)
{
	biquad_set(&lr4->bq, type, freq);
	lr4->x1 = 0;
	lr4->x2 = 0;
	lr4->y1 = 0;
	lr4->y2 = 0;
	lr4->z1 = 0;
	lr4->z2 = 0;
}

void lr4_process_float32(struct lr4 *lr4, int samples, int channels, float *src, float *dest)
{
	float lx1 = lr4->x1;
	float lx2 = lr4->x2;
	float ly1 = lr4->y1;
	float ly2 = lr4->y2;
	float lz1 = lr4->z1;
	float lz2 = lr4->z2;
	float lb0 = lr4->bq.b0;
	float lb1 = lr4->bq.b1;
	float lb2 = lr4->bq.b2;
	float la1 = lr4->bq.a1;
	float la2 = lr4->bq.a2;

	int i;
	for (i = 0; i < samples * channels; i += channels) {
		float x, y, z;
		x = src[i];
		y = lb0*x + lb1*lx1 + lb2*lx2 - la1*ly1 - la2*ly2;
		z = lb0*y + lb1*ly1 + lb2*ly2 - la1*lz1 - la2*lz2;
		lx2 = lx1;
		lx1 = x;
		ly2 = ly1;
		ly1 = y;
		lz2 = lz1;
		lz1 = z;
		dest[i] = z;
	}

	lr4->x1 = lx1;
	lr4->x2 = lx2;
	lr4->y1 = ly1;
	lr4->y2 = ly2;
	lr4->z1 = lz1;
	lr4->z2 = lz2;
}

void lr4_process_s16(struct lr4 *lr4, int samples, int channels, short *src, short *dest)
{
	float lx1 = lr4->x1;
	float lx2 = lr4->x2;
	float ly1 = lr4->y1;
	float ly2 = lr4->y2;
	float lz1 = lr4->z1;
	float lz2 = lr4->z2;
	float lb0 = lr4->bq.b0;
	float lb1 = lr4->bq.b1;
	float lb2 = lr4->bq.b2;
	float la1 = lr4->bq.a1;
	float la2 = lr4->bq.a2;

	int i;
	for (i = 0; i < samples * channels; i += channels) {
		float x, y, z;
		x = src[i];
		y = lb0*x + lb1*lx1 + lb2*lx2 - la1*ly1 - la2*ly2;
		z = lb0*y + lb1*ly1 + lb2*ly2 - la1*lz1 - la2*lz2;
		lx2 = lx1;
		lx1 = x;
		ly2 = ly1;
		ly1 = y;
		lz2 = lz1;
		lz1 = z;
		dest[i] = PA_CLAMP_UNLIKELY((int) z, -0x8000, 0x7fff);
	}

	lr4->x1 = lx1;
	lr4->x2 = lx2;
	lr4->y1 = ly1;
	lr4->y2 = ly2;
	lr4->z1 = lz1;
	lr4->z2 = lz2;
}
