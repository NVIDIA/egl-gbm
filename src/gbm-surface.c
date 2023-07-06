/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gbm-surface.h"
#include "gbm-display.h"
#include "gbm-utils.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <EGL/eglext.h>
#include <gbmint.h>
#include <unistd.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

#define MAX_STREAM_IMAGES 10

// One front, one back.
#define WINDOW_STREAM_FIFO_LENGTH 2

typedef struct GbmSurfaceImageRec {
    EGLImage image;
    struct gbm_bo* bo;
    struct GbmSurfaceImageRec* nextAcquired;
    bool locked;
} GbmSurfaceImage;

typedef struct GbmSurfaceRec {
    GbmObject base;
    EGLStreamKHR stream;
    EGLSurface egl;
    EGLSyncKHR sync;
    GbmSurfaceImage images[MAX_STREAM_IMAGES];
    struct {
        GbmSurfaceImage *first;
        GbmSurfaceImage *last;
    } acquiredImages;

    /*
     * The number of free color buffers. This is initially set to the stream's
     * FIFO length, and updated whenever we acquire or release an EGLImage
     * to/from the stream.
     *
     * FIXME: Our EGLImage handling is wrong: If the application calls
     * eglSwapBuffers more than once without calling
     * gbm_surface_lock_front_buffer, then gbm_surface_lock_front_buffer will
     * return the buffer from the oldest swap, but it should return the newest
     * swap.
     *
     * Also, if an application calls eglSwapBuffers more times than the FIFO
     * depth without calling gbm_surface_lock_front_buffer, then it will fill
     * up the FIFO and hang.
     *
     * To get something closer to correct behavior, in eglSwapBuffers we'd need
     * to call eglStreamReleaseImageNV on all unlocked buffers, then call into
     * the driver's eglSwapBuffers, and then call eglStreamAcquireImageNV to
     * fetch the EGLImage for that frame. That would require adding an
     * eglSwapBuffers hook and rewriting eGbmSurfaceLockFrontBuffer and
     * eGbmSurfaceReleaseBuffer.
     */
    int numFreeImages;
} GbmSurface;

/*
 * Returns a pointer to a pointer in the NV-private structure that wraps the
 * gbm_surface structure. This pointer is reserved for use by this library.
 */
static inline GbmSurface**
GetPrivPtr(struct gbm_surface* s)
{
    uint8_t *ptr = (uint8_t *)s;

    return (GbmSurface **)(ptr - sizeof(void*));
}

static inline GbmSurface*
GetSurf(struct gbm_surface* s)
{
    return s ? *GetPrivPtr(s) : NULL;
}

static inline void
SetSurf(struct gbm_surface* s, GbmSurface *surf)
{
    GbmSurface **priv = GetPrivPtr(s);

    *priv = surf;
}

static bool
AddSurfImage(GbmDisplay* display, GbmSurface* surf)
{
    GbmPlatformData* data = display->data;
    unsigned int i;

    for (i = 0; i < ARRAY_LEN(surf->images); i++) {
        if (surf->images[i].image == EGL_NO_IMAGE_KHR &&
            surf->images[i].bo == NULL) {
            surf->images[i].image =
                data->egl.CreateImageKHR(display->devDpy,
                                         EGL_NO_CONTEXT,
                                         EGL_STREAM_CONSUMER_IMAGE_NV,
                                         (EGLClientBuffer)surf->stream,
                                         NULL);
            if (surf->images[i].image == EGL_NO_IMAGE_KHR) break;

            return true;
        }
    }

    return false;
}

