/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef GROOVE_ATOMICS_H
#define GROOVE_ATOMICS_H

// Simple wrappers around atomic values so that the compiler will catch it if
// I accidentally use operators such as +, -, += on them.

#include <stdatomic.h>

struct GrooveAtomicLong {
    atomic_long x;
};

struct GrooveAtomicInt {
    atomic_int x;
};

struct GrooveAtomicBool {
    atomic_bool x;
};

#define GROOVE_ATOMIC_LOAD(a) atomic_load(&a.x)
#define GROOVE_ATOMIC_FETCH_ADD(a, delta) atomic_fetch_add(&a.x, delta)
#define GROOVE_ATOMIC_STORE(a, value) atomic_store(&a.x, value)
#define GROOVE_ATOMIC_EXCHANGE(a, value) atomic_exchange(&a.x, value)

#endif
