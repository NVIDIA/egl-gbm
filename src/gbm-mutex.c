/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* For PTHREAD_MUTEX_ERRORCHECK */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "gbm-mutex.h"

#include <assert.h>
#include <pthread.h>

static pthread_mutex_t handlesMutex;
static pthread_once_t onceControl = PTHREAD_ONCE_INIT;
static bool mutexInitialized = false;


static void
InitMutex(void)
{
    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr)) {
        assert(!"Failed to initialize pthread mutex attributes");
        return;
    }

    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) {
        assert(!"Failed to set PTHREAD_MUTEX_ERRORCHECK attribute");
        goto fail;
    }

    if (pthread_mutex_init(&handlesMutex, &attr)) {
        assert(!"Failed to initialize handles mutex");
        goto fail;
    }

    mutexInitialized = true;

fail:
    if (pthread_mutexattr_destroy(&attr)) {
        assert(!"Failed to destroy pthread mutex attributes");
    }
}

bool
eGbmHandlesLock(void)
{
    if (pthread_once(&onceControl, InitMutex)) {
        assert(!"pthread_once() failed");
        return false;
    }

    if (!mutexInitialized || pthread_mutex_lock(&handlesMutex)) {
        assert(!"Failed to lock handles mutex");
        return false;
    }

    return true;
}

void
eGbmHandlesUnlock(void)
{
    assert(mutexInitialized);

    if (pthread_mutex_unlock(&handlesMutex))
        assert(!"Failed to unlock pthread mutex");
}

