/***
  This file is part of PulseAudio.

  Copyright 2016 Ahmed S. Darwish <darwish.07@gmail.com>

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

#include <pulsecore/core-util.h>
#include <pulsecore/creds.h>
#include <pulsecore/macro.h>
#include <pulsecore/pdispatch.h>
#include <pulsecore/pstream.h>
#include <pulsecore/tagstruct.h>

#include "native-common.h"

/*
 * Command handlers shared between client and server
 */

/* Check pa_pstream_register_memfd_mempool() for further details */
int pa_common_command_register_memfd_shmid(pa_pstream *p, pa_pdispatch *pd, uint32_t version,
                                           uint32_t command, pa_tagstruct *t) {
#if defined(HAVE_CREDS) && defined(HAVE_MEMFD)
    pa_cmsg_ancil_data *ancil = NULL;
    unsigned shm_id;
    int ret = -1;

    pa_assert(pd);
    pa_assert(command == PA_COMMAND_REGISTER_MEMFD_SHMID);
    pa_assert(t);

    ancil = pa_pdispatch_take_ancil_data(pd);
    if (!ancil)
        goto finish;

    /* Upon fd leaks and reaching our open fd limit, recvmsg(2)
     * just strips all passed fds from the ancillary data */
    if (ancil->nfd == 0) {
        pa_log("Expected 1 memfd fd to be received over pipe; got 0");
        pa_log("Did we reach our open file descriptors limit?");
        goto finish;
    }

    if (ancil->nfd != 1 || ancil->fds[0] == -1)
        goto finish;

    if (version < 31 || pa_tagstruct_getu32(t, &shm_id) < 0 || !pa_tagstruct_eof(t))
        goto finish;

    pa_pstream_attach_memfd_shmid(p, shm_id, ancil->fds[0]);

    ret = 0;
finish:
    if (ancil)
        pa_cmsg_ancil_data_close_fds(ancil);

    return ret;
#else
    return -1;
#endif
}
