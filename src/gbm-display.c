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

#include "gbm-display.h"
#include "gbm-utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <gbm.h>
#include <xf86drm.h>

#if !defined(O_CLOEXEC)
#if ((defined(__sun__) && defined(__svr4__)) || defined(__SUNPRO_C) || defined(__SUNPRO_CC))
/*
 * Allow building against old SunOS headers with the assumption this flag will
 * be available at runtime.
 */
#define O_CLOEXEC 0x00800000
#else
#error "No definition of O_CLOEXEC available"
#endif
#endif

static bool
CheckDevicePath(const GbmPlatformData* data,
                EGLDeviceEXT dev,
                EGLenum pathEnum,
                dev_t gbmDev)
{
    struct stat statbuf;
    const char *devPath;

    devPath = data->egl.QueryDeviceStringEXT(dev, pathEnum);

    if (!devPath) return false;

    memset(&statbuf, 0, sizeof(statbuf));
    if (stat(devPath, &statbuf)) return false;

    if (memcmp(&statbuf.st_rdev, &gbmDev, sizeof(gbmDev))) return false;

    return true;
}

static EGLDeviceEXT
FindGbmDevice(GbmPlatformData* data, struct gbm_device* gbm)
{
    EGLDeviceEXT* devs = NULL;
    const char* devExts;
    struct stat statbuf;
    EGLDeviceEXT dev = EGL_NO_DEVICE_EXT;
    EGLint maxDevs, numDevs;
    int gbmFd = gbm_device_get_fd(gbm);
    int i;

    if (gbmFd < 0) {
        /*
         * No need to set an error here or various other cases that boil down
         * to an invalid native display. From the EGL 1.5 spec:
         *
         * "If platform is valid but no display matching <native_display> is
         * available, then EGL_NO_DISPLAY is returned; no error condition is
         * raised in this case."
         */
        goto done;
    }

    memset(&statbuf, 0, sizeof(statbuf));
    if (fstat(gbmFd, &statbuf)) goto done;

    if (data->egl.QueryDevicesEXT(0, NULL, &maxDevs) != EGL_TRUE) goto done;

    if (maxDevs <= 0) goto done;

    devs = malloc(maxDevs * sizeof(*devs));

    if (!devs) {
        eGbmSetError(data, EGL_BAD_ALLOC);
        goto done;
    }

    if (data->egl.QueryDevicesEXT(maxDevs, devs, &numDevs) != EGL_TRUE)
        goto done;

    for (i = 0; i < numDevs; i++) {
        devExts = data->egl.QueryDeviceStringEXT(devs[i], EGL_EXTENSIONS);

        if (!eGbmFindExtension("EGL_EXT_device_drm", devExts)) continue;

        if (CheckDevicePath(data,
                            devs[i],
                            EGL_DRM_DEVICE_FILE_EXT,
                            statbuf.st_rdev)) {
            dev = devs[i];
            break;
        }

        if (!eGbmFindExtension("EGL_EXT_device_drm_render_node", devExts))
            continue;

        if (CheckDevicePath(data,
                            devs[i],
                            EGL_DRM_RENDER_NODE_FILE_EXT,
                            statbuf.st_rdev)) {
            dev = devs[i];
            break;
        }
    }

done:
    free(devs);

    return dev;

}

static int
OpenDefaultDrmDevice(void)
{
    drmDevicePtr devices[1];
    int numDevices = drmGetDevices2(0, devices, 1);
    int fd = -1;

    if (numDevices == 0)
        return -1;

    if (devices[0]->nodes[DRM_NODE_RENDER])
        fd = open(devices[0]->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);

    if ((fd < 0) && devices[0]->nodes[DRM_NODE_PRIMARY])
        fd = open(devices[0]->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);

    drmFreeDevices(devices, 1);

    return fd;
}

static void
FreeDisplay(GbmObject* obj)
{
    if (obj) {
        GbmDisplay* display = (GbmDisplay*)obj;

        /*
         * The device file is only opened when the display is
         * EGL_DEFAULT_DISPLAY, and is the first resource created by that code
         * path.
         */
        if (display->fd >= 0) {
            if (display->gbm) gbm_device_destroy(display->gbm);
            close(display->fd);
        }

        free(obj);
    }
}

