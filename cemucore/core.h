/*
 * Copyright (c) 2015-2020 CE Programming.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CEMUCORE_INTERNAL_H
#define CEMUCORE_INTERNAL_H

#include "cemucore.h"

#ifndef CEMUCORE_NOTHREADS
#include <pthread.h>
#include <stdatomic.h>
#endif

struct cemucore
{
#ifndef CEMUCORE_NOTHREADS
    pthread_t thread;
#if ATOMIC_INT_LOCK_FREE != 2
    atomic_flag no_pending;
#endif
    atomic_uint pending;
#endif
    cemucore_signal_t signal;
    void *signal_data;
};

#endif