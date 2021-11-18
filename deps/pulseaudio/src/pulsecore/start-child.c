/***
  This file is part of PulseAudio.

  Copyright 2007 Lennart Poettering

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
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/pipe.h>

#include "start-child.h"

int pa_start_child_for_read(const char *name, const char *argv1, pid_t *pid) {
#ifdef HAVE_FORK
    pid_t child;
    int pipe_fds[2] = { -1, -1 };

    if (pipe(pipe_fds) < 0) {
        pa_log("pipe() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    if ((child = fork()) == (pid_t) -1) {
        pa_log("fork() failed: %s", pa_cstrerror(errno));
        goto fail;

    } else if (child != 0) {

        /* Parent */
        pa_assert_se(pa_close(pipe_fds[1]) == 0);

        if (pid)
            *pid = child;

        return pipe_fds[0];
    } else {
        /* child */

        pa_reset_personality();

        pa_assert_se(pa_close(pipe_fds[0]) == 0);
        pa_assert_se(dup2(pipe_fds[1], STDOUT_FILENO) == STDOUT_FILENO);

        if (pipe_fds[1] != STDOUT_FILENO)
            pa_assert_se(pa_close(pipe_fds[1]) == 0);

        pa_close(STDIN_FILENO);
        pa_assert_se(open("/dev/null", O_RDONLY) == STDIN_FILENO);

        pa_close(STDERR_FILENO);
        pa_assert_se(open("/dev/null", O_WRONLY) == STDERR_FILENO);

        pa_close_all(-1);
        pa_reset_sigs(-1);
        pa_unblock_sigs(-1);
        pa_reset_priority();
        pa_unset_env_recorded();

        /* Make sure our children are not influenced by the
         * LD_BIND_NOW we set for ourselves. */
        pa_unset_env("LD_BIND_NOW");

#ifdef PR_SET_PDEATHSIG
        /* On Linux we can use PR_SET_PDEATHSIG to have the helper
        process killed when the daemon dies abnormally. On non-Linux
        machines the client will die as soon as it writes data to
        stdout again (SIGPIPE) */

        prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
#endif

        execl(name, name, argv1, NULL);
        _exit(1);
    }

fail:
    pa_close_pipe(pipe_fds);
#endif /* HAVE_FORK */

    return -1;
}
