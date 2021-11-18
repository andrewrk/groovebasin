/***
  This file is part of PulseAudio.

  Copyright 2020 Igor V. Kovalenko <igor.v.kovalenko@gmail.com>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <dirent.h>

#include <pulse/xmalloc.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>

#include "database.h"
#include "core-error.h"

pa_database* pa_database_open(const char *path, const char *fn, bool prependmid, bool for_write) {

    const char *filename_suffix = pa_database_get_filename_suffix();

    char *machine_id = NULL, *filename_prefix, *full_path;

    DIR *database_dir = NULL;
    struct dirent *de;

    pa_database *f;

    pa_assert(filename_suffix && filename_suffix[0]);

    if (prependmid && !(machine_id = pa_machine_id())) {
        return NULL;
    }

    /* Database file name starts with ${machine_id}-${fn} */
    if (machine_id)
        filename_prefix = pa_sprintf_malloc("%s-%s", machine_id, fn);
    else
        filename_prefix = pa_xstrdup(fn);

    /* Search for existing database directory entry name matching architecture suffix and filename suffix. */
    database_dir = opendir(path);

    if (database_dir) {
        for (;;) {
            errno = 0;
            de = readdir(database_dir);
            if (!de) {
                if (errno) {
                    pa_log_warn("Unable to search for existing database file, readdir() failed: %s", pa_cstrerror(errno));
                    /* can continue as if there is no matching database file candidate */
                }

                break;
            }

            if (pa_startswith(de->d_name, filename_prefix)
                && de->d_name[strlen(filename_prefix)] == '.'
                && pa_endswith(de->d_name + strlen(filename_prefix) + 1, filename_suffix)) {

                /* candidate filename found, replace filename_prefix with this one */

                pa_log_debug("Found existing database file '%s/%s', using it", path, de->d_name);
                pa_xfree(filename_prefix);
                filename_prefix = pa_xstrndup(de->d_name, strlen(de->d_name) - strlen(filename_suffix));
                break;
            }
        }

        closedir(database_dir);
    } else {
        pa_log_warn("Unable to search for existing database file, failed to open directory %s: %s", path, pa_cstrerror(errno));
    }

    full_path = pa_sprintf_malloc("%s" PA_PATH_SEP "%s%s", path, filename_prefix, filename_suffix);

    f = pa_database_open_internal(full_path, for_write);

    if (f)
        pa_log_info("Successfully opened '%s' database file '%s'.", fn, full_path);
    else
        pa_log("Failed to open '%s' database file '%s': %s", fn, full_path, pa_cstrerror(errno));

    pa_xfree(full_path);
    pa_xfree(filename_prefix);

    /* deallocate machine_id if it was used to construct file name */
    pa_xfree(machine_id);

    return f;
}
