/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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

#ifndef GBM_UTILS_H
#define GBM_UTILS_H

#include "gbm-platform.h"

#include <EGL/egl.h>

#if defined(__QNX__)
#define HAS_MINCORE 0
#else
#define HAS_MINCORE 1
#endif

#ifdef NDEBUG
#define eGbmSetError(data, err) \
    eGbmSetErrorInternal(data, err, NULL, 0);
#else
#define eGbmSetError(data, err) \
    eGbmSetErrorInternal(data, err, __FILE__, __LINE__);
#endif

EGLBoolean eGbmFindExtension(const char* extension, const char* extensions);
void eGbmSetErrorInternal(GbmPlatformData *data, EGLint error,
                          const char *file, int line);

#if HAS_MINCORE
EGLBoolean eGbmPointerIsDereferenceable(void* p);
#endif

#endif /* GBM_UTILS_H */
