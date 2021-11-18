/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#ifdef HAVE_GLOB_H
#include <glob.h>
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <pulse/mainloop.h>
#include <pulse/channelmap.h>
#include <pulse/timeval.h>
#include <pulse/util.h>
#include <pulse/volume.h>
#include <pulse/xmalloc.h>
#include <pulse/rtclock.h>

#include <pulsecore/sink-input.h>
#include <pulsecore/play-memchunk.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/namereg.h>
#include <pulsecore/sound-file.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/core-error.h>
#include <pulsecore/macro.h>

#include "core-scache.h"

#define UNLOAD_POLL_TIME (60 * PA_USEC_PER_SEC)

static void timeout_callback(pa_mainloop_api *m, pa_time_event *e, const struct timeval *t, void *userdata) {
    pa_core *c = userdata;

    pa_assert(c);
    pa_assert(c->mainloop == m);
    pa_assert(c->scache_auto_unload_event == e);

    pa_scache_unload_unused(c);

    pa_core_rttime_restart(c, e, pa_rtclock_now() + UNLOAD_POLL_TIME);
}

static void free_entry(pa_scache_entry *e) {
    pa_assert(e);

    pa_namereg_unregister(e->core, e->name);
    pa_subscription_post(e->core, PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE|PA_SUBSCRIPTION_EVENT_REMOVE, e->index);
    pa_hook_fire(&e->core->hooks[PA_CORE_HOOK_SAMPLE_CACHE_UNLINK], e);
    pa_xfree(e->name);
    pa_xfree(e->filename);
    if (e->memchunk.memblock)
        pa_memblock_unref(e->memchunk.memblock);
    if (e->proplist)
        pa_proplist_free(e->proplist);
    pa_xfree(e);
}

static pa_scache_entry* scache_add_item(pa_core *c, const char *name, bool *new_sample) {
    pa_scache_entry *e;

    pa_assert(c);
    pa_assert(name);
    pa_assert(new_sample);

    if ((e = pa_namereg_get(c, name, PA_NAMEREG_SAMPLE))) {
        if (e->memchunk.memblock)
            pa_memblock_unref(e->memchunk.memblock);

        pa_xfree(e->filename);
        pa_proplist_clear(e->proplist);

        pa_assert(e->core == c);

        pa_subscription_post(c, PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE|PA_SUBSCRIPTION_EVENT_CHANGE, e->index);
        *new_sample = false;
    } else {
        e = pa_xnew(pa_scache_entry, 1);

        if (!pa_namereg_register(c, name, PA_NAMEREG_SAMPLE, e, true)) {
            pa_xfree(e);
            return NULL;
        }

        e->name = pa_xstrdup(name);
        e->core = c;
        e->proplist = pa_proplist_new();

        pa_idxset_put(c->scache, e, &e->index);

        pa_subscription_post(c, PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE|PA_SUBSCRIPTION_EVENT_NEW, e->index);
        *new_sample = true;
    }

    e->last_used_time = 0;
    pa_memchunk_reset(&e->memchunk);
    e->filename = NULL;
    e->lazy = false;
    e->last_used_time = 0;

    pa_sample_spec_init(&e->sample_spec);
    pa_channel_map_init(&e->channel_map);
    pa_cvolume_init(&e->volume);
    e->volume_is_set = false;

    pa_proplist_sets(e->proplist, PA_PROP_MEDIA_ROLE, "event");

    return e;
}

int pa_scache_add_item(
        pa_core *c,
        const char *name,
        const pa_sample_spec *ss,
        const pa_channel_map *map,
        const pa_memchunk *chunk,
        pa_proplist *p,
        uint32_t *idx) {

    pa_scache_entry *e;
    char st[PA_SAMPLE_SPEC_SNPRINT_MAX];
    pa_channel_map tmap;
    bool new_sample;

    pa_assert(c);
    pa_assert(name);
    pa_assert(!ss || pa_sample_spec_valid(ss));
    pa_assert(!map || (pa_channel_map_valid(map) && ss && pa_channel_map_compatible(map, ss)));

    if (ss && !map) {
        pa_channel_map_init_extend(&tmap, ss->channels, PA_CHANNEL_MAP_DEFAULT);
        map = &tmap;
    }

    if (chunk && chunk->length > PA_SCACHE_ENTRY_SIZE_MAX)
        return -1;

    if (!(e = scache_add_item(c, name, &new_sample)))
        return -1;

    pa_sample_spec_init(&e->sample_spec);
    pa_channel_map_init(&e->channel_map);
    pa_cvolume_init(&e->volume);
    e->volume_is_set = false;

    if (ss) {
        e->sample_spec = *ss;
        pa_cvolume_reset(&e->volume, ss->channels);
    }

    if (map)
        e->channel_map = *map;

    if (chunk) {
        e->memchunk = *chunk;
        pa_memblock_ref(e->memchunk.memblock);
    }

    if (p)
        pa_proplist_update(e->proplist, PA_UPDATE_REPLACE, p);

    if (idx)
        *idx = e->index;

    pa_log_debug("Created sample \"%s\" (#%d), %lu bytes with sample spec %s",
                 name, e->index, (unsigned long) e->memchunk.length,
                 pa_sample_spec_snprint(st, sizeof(st), &e->sample_spec));

    pa_hook_fire(&e->core->hooks[new_sample ? PA_CORE_HOOK_SAMPLE_CACHE_NEW : PA_CORE_HOOK_SAMPLE_CACHE_CHANGED], e);

    return 0;
}

