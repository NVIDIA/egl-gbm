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

#include "gbm-utils.h"
#include "gbm-display.h"
#include "gbm-platform.h"

#include <stdlib.h>
#include <string.h>

static void
DestroyPlatformData(GbmPlatformData* data)
{
    free(data);
}

static GbmPlatformData*
CreatePlatformData(const EGLExtDriver *driver)
{
    const char* clExts;
    GbmPlatformData *res = calloc(1, sizeof(*res));

    if (!res) return NULL;

#define DO_EGL_FUNC(_PROTO, _FUNC) \
    res->egl._FUNC = (_PROTO)driver->getProcAddress("egl" #_FUNC);
#include "gbm-egl-imports.h"
#undef DO_EGL_FUNC

    clExts = res->egl.QueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

    if (!eGbmFindExtension("EGL_EXT_platform_device", clExts) ||
        (!eGbmFindExtension("EGL_EXT_device_query", clExts) &&
         !eGbmFindExtension("EGL_EXT_device_base", clExts))) {
        DestroyPlatformData(res);
        return NULL;
    }

    return res;
}

static EGLSurface
CreatePlatformPixmapSurfaceHook(EGLDisplay dpy,
                                EGLConfig config,
                                void *nativePixmap,
                                const EGLAttrib *attribs)
{
    (void)dpy;
    (void)config;
    (void)nativePixmap;
    (void)attribs;

    /* XXX Set error */
    return EGL_NO_SURFACE;
}

static EGLSurface
CreatePlatformWindowSurfaceHook(EGLDisplay dpy,
                                EGLConfig config,
                                void *nativeWindow,
                                const EGLAttrib *attribs)
{
    (void)dpy;
    (void)config;
    (void)nativeWindow;
    (void)attribs;

    /* XXX Set error */
    return EGL_NO_SURFACE;
}

static EGLSurface
CreatePbufferSurfaceHook(EGLDisplay dpy,
                         EGLConfig config,
                         const EGLint *attribs)
{
    (void)dpy;
    (void)config;
    (void)attribs;

    /* XXX Pass the request through */

    /* XXX Set error */
    return EGL_NO_SURFACE;
}

typedef struct GbmEglHookRec {
    const char *name;
    void *func;
} GbmEglHook;

static const GbmEglHook EglHooksMap[] = {
    /* Keep names in ascending order */
    { "eglCreatePbufferSurface", CreatePbufferSurfaceHook },
    { "eglCreatePlatformPixmapSurface", CreatePlatformPixmapSurfaceHook },
    { "eglCreatePlatformWindowSurface", CreatePlatformWindowSurfaceHook },
    { "eglInitialize", eGbmInitializeHook },
    { "eglTerminate", eGbmTerminateHook },
};

static int
HookCmp(const void* elemA, const void* elemB)
{
    const char* key = (const char*)elemA;
    const GbmEglHook* hook = (const GbmEglHook*)elemB;

    return strcmp(key, hook->name);
}

static void*
GetHookAddressExport(void *data, const char *name)
{
    GbmEglHook *hook;
    (void)data;

    hook = (GbmEglHook*)bsearch((const void*)name,
                                (const void*)EglHooksMap,
                                sizeof(EglHooksMap)/sizeof(GbmEglHook),
                                sizeof(GbmEglHook),
                                HookCmp);

    if (hook) return hook->func;

    return NULL;
}

static EGLBoolean
UnloadPlatformExport(void *data)
{
    (void)data;

    return EGL_TRUE;
}

EGLBoolean
loadEGLExternalPlatform(int major, int minor,
                        const EGLExtDriver *driver,
                        EGLExtPlatform *platform)
{
    if (!platform ||
        !EGL_EXTERNAL_PLATFORM_VERSION_CHECK(major, minor)) {
        return EGL_FALSE;
    }

    platform->version.major = GBM_EXTERNAL_VERSION_MAJOR;
    platform->version.minor = GBM_EXTERNAL_VERSION_MINOR;
    platform->version.micro = GBM_EXTERNAL_VERSION_MICRO;

    platform->platform = EGL_PLATFORM_GBM_KHR;

    platform->data = (void *)CreatePlatformData(driver);
    if (platform->data == NULL) {
        return EGL_FALSE;
    }

    platform->exports.unloadEGLExternalPlatform = UnloadPlatformExport;

    platform->exports.getHookAddress       = GetHookAddressExport;
    platform->exports.isValidNativeDisplay = eGbmIsValidNativeDisplayExport;
    platform->exports.getPlatformDisplay   = eGbmGetPlatformDisplayExport;
    platform->exports.queryString          = eGbmQueryStringExport;
    platform->exports.getInternalHandle    = eGbmGetInternalHandleExport;

    return EGL_TRUE;
}
