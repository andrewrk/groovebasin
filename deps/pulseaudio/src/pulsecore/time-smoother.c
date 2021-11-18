/***
  This file is part of PulseAudio.

  Copyright 2007 Lennart Poettering

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

#include <stdio.h>
#include <math.h>

#include <pulse/sample.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>

#include "time-smoother.h"

#define HISTORY_MAX 64

/*
 * Implementation of a time smoothing algorithm to synchronize remote
 * clocks to a local one. Evens out noise, adjusts to clock skew and
 * allows cheap estimations of the remote time while clock updates may
 * be seldom and received in non-equidistant intervals.
 *
 * Basically, we estimate the gradient of received clock samples in a
 * certain history window (of size 'history_time') with linear
 * regression. With that info we estimate the remote time in
 * 'adjust_time' ahead and smoothen our current estimation function
 * towards that point with a 3rd order polynomial interpolation with
 * fitting derivatives. (more or less a b-spline)
 *
 * The larger 'history_time' is chosen the better we will suppress
 * noise -- but we'll adjust to clock skew slower..
 *
 * The larger 'adjust_time' is chosen the smoother our estimation
 * function will be -- but we'll adjust to clock skew slower, too.
 *
 * If 'monotonic' is true the resulting estimation function is
 * guaranteed to be monotonic.
 */

struct pa_smoother {
    pa_usec_t adjust_time, history_time;

    pa_usec_t time_offset;

    pa_usec_t px, py;     /* Point p, where we want to reach stability */
    double dp;            /* Gradient we want at point p */

    pa_usec_t ex, ey;     /* Point e, which we estimated before and need to smooth to */
    double de;            /* Gradient we estimated for point e */
    pa_usec_t ry;         /* The original y value for ex */

                          /* History of last measurements */
    pa_usec_t history_x[HISTORY_MAX], history_y[HISTORY_MAX];
    unsigned history_idx, n_history;

    /* To even out for monotonicity */
    pa_usec_t last_y, last_x;

    /* Cached parameters for our interpolation polynomial y=ax^3+b^2+cx */
    double a, b, c;
    bool abc_valid:1;

    bool monotonic:1;
    bool paused:1;
    bool smoothing:1; /* If false we skip the polynomial interpolation step */

    pa_usec_t pause_time;

    unsigned min_history;
};

pa_smoother* pa_smoother_new(
        pa_usec_t adjust_time,
        pa_usec_t history_time,
        bool monotonic,
        bool smoothing,
        unsigned min_history,
        pa_usec_t time_offset,
        bool paused) {

    pa_smoother *s;

    pa_assert(adjust_time > 0);
    pa_assert(history_time > 0);
    pa_assert(min_history >= 2);
    pa_assert(min_history <= HISTORY_MAX);

    s = pa_xnew(pa_smoother, 1);
    s->adjust_time = adjust_time;
    s->history_time = history_time;
    s->min_history = min_history;
    s->monotonic = monotonic;
    s->smoothing = smoothing;

    pa_smoother_reset(s, time_offset, paused);

    return s;
}

void pa_smoother_free(pa_smoother* s) {
    pa_assert(s);

    pa_xfree(s);
}

#define REDUCE(x)                               \
    do {                                        \
        x = (x) % HISTORY_MAX;                  \
    } while(false)

#define REDUCE_INC(x)                           \
    do {                                        \
        x = ((x)+1) % HISTORY_MAX;              \
    } while(false)

static void drop_old(pa_smoother *s, pa_usec_t x) {

    /* Drop items from history which are too old, but make sure to
     * always keep min_history in the history */

    while (s->n_history > s->min_history) {

        if (s->history_x[s->history_idx] + s->history_time >= x)
            /* This item is still valid, and thus all following ones
             * are too, so let's quit this loop */
            break;

        /* Item is too old, let's drop it */
        REDUCE_INC(s->history_idx);

        s->n_history --;
    }
}

static void add_to_history(pa_smoother *s, pa_usec_t x, pa_usec_t y) {
    unsigned j, i;
    pa_assert(s);

    /* First try to update an existing history entry */
    i = s->history_idx;
    for (j = s->n_history; j > 0; j--) {

        if (s->history_x[i] == x) {
            s->history_y[i] = y;
            return;
        }

        REDUCE_INC(i);
    }

    /* Drop old entries */
    drop_old(s, x);

    /* Calculate position for new entry */
    j = s->history_idx + s->n_history;
    REDUCE(j);

    /* Fill in entry */
    s->history_x[j] = x;
    s->history_y[j] = y;

    /* Adjust counter */
    s->n_history ++;

    /* And make sure we don't store more entries than fit in */
    if (s->n_history > HISTORY_MAX) {
        s->history_idx += s->n_history - HISTORY_MAX;
        REDUCE(s->history_idx);
        s->n_history = HISTORY_MAX;
    }
}

