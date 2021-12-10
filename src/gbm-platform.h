/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GBM_PLATFORM_H
#define GBM_PLATFORM_H

#include <stdbool.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

/*
 * <GBM_EXTERNAL_VERSION_MAJOR>.<GBM_EXTERNAL_VERSION_MINOR>.
 * <GBM_EXTERNAL_VERSION_MICRO> defines the EGL external Wayland
 * implementation version.
 *
 * The includer of this file can override either GBM_EXTERNAL_VERSION_MAJOR
 * or GBM_EXTERNAL_VERSION_MINOR in order to build against a certain EGL
 * external API version.
 *
 *
 * How to update this version numbers:
 *
 *  - GBM_EXTERNAL_VERSION_MAJOR must match the EGL external API major
 *    number this platform implements
 *
 *  - GBM_EXTERNAL_VERSION_MINOR must match the EGL external API minor
 *    number this platform implements
 *
 *  - If the platform implementation is changed in any way, increase
 *    GBM_EXTERNAL_VERSION_MICRO by 1
 */
#if !defined(GBM_EXTERNAL_VERSION_MAJOR)
 #define GBM_EXTERNAL_VERSION_MAJOR                      1
 #if !defined(GBM_EXTERNAL_VERSION_MINOR)
  #define GBM_EXTERNAL_VERSION_MINOR                     1
 #endif
#elif !defined(GBM_EXTERNAL_VERSION_MINOR)
 #define GBM_EXTERNAL_VERSION_MINOR                      0
#endif

#define GBM_EXTERNAL_VERSION_MICRO                       0

#define EGL_EXTERNAL_PLATFORM_VERSION_MAJOR GBM_EXTERNAL_VERSION_MAJOR
#define EGL_EXTERNAL_PLATFORM_VERSION_MINOR GBM_EXTERNAL_VERSION_MINOR
#include <eglexternalplatform.h>

#define EGBM_EXPORT __attribute__ ((visibility ("default")))

typedef struct GbmPlatformDataRec {
    struct {
#define DO_EGL_FUNC(_PROTO, _FUNC) \
        _PROTO                              _FUNC;
#include "gbm-egl-imports.h"
#undef DO_EGL_FUNC
    } egl;

    struct {
        PEGLEXTFNSETERROR setError;
    } driver;

    bool supportsDisplayReference;
} GbmPlatformData;

EGBM_EXPORT EGLBoolean loadEGLExternalPlatform(int major, int minor,
                                               const EGLExtDriver *driver,
                                               EGLExtPlatform *platform);

#endif /* GBM_PLATFORM_H */
