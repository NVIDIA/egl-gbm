/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GBM_DISPLAY_H
#define GBM_DISPLAY_H

#include "gbm-platform.h"
#include "gbm-handle.h"

typedef struct GbmDisplayRec {
    GbmObject base;
    GbmPlatformData* data;
    EGLDeviceEXT dev;
    EGLDisplay devDpy;
    struct gbm_device* gbm;
    int fd;
} GbmDisplay;

EGLDisplay eGbmGetPlatformDisplayExport(void *data,
                                        EGLenum platform,
                                        void *nativeDpy,
                                        const EGLAttrib *attribs);
const char* eGbmQueryStringExport(void *data,
                                  EGLDisplay dpy,
                                  EGLExtPlatformString name);
EGLBoolean eGbmIsValidNativeDisplayExport(void *data, void *nativeDpy);
void* eGbmGetInternalHandleExport(EGLDisplay dpy, EGLenum type, void *handle);

EGLBoolean eGbmInitializeHook(EGLDisplay dpy, EGLint* major, EGLint* minor);
EGLBoolean eGbmQueryDisplayAttribHook(EGLDisplay dpy,
                                      EGLint name,
                                      EGLAttrib *value);
EGLBoolean eGbmTerminateHook(EGLDisplay dpy);
EGLBoolean eGbmChooseConfigHook(EGLDisplay dpy,
                                EGLint const* attribs,
                                EGLConfig* configs,
                                EGLint configSize,
                                EGLint *numConfig);

EGLBoolean eGbmGetConfigAttribHook(EGLDisplay dpy,
                                   EGLConfig config,
                                   EGLint attribute,
                                   EGLint* value);

#endif /* GBM_DISPLAY_H */
