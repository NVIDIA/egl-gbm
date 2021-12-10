/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GBM_HANDLE_H
#define GBM_HANDLE_H

#include <EGL/egl.h>
#include <stdbool.h>

typedef struct GbmObjectRec {
    void (*free)(struct GbmObjectRec *obj);
    struct GbmDisplayRec* dpy;
    EGLenum type;
    int refCount;
    bool destroyed;
} GbmObject;

typedef const GbmObject* GbmHandle;

GbmHandle eGbmAddObject(GbmObject* obj);
GbmObject* eGbmRefHandle(GbmHandle handle);
void eGbmUnrefObject(GbmObject* obj);
bool eGbmDestroyHandle(GbmHandle handle);

#endif /* GBM_HANDLE_H */