int pa_scache_add_file(pa_core *c, const char *name, const char *filename, uint32_t *idx) {
    pa_sample_spec ss;
    pa_channel_map map;
    pa_memchunk chunk;
    int r;
    pa_proplist *p;

#ifdef OS_IS_WIN32
    char buf[MAX_PATH];

    if (ExpandEnvironmentStrings(filename, buf, MAX_PATH))
        filename = buf;
#endif

    pa_assert(c);
    pa_assert(name);
    pa_assert(filename);

    p = pa_proplist_new();
    pa_proplist_sets(p, PA_PROP_MEDIA_FILENAME, filename);

    if (pa_sound_file_load(c->mempool, filename, &ss, &map, &chunk, p) < 0) {
        pa_proplist_free(p);
        return -1;
    }

    r = pa_scache_add_item(c, name, &ss, &map, &chunk, p, idx);
    pa_memblock_unref(chunk.memblock);
    pa_proplist_free(p);

    return r;
}

int pa_scache_add_file_lazy(pa_core *c, const char *name, const char *filename, uint32_t *idx) {
    pa_scache_entry *e;
    bool new_sample;

#ifdef OS_IS_WIN32
    char buf[MAX_PATH];

    if (ExpandEnvironmentStrings(filename, buf, MAX_PATH))
        filename = buf;
#endif

    pa_assert(c);
    pa_assert(name);
    pa_assert(filename);

    if (!(e = scache_add_item(c, name, &new_sample)))
        return -1;

    e->lazy = true;
    e->filename = pa_xstrdup(filename);

    pa_proplist_sets(e->proplist, PA_PROP_MEDIA_FILENAME, filename);

    if (!c->scache_auto_unload_event)
        c->scache_auto_unload_event = pa_core_rttime_new(c, pa_rtclock_now() + UNLOAD_POLL_TIME, timeout_callback, c);

    if (idx)
        *idx = e->index;

    pa_hook_fire(&e->core->hooks[new_sample ? PA_CORE_HOOK_SAMPLE_CACHE_NEW : PA_CORE_HOOK_SAMPLE_CACHE_CHANGED], e);

    return 0;
}

int pa_scache_remove_item(pa_core *c, const char *name) {
    pa_scache_entry *e;

    pa_assert(c);
    pa_assert(name);

    if (!(e = pa_namereg_get(c, name, PA_NAMEREG_SAMPLE)))
        return -1;

    pa_assert_se(pa_idxset_remove_by_data(c->scache, e, NULL) == e);

    pa_log_debug("Removed sample \"%s\"", name);

    free_entry(e);

    return 0;
}

void pa_scache_free_all(pa_core *c) {
    pa_assert(c);

    pa_idxset_remove_all(c->scache, (pa_free_cb_t) free_entry);

    if (c->scache_auto_unload_event) {
        c->mainloop->time_free(c->scache_auto_unload_event);
        c->scache_auto_unload_event = NULL;
    }
}

int pa_scache_play_item(pa_core *c, const char *name, pa_sink *sink, pa_volume_t volume, pa_proplist *p, uint32_t *sink_input_idx) {
    pa_scache_entry *e;
    pa_cvolume r;
    pa_proplist *merged;
    bool pass_volume;

    pa_assert(c);
    pa_assert(name);
    pa_assert(sink);

    if (!(e = pa_namereg_get(c, name, PA_NAMEREG_SAMPLE)))
        return -1;

    merged = pa_proplist_new();
    pa_proplist_sets(merged, PA_PROP_MEDIA_NAME, name);
    pa_proplist_sets(merged, PA_PROP_EVENT_ID, name);

    if (e->lazy && !e->memchunk.memblock) {
        pa_channel_map old_channel_map = e->channel_map;

        if (pa_sound_file_load(c->mempool, e->filename, &e->sample_spec, &e->channel_map, &e->memchunk, merged) < 0)
            goto fail;

        pa_subscription_post(c, PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE|PA_SUBSCRIPTION_EVENT_CHANGE, e->index);

        if (e->volume_is_set) {
            if (pa_cvolume_valid(&e->volume))
                pa_cvolume_remap(&e->volume, &old_channel_map, &e->channel_map);
            else
                pa_cvolume_reset(&e->volume, e->sample_spec.channels);
        }
    }

    if (!e->memchunk.memblock)
        goto fail;

    pa_log_debug("Playing sample \"%s\" on \"%s\"", name, sink->name);

    pass_volume = true;

    if (e->volume_is_set && PA_VOLUME_IS_VALID(volume)) {
        pa_cvolume_set(&r, e->sample_spec.channels, volume);
        pa_sw_cvolume_multiply(&r, &r, &e->volume);
    } else if (e->volume_is_set)
        r = e->volume;
    else if (PA_VOLUME_IS_VALID(volume))
        pa_cvolume_set(&r, e->sample_spec.channels, volume);
    else
        pass_volume = false;

    pa_proplist_update(merged, PA_UPDATE_REPLACE, e->proplist);

    if (p)
        pa_proplist_update(merged, PA_UPDATE_REPLACE, p);

    if (pa_play_memchunk(sink,
                         &e->sample_spec, &e->channel_map,
                         &e->memchunk,
                         pass_volume ? &r : NULL,
                         merged,
                         PA_SINK_INPUT_NO_CREATE_ON_SUSPEND|PA_SINK_INPUT_KILL_ON_SUSPEND, sink_input_idx) < 0)
        goto fail;

    pa_proplist_free(merged);

    if (e->lazy)
        time(&e->last_used_time);

    return 0;

fail:
    pa_proplist_free(merged);
    return -1;
}

