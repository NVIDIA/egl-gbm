/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gbm-display.h"
#include "gbm-utils.h"
#include "gbm-surface.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <gbm.h>
#include <gbmint.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

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

    if (numDevices <= 0)
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

    if (data->ptr_gbm_device_get_backend_name != NULL) {
        const char *name = data->ptr_gbm_device_get_backend_name(display->gbm);
        if (name == NULL || strcmp(name, "nvidia") != 0) {
            /*
             * This is not an NVIDIA device. Return failure, so that libglvnd can
             * move on to the next driver.
             */
            goto fail;
        }
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
    GbmPlatformData* data;
    const char* exts;
    EGLBoolean res;

    if (!display) {
        /*  No platform data. Can't set error EGL_NO_DISPLAY */
        return EGL_FALSE;
    }

    data = display->data;

    res = data->egl.Initialize(display->devDpy, major, minor);

    if (!res) goto done;

    exts = data->egl.QueryString(display->devDpy, EGL_EXTENSIONS);

    if (!exts ||
        !eGbmFindExtension("EGL_KHR_stream", exts) ||
        !eGbmFindExtension("EGL_KHR_stream_producer_eglsurface", exts) ||
        !eGbmFindExtension("EGL_KHR_image_base", exts) ||
        !eGbmFindExtension("EGL_NV_stream_consumer_eglimage", exts) ||
        !eGbmFindExtension("EGL_MESA_image_dma_buf_export", exts) ||
        !eGbmFindExtension("EGL_EXT_sync_reuse", exts)) {
        data->egl.Terminate(display->devDpy);
        eGbmSetError(data, EGL_NOT_INITIALIZED);
        res = EGL_FALSE;
    }

    display->gbm->v0.surface_lock_front_buffer = eGbmSurfaceLockFrontBuffer;
    display->gbm->v0.surface_release_buffer = eGbmSurfaceReleaseBuffer;
    display->gbm->v0.surface_has_free_buffers = eGbmSurfaceHasFreeBuffers;

done:
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

    /* GBM devices are pointers to instances of "struct gbm_device". */
    if (!eGbmPointerIsDereferenceable(nativeDpy))
        return EGL_FALSE;

    /*
     * The first member of struct gbm_device is "dummy", a pointer to the
     * function gbm_create_device() that is there precisely for this purpose:
     */
    return (*(void**)nativeDpy == gbm_create_device) ? EGL_TRUE : EGL_FALSE;
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
    case EGL_OBJECT_SURFACE_KHR:
        res = eGbmSurfaceUnwrap(obj);
        break;

    default:
        break;
    }

done:
    eGbmUnrefObject(obj);
    return res;
}

static uint32_t ConfigToDrmFourCC(GbmDisplay* display, EGLConfig config)
{
    EGLDisplay dpy = display->devDpy;
    EGLint r, g, b, a, componentType;
    EGLBoolean ret = EGL_TRUE;

    ret &= display->data->egl.GetConfigAttrib(dpy,
                                              config,
                                              EGL_RED_SIZE,
                                              &r);
    ret &= display->data->egl.GetConfigAttrib(dpy,
                                              config,
                                              EGL_GREEN_SIZE,
                                              &g);
    ret &= display->data->egl.GetConfigAttrib(dpy,
                                              config,
                                              EGL_BLUE_SIZE,
                                              &b);
    ret &= display->data->egl.GetConfigAttrib(dpy,
                                              config,
                                              EGL_ALPHA_SIZE,
                                              &a);
    ret &= display->data->egl.GetConfigAttrib(dpy,
                                              config,
                                              EGL_COLOR_COMPONENT_TYPE_EXT,
                                              &componentType);

    if (!ret) {
        /*
         * The only reason this could fail is some internal error in the
         * platform library code or if the application terminated the display
         * in another thread while this code was running. In either case,
         * behave as if there is no DRM fourcc format associated with this
         * config.
         */
        return 0; /* DRM_FORMAT_INVALID */
    }

    /* Handles configs with up to 255 bits per component */
    assert(a < 256 && g < 256 && b < 256 && a < 256);
#define PACK_CONFIG(r_, g_, b_, a_) \
    (((r_) << 24ULL) | ((g_) << 16ULL) | ((b_) << 8ULL) | (a_))

    if (componentType == EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT) {
        switch (PACK_CONFIG(r, g, b, a)) {
        case PACK_CONFIG(16, 16, 16, 0):
            return DRM_FORMAT_XBGR16161616F;
        case PACK_CONFIG(16, 16, 16, 16):
            return DRM_FORMAT_ABGR16161616F;
        default:
            return 0; /* DRM_FORMAT_INVALID */
        }
    } else {
        switch (PACK_CONFIG(r, g, b, a)) {
        case PACK_CONFIG(8, 8, 8, 0):
            return DRM_FORMAT_XRGB8888;
        case PACK_CONFIG(8, 8, 8, 8):
            return DRM_FORMAT_ARGB8888;
        case PACK_CONFIG(5, 6, 5, 0):
            return DRM_FORMAT_RGB565;
        case PACK_CONFIG(10, 10, 10, 0):
            return DRM_FORMAT_XRGB2101010;
        case PACK_CONFIG(10, 10, 10, 2):
            return DRM_FORMAT_ARGB2101010;
        case PACK_CONFIG(16, 16, 16, 0):
            return DRM_FORMAT_XBGR16161616;
        case PACK_CONFIG(16, 16, 16, 16):
            return DRM_FORMAT_ABGR16161616;
        default:
            return 0; /* DRM_FORMAT_INVALID */
        }
    }
}

