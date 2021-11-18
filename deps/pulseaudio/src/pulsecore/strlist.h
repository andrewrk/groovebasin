#ifndef foostrlisthfoo
#define foostrlisthfoo

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

typedef struct pa_strlist pa_strlist;

/* Add the specified server string to the list, return the new linked list head */
pa_strlist* pa_strlist_prepend(pa_strlist *l, const char *s);

/* Remove the specified string from the list, return the new linked list head */
pa_strlist* pa_strlist_remove(pa_strlist *l, const char *s);

/* Make a whitespace separated string of all server strings. Returned memory has to be freed with pa_xfree() */
char *pa_strlist_to_string(pa_strlist *l);

/* Free the entire list */
void pa_strlist_free(pa_strlist *l);

/* Return the next entry in the list in *string and remove it from
 * the list. Returns the new list head. The memory *string points to
 * has to be freed with pa_xfree() */
pa_strlist* pa_strlist_pop(pa_strlist *l, char **s);

/* Parse a whitespace separated server list */
pa_strlist* pa_strlist_parse(const char *s);

/* Reverse string list */
pa_strlist *pa_strlist_reverse(pa_strlist *l);

/* Return the next item in the list */
pa_strlist *pa_strlist_next(pa_strlist *s);

/* Return the string associated to the current item */
const char *pa_strlist_data(pa_strlist *s);

#endif
