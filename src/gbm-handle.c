/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gbm-handle.h"
#include "gbm-mutex.h"

#include <stddef.h>
#include <search.h>
#include <assert.h>

static int
HandleCompar(const void *a, const void *b)
{
    const GbmObject* objA = a;
    const GbmObject* objB = b;

    if (objA->type != objB->type)
        return objA->type - objB->type;

    if (objA->dpy != objB->dpy)
        return (const uint8_t *)objA->dpy - (const uint8_t *)objB->dpy;

    return objA - objB;
}

void *handleTreeRoot = NULL;

GbmHandle
eGbmAddObject(GbmObject* obj)
{
    GbmObject** res = NULL;

    if (!eGbmHandlesLock())
        return NULL;

    assert(obj->refCount == 1);

    if (tfind(obj, &handleTreeRoot, HandleCompar))
        goto fail;

    res = tsearch(obj, &handleTreeRoot, HandleCompar);

fail:
    eGbmHandlesUnlock();

    return res ? *res : NULL;
}

GbmObject*
eGbmRefHandle(GbmHandle handle)
{
    GbmObject **res = NULL;

    if (!eGbmHandlesLock())
        return NULL;

    res = tfind(handle, &handleTreeRoot, HandleCompar);

    if (!res) goto fail;

    assert((*res)->refCount >= 1);
    (*res)->refCount++;

fail:
    eGbmHandlesUnlock();

    return res ? *res : NULL;
}

static void
UnrefObjectLocked(GbmObject* obj)
{
    assert(obj->refCount >= 1);

    if (--obj->refCount == 0) {
        if (!tdelete(obj, &handleTreeRoot, HandleCompar))
            assert(!"Failed to find handle in tree for deletion");

        eGbmHandlesUnlock();
        obj->free(obj);
        return;
    }

    eGbmHandlesUnlock();
}

void
eGbmUnrefObject(GbmObject* obj)
{
    if (!eGbmHandlesLock()) {
        assert(!"Failed to lock handle list to unref object");
        return;
    }

    /* UnrefObjectLocked releases the lock */
    UnrefObjectLocked(obj);
}

bool
eGbmDestroyHandle(GbmHandle handle)
{
    GbmObject **res = NULL;

    if (!eGbmHandlesLock()) {
        assert(!"Failed to lock handle list to unref object");
        return false;
    }

    res = tfind(handle, &handleTreeRoot, HandleCompar);

    if (!res || (*res)->destroyed) {
        eGbmHandlesUnlock();
        return false;
    }

    (*res)->destroyed = true;

    /* UnrefObjectLocked releases the lock */
    UnrefObjectLocked(*res);
    return true;
}