static void
RemoveSurfImage(GbmDisplay* display, GbmSurface* surf, EGLImage img)
{
    GbmPlatformData* data = display->data;
    GbmSurfaceImage* acqImg;
    GbmSurfaceImage* prev = NULL;
    unsigned int i;

    for (i = 0; i < ARRAY_LEN(surf->images); i++) {
        if (surf->images[i].image == img) {
            /*
             * The EGL_NV_stream_consumer_eglimage spec is unclear if removed
             * images that are currently acquired still need to be released, but
             * it does say this:
             *
             *   If an acquired EGLImage has not yet released when
             *   eglDestroyImage is called, then, then an implicit
             *   eglStreamReleaseImageNV will be called.
             *
             * so this should be sufficient either way.
             */
            data->egl.DestroyImageKHR(display->devDpy, img);
            surf->images[i].image = EGL_NO_IMAGE_KHR;
            if (!surf->images[i].locked && surf->images[i].bo) {
                gbm_bo_destroy(surf->images[i].bo);
                surf->images[i].bo = NULL;
            } else {
                /*
                 * If the image is currently acquired from the stream and
                 * available for locking, remove it from the acquired images list.
                 */
                for (acqImg = surf->acquiredImages.first;
                     acqImg;
                     prev = acqImg, acqImg = acqImg->nextAcquired) {
                    if (acqImg == &surf->images[i]) {
                        if (prev)
                            prev->nextAcquired = acqImg->nextAcquired;
                        else
                            surf->acquiredImages.first = acqImg->nextAcquired;

                        if (surf->acquiredImages.last == acqImg)
                            surf->acquiredImages.last = prev;

                        assert(surf->numFreeImages < WINDOW_STREAM_FIFO_LENGTH);
                        surf->numFreeImages++;
                        break;
                    }
                }
            }

            break;
        }
    }
}

static bool AcquireSurfImage(GbmDisplay* display, GbmSurface* surf)
{
    GbmPlatformData* data = display->data;
    EGLDisplay dpy = display->devDpy;
    GbmSurfaceImage* image = NULL;
    EGLImage img;
    unsigned int i;
    EGLBoolean res;

    res = data->egl.StreamAcquireImageNV(dpy,
                                         surf->stream,
                                         &img,
                                         surf->sync);

    if (!res) {
        /*
         * Match Mesa EGL dri2 platform behavior when no buffer is available
         * even though this function is not called from an EGL entry point
         */
        eGbmSetError(data, EGL_BAD_SURFACE);
        return false;
    }

    if (data->egl.ClientWaitSyncKHR(dpy, surf->sync, 0, EGL_FOREVER_KHR) !=
        EGL_CONDITION_SATISFIED_KHR) {
        /* Release the image back to the stream */
        data->egl.StreamReleaseImageNV(dpy,
                                       surf->stream,
                                       img,
                                       surf->sync);
        /* Not clear what error to use. Pretend no buffer was available. */
        eGbmSetError(data, EGL_BAD_SURFACE);
        return false;
    }

    for (i = 0; i < ARRAY_LEN(surf->images); i++) {
        if (surf->images[i].image == img) {
            image = &surf->images[i];
            break;
        }
    }

    if (surf->acquiredImages.last)
        surf->acquiredImages.last->nextAcquired = image;
    else
        surf->acquiredImages.first = image;
    surf->acquiredImages.last = image;
    surf->numFreeImages--;

    return true;
}

static bool
PumpSurfEvents(GbmDisplay* display, GbmSurface* surf)
{
    GbmPlatformData* data = display->data;
    EGLenum event;
    EGLAttrib aux;
    EGLint evStatus;

    while (true) {
        evStatus = data->egl.QueryStreamConsumerEventNV(display->devDpy,
                                                        surf->stream,
                                                        0,
                                                        &event,
                                                        &aux);

        if (evStatus != EGL_TRUE) break;

        switch (event) {
        case EGL_STREAM_IMAGE_AVAILABLE_NV:
            /*
             * The image must be acquired to clear the IMAGE_AVAILABLE event,
             * so acquire it here rather than in eGbmSurfaceLockFrontBuffer().
             */
            if (!AcquireSurfImage(display, surf)) return false;
            break;
        case EGL_STREAM_IMAGE_ADD_NV:
            if (!AddSurfImage(display, surf)) return false;
            break;

        case EGL_STREAM_IMAGE_REMOVE_NV:
            RemoveSurfImage(display, surf, (EGLImage)aux);
            break;

        default:
            assert(!"Unhandled EGLImage stream consumer event");

        }
    }

    return evStatus != EGL_FALSE;
}

int
eGbmSurfaceHasFreeBuffers(struct gbm_surface* s)
{
    GbmSurface* surf = GetSurf(s);

    if (!surf) return 0;

    if (!PumpSurfEvents(surf->base.dpy, surf)) return 0;

    return (surf->numFreeImages > 0);
}

