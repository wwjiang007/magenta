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
#include <mdi/mdi-defs.h>

// for now we are linked directly into the devhost
extern mx_handle_t devhost_get_hmdi(void);


typedef struct {
    mx_device_t* mxdev;
} platform_dev_t;

static void platform_dev_release(void* ctx) {
    platform_dev_t* dev = ctx;
    free(dev);
}

static mx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_dev_release,
};

static void platform_bus_publish_devices(mdi_node_ref_t* bus_node, mx_driver_t* driver) {
    mdi_node_ref_t  driver_node;
    mdi_each_child(bus_node, &driver_node) {
        if (mdi_id(&driver_node) != MDI_PLATFORM_BUS_DRIVER) {
            printf("unexpected node %d in platform_bus_publish_devices\n", mdi_id(&driver_node));
            continue;
        }

        const char* name = NULL;
        mdi_node_ref_t  name_node;
        mx_status_t status = mdi_find_node(&driver_node, MDI_PLATFORM_BUS_DRIVER_NAME, &name_node);
        if (status != NO_ERROR) {
            printf("could not find MDI_PLATFORM_BUS_DRIVER_NAME\n");
            continue;
        }
        name = mdi_node_string(&name_node);
        if (!name) {
            printf("could not find MDI_PLATFORM_BUS_DRIVER_NAME\n");
            continue;
        }

        platform_dev_t* dev = calloc(1, sizeof(platform_dev_t));
        if (!dev) return;

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = dev,
            .driver = driver,
            .ops = &platform_dev_proto,
        };

        status = device_add(driver_get_root_device(), &args, &dev->mxdev);
        if (status != NO_ERROR) {
            printf("platform-bus failed to add device for %s\n", name);
        } else {
            printf("platform-bus added device %s\n", name);
        }
    }
}

static mx_status_t platform_bus_init(mx_driver_t* driver) {
    printf("platform_bus_init\n");

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

    mdi_node_ref_t  bus_node;
    if (mdi_find_node(&root_node, MDI_PLATFORM_BUS, &bus_node) != NO_ERROR) {
        printf("platform_bus_init couldn't find MDI_PLATFORM_BUS\n");
        goto fail;
    }
    platform_bus_publish_devices(&bus_node, driver);
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
