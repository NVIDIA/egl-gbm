/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GBM_MUTEX_H
#define GBM_MUTEX_H

#include <stdbool.h>

bool eGbmHandlesLock(void);
void eGbmHandlesUnlock(void);

#endif /* GBM_MUTEX_H */
