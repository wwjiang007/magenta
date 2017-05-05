// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mdi/mdi.h>

// for now we are linked directly into the devhost
extern mx_handle_t devhost_get_hmdi(void);


static mx_status_t platform_bus_init(mx_driver_t* driver) {

    mx_handle_t mdi_handle = devhost_get_hmdi();
    if (mdi_handle == MX_HANDLE_INVALID) {
        return ERR_NOT_SUPPORTED;
    }

    void* addr = NULL;
    size_t size;
    mx_status_t status = mx_vmo_get_size(mdi_handle, &size);
    if (status != NO_ERROR) {
        printf("platform_bus_init mx_vmo_get_size failed %d\n", status);
        goto fail;
    }
    status = mx_vmar_map(mx_vmar_root_self(), 0, mdi_handle, 0, size, MX_VM_FLAG_PERM_READ,
                         (uintptr_t *)&addr);
    if (status != NO_ERROR) {
        printf("platform_bus_init mx_vmar_map failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t root_node;
    status = mdi_init(addr, size, &root_node);
    if (status != NO_ERROR) {
        printf("platform_bus_init mdi_init failed %d\n", status);
        goto fail;
    }

    printf("platform_bus_init SUCCESS\n");

    return NO_ERROR;

fail:
    if (addr) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)addr, size);
    }
    mx_handle_close(mdi_handle);
    return status;
}

static mx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = platform_bus_init,
};

MAGENTA_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "magenta", "0.1", 0)
MAGENTA_DRIVER_END(platform_bus)
