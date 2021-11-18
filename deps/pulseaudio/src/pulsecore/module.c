/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ltdl.h>

#include <pulse/xmalloc.h>
#include <pulse/proplist.h>

#include <pulsecore/core-subscribe.h>
#include <pulsecore/log.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/ltdl-helper.h>
#include <pulsecore/modinfo.h>

#include "module.h"

#define PA_SYMBOL_INIT "pa__init"
#define PA_SYMBOL_DONE "pa__done"
#define PA_SYMBOL_LOAD_ONCE "pa__load_once"
#define PA_SYMBOL_GET_N_USED "pa__get_n_used"
#define PA_SYMBOL_GET_DEPRECATE "pa__get_deprecated"

bool pa_module_exists(const char *name) {
    const char *paths, *state = NULL;
    char *n, *p, *pathname;
    bool result;

    pa_assert(name);

    if (name[0] == PA_PATH_SEP_CHAR) {
        result = access(name, F_OK) == 0 ? true : false;
        pa_log_debug("Checking for existence of '%s': %s", name, result ? "success" : "failure");
        if (result)
            return true;
    }

    if (!(paths = lt_dlgetsearchpath()))
        return false;

    /* strip .so from the end of name, if present */
    n = pa_xstrdup(name);
    p = strrchr(n, '.');
    if (p && pa_streq(p, PA_SOEXT))
        p[0] = 0;

    while ((p = pa_split(paths, ":", &state))) {
        pathname = pa_sprintf_malloc("%s" PA_PATH_SEP "%s" PA_SOEXT, p, n);
        result = access(pathname, F_OK) == 0 ? true : false;
        pa_log_debug("Checking for existence of '%s': %s", pathname, result ? "success" : "failure");
        pa_xfree(pathname);
        pa_xfree(p);
        if (result) {
            pa_xfree(n);
            return true;
        }
    }

    state = NULL;
    if (PA_UNLIKELY(pa_run_from_build_tree())) {
        while ((p = pa_split(paths, ":", &state))) {
#ifdef MESON_BUILD
            pathname = pa_sprintf_malloc("%s" PA_PATH_SEP "src" PA_PATH_SEP "modules" PA_PATH_SEP "%s" PA_SOEXT, p, n);
#else
            pathname = pa_sprintf_malloc("%s" PA_PATH_SEP ".libs" PA_PATH_SEP "%s" PA_SOEXT, p, n);
#endif
            result = access(pathname, F_OK) == 0 ? true : false;
            pa_log_debug("Checking for existence of '%s': %s", pathname, result ? "success" : "failure");
            pa_xfree(pathname);
            pa_xfree(p);
            if (result) {
                pa_xfree(n);
                return true;
            }
        }
    }

    pa_xfree(n);
    return false;
}

void pa_module_hook_connect(pa_module *m, pa_hook *hook, pa_hook_priority_t prio, pa_hook_cb_t cb, void *data) {
    pa_assert(m);
    pa_assert(hook);
    pa_assert(m->hooks);
    pa_dynarray_append(m->hooks, pa_hook_connect(hook, prio, cb, data));
}