int pa_scache_play_item_by_name(pa_core *c, const char *name, const char*sink_name, pa_volume_t volume, pa_proplist *p, uint32_t *sink_input_idx) {
    pa_sink *sink;

    pa_assert(c);
    pa_assert(name);

    if (!(sink = pa_namereg_get(c, sink_name, PA_NAMEREG_SINK)))
        return -1;

    return pa_scache_play_item(c, name, sink, volume, p, sink_input_idx);
}

const char *pa_scache_get_name_by_id(pa_core *c, uint32_t id) {
    pa_scache_entry *e;

    pa_assert(c);
    pa_assert(id != PA_IDXSET_INVALID);

    if (!c->scache || !(e = pa_idxset_get_by_index(c->scache, id)))
        return NULL;

    return e->name;
}

uint32_t pa_scache_get_id_by_name(pa_core *c, const char *name) {
    pa_scache_entry *e;

    pa_assert(c);
    pa_assert(name);

    if (!(e = pa_namereg_get(c, name, PA_NAMEREG_SAMPLE)))
        return PA_IDXSET_INVALID;

    return e->index;
}

size_t pa_scache_total_size(pa_core *c) {
    pa_scache_entry *e;
    uint32_t idx;
    size_t sum = 0;

    pa_assert(c);

    if (!c->scache || !pa_idxset_size(c->scache))
        return 0;

    PA_IDXSET_FOREACH(e, c->scache, idx)
        if (e->memchunk.memblock)
            sum += e->memchunk.length;

    return sum;
}

void pa_scache_unload_unused(pa_core *c) {
    pa_scache_entry *e;
    time_t now;
    uint32_t idx;

    pa_assert(c);

    if (!c->scache || !pa_idxset_size(c->scache))
        return;

    time(&now);

    PA_IDXSET_FOREACH(e, c->scache, idx) {

        if (!e->lazy || !e->memchunk.memblock)
            continue;

        if (e->last_used_time + c->scache_idle_time > now)
            continue;

        pa_memblock_unref(e->memchunk.memblock);
        pa_memchunk_reset(&e->memchunk);

        pa_subscription_post(c, PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE|PA_SUBSCRIPTION_EVENT_CHANGE, e->index);
    }
}

static void add_file(pa_core *c, const char *pathname) {
    struct stat st;
    const char *e;

    pa_core_assert_ref(c);
    pa_assert(pathname);

    e = pa_path_get_filename(pathname);

    if (stat(pathname, &st) < 0) {
        pa_log("stat('%s'): %s", pathname, pa_cstrerror(errno));
        return;
    }

#if defined(S_ISREG) && defined(S_ISLNK)
    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
#endif
        pa_scache_add_file_lazy(c, e, pathname, NULL);
}

int pa_scache_add_directory_lazy(pa_core *c, const char *pathname) {
    DIR *dir;

    pa_core_assert_ref(c);
    pa_assert(pathname);

    /* First try to open this as directory */
    if (!(dir = opendir(pathname))) {
#ifdef HAVE_GLOB_H
        glob_t p;
        unsigned int i;
        /* If that fails, try to open it as shell glob */

        if (glob(pathname, GLOB_ERR|GLOB_NOSORT, NULL, &p) < 0) {
            pa_log("failed to open directory '%s': %s", pathname, pa_cstrerror(errno));
            return -1;
        }

        for (i = 0; i < p.gl_pathc; i++)
            add_file(c, p.gl_pathv[i]);

        globfree(&p);
#else
        return -1;
#endif
    } else {
        struct dirent *e;

        while ((e = readdir(dir))) {
            char *p;

            if (e->d_name[0] == '.')
                continue;

            p = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", pathname, e->d_name);
            add_file(c, p);
            pa_xfree(p);
        }

        closedir(dir);
    }

    return 0;
}