EGLBoolean
eGbmChooseConfigHook(EGLDisplay dpy,
                     EGLint const* attribs,
                     EGLConfig* configs,
                     EGLint configSize,
                     EGLint *numConfig)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    GbmPlatformData* data;
    EGLint *newAttribs = NULL;
    EGLConfig *newConfigs = NULL;
    EGLint nAttribs = 0;
    EGLint nNewAttribs = 0;
    EGLint nNewConfigs = 0;
    EGLint cfg;
    EGLint nativeVisual = EGL_DONT_CARE;
    EGLint err = EGL_SUCCESS;
    EGLBoolean ret = EGL_FALSE;
    bool surfType = false;
    bool nativeVisualID = false;

    if (!display) {
        /*  No platform data. Can't set error EGL_NO_DISPLAY */
        return EGL_FALSE;
    }

    data = display->data;

    if (attribs) {
        for (; attribs[nAttribs] != EGL_NONE; nAttribs += 2) {
            surfType = surfType || (attribs[nAttribs] == EGL_SURFACE_TYPE);
            if (attribs[nAttribs] == EGL_NATIVE_VISUAL_ID) {
                nativeVisual = attribs[nAttribs + 1];
                nativeVisualID = true;
            }
        }
    }

    /*
     * Add room for EGL_SURFACE_TYPE attrib if not present, remove the
     * EGL_NATIVE_VISUAL_ID attrib if present
     */
    nNewAttribs = (surfType ? nAttribs : nAttribs + 2);
    nNewAttribs = (nativeVisualID ? nNewAttribs - 2 : nNewAttribs);

    newAttribs = malloc((nNewAttribs + 1) * sizeof(*newAttribs));

    if (!newAttribs) {
        err = EGL_BAD_ALLOC;
        goto done;
    }

    for (nAttribs = 0, nNewAttribs = 0;
         attribs[nAttribs] != EGL_NONE;
         nAttribs += 2) {
        /*
         * Convert all instances of EGL_WINDOW_BIT in an EGL_SURFACE_TYPE
         * attribute's value to EGL_STREAM_BIT_KHR
         */
        if ((attribs[nAttribs] == EGL_SURFACE_TYPE) &&
            (attribs[nAttribs + 1] != EGL_DONT_CARE) &&
            (attribs[nAttribs + 1] & EGL_WINDOW_BIT)) {
            newAttribs[nNewAttribs++] = attribs[nAttribs];
            newAttribs[nNewAttribs++] =
                (attribs[nAttribs + 1] & ~EGL_WINDOW_BIT) |
                EGL_STREAM_BIT_KHR;

        /* Remove all instances of the EGL_nATIVE_VISUAL_ID attribute */
        } else if (attribs[nAttribs] != EGL_NATIVE_VISUAL_ID) {
            newAttribs[nNewAttribs++] = attribs[nAttribs];
            newAttribs[nNewAttribs++] = attribs[nAttribs + 1];
        }
    }

    if (!surfType) {
        /*
         * If EGL_SURFACE_TYPE was not specified, convert the default
         * EGL_WINDOW_BIT to EGL_STREAM_BIT_KHR
         */
        newAttribs[nNewAttribs++] = EGL_SURFACE_TYPE;
        newAttribs[nNewAttribs++] = EGL_STREAM_BIT_KHR;
    }

    newAttribs[nNewAttribs] = EGL_NONE;

    if (nativeVisual != EGL_DONT_CARE) {
        /*
         * Need to query *all* configs that match everything but the specified
         * native visual ID, then filter them down based on visual ID before
         * clamping to the number of configs requested.
         */
        ret = data->egl.ChooseConfig(display->devDpy,
                                     newAttribs,
                                     NULL,
                                     0,
                                     &nNewConfigs);

        if (!ret || !*numConfig) goto done;

        newConfigs = malloc(sizeof(EGLConfig) * *numConfig);

        if (!newConfigs) {
            err = EGL_BAD_ALLOC;
            goto done;
        }

        ret = data->egl.ChooseConfig(display->devDpy,
                                     newAttribs,
                                     newConfigs,
                                     nNewConfigs,
                                     &nNewConfigs);

        if (!ret) goto done;

        for (cfg = 0, *numConfig = 0;
             cfg < nNewConfigs && (!configs || *numConfig < configSize);
             cfg++) {
            if (ConfigToDrmFourCC(display, newConfigs[cfg]) !=
                (uint32_t)nativeVisual) {
                continue;
            }

            if (!configs) {
                (*numConfig)++;
                continue;
            }

            configs[(*numConfig)++] = newConfigs[cfg];
        }
    } else {
        ret = data->egl.ChooseConfig(display->devDpy,
                                     newAttribs,
                                     configs,
                                     configSize,
                                     numConfig);
    }

done:
    free(newAttribs);
    free(newConfigs);
    if (err != EGL_SUCCESS) eGbmSetError(data, err);

    eGbmUnrefObject(&display->base);

    return ret;
}

EGLBoolean
eGbmGetConfigAttribHook(EGLDisplay dpy,
                        EGLConfig config,
                        EGLint attribute,
                        EGLint* value)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    EGLBoolean ret;

    if (!display) {
        /*  No platform data. Can't set error EGL_NO_DISPLAY */
        return EGL_FALSE;
    }

    ret = display->data->egl.GetConfigAttrib(display->devDpy,
                                             config,
                                             attribute,
                                             value);

    if (ret) {
        switch (attribute) {
        case EGL_SURFACE_TYPE:
            if (*value & EGL_STREAM_BIT_KHR) {
                *value |= EGL_WINDOW_BIT;
            } else {
                *value &= ~EGL_WINDOW_BIT;
            }
            break;

        case EGL_NATIVE_VISUAL_ID:
            *value = ConfigToDrmFourCC(display, config);
            break;

        default:
            break;
        }
    }

    eGbmUnrefObject(&display->base);

    return ret;
}