EGLDisplay
eGbmGetPlatformDisplayExport(void *dataVoid,
                             EGLenum platform,
                             void *nativeDpy,
                             const EGLAttrib *attribs)
{
    static const EGLAttrib refAttrs[] = {
        EGL_TRACK_REFERENCES_KHR, EGL_TRUE,
        EGL_NONE
    };
    GbmPlatformData* data = dataVoid;
    GbmDisplay* display = NULL;
    const EGLAttrib *attrs = data->supportsDisplayReference ? refAttrs : NULL;

    (void)attribs;

    if (platform != EGL_PLATFORM_GBM_KHR) {
        eGbmSetError(data, EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }

    display = calloc(1, sizeof(*display));

    if (!display) {
        eGbmSetError(data, EGL_BAD_ALLOC);
        return EGL_NO_DISPLAY;
    }

    display->base.dpy = display;
    display->base.type = EGL_OBJECT_DISPLAY_KHR;
    display->base.refCount = 1;
    display->base.free = FreeDisplay;
    display->data = data;
    display->fd = -1;
    display->gbm = nativeDpy;

    if (nativeDpy == EGL_DEFAULT_DISPLAY) {
        if ((display->fd = OpenDefaultDrmDevice()) < 0) goto fail;
        if (!(display->gbm = gbm_create_device(display->fd))) goto fail;
    }

    display->dev = FindGbmDevice(data, display->gbm);

    if (display->dev == EGL_NO_DEVICE_EXT) {
        /* FindGbmDevice() sets an appropriate EGL error on failure */
        goto fail;
    }

    display->devDpy =
        display->data->egl.GetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
                                              display->dev,
                                              attrs);

    if (display->devDpy == EGL_NO_DISPLAY) {
        /* GetPlatformDisplay will set an appropriate error */
        goto fail;
    }

    if (!eGbmAddObject(&display->base)) {
        eGbmSetError(data, EGL_BAD_ALLOC);
        goto fail;
    }

    return (EGLDisplay)display;

fail:
    FreeDisplay(&display->base);
    return EGL_NO_DISPLAY;
}

EGLBoolean
eGbmInitializeHook(EGLDisplay dpy, EGLint* major, EGLint* minor)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    EGLBoolean res;

    if (!display) {
        /*  No platform data. Can't set error EGL_NO_DISPLAY */
        return EGL_FALSE;
    }

    res = display->data->egl.Initialize(display->devDpy, major, minor);

    eGbmUnrefObject(&display->base);
    return res;
}

EGLBoolean
eGbmTerminateHook(EGLDisplay dpy)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    EGLBoolean res;

    if (!display) {
        /*  No platform data. Can't set error EGL_NO_DISPLAY */
        return EGL_FALSE;
    }

    res = display->data->egl.Terminate(display->devDpy);

    eGbmUnrefObject(&display->base);
    return res;
}

const char*
eGbmQueryStringExport(void *data,
                      EGLDisplay dpy,
                      EGLExtPlatformString name)
{
    (void)data;
    (void)dpy;

    switch (name) {
    case EGL_EXT_PLATFORM_PLATFORM_CLIENT_EXTENSIONS:
        return "EGL_KHR_platform_gbm EGL_MESA_platform_gbm";

    default:
        break;
    }

    return NULL;
}

EGLBoolean
eGbmIsValidNativeDisplayExport(void *data, void *nativeDpy)
{
    /* Is <nativeDpy> a GBM device? */
    char *envPlatform = getenv("EGL_PLATFORM");

    (void)data;

    /* Yes, because the environment said so. */
    if (envPlatform && !strcasecmp(envPlatform, "gbm"))
        return EGL_TRUE;

#if HAS_MINCORE
    /* GBM devices are pointers to instances of "struct gbm_device". */
    if (!eGbmPointerIsDereferenceable(nativeDpy))
        return EGL_FALSE;

    /*
     * The first member of struct gbm_device is "dummy", a pointer to the
     * function gbm_create_device() that is there precisely for this purpose:
     */
    return (*(void**)nativeDpy == gbm_create_device) ? EGL_TRUE : EGL_FALSE;
#else
    (void)nativeDpy;

    /*
     * No way to know, so assume it's not a GBM device given GBM isn't the
     * most widely-used EGL platform in most environments. Users/applications
     * will need to use the environment variable or eglGetPlatformDisplay()
     * on platforms that don't have mincore().
     */
    return EGL_FALSE;
#endif
}

void*
eGbmGetInternalHandleExport(EGLDisplay dpy, EGLenum type, void *handle)
{
    GbmObject* obj = handle ? eGbmRefHandle(handle) : NULL;
    void* res = handle;

    if (!obj) return res;
    if (obj->type != type || obj->dpy != dpy) goto done;

    switch (type) {
    case EGL_OBJECT_DISPLAY_KHR:
        res = ((GbmDisplay*)obj)->devDpy;
        break;

    default:
        break;
    }

done:
    eGbmUnrefObject(obj);
    return res;
}
