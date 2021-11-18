#ifndef foocpux86hfoo
#define foocpux86hfoo

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

#include <stdint.h>
#include <pulsecore/macro.h>

typedef enum pa_cpu_x86_flag {
    PA_CPU_X86_MMX       = (1 << 0),
    PA_CPU_X86_MMXEXT    = (1 << 1),
    PA_CPU_X86_SSE       = (1 << 2),
    PA_CPU_X86_SSE2      = (1 << 3),
    PA_CPU_X86_SSE3      = (1 << 4),
    PA_CPU_X86_SSSE3     = (1 << 5),
    PA_CPU_X86_SSE4_1    = (1 << 6),
    PA_CPU_X86_SSE4_2    = (1 << 7),
    PA_CPU_X86_3DNOW     = (1 << 8),
    PA_CPU_X86_3DNOWEXT  = (1 << 9),
    PA_CPU_X86_CMOV      = (1 << 10)
} pa_cpu_x86_flag_t;

void pa_cpu_get_x86_flags(pa_cpu_x86_flag_t *flags);
bool pa_cpu_init_x86 (pa_cpu_x86_flag_t *flags);

#if defined (__i386__)
typedef int32_t pa_reg_x86;
#elif defined (__amd64__)
typedef int64_t pa_reg_x86;
#endif

/* some optimized functions */
void pa_volume_func_init_mmx(pa_cpu_x86_flag_t flags);
void pa_volume_func_init_sse(pa_cpu_x86_flag_t flags);

void pa_remap_func_init_mmx(pa_cpu_x86_flag_t flags);
void pa_remap_func_init_sse(pa_cpu_x86_flag_t flags);

void pa_convert_func_init_sse (pa_cpu_x86_flag_t flags);

#endif /* foocpux86hfoo */
