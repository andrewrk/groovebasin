/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2009 Wim Taymans <wim.taymans@collabora.co.uk>

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

#include <pulse/rtclock.h>

#include <pulsecore/random.h>
#include <pulsecore/macro.h>
#include <pulsecore/endianmacros.h>

#include "cpu-x86.h"

#include "sample-util.h"

#if (!defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__FreeBSD_kernel__) && defined (__i386__)) || defined (__amd64__)
/* in s: 2 int16_t samples
 * in v: 2 int32_t volumes, fixed point 16:16
 * out s: contains scaled and clamped int16_t samples.
 *
 * We calculate the high 32 bits of a 32x16 multiply which we then
 * clamp to 16 bits. The calculation is:
 *
 *  vl = (v & 0xffff)
 *  vh = (v >> 16)
 *  s = ((s * vl) >> 16) + (s * vh);
 *
 * For the first multiply we have to do a sign correction as we need to
 * multiply a signed int with an unsigned int. Hacker's delight 8-3 gives a
 * simple formula to correct the sign of the high word after the signed
 * multiply.
 */
#define VOLUME_32x16(s,v)                  /* .. |   vh  |   vl  | */                   \
      " pxor  %%mm4, %%mm4           \n\t" /* .. |    0  |    0  | */                   \
      " punpcklwd %%mm4, "#s"        \n\t" /* .. |    0  |   p0  | */                   \
      " pcmpgtw "#v", %%mm4          \n\t" /* .. |    0  | s(vl) | */                   \
      " pand "#s", %%mm4             \n\t" /* .. |    0  |  (p0) |  (vl >> 15) & p */   \
      " movq "#s", %%mm5             \n\t"                                              \
      " pmulhw "#v", "#s"            \n\t" /* .. |    0  | vl*p0 | */                   \
      " paddw %%mm4, "#s"            \n\t" /* .. |    0  | vl*p0 | + sign correct */    \
      " pslld $16, "#s"              \n\t" /* .. | vl*p0 |   0   | */                   \
      " psrld $16, "#v"              \n\t" /* .. |    0  |   vh  | */                   \
      " psrad $16, "#s"              \n\t" /* .. |     vl*p0     | sign extend */       \
      " pmaddwd %%mm5, "#v"          \n\t" /* .. |    p0 * vh    | */                   \
      " paddd "#s", "#v"             \n\t" /* .. |    p0 * v0    | */                   \
      " packssdw "#v", "#v"          \n\t" /* .. | p1*v1 | p0*v0 | */

/* approximately advances %3 = (%3 + a) % b. This function requires that
 * a <= b. */
#define MOD_ADD(a,b) \
      " add "#a", %3                 \n\t" \
      " mov %3, %4                   \n\t" \
      " sub "#b", %4                 \n\t" \
      " cmovae %4, %3                \n\t"

/* swap 16 bits */
#define SWAP_16(s) \
      " movq "#s", %%mm4             \n\t" /* .. |  h  l |  */ \
      " psrlw $8, %%mm4              \n\t" /* .. |  0  h |  */ \
      " psllw $8, "#s"               \n\t" /* .. |  l  0 |  */ \
      " por %%mm4, "#s"              \n\t" /* .. |  l  h |  */

/* swap 2 registers 16 bits for better pairing */
#define SWAP_16_2(s1,s2) \
      " movq "#s1", %%mm4            \n\t" /* .. |  h  l |  */ \
      " movq "#s2", %%mm5            \n\t"                     \
      " psrlw $8, %%mm4              \n\t" /* .. |  0  h |  */ \
      " psrlw $8, %%mm5              \n\t"                     \
      " psllw $8, "#s1"              \n\t" /* .. |  l  0 |  */ \
      " psllw $8, "#s2"              \n\t"                     \
      " por %%mm4, "#s1"             \n\t" /* .. |  l  h |  */ \
      " por %%mm5, "#s2"             \n\t"