int pa_module_load(pa_module** module, pa_core *c, const char *name, const char *argument) {
    pa_module *m = NULL;
    bool (*load_once)(void);
    const char* (*get_deprecated)(void);
    pa_modinfo *mi;
    int errcode, rval;

    pa_assert(module);
    pa_assert(c);
    pa_assert(name);

    if (c->disallow_module_loading) {
        errcode = -PA_ERR_ACCESS;
        goto fail;
    }

    m = pa_xnew(pa_module, 1);
    m->name = pa_xstrdup(name);
    m->argument = pa_xstrdup(argument);
    m->load_once = false;
    m->proplist = pa_proplist_new();
    m->hooks = pa_dynarray_new((pa_free_cb_t) pa_hook_slot_free);
    m->index = PA_IDXSET_INVALID;

    if (!(m->dl = lt_dlopenext(name))) {
        /* We used to print the error that is returned by lt_dlerror(), but
         * lt_dlerror() is useless. It returns pretty much always "file not
         * found". That's because if there are any problems with loading the
         * module with normal loaders, libltdl falls back to the "preload"
         * loader, which never finds anything, and therefore says "file not
         * found". */
        pa_log("Failed to open module \"%s\".", name);
        errcode = -PA_ERR_IO;
        goto fail;
    }

    if ((load_once = (bool (*)(void)) pa_load_sym(m->dl, name, PA_SYMBOL_LOAD_ONCE))) {

        m->load_once = load_once();

        if (m->load_once) {
            pa_module *i;
            uint32_t idx;
            /* OK, the module only wants to be loaded once, let's make sure it is */

            PA_IDXSET_FOREACH(i, c->modules, idx) {
                if (pa_streq(name, i->name)) {
                    pa_log("Module \"%s\" should be loaded once at most. Refusing to load.", name);
                    errcode = -PA_ERR_EXIST;
                    goto fail;
                }
            }
        }
    }

    if ((get_deprecated = (const char* (*) (void)) pa_load_sym(m->dl, name, PA_SYMBOL_GET_DEPRECATE))) {
        const char *t;

        if ((t = get_deprecated()))
            pa_log_warn("%s is deprecated: %s", name, t);
    }

    if (!(m->init = (int (*)(pa_module*_m)) pa_load_sym(m->dl, name, PA_SYMBOL_INIT))) {
        pa_log("Failed to load module \"%s\": symbol \""PA_SYMBOL_INIT"\" not found.", name);
        errcode = -PA_ERR_IO;
        goto fail;
    }

    m->done = (void (*)(pa_module*_m)) pa_load_sym(m->dl, name, PA_SYMBOL_DONE);
    m->get_n_used = (int (*)(pa_module*_m)) pa_load_sym(m->dl, name, PA_SYMBOL_GET_N_USED);
    m->userdata = NULL;
    m->core = c;
    m->unload_requested = false;

    pa_assert_se(pa_idxset_put(c->modules, m, &m->index) >= 0);
    pa_assert(m->index != PA_IDXSET_INVALID);

    if ((rval = m->init(m)) < 0) {
        if (rval == -PA_MODULE_ERR_SKIP) {
            errcode = -PA_ERR_NOENTITY;
            goto fail;
        }
        pa_log_error("Failed to load module \"%s\" (argument: \"%s\"): initialization failed.", name, argument ? argument : "");
        errcode = -PA_ERR_IO;
        goto fail;
    }

    pa_log_info("Loaded \"%s\" (index: #%u; argument: \"%s\").", m->name, m->index, m->argument ? m->argument : "");

    pa_subscription_post(c, PA_SUBSCRIPTION_EVENT_MODULE|PA_SUBSCRIPTION_EVENT_NEW, m->index);

    if ((mi = pa_modinfo_get_by_handle(m->dl, name))) {

        if (mi->author && !pa_proplist_contains(m->proplist, PA_PROP_MODULE_AUTHOR))
            pa_proplist_sets(m->proplist, PA_PROP_MODULE_AUTHOR, mi->author);

        if (mi->description && !pa_proplist_contains(m->proplist, PA_PROP_MODULE_DESCRIPTION))
            pa_proplist_sets(m->proplist, PA_PROP_MODULE_DESCRIPTION, mi->description);

        if (mi->version && !pa_proplist_contains(m->proplist, PA_PROP_MODULE_VERSION))
            pa_proplist_sets(m->proplist, PA_PROP_MODULE_VERSION, mi->version);

        pa_modinfo_free(mi);
    }

    pa_hook_fire(&m->core->hooks[PA_CORE_HOOK_MODULE_NEW], m);

    *module = m;

    return 0;

fail:

    if (m) {
        if (m->index != PA_IDXSET_INVALID)
            pa_idxset_remove_by_index(c->modules, m->index);

        if (m->hooks)
            pa_dynarray_free(m->hooks);

        if (m->proplist)
            pa_proplist_free(m->proplist);

        pa_xfree(m->argument);
        pa_xfree(m->name);

        if (m->dl)
            lt_dlclose(m->dl);

        pa_xfree(m);
    }

    *module = NULL;

    return errcode;
}

static void postponed_dlclose(pa_mainloop_api *api, void *userdata) {
    lt_dlhandle dl = userdata;

    lt_dlclose(dl);
}

static void pa_module_free(pa_module *m) {
    pa_assert(m);
    pa_assert(m->core);

    pa_log_info("Unloading \"%s\" (index: #%u).", m->name, m->index);
    pa_hook_fire(&m->core->hooks[PA_CORE_HOOK_MODULE_UNLINK], m);

    if (m->hooks) {
       pa_dynarray_free(m->hooks);
       m->hooks = NULL;
    }

    if (m->done)
        m->done(m);

    if (m->proplist)
        pa_proplist_free(m->proplist);

    /* If a module unloads itself with pa_module_unload(), we can't call
     * lt_dlclose() here, because otherwise pa_module_unload() may return to a
     * code location that has been removed from memory. Therefore, let's
     * postpone the lt_dlclose() call a bit.
     *
     * Apparently lt_dlclose() doesn't always remove the module from memory,
     * but it can happen, as can be seen here:
     * https://bugs.freedesktop.org/show_bug.cgi?id=96831 */
    pa_mainloop_api_once(m->core->mainloop, postponed_dlclose, m->dl);

    pa_hashmap_remove(m->core->modules_pending_unload, m);

    pa_log_info("Unloaded \"%s\" (index: #%u).", m->name, m->index);

    pa_subscription_post(m->core, PA_SUBSCRIPTION_EVENT_MODULE|PA_SUBSCRIPTION_EVENT_REMOVE, m->index);

    pa_xfree(m->name);
    pa_xfree(m->argument);
    pa_xfree(m);
}

