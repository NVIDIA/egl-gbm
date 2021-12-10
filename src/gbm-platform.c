/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gbm-utils.h"
#include "gbm-display.h"
#include "gbm-platform.h"
#include "gbm-surface.h"

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

    res->driver.setError = driver->setError;

    clExts = res->egl.QueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

    if (!eGbmFindExtension("EGL_EXT_platform_device", clExts) ||
        (!eGbmFindExtension("EGL_EXT_device_query", clExts) &&
         !eGbmFindExtension("EGL_EXT_device_base", clExts))) {
        DestroyPlatformData(res);
        return NULL;
    }

    if (eGbmFindExtension("EGL_KHR_display_reference", clExts))
        res->supportsDisplayReference = true;
    else
        res->supportsDisplayReference = false;

    return res;
}

static EGLSurface
CreatePlatformPixmapSurfaceHook(EGLDisplay dpy,
                                EGLConfig config,
                                void *nativePixmap,
                                const EGLAttrib *attribs)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    (void)config;
    (void)nativePixmap;
    (void)attribs;

    if (!display) return EGL_NO_SURFACE;

    /*
     * From the EGL 1.5 spec:
     *
     * "If config does not support rendering to pixmaps (the EGL_SURFACE_TYPE
     * attribute does not contain EGL_PIXMAP_BIT), an EGL_BAD_MATCH error is
     * generated."
     *
     * GBM does not have a native pixmap type. See EGL_KHR_platform_gbm, and
     * none of the currently advertised EGLConfigs, which are passed through
     * unmodified from the EGLDevice, would support rendering to pixmaps even
     * if GBM did.
     */
    eGbmSetError(display->data, EGL_BAD_MATCH);
    eGbmUnrefObject(&display->base);

    return EGL_NO_SURFACE;
}

static EGLSurface
CreatePbufferSurfaceHook(EGLDisplay dpy,
                         EGLConfig config,
                         const EGLint *attribs)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    GbmPlatformData* data = display->data;

    if (!display) {
        /*  No platform data. Can't set error EGL_NO_DISPLAY */
        return EGL_NO_SURFACE;
    }

    return data->egl.CreatePbufferSurface(display->devDpy, config, attribs);
}

typedef struct GbmEglHookRec {
    const char *name;
    void *func;
} GbmEglHook;

static const GbmEglHook EglHooksMap[] = {
    /* Keep names in ascending order */
    { "eglChooseConfig", eGbmChooseConfigHook },
    { "eglCreatePbufferSurface", CreatePbufferSurfaceHook },
    { "eglCreatePlatformPixmapSurface", CreatePlatformPixmapSurfaceHook },
    { "eglCreatePlatformWindowSurface", eGbmCreatePlatformWindowSurfaceHook },
    { "eglDestroySurface", eGbmDestroySurfaceHook },
    { "eglGetConfigAttrib", eGbmGetConfigAttribHook },
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
