/***
  This file is part of PulseAudio.

  Copyright 2014 Peter Meerwald <pmeerw@pmeerw.net>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "cpu.h"
#include "cpu-orc.h"

void pa_cpu_init(pa_cpu_info *cpu_info) {
    cpu_info->cpu_type = PA_CPU_UNDEFINED;
    /* don't force generic code, used for testing only */
    cpu_info->force_generic_code = false;
    if (!getenv("PULSE_NO_SIMD")) {
        if (pa_cpu_init_x86(&cpu_info->flags.x86))
            cpu_info->cpu_type = PA_CPU_X86;
        else if (pa_cpu_init_arm(&cpu_info->flags.arm))
            cpu_info->cpu_type = PA_CPU_ARM;
        pa_cpu_init_orc(*cpu_info);
    }

    pa_remap_func_init(cpu_info);
    pa_mix_func_init(cpu_info);
}
