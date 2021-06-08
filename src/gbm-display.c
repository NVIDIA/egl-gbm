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
#include "gbm-platform.h"
#include <stdlib.h>

struct gbm_device;

typedef struct GbmDisplayRec {
    GbmPlatformData* data;
    EGLDeviceEXT dev;
    EGLDisplay devDpy;
    struct gbm_device* gbm;
} GbmDisplay;

static EGLDeviceEXT
FindGbmDevice(GbmPlatformData* data, struct gbm_device* gbm)
{
    EGLint maxDevices, numDevices;
    EGLDeviceEXT* devices;
    EGLDeviceEXT dev;

    if (data->egl.QueryDevicesEXT(0, NULL, &maxDevices) != EGL_TRUE) {
        return EGL_NO_DEVICE_EXT;
    }

    if (maxDevices <= 0) {
        /* XXX Set error */
        return EGL_NO_DEVICE_EXT;
    }

    devices = malloc(maxDevices * sizeof(*devices));

    if (!devices) {
        /* XXX Set error */
        return EGL_NO_DEVICE_EXT;
    }

    if (data->egl.QueryDevicesEXT(maxDevices, devices, &numDevices) != EGL_TRUE) {
        free(devices);
        return EGL_NO_DEVICE_EXT;
    }

    /* XXX Look up device properly */
    dev = devices[0];

    free(devices);

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
