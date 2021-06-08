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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <gbm.h>

typedef struct GbmDisplayRec {
    GbmPlatformData* data;
    EGLDeviceEXT dev;
    EGLDisplay devDpy;
    struct gbm_device* gbm;
} GbmDisplay;

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
        /* XXX Set error */
        goto done;
    }

    memset(&statbuf, 0, sizeof(statbuf));
    if (fstat(gbmFd, &statbuf)) {
        /* XXX Set error */
        goto done;
    }

    if (data->egl.QueryDevicesEXT(0, NULL, &maxDevs) != EGL_TRUE) goto done;

    if (maxDevs <= 0) {
        /* XXX Set error */
        goto done;
    }

    devs = malloc(maxDevs * sizeof(*devs));

    if (!devs) {
        /* XXX Set error */
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

EGLDisplay
eGbmGetPlatformDisplayExport(void *data,
                             EGLenum platform,
                             void *nativeDpy,
                             const EGLAttrib *attribs)
{
    GbmDisplay* display;
    EGLDeviceEXT dev;

    (void)attribs;

    if (platform != EGL_PLATFORM_GBM_KHR) {
        /* XXX Set error */
        return EGL_NO_DISPLAY;
    }

    dev = FindGbmDevice(data, nativeDpy);

    if (dev == EGL_NO_DEVICE_EXT) {
        /* FindGbmDevice() sets an appropriate EGL error on failure */
        return EGL_NO_DISPLAY;
    }

    display = calloc(1, sizeof(*display));

    if (!display) {
        /* XXX Set error */
        return EGL_NO_DISPLAY;
    }

    display->data = data;
    display->gbm = nativeDpy;
    display->dev = dev;
    display->devDpy =
        display->data->egl.GetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
                                              display->dev,
                                              NULL);

    /* XXX Handle EGL_DEFAULT_DISPLAY? */

    return (EGLDisplay)display;
}

EGLBoolean
eGbmInitializeHook(EGLDisplay dpy, EGLint* major, EGLint* minor)
{
    GbmDisplay* display = (GbmDisplay*)dpy;

    return display->data->egl.Initialize(display->devDpy, major, minor);
}

EGLBoolean
eGbmTerminateHook(EGLDisplay dpy)
{
    GbmDisplay* display = (GbmDisplay*)dpy;

    return display->data->egl.Terminate(display->devDpy);
}

const char*
eGbmQueryStringExport(void *data,
                      EGLDisplay dpy,
                      EGLExtPlatformString name)
{
    /* XXX TODO */

    return EGL_FALSE;
}

EGLBoolean
eGbmIsValidNativeDisplayExport(void *data, void *nativeDpy)
{
    /* XXX TODO */

    return EGL_FALSE;
}

void*
eGbmGetInternalHandleExport(EGLDisplay dpy, EGLenum type, void *handle)
{
    switch (type) {
    case EGL_OBJECT_DISPLAY_KHR:
        return ((GbmDisplay*)handle)->devDpy;

    /* XXX TODO: Other types */
    default:
        break;
    }

    return NULL;
}