static void pa_volume_s16ne_mmx(int16_t *samples, const int32_t *volumes, unsigned channels, unsigned length) {
    pa_reg_x86 channel, temp;

    /* Channels must be at least 4, and always a multiple of the original number.
     * This is also the max amount we overread the volume array, which should
     * have enough padding. */
    channels = channels == 3 ? 6 : PA_MAX (4U, channels);

    __asm__ __volatile__ (
        " xor %3, %3                    \n\t"
        " sar $1, %2                    \n\t" /* length /= sizeof (int16_t) */

        " test $1, %2                   \n\t" /* check for odd samples */
        " je 2f                         \n\t"

        " movd (%q1, %3, 4), %%mm0      \n\t" /* |  v0h  |  v0l  | */
        " movw (%0), %w4                \n\t" /*     ..  |  p0   | */
        " movd %4, %%mm1                \n\t"
        VOLUME_32x16 (%%mm1, %%mm0)
        " movd %%mm0, %4                \n\t" /*     ..  | p0*v0 | */
        " movw %w4, (%0)                \n\t"
        " add $2, %0                    \n\t"
        MOD_ADD ($1, %5)

        "2:                             \n\t"
        " sar $1, %2                    \n\t" /* prepare for processing 2 samples at a time */
        " test $1, %2                   \n\t" /* check for odd samples */
        " je 4f                         \n\t"

        "3:                             \n\t" /* do samples in groups of 2 */
        " movq (%q1, %3, 4), %%mm0      \n\t" /* |  v1h  |  v1l  |  v0h  |  v0l  | */
        " movd (%0), %%mm1              \n\t" /*              .. |   p1  |  p0   | */
        VOLUME_32x16 (%%mm1, %%mm0)
        " movd %%mm0, (%0)              \n\t" /*              .. | p1*v1 | p0*v0 | */
        " add $4, %0                    \n\t"
        MOD_ADD ($2, %5)

        "4:                             \n\t"
        " sar $1, %2                    \n\t" /* prepare for processing 4 samples at a time */
        " cmp $0, %2                    \n\t"
        " je 6f                         \n\t"

        "5:                             \n\t" /* do samples in groups of 4 */
        " movq (%q1, %3, 4), %%mm0      \n\t" /* |  v1h  |  v1l  |  v0h  |  v0l  | */
        " movq 8(%q1, %3, 4), %%mm2     \n\t" /* |  v3h  |  v3l  |  v2h  |  v2l  | */
        " movd (%0), %%mm1              \n\t" /*              .. |   p1  |  p0   | */
        " movd 4(%0), %%mm3             \n\t" /*              .. |   p3  |  p2   | */
        VOLUME_32x16 (%%mm1, %%mm0)
        VOLUME_32x16 (%%mm3, %%mm2)
        " movd %%mm0, (%0)              \n\t" /*              .. | p1*v1 | p0*v0 | */
        " movd %%mm2, 4(%0)             \n\t" /*              .. | p3*v3 | p2*v2 | */
        " add $8, %0                    \n\t"
        MOD_ADD ($4, %5)
        " dec %2                        \n\t"
        " jne 5b                        \n\t"

        "6:                             \n\t"
        " emms                          \n\t"

        : "+r" (samples), "+r" (volumes), "+r" (length), "=&D" (channel), "=&r" (temp)
#if defined (__i386__)
        : "m" (channels)
#else
        : "r" ((pa_reg_x86)channels)
#endif
        : "cc"
    );
}

