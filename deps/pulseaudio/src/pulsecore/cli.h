#ifndef fooclihfoo
#define fooclihfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <pulsecore/iochannel.h>
#include <pulsecore/core.h>
#include <pulsecore/module.h>

typedef struct pa_cli pa_cli;

typedef void (*pa_cli_eof_cb_t)(pa_cli *c, void *userdata);

/* Create a new command line session on the specified io channel owned by the specified module */
pa_cli* pa_cli_new(pa_core *core, pa_iochannel *io, pa_module *m);
void pa_cli_free(pa_cli *cli);

/* Set a callback function that is called whenever the command line session is terminated */
void pa_cli_set_eof_callback(pa_cli *cli, pa_cli_eof_cb_t cb, void *userdata);

pa_module *pa_cli_get_module(pa_cli *c);

#endif
