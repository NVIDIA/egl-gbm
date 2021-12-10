/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gbm-utils.h"
#include <string.h>
#include <stdio.h>

#if HAS_MINCORE
#include <unistd.h>
#include <dlfcn.h>
#endif

EGLBoolean
eGbmFindExtension(const char* extension, const char* extensions)
{
    const char* start;
    const char* where;
    const char* terminator;

    start = extensions;
    for (;;) {
        where = strstr(start, extension);
        if (!where) {
            break;
        }
        terminator = where + strlen(extension);
        if (where == start || *(where - 1) == ' ') {
            if (*terminator == ' ' || *terminator == '\0') {
                return EGL_TRUE;
            }
        }
        start = terminator;
    }

    return EGL_FALSE;
}

void
eGbmSetErrorInternal(GbmPlatformData *data, EGLint error,
                          const char *file, int line)
{
    static const char *defaultMsg = "GBM external platform error";
    char msg[256];

    if (!data || !data->driver.setError) return;

    if (!file || (snprintf(msg, sizeof(msg), "%s:%d: %s",
                           file, line, defaultMsg) <= 0)) {
        data->driver.setError(error, EGL_DEBUG_MSG_ERROR_KHR, defaultMsg);
        return;
    }

    data->driver.setError(error, EGL_DEBUG_MSG_ERROR_KHR, msg);
}

#if HAS_MINCORE
EGLBoolean
eGbmPointerIsDereferenceable(void* p)
{
    /*
     * BSD and Solaris have slightly different prototypes for mincore, but
     * they should be compatible with this.  BSD uses:
     *
     *   (const void*, size_t, char*)
     *
     * And Solaris uses:
     *
     *   (caddr_t, size_t, char*)
     *
     * Which I believe are all ABI compatible with the Linux prototype used
     * below for MINCOREPROC.
     */
    typedef int (*MINCOREPROC)(void*, size_t, unsigned char*);
    static MINCOREPROC pMinCore = NULL;
    static EGLBoolean minCoreLoadAttempted = EGL_FALSE;
    uintptr_t addr = (uintptr_t)p;
    unsigned char unused;
    const long page_size = getpagesize();

    if (minCoreLoadAttempted == EGL_FALSE) {
        minCoreLoadAttempted = EGL_TRUE;

        /*
         * According to its manpage, mincore was introduced in Linux 2.3.99pre1
         * and glibc 2.2.  The minimum glibc our driver supports is 2.0, so this
         * mincore can not be linked in directly.  It does however seem
         * reasonable to assume that Wayland will not be run on glibc < 2.2.
         *
         * Attempt to load mincore from the currently available libraries.
         * mincore comes from libc, which the EGL driver depends on, so it
         * should always be loaded if our driver is running.
         */
        pMinCore = (MINCOREPROC)dlsym(NULL, "mincore");
    }

    /*
     * If the pointer can't be tested for safety, or is obviously unsafe,
     * assume it can't be dereferenced.
     */
    if (p == NULL || !pMinCore) {
        dlerror();
        return EGL_FALSE;
    }

    /* align addr to page_size */
    addr &= ~(page_size - 1);

    /*
     * mincore() returns 0 on success, and -1 on failure.  The last parameter
     * is a vector of bytes with one entry for each page queried.  mincore
     * returns page residency information in the first bit of each byte in the
     * vector.
     *
     * Residency doesn't actually matter when determining whether a pointer is
     * dereferenceable, so the output vector can be ignored.  What matters is
     * whether mincore succeeds.  It will fail with ENOMEM if the range
     * [addr, addr + length) is not mapped into the process, so all that needs
     * to be checked there is whether the mincore call succeeds or not, as it
     * can only succeed on dereferenceable memory ranges.
     */
    return (pMinCore((void*)addr, page_size, &unused) >= 0);
}
#endif /* HAS_MINCORE */
