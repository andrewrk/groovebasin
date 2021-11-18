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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <pulsecore/macro.h>
#include <pulsecore/endianmacros.h>

#include "cpu-x86.h"
#include "sconv.h"

#if (!defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__FreeBSD_kernel__) && defined (__i386__)) || defined (__amd64__)

static const PA_DECLARE_ALIGNED (16, float, scale[4]) = { 0x8000, 0x8000, 0x8000, 0x8000 };

static void pa_sconv_s16le_from_f32ne_sse(unsigned n, const float *a, int16_t *b) {
    pa_reg_x86 temp, i;

    __asm__ __volatile__ (
        " movaps %5, %%xmm5             \n\t"
        " xor %0, %0                    \n\t"

        " mov %4, %1                    \n\t"
        " sar $3, %1                    \n\t" /* 8 floats at a time */
        " cmp $0, %1                    \n\t"
        " je 2f                         \n\t"

        "1:                             \n\t"
        " movups (%q2, %0, 2), %%xmm0   \n\t" /* read 8 floats */
        " movups 16(%q2, %0, 2), %%xmm2 \n\t"
        " mulps  %%xmm5, %%xmm0         \n\t" /* *= 0x8000 */
        " mulps  %%xmm5, %%xmm2         \n\t"

        " cvtps2pi %%xmm0, %%mm0        \n\t" /* low part to int */
        " cvtps2pi %%xmm2, %%mm2        \n\t"
        " movhlps  %%xmm0, %%xmm0       \n\t" /* bring high part in position */
        " movhlps  %%xmm2, %%xmm2       \n\t"
        " cvtps2pi %%xmm0, %%mm1        \n\t" /* high part to int */
        " cvtps2pi %%xmm2, %%mm3        \n\t"

        " packssdw %%mm1, %%mm0         \n\t" /* pack parts */
        " packssdw %%mm3, %%mm2         \n\t"
        " movq     %%mm0, (%q3, %0)     \n\t"
        " movq     %%mm2, 8(%q3, %0)    \n\t"

        " add $16, %0                   \n\t"
        " dec %1                        \n\t"
        " jne 1b                        \n\t"

        "2:                             \n\t"
        " mov %4, %1                    \n\t" /* prepare for leftovers */
        " and $7, %1                    \n\t"
        " je 5f                         \n\t"

        "3:                             \n\t"
        " movss (%q2, %0, 2), %%xmm0    \n\t"
        " mulss  %%xmm5, %%xmm0         \n\t"
        " cvtss2si %%xmm0, %4           \n\t"
        " add $0x8000, %4               \n\t" /* check for saturation */
        " and $~0xffff, %4              \n\t"
        " cvtss2si %%xmm0, %4           \n\t"
        " je 4f                         \n\t"
        " sar $31, %4                   \n\t"
        " xor $0x7fff, %4               \n\t"

        "4:                             \n\t"
        " movw  %w4, (%q3, %0)          \n\t" /* store leftover */
        " add $2, %0                    \n\t"
        " dec %1                        \n\t"
        " jne 3b                        \n\t"

        "5:                             \n\t"
        " emms                          \n\t"

        : "=&r" (i), "=&r" (temp)
        : "r" (a), "r" (b), "r" ((pa_reg_x86)n), "m" (*scale)
        : "cc", "memory"
    );
}

static void pa_sconv_s16le_from_f32ne_sse2(unsigned n, const float *a, int16_t *b) {
    pa_reg_x86 temp, i;

    __asm__ __volatile__ (
        " movaps %5, %%xmm5             \n\t"
        " xor %0, %0                    \n\t"

        " mov %4, %1                    \n\t"
        " sar $3, %1                    \n\t" /* 8 floats at a time */
        " cmp $0, %1                    \n\t"
        " je 2f                         \n\t"

        "1:                             \n\t"
        " movups (%q2, %0, 2), %%xmm0   \n\t" /* read 8 floats */
        " movups 16(%q2, %0, 2), %%xmm2 \n\t"
        " mulps  %%xmm5, %%xmm0         \n\t" /* *= 0x8000 */
        " mulps  %%xmm5, %%xmm2         \n\t"

        " cvtps2dq %%xmm0, %%xmm0       \n\t"
        " cvtps2dq %%xmm2, %%xmm2       \n\t"

        " packssdw %%xmm2, %%xmm0       \n\t"
        " movdqu   %%xmm0, (%q3, %0)    \n\t"

        " add $16, %0                   \n\t"
        " dec %1                        \n\t"
        " jne 1b                        \n\t"

        "2:                             \n\t"
        " mov %4, %1                    \n\t" /* prepare for leftovers */
        " and $7, %1                    \n\t"
        " je 5f                         \n\t"

        "3:                             \n\t"
        " movss (%q2, %0, 2), %%xmm0    \n\t"
        " mulss  %%xmm5, %%xmm0         \n\t"
        " cvtss2si %%xmm0, %4           \n\t"
        " add $0x8000, %4               \n\t"
        " and $~0xffff, %4              \n\t" /* check for saturation */
        " cvtss2si %%xmm0, %4           \n\t"
        " je 4f                         \n\t"
        " sar $31, %4                   \n\t"
        " xor $0x7fff, %4               \n\t"

        "4:                             \n\t"
        " movw  %w4, (%q3, %0)          \n\t" /* store leftover */
        " add $2, %0                    \n\t"
        " dec %1                        \n\t"
        " jne 3b                        \n\t"

        "5:                             \n\t"

        : "=&r" (i), "=&r" (temp)
        : "r" (a), "r" (b), "r" ((pa_reg_x86)n), "m" (*scale)
        : "cc", "memory"
    );
}

#endif /* defined (__i386__) || defined (__amd64__) */

void pa_convert_func_init_sse(pa_cpu_x86_flag_t flags) {
#if (!defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__FreeBSD_kernel__) && defined (__i386__)) || defined (__amd64__)

    if (flags & PA_CPU_X86_SSE2) {
        pa_log_info("Initialising SSE2 optimized conversions.");
        pa_set_convert_from_float32ne_function(PA_SAMPLE_S16LE, (pa_convert_func_t) pa_sconv_s16le_from_f32ne_sse2);
        pa_set_convert_to_s16ne_function(PA_SAMPLE_FLOAT32LE, (pa_convert_func_t) pa_sconv_s16le_from_f32ne_sse2);
    } else if (flags & PA_CPU_X86_SSE) {
        pa_log_info("Initialising SSE optimized conversions.");
        pa_set_convert_from_float32ne_function(PA_SAMPLE_S16LE, (pa_convert_func_t) pa_sconv_s16le_from_f32ne_sse);
        pa_set_convert_to_s16ne_function(PA_SAMPLE_FLOAT32LE, (pa_convert_func_t) pa_sconv_s16le_from_f32ne_sse);
    }

#endif /* defined (__i386__) || defined (__amd64__) */
}
