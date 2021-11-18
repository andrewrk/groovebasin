#ifndef foosocketclienthfoo
#define foosocketclienthfoo

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

#include <inttypes.h>

#include <pulse/mainloop-api.h>
#include <pulsecore/iochannel.h>

struct sockaddr;

typedef struct pa_socket_client pa_socket_client;

typedef void (*pa_socket_client_cb_t)(pa_socket_client *c, pa_iochannel*io, void *userdata);

pa_socket_client* pa_socket_client_new_ipv4(pa_mainloop_api *m, uint32_t address, uint16_t port);
pa_socket_client* pa_socket_client_new_ipv6(pa_mainloop_api *m, uint8_t address[16], uint16_t port);
pa_socket_client* pa_socket_client_new_unix(pa_mainloop_api *m, const char *filename);
pa_socket_client* pa_socket_client_new_sockaddr(pa_mainloop_api *m, const struct sockaddr *sa, size_t salen);
pa_socket_client* pa_socket_client_new_string(pa_mainloop_api *m, bool use_rtclock, const char *a, uint16_t default_port);

pa_socket_client* pa_socket_client_ref(pa_socket_client *c);
void pa_socket_client_unref(pa_socket_client *c);

void pa_socket_client_set_callback(pa_socket_client *c, pa_socket_client_cb_t on_connection, void *userdata);

bool pa_socket_client_is_local(pa_socket_client *c);

#endif
