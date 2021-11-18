#ifndef foocoremessageshfoo
#define foocoremessageshfoo

/***
  This file is part of PulseAudio.

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

#include <pulsecore/core.h>

/* Message handler types and functions */

/* Prototype for message callback */
typedef int (*pa_message_handler_cb_t)(
        const char *object_path,
        const char *message,
        const char *message_parameters,
        char **response,
        void *userdata);

/* Message handler object */
struct pa_message_handler {
    char *object_path;
    char *description;
    pa_message_handler_cb_t callback;
    void *userdata;
};

/* Handler registration */
void pa_message_handler_register(pa_core *c, const char *object_path, const char *description, pa_message_handler_cb_t cb, void *userdata);
void pa_message_handler_unregister(pa_core *c, const char *object_path);

/* Send message to the specified object path */
int pa_message_handler_send_message(pa_core *c, const char *object_path, const char *message, const char *message_parameters, char **response);

/* Set handler description */
int pa_message_handler_set_description(pa_core *c, const char *object_path, const char *description);
#endif
