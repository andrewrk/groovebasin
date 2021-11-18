#ifndef fooiochannelhfoo
#define fooiochannelhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

#include <sys/types.h>

#include <pulse/mainloop-api.h>
#include <pulsecore/creds.h>
#include <pulsecore/macro.h>

/* A wrapper around UNIX file descriptors for attaching them to the a
   main event loop. Every time new data may be read or be written to
   the channel a callback function is called. It is safe to destroy
   the calling iochannel object from the callback */

typedef struct pa_iochannel pa_iochannel;

/* Create a new IO channel for the specified file descriptors for
input resp. output. It is safe to pass the same file descriptor for
both parameters (in case of full-duplex channels). For a simplex
channel specify -1 for the other direction. */

pa_iochannel* pa_iochannel_new(pa_mainloop_api*m, int ifd, int ofd);
void pa_iochannel_free(pa_iochannel*io);

/* Returns: length written on success, 0 if a retry is needed, negative value
 * on error. */
ssize_t pa_iochannel_write(pa_iochannel*io, const void*data, size_t l);
ssize_t pa_iochannel_read(pa_iochannel*io, void*data, size_t l);

#ifdef HAVE_CREDS
bool pa_iochannel_creds_supported(pa_iochannel *io);
int pa_iochannel_creds_enable(pa_iochannel *io);

ssize_t pa_iochannel_write_with_fds(pa_iochannel*io, const void*data, size_t l, int nfd, const int *fds);
ssize_t pa_iochannel_write_with_creds(pa_iochannel*io, const void*data, size_t l, const pa_creds *ucred);
ssize_t pa_iochannel_read_with_ancil_data(pa_iochannel*io, void*data, size_t l, pa_cmsg_ancil_data *ancil_data);
#endif

bool pa_iochannel_is_readable(pa_iochannel*io);
bool pa_iochannel_is_writable(pa_iochannel*io);
bool pa_iochannel_is_hungup(pa_iochannel*io);

/* Don't close the file descriptors when the io channel is freed. By
 * default the file descriptors are closed. */
void pa_iochannel_set_noclose(pa_iochannel*io, bool b);

/* Set the callback function that is called whenever data becomes available for read or write */
typedef void (*pa_iochannel_cb_t)(pa_iochannel*io, void *userdata);
void pa_iochannel_set_callback(pa_iochannel*io, pa_iochannel_cb_t callback, void *userdata);

/* In case the file descriptor is a socket, return a pretty-printed string in *s which describes the peer connected */
void pa_iochannel_socket_peer_to_string(pa_iochannel*io, char*s, size_t l);

/* Use setsockopt() to tune the receive and send buffers of TCP sockets */
int pa_iochannel_socket_set_rcvbuf(pa_iochannel*io, size_t l);
int pa_iochannel_socket_set_sndbuf(pa_iochannel*io, size_t l);

bool pa_iochannel_socket_is_local(pa_iochannel *io);

pa_mainloop_api* pa_iochannel_get_mainloop_api(pa_iochannel *io);

int pa_iochannel_get_recv_fd(pa_iochannel *io);
int pa_iochannel_get_send_fd(pa_iochannel *io);

#endif