static double avg_gradient(pa_smoother *s, pa_usec_t x) {
    unsigned i, j, c = 0;
    int64_t ax = 0, ay = 0, k, t;
    double r;

    /* FIXME: Optimization: Jason Newton suggested that instead of
     * going through the history on each iteration we could calculated
     * avg_gradient() as we go.
     *
     * Second idea: it might make sense to weight history entries:
     * more recent entries should matter more than old ones. */

    /* Too few measurements, assume gradient of 1 */
    if (s->n_history < s->min_history)
        return 1;

    /* First, calculate average of all measurements */
    i = s->history_idx;
    for (j = s->n_history; j > 0; j--) {

        ax += (int64_t) s->history_x[i];
        ay += (int64_t) s->history_y[i];
        c++;

        REDUCE_INC(i);
    }

    pa_assert(c >= s->min_history);
    ax /= c;
    ay /= c;

    /* Now, do linear regression */
    k = t = 0;

    i = s->history_idx;
    for (j = s->n_history; j > 0; j--) {
        int64_t dx, dy;

        dx = (int64_t) s->history_x[i] - ax;
        dy = (int64_t) s->history_y[i] - ay;

        k += dx*dy;
        t += dx*dx;

        REDUCE_INC(i);
    }

    r = (double) k / (double) t;

    return (s->monotonic && r < 0) ? 0 : r;
}

static void calc_abc(pa_smoother *s) {
    pa_usec_t ex, ey, px, py;
    int64_t kx, ky;
    double de, dp;

    pa_assert(s);

    if (s->abc_valid)
        return;

    /* We have two points: (ex|ey) and (px|py) with two gradients at
     * these points de and dp. We do a polynomial
     * interpolation of degree 3 with these 6 values */

    ex = s->ex; ey = s->ey;
    px = s->px; py = s->py;
    de = s->de; dp = s->dp;

    pa_assert(ex < px);

    /* To increase the dynamic range and simplify calculation, we
     * move these values to the origin */
    kx = (int64_t) px - (int64_t) ex;
    ky = (int64_t) py - (int64_t) ey;

    /* Calculate a, b, c for y=ax^3+bx^2+cx */
    s->c = de;
    s->b = (((double) (3*ky)/ (double) kx - dp - (double) (2*de))) / (double) kx;
    s->a = (dp/(double) kx - 2*s->b - de/(double) kx) / (double) (3*kx);

    s->abc_valid = true;
}

static void estimate(pa_smoother *s, pa_usec_t x, pa_usec_t *y, double *deriv) {
    pa_assert(s);
    pa_assert(y);

    if (x >= s->px) {
        /* Linear interpolation right from px */
        int64_t t;

        /* The requested point is right of the point where we wanted
         * to be on track again, thus just linearly estimate */

        t = (int64_t) s->py + (int64_t) llrint(s->dp * (double) (x - s->px));

        if (t < 0)
            t = 0;

        *y = (pa_usec_t) t;

        if (deriv)
            *deriv = s->dp;

    } else if (x <= s->ex) {
        /* Linear interpolation left from ex */
        int64_t t;

        t = (int64_t) s->ey - (int64_t) llrint(s->de * (double) (s->ex - x));

        if (t < 0)
            t = 0;

        *y = (pa_usec_t) t;

        if (deriv)
            *deriv = s->de;

    } else {
        /* Spline interpolation between ex and px */
        double tx, ty;

        /* Ok, we're not yet on track, thus let's interpolate, and
         * make sure that the first derivative is smooth */

        calc_abc(s);

        /* Move to origin */
        tx = (double) (x - s->ex);

        /* Horner scheme */
        ty = (tx * (s->c + tx * (s->b + tx * s->a)));

        /* Move back from origin */
        ty += (double) s->ey;

        *y = ty >= 0 ? (pa_usec_t) llrint(ty) : 0;

        /* Horner scheme */
        if (deriv)
            *deriv = s->c + (tx * (s->b*2 + tx * s->a*3));
    }

    /* Guarantee monotonicity */
    if (s->monotonic) {

        if (deriv && *deriv < 0)
            *deriv = 0;
    }
}