void pa_module_unload(pa_module *m, bool force) {
    pa_assert(m);

    if (m->core->disallow_module_loading && !force)
        return;

    if (!(m = pa_idxset_remove_by_data(m->core->modules, m, NULL)))
        return;

    pa_module_free(m);
}

void pa_module_unload_by_index(pa_core *c, uint32_t idx, bool force) {
    pa_module *m;
    pa_assert(c);
    pa_assert(idx != PA_IDXSET_INVALID);

    if (c->disallow_module_loading && !force)
        return;

    if (!(m = pa_idxset_remove_by_index(c->modules, idx)))
        return;

    pa_module_free(m);
}

void pa_module_unload_all(pa_core *c) {
    pa_module *m;
    uint32_t *indices;
    uint32_t state;
    int i;

    pa_assert(c);
    pa_assert(c->modules);

    if (pa_idxset_isempty(c->modules))
        return;

    /* Unload modules in reverse order by default */
    indices = pa_xnew(uint32_t, pa_idxset_size(c->modules));
    i = 0;
    PA_IDXSET_FOREACH(m, c->modules, state)
        indices[i++] = state;
    pa_assert(i == (int) pa_idxset_size(c->modules));
    i--;
    for (; i >= 0; i--) {
        m = pa_idxset_remove_by_index(c->modules, indices[i]);
        if (m)
            pa_module_free(m);
    }
    pa_xfree(indices);

    /* Just in case module unloading caused more modules to load */
    PA_IDXSET_FOREACH(m, c->modules, state)
        pa_log_warn("After module unload, module '%s' was still loaded!", m->name);
    c->disallow_module_loading = 1;
    pa_idxset_remove_all(c->modules, (pa_free_cb_t) pa_module_free);
    pa_assert(pa_idxset_isempty(c->modules));

    if (c->module_defer_unload_event) {
        c->mainloop->defer_free(c->module_defer_unload_event);
        c->module_defer_unload_event = NULL;
    }
    pa_assert(pa_hashmap_isempty(c->modules_pending_unload));
}

static void defer_cb(pa_mainloop_api*api, pa_defer_event *e, void *userdata) {
    pa_core *c = PA_CORE(userdata);
    pa_module *m;

    pa_core_assert_ref(c);
    api->defer_enable(e, 0);

    while ((m = pa_hashmap_first(c->modules_pending_unload)))
        pa_module_unload(m, true);
}

void pa_module_unload_request(pa_module *m, bool force) {
    pa_assert(m);

    if (m->core->disallow_module_loading && !force)
        return;

    m->unload_requested = true;
    pa_hashmap_put(m->core->modules_pending_unload, m, m);

    if (!m->core->module_defer_unload_event)
        m->core->module_defer_unload_event = m->core->mainloop->defer_new(m->core->mainloop, defer_cb, m->core);

    m->core->mainloop->defer_enable(m->core->module_defer_unload_event, 1);
}

void pa_module_unload_request_by_index(pa_core *c, uint32_t idx, bool force) {
    pa_module *m;
    pa_assert(c);

    if (!(m = pa_idxset_get_by_index(c->modules, idx)))
        return;

    pa_module_unload_request(m, force);
}

int pa_module_get_n_used(pa_module*m) {
    pa_assert(m);

    if (!m->get_n_used)
        return -1;

    return m->get_n_used(m);
}

void pa_module_update_proplist(pa_module *m, pa_update_mode_t mode, pa_proplist *p) {
    pa_assert(m);

    if (p)
        pa_proplist_update(m->proplist, mode, p);

    pa_subscription_post(m->core, PA_SUBSCRIPTION_EVENT_MODULE|PA_SUBSCRIPTION_EVENT_CHANGE, m->index);
    pa_hook_fire(&m->core->hooks[PA_CORE_HOOK_MODULE_PROPLIST_CHANGED], m);
}
