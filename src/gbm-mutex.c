/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
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