void pa_smoother_put(pa_smoother *s, pa_usec_t x, pa_usec_t y) {
    pa_usec_t ney;
    double nde;
    bool is_new;

    pa_assert(s);

    /* Fix up x value */
    if (s->paused)
        x = s->pause_time;

    x = PA_LIKELY(x >= s->time_offset) ? x - s->time_offset : 0;

    is_new = x >= s->ex;

    if (is_new) {
        /* First, we calculate the position we'd estimate for x, so that
         * we can adjust our position smoothly from this one */
        estimate(s, x, &ney, &nde);
        s->ex = x; s->ey = ney; s->de = nde;
        s->ry = y;
    }

    /* Then, we add the new measurement to our history */
    add_to_history(s, x, y);

    /* And determine the average gradient of the history */
    s->dp = avg_gradient(s, x);

    /* And calculate when we want to be on track again */
    if (s->smoothing) {
        s->px = s->ex + s->adjust_time;
        s->py = s->ry + (pa_usec_t) llrint(s->dp * (double) s->adjust_time);
    } else {
        s->px = s->ex;
        s->py = s->ry;
    }

    s->abc_valid = false;

#ifdef DEBUG_DATA
    pa_log_debug("%p, put(%llu | %llu) = %llu", s, (unsigned long long) (x + s->time_offset), (unsigned long long) x, (unsigned long long) y);
#endif
}

pa_usec_t pa_smoother_get(pa_smoother *s, pa_usec_t x) {
    pa_usec_t y;

    pa_assert(s);

    /* Fix up x value */
    if (s->paused)
        x = s->pause_time;

    x = PA_LIKELY(x >= s->time_offset) ? x - s->time_offset : 0;

    if (s->monotonic)
        if (x <= s->last_x)
            x = s->last_x;

    estimate(s, x, &y, NULL);

    if (s->monotonic) {

        /* Make sure the querier doesn't jump forth and back. */
        s->last_x = x;

        if (y < s->last_y)
            y = s->last_y;
        else
            s->last_y = y;
    }

#ifdef DEBUG_DATA
    pa_log_debug("%p, get(%llu | %llu) = %llu", s, (unsigned long long) (x + s->time_offset), (unsigned long long) x, (unsigned long long) y);
#endif

    return y;
}

void pa_smoother_set_time_offset(pa_smoother *s, pa_usec_t offset) {
    pa_assert(s);

    s->time_offset = offset;

#ifdef DEBUG_DATA
    pa_log_debug("offset(%llu)", (unsigned long long) offset);
#endif
}

void pa_smoother_pause(pa_smoother *s, pa_usec_t x) {
    pa_assert(s);

    if (s->paused)
        return;

#ifdef DEBUG_DATA
    pa_log_debug("pause(%llu)", (unsigned long long) x);
#endif

    s->paused = true;
    s->pause_time = x;
}

void pa_smoother_resume(pa_smoother *s, pa_usec_t x, bool fix_now) {
    pa_assert(s);

    if (!s->paused)
        return;

    if (x < s->pause_time)
        x = s->pause_time;

#ifdef DEBUG_DATA
    pa_log_debug("resume(%llu)", (unsigned long long) x);
#endif

    s->paused = false;
    s->time_offset += x - s->pause_time;

    if (fix_now)
        pa_smoother_fix_now(s);
}

void pa_smoother_fix_now(pa_smoother *s) {
    pa_assert(s);

    s->px = s->ex;
    s->py = s->ry;
}

pa_usec_t pa_smoother_translate(pa_smoother *s, pa_usec_t x, pa_usec_t y_delay) {
    pa_usec_t ney;
    double nde;

    pa_assert(s);

    /* Fix up x value */
    if (s->paused)
        x = s->pause_time;

    x = PA_LIKELY(x >= s->time_offset) ? x - s->time_offset : 0;

    estimate(s, x, &ney, &nde);

    /* Play safe and take the larger gradient, so that we wakeup
     * earlier when this is used for sleeping */
    if (s->dp > nde)
        nde = s->dp;

#ifdef DEBUG_DATA
    pa_log_debug("translate(%llu) = %llu (%0.2f)", (unsigned long long) y_delay, (unsigned long long) ((double) y_delay / nde), nde);
#endif

    return (pa_usec_t) llrint((double) y_delay / nde);
}

void pa_smoother_reset(pa_smoother *s, pa_usec_t time_offset, bool paused) {
    pa_assert(s);

    s->px = s->py = 0;
    s->dp = 1;

    s->ex = s->ey = s->ry = 0;
    s->de = 1;

    s->history_idx = 0;
    s->n_history = 0;

    s->last_y = s->last_x = 0;

    s->abc_valid = false;

    s->paused = paused;
    s->time_offset = s->pause_time = time_offset;

#ifdef DEBUG_DATA
    pa_log_debug("reset()");
#endif
}
