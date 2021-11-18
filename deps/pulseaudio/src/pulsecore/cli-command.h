#ifndef fooclicommandhfoo
#define fooclicommandhfoo

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

#include <pulsecore/strbuf.h>
#include <pulsecore/core.h>

/* Execute a single CLI command. Write the results to the string
 * buffer *buf. If *fail is non-zero the function will return -1 when
 * one or more of the executed commands failed. *fail
 * may be modified by the function call. */
int pa_cli_command_execute_line(pa_core *c, const char *s, pa_strbuf *buf, bool *fail);

/* Execute a whole file of CLI commands */
int pa_cli_command_execute_file(pa_core *c, const char *fn, pa_strbuf *buf, bool *fail);

/* Execute a whole file of CLI commands */
int pa_cli_command_execute_file_stream(pa_core *c, FILE *f, pa_strbuf *buf, bool *fail);

/* Split the specified string into lines and run pa_cli_command_execute_line() for each. */
int pa_cli_command_execute(pa_core *c, const char *s, pa_strbuf *buf, bool *fail);

/* Same as pa_cli_command_execute_line() but also take ifstate var. */
int pa_cli_command_execute_line_stateful(pa_core *c, const char *s, pa_strbuf *buf, bool *fail, int *ifstate);

#endif
