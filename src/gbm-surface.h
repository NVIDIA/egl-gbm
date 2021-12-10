/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GBM_SURFACE_H
#define GBM_SURFACE_H

#include "gbm-handle.h"

#include <EGL/egl.h>
#include <gbm.h>

int eGbmSurfaceHasFreeBuffers(struct gbm_surface* s);
struct gbm_bo* eGbmSurfaceLockFrontBuffer(struct gbm_surface* s);
void eGbmSurfaceReleaseBuffer(struct gbm_surface* s, struct gbm_bo *bo);
EGLSurface eGbmCreatePlatformWindowSurfaceHook(EGLDisplay dpy,
                                               EGLConfig config,
                                               void* nativeWin,
                                               const EGLAttrib* attribs);
void* eGbmSurfaceUnwrap(GbmObject* obj);
EGLBoolean
eGbmDestroySurfaceHook(EGLDisplay dpy, EGLSurface eglSurf);

#endif /* GBM_SURFACE_H */
