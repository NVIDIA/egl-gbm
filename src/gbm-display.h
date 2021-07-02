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

#endif /* GBM_DISPLAY_H */