struct gbm_bo*
eGbmSurfaceLockFrontBuffer(struct gbm_surface* s)
{
    GbmSurface* surf = GetSurf(s);
    GbmSurfaceImage* image;
    GbmPlatformData* data;
    EGLDisplay dpy;
    uint32_t i;

    if (!surf) return NULL;

    data = surf->base.dpy->data;
    dpy = surf->base.dpy->devDpy;

    /* Must pump events to ensure images are created before acquiring them */
    if (!PumpSurfEvents(surf->base.dpy, surf)) return NULL;

    if (!surf->acquiredImages.first) return NULL;

    image = surf->acquiredImages.first;
    assert(image->image);

    if (!image->bo) {
        struct gbm_import_fd_modifier_data buf;
        uint64_t modifier;
        EGLint stride; /* XXX support planar formats */
        EGLint offset; /* XXX support planar formats */
        int format;
        int planes;
        int fd; /* XXX support planar separate memory objects */

        if (!data->egl.ExportDMABUFImageQueryMESA(dpy,
                                                  image->image,
                                                  &format,
                                                  &planes,
                                                  &modifier)) goto fail;

        assert(planes == 1); /* XXX support planar formats */

        if (!data->egl.ExportDMABUFImageMESA(dpy, image->image,
                                             &fd, &stride, &offset)) {
            goto fail;
        }

        memset(&buf, 0, sizeof(buf));
        buf.width = s->v0.width;
        buf.height = s->v0.height;
        buf.format = s->v0.format;
        buf.num_fds = 1; /* XXX support planar separate memory objects */
        buf.fds[0] = fd;
        buf.strides[0] = stride;
        buf.offsets[0] = offset;
        buf.modifier = modifier;
        image->bo = gbm_bo_import(surf->base.dpy->gbm,
                                  GBM_BO_IMPORT_FD_MODIFIER,
                                  &buf, 0);

        for (i = 0; i < buf.num_fds; i++) {
            close(buf.fds[i]);
        }

        if (!image->bo) goto fail;
    }

    surf->acquiredImages.first = image->nextAcquired;
    if (!surf->acquiredImages.first)
        surf->acquiredImages.last = NULL;
    image->locked = true;

    return image->bo;

fail:
    /* XXX Can this be called from outside an EGL entry point? */
    eGbmSetError(data, EGL_BAD_ALLOC);

    return NULL;

}

void
eGbmSurfaceReleaseBuffer(struct gbm_surface* s, struct gbm_bo *bo)
{
    GbmSurface* surf = GetSurf(s);
    GbmDisplay* display;
    EGLImage img = EGL_NO_IMAGE_KHR;
    unsigned int i;

    if (!surf || !bo) return;

    display = surf->base.dpy;

    for (i = 0; i < ARRAY_LEN(surf->images); i++) {
        if (surf->images[i].bo == bo) {
            surf->images[i].locked = false;
            img = surf->images[i].image;

            if (!img) {
                /*
                 * The stream removed this image while it was locked. Free the
                 * buffer object associated with it as well.
                 */
                gbm_bo_destroy(surf->images[i].bo);
            }

            break;
        }
    }

    assert(img != EGL_NO_IMAGE_KHR);

    if (img != EGL_NO_IMAGE_KHR) {
        display->data->egl.StreamReleaseImageNV(display->devDpy,
                                                surf->stream,
                                                img,
                                                EGL_NO_SYNC_KHR);
        assert(surf->numFreeImages < WINDOW_STREAM_FIFO_LENGTH);
        surf->numFreeImages++;
    }
}

static void
FreeSurface(GbmObject* obj)
{
    if (obj) {
        GbmSurface* surf = (GbmSurface*)obj;
        GbmPlatformData* data = obj->dpy->data;
        EGLDisplay dpy = obj->dpy->devDpy;
        unsigned int i;

        for (i = 0; i < ARRAY_LEN(surf->images); i++) {
            if (surf->images[i].image != EGL_NO_IMAGE_KHR)
                data->egl.DestroyImageKHR(dpy, surf->images[i].image);

            if (surf->images[i].bo != NULL)
                gbm_bo_destroy(surf->images[i].bo);
        }

        if (surf->egl != EGL_NO_SURFACE)
            data->egl.DestroySurface(dpy, surf->egl);
        if (surf->stream != EGL_NO_STREAM_KHR)
            data->egl.DestroyStreamKHR(dpy, surf->stream);
        if (surf->sync != EGL_NO_SYNC_KHR)
            data->egl.DestroySyncKHR(dpy, surf->sync);

        /* Drop reference to the display acquired at creation time */
        eGbmUnrefObject(&obj->dpy->base);

        free(obj);
    }
}

