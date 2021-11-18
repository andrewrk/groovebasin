#ifndef foocredshfoo
#define foocredshfoo

/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

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

#include <sys/types.h>

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

#include <pulsecore/socket.h>
#include <stdbool.h>

#define MAX_ANCIL_DATA_FDS 2

typedef struct pa_creds pa_creds;
typedef struct pa_cmsg_ancil_data pa_cmsg_ancil_data;

#if defined(SCM_CREDENTIALS)

#define HAVE_CREDS 1

struct pa_creds {
    gid_t gid;
    uid_t uid;
};

/* Struct for handling ancillary data, i e, extra data that can be sent together with a message
 * over unix pipes. Supports sending and receiving credentials and file descriptors. */
struct pa_cmsg_ancil_data {
    pa_creds creds;
    bool creds_valid;
    int nfd;

    /* Don't close these fds by your own. Check pa_cmsg_ancil_data_close_fds() */
    int fds[MAX_ANCIL_DATA_FDS];
    bool close_fds_on_cleanup;
};

void pa_cmsg_ancil_data_close_fds(struct pa_cmsg_ancil_data *ancil);

#else
#undef HAVE_CREDS
#endif

#endif
