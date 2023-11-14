/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
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

EGLBoolean eGbmPointerIsDereferenceable(void* p);

#endif /* GBM_UTILS_H */