EGLSurface
eGbmCreatePlatformWindowSurfaceHook(EGLDisplay dpy,
                                    EGLConfig config,
                                    void* nativeWin,
                                    const EGLAttrib* attribs)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    GbmPlatformData* data;
    struct gbm_surface* s = nativeWin;
    GbmSurface* surf = NULL;
    EGLint surfType;
    EGLint err = EGL_BAD_ALLOC;
    EGLBoolean res;
    const EGLint surfAttrs[] = {
        /* XXX Merge in relevant <attribs> here as well */
        EGL_WIDTH, s->v0.width,
        EGL_HEIGHT, s->v0.height,
        EGL_NONE
    };
    static const EGLint streamAttrs[] = {
        EGL_STREAM_FIFO_LENGTH_KHR, WINDOW_STREAM_FIFO_LENGTH,
        EGL_NONE
    };
    static const EGLint syncAttrs[] = {
        EGL_SYNC_STATUS_KHR, EGL_SIGNALED_KHR,
        EGL_NONE
    };

    (void)attribs;

    if (!display) {
        /*  No platform data. Can't set error EGL_NO_DISPLAY */
        return EGL_NO_SURFACE;
    }

    data = display->data;
    dpy = display->devDpy;

    if (!s) {
        err = EGL_BAD_NATIVE_WINDOW;
        goto fail;
    }

    if (s->gbm != display->gbm) {
        err = EGL_BAD_NATIVE_WINDOW;
        goto fail;
    }

    res = data->egl.GetConfigAttrib(dpy, config, EGL_SURFACE_TYPE, &surfType);

    if (!res || !(surfType & EGL_STREAM_BIT_KHR)) {
        err = EGL_BAD_CONFIG;
        goto fail;
    }

    surf = calloc(1, sizeof(*surf));

    if (!surf) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surf->base.dpy = display;
    surf->base.type = EGL_OBJECT_SURFACE_KHR;
    surf->base.refCount = 1;
    surf->base.free = FreeSurface;
    surf->stream = data->egl.CreateStreamKHR(dpy, streamAttrs);
    surf->numFreeImages = WINDOW_STREAM_FIFO_LENGTH;

    if (!surf->stream) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (!data->egl.StreamImageConsumerConnectNV(dpy,
                                                surf->stream,
                                                s->v0.count,
                                                s->v0.modifiers,
                                                NULL)) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surf->egl = data->egl.CreateStreamProducerSurfaceKHR(dpy,
                                                         config,
                                                         surf->stream,
                                                         surfAttrs);

    if (!surf->egl) {
        err = data->egl.GetError();
        // Pass EGL_BAD_MATCH through, since that's an allowed error for
        // eglCreateWindowSurface, and it would still make sense to the
        // application. Otherwise, send back EGL_BAD_ALLOC.
        if (err != EGL_BAD_MATCH) {
            err = EGL_BAD_ALLOC;
        }
        goto fail;
    }

    surf->sync = data->egl.CreateSyncKHR(dpy,
                                         EGL_SYNC_FENCE_KHR,
                                         syncAttrs);

    if (!surf->sync) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (!PumpSurfEvents(display, surf)) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* The reference to the display object is retained by surf */
    if (!eGbmAddObject(&surf->base)) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    SetSurf(s, surf);

    return (EGLSurface)surf;

fail:
    FreeSurface(&surf->base);

    eGbmSetError(display->data, err);

    return EGL_NO_SURFACE;
}

void*
eGbmSurfaceUnwrap(GbmObject* obj)
{
    return ((GbmSurface*)obj)->egl;
}

EGLBoolean
eGbmDestroySurfaceHook(EGLDisplay dpy, EGLSurface eglSurf)
{
    GbmDisplay* display = (GbmDisplay*)eGbmRefHandle(dpy);
    EGLBoolean ret = EGL_FALSE;

    if (!display) return ret;

    if (eGbmDestroyHandle(eglSurf)) ret = EGL_TRUE;

    eGbmUnrefObject(&display->base);

    return ret;
}
