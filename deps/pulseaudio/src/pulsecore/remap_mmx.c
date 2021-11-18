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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/sample.h>
#include <pulse/volume.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "cpu-x86.h"
#include "remap.h"

#define LOAD_SAMPLES                                   \
                " movq (%1), %%mm0              \n\t"  \
                " movq 8(%1), %%mm2             \n\t"  \
                " movq 16(%1), %%mm4            \n\t"  \
                " movq 24(%1), %%mm6            \n\t"  \
                " movq %%mm0, %%mm1             \n\t"  \
                " movq %%mm2, %%mm3             \n\t"  \
                " movq %%mm4, %%mm5             \n\t"  \
                " movq %%mm6, %%mm7             \n\t"

#define UNPACK_SAMPLES(s)                              \
                " punpckl"#s" %%mm0, %%mm0      \n\t"  \
                " punpckh"#s" %%mm1, %%mm1      \n\t"  \
                " punpckl"#s" %%mm2, %%mm2      \n\t"  \
                " punpckh"#s" %%mm3, %%mm3      \n\t"  \
                " punpckl"#s" %%mm4, %%mm4      \n\t"  \
                " punpckh"#s" %%mm5, %%mm5      \n\t"  \
                " punpckl"#s" %%mm6, %%mm6      \n\t"  \
                " punpckh"#s" %%mm7, %%mm7      \n\t"

#define STORE_SAMPLES                                  \
                " movq %%mm0, (%0)              \n\t"  \
                " movq %%mm1, 8(%0)             \n\t"  \
                " movq %%mm2, 16(%0)            \n\t"  \
                " movq %%mm3, 24(%0)            \n\t"  \
                " movq %%mm4, 32(%0)            \n\t"  \
                " movq %%mm5, 40(%0)            \n\t"  \
                " movq %%mm6, 48(%0)            \n\t"  \
                " movq %%mm7, 56(%0)            \n\t"  \
                " add $32, %1                   \n\t"  \
                " add $64, %0                   \n\t"

#define HANDLE_SINGLE_dq()                            \
                " movd (%1), %%mm0              \n\t"  \
                " punpckldq %%mm0, %%mm0        \n\t"  \
                " movq %%mm0, (%0)              \n\t"  \
                " add $4, %1                    \n\t"  \
                " add $8, %0                    \n\t"

#define HANDLE_SINGLE_wd()                             \
                " movw (%1), %w3                \n\t"  \
                " movd %3,  %%mm0               \n\t"  \
                " punpcklwd %%mm0, %%mm0        \n\t"  \
                " movd %%mm0, (%0)              \n\t"  \
                " add $2, %1                    \n\t"  \
                " add $4, %0                    \n\t"

#define MONO_TO_STEREO(s,shift,mask)                   \
                " mov %4, %2                    \n\t"  \
                " sar $"#shift", %2             \n\t"  \
                " cmp $0, %2                    \n\t"  \
                " je 2f                         \n\t"  \
                "1:                             \n\t"  \
                LOAD_SAMPLES                           \
                UNPACK_SAMPLES(s)                      \
                STORE_SAMPLES                          \
                " dec %2                        \n\t"  \
                " jne 1b                        \n\t"  \
                "2:                             \n\t"  \
                " mov %4, %2                    \n\t"  \
                " and $"#mask", %2              \n\t"  \
                " je 4f                         \n\t"  \
                "3:                             \n\t"  \
                HANDLE_SINGLE_##s()                    \
                " dec %2                        \n\t"  \
                " jne 3b                        \n\t"  \
                "4:                             \n\t"  \
                " emms                          \n\t"

#if defined (__i386__) || defined (__amd64__)
static void remap_mono_to_stereo_s16ne_mmx(pa_remap_t *m, int16_t *dst, const int16_t *src, unsigned n) {
    pa_reg_x86 temp, temp2;

    __asm__ __volatile__ (
        MONO_TO_STEREO(wd,4,15) /* do words to doubles */
        : "+r" (dst), "+r" (src), "=&r" (temp), "=&r" (temp2)
        : "r" ((pa_reg_x86)n)
        : "cc"
    );
}

/* Works for both S32NE and FLOAT32NE */
static void remap_mono_to_stereo_any32ne_mmx(pa_remap_t *m, float *dst, const float *src, unsigned n) {
    pa_reg_x86 temp, temp2;

    __asm__ __volatile__ (
        MONO_TO_STEREO(dq,3,7) /* do doubles to quads */
        : "+r" (dst), "+r" (src), "=&r" (temp), "=&r" (temp2)
        : "r" ((pa_reg_x86)n)
        : "cc"
    );
}

/* set the function that will execute the remapping based on the matrices */
static void init_remap_mmx(pa_remap_t *m) {
    unsigned n_oc, n_ic;

    n_oc = m->o_ss.channels;
    n_ic = m->i_ss.channels;

    /* find some common channel remappings, fall back to full matrix operation. */
    if (n_ic == 1 && n_oc == 2 &&
            m->map_table_i[0][0] == 0x10000 && m->map_table_i[1][0] == 0x10000) {

        pa_log_info("Using MMX mono to stereo remapping");
        pa_set_remap_func(m, (pa_do_remap_func_t) remap_mono_to_stereo_s16ne_mmx,
            (pa_do_remap_func_t) remap_mono_to_stereo_any32ne_mmx,
            (pa_do_remap_func_t) remap_mono_to_stereo_any32ne_mmx);
    }
}
#endif /* defined (__i386__) || defined (__amd64__) */

void pa_remap_func_init_mmx(pa_cpu_x86_flag_t flags) {
#if defined (__i386__) || defined (__amd64__)

    if (flags & PA_CPU_X86_MMX) {
        pa_log_info("Initialising MMX optimized remappers.");

        pa_set_init_remap_func((pa_init_remap_func_t) init_remap_mmx);
    }

#endif /* defined (__i386__) || defined (__amd64__) */
}
