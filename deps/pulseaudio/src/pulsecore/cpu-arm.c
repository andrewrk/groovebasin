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

#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>

#include <pulse/xmalloc.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>

#include "cpu-arm.h"

#if defined (__arm__) && defined (__linux__)

#define MAX_BUFFER 4096
static char *
get_cpuinfo_line(char *cpuinfo, const char *tag) {
    char *line, *end, *colon;

    if (!(line = strstr(cpuinfo, tag)))
        return NULL;

    if (!(end = strchr(line, '\n')))
        return NULL;

    if (!(colon = strchr(line, ':')))
        return NULL;

    if (++colon >= end)
        return NULL;

    return pa_xstrndup(colon, end - colon);
}

static char *get_cpuinfo(void) {
    char *cpuinfo;
    int n, fd;

    cpuinfo = pa_xmalloc(MAX_BUFFER);

    if ((fd = pa_open_cloexec("/proc/cpuinfo", O_RDONLY, 0)) < 0) {
        pa_xfree(cpuinfo);
        return NULL;
    }

    if ((n = pa_read(fd, cpuinfo, MAX_BUFFER-1, NULL)) < 0) {
        pa_xfree(cpuinfo);
        pa_close(fd);
        return NULL;
    }
    cpuinfo[n] = 0;
    pa_close(fd);

    return cpuinfo;
}
#endif /* defined (__arm__) && defined (__linux__) */

void pa_cpu_get_arm_flags(pa_cpu_arm_flag_t *flags) {
#if defined (__arm__) && defined (__linux__)
    char *cpuinfo, *line;
    int arch, part;

    /* We need to read the CPU flags from /proc/cpuinfo because there is no user
     * space support to get the CPU features. This only works on linux AFAIK. */
    if (!(cpuinfo = get_cpuinfo())) {
        pa_log("Can't read cpuinfo");
        return;
    }

    *flags = 0;

    /* get the CPU architecture */
    if ((line = get_cpuinfo_line(cpuinfo, "CPU architecture"))) {
        arch = strtoul(line, NULL, 0);
        if (arch >= 6)
            *flags |= PA_CPU_ARM_V6;
        if (arch >= 7)
            *flags |= PA_CPU_ARM_V7;

        pa_xfree(line);
    }

    /* get the CPU features */
    if ((line = get_cpuinfo_line(cpuinfo, "Features"))) {
        const char *state = NULL;
        char *current;

        while ((current = pa_split_spaces(line, &state))) {
            if (pa_streq(current, "vfp"))
                *flags |= PA_CPU_ARM_VFP;
            else if (pa_streq(current, "edsp"))
                *flags |= PA_CPU_ARM_EDSP;
            else if (pa_streq(current, "neon"))
                *flags |= PA_CPU_ARM_NEON;
            else if (pa_streq(current, "vfpv3"))
                *flags |= PA_CPU_ARM_VFPV3;

            pa_xfree(current);
        }
        pa_xfree(line);
    }

    /* get the CPU part number */
    if ((line = get_cpuinfo_line(cpuinfo, "CPU part"))) {
        part = strtoul(line, NULL, 0);
        if (part == 0xc08)
            *flags |= PA_CPU_ARM_CORTEX_A8;
        pa_xfree(line);
    }
    pa_xfree(cpuinfo);

    pa_log_info("CPU flags: %s%s%s%s%s%s%s",
          (*flags & PA_CPU_ARM_V6) ? "V6 " : "",
          (*flags & PA_CPU_ARM_V7) ? "V7 " : "",
          (*flags & PA_CPU_ARM_VFP) ? "VFP " : "",
          (*flags & PA_CPU_ARM_EDSP) ? "EDSP " : "",
          (*flags & PA_CPU_ARM_NEON) ? "NEON " : "",
          (*flags & PA_CPU_ARM_VFPV3) ? "VFPV3 " : "",
          (*flags & PA_CPU_ARM_CORTEX_A8) ? "Cortex-A8 " : "");
#endif
}

bool pa_cpu_init_arm(pa_cpu_arm_flag_t *flags) {
#if defined (__arm__)
#if defined (__linux__)
    pa_cpu_get_arm_flags(flags);

    if (*flags & PA_CPU_ARM_V6)
        pa_volume_func_init_arm(*flags);

#ifdef HAVE_NEON
    if (*flags & PA_CPU_ARM_NEON) {
        pa_convert_func_init_neon(*flags);
        pa_mix_func_init_neon(*flags);
        pa_remap_func_init_neon(*flags);
    }
#endif

    return true;

#else /* defined (__linux__) */
    pa_log("Reading ARM CPU features not yet supported on this OS");
#endif /* defined (__linux__) */

#else /* defined (__arm__) */
    return false;
#endif /* defined (__arm__) */
}