static void pa_volume_s16re_mmx(int16_t *samples, const int32_t *volumes, unsigned channels, unsigned length) {
    pa_reg_x86 channel, temp;

    /* Channels must be at least 4, and always a multiple of the original number.
     * This is also the max amount we overread the volume array, which should
     * have enough padding. */
    channels = channels == 3 ? 6 : PA_MAX (4U, channels);

    __asm__ __volatile__ (
        " xor %3, %3                    \n\t"
        " sar $1, %2                    \n\t" /* length /= sizeof (int16_t) */
        " pcmpeqw %%mm6, %%mm6          \n\t" /* .. |  ffff |  ffff | */
        " pcmpeqw %%mm7, %%mm7          \n\t" /* .. |  ffff |  ffff | */
        " pslld  $16, %%mm6             \n\t" /* .. |  ffff |     0 | */
        " psrld  $31, %%mm7             \n\t" /* .. |     0 |     1 | */

        " test $1, %2                   \n\t" /* check for odd samples */
        " je 2f                         \n\t"

        " movd (%q1, %3, 4), %%mm0      \n\t" /* |  v0h  |  v0l  | */
        " movw (%0), %w4                \n\t" /*     ..  |  p0   | */
        " rorw $8, %w4                  \n\t"
        " movd %4, %%mm1                \n\t"
        VOLUME_32x16 (%%mm1, %%mm0)
        " movd %%mm0, %4                \n\t" /*     ..  | p0*v0 | */
        " rorw $8, %w4                  \n\t"
        " movw %w4, (%0)                \n\t"
        " add $2, %0                    \n\t"
        MOD_ADD ($1, %5)

        "2:                             \n\t"
        " sar $1, %2                    \n\t" /* prepare for processing 2 samples at a time */
        " test $1, %2                   \n\t" /* check for odd samples */
        " je 4f                         \n\t"

        "3:                             \n\t" /* do samples in groups of 2 */
        " movq (%q1, %3, 4), %%mm0      \n\t" /* |  v1h  |  v1l  |  v0h  |  v0l  | */
        " movd (%0), %%mm1              \n\t" /*              .. |   p1  |  p0   | */
        SWAP_16 (%%mm1)
        VOLUME_32x16 (%%mm1, %%mm0)
        SWAP_16 (%%mm0)
        " movd %%mm0, (%0)              \n\t" /*              .. | p1*v1 | p0*v0 | */
        " add $4, %0                    \n\t"
        MOD_ADD ($2, %5)

        "4:                             \n\t"
        " sar $1, %2                    \n\t" /* prepare for processing 4 samples at a time */
        " cmp $0, %2                    \n\t"
        " je 6f                         \n\t"

        "5:                             \n\t" /* do samples in groups of 4 */
        " movq (%q1, %3, 4), %%mm0      \n\t" /* |  v1h  |  v1l  |  v0h  |  v0l  | */
        " movq 8(%q1, %3, 4), %%mm2     \n\t" /* |  v3h  |  v3l  |  v2h  |  v2l  | */
        " movd (%0), %%mm1              \n\t" /*              .. |   p1  |  p0   | */
        " movd 4(%0), %%mm3             \n\t" /*              .. |   p3  |  p2   | */
        SWAP_16_2 (%%mm1, %%mm3)
        VOLUME_32x16 (%%mm1, %%mm0)
        VOLUME_32x16 (%%mm3, %%mm2)
        SWAP_16_2 (%%mm0, %%mm2)
        " movd %%mm0, (%0)              \n\t" /*              .. | p1*v1 | p0*v0 | */
        " movd %%mm2, 4(%0)             \n\t" /*              .. | p3*v3 | p2*v2 | */
        " add $8, %0                    \n\t"
        MOD_ADD ($4, %5)
        " dec %2                        \n\t"
        " jne 5b                        \n\t"

        "6:                             \n\t"
        " emms                          \n\t"

        : "+r" (samples), "+r" (volumes), "+r" (length), "=&D" (channel), "=&r" (temp)
#if defined (__i386__)
        : "m" (channels)
#else
        : "r" ((pa_reg_x86)channels)
#endif
        : "cc"
    );
}

#endif /* (!defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__FreeBSD_kernel__) && defined (__i386__)) || defined (__amd64__) */

void pa_volume_func_init_mmx(pa_cpu_x86_flag_t flags) {
#if (!defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__FreeBSD_kernel__) && defined (__i386__)) || defined (__amd64__)
    if ((flags & PA_CPU_X86_MMX) && (flags & PA_CPU_X86_CMOV)) {
        pa_log_info("Initialising MMX optimized volume functions.");

        pa_set_volume_func(PA_SAMPLE_S16NE, (pa_do_volume_func_t) pa_volume_s16ne_mmx);
        pa_set_volume_func(PA_SAMPLE_S16RE, (pa_do_volume_func_t) pa_volume_s16re_mmx);
    }
#endif /* (!defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__FreeBSD_kernel__) && defined (__i386__)) || defined (__amd64__) */
}
