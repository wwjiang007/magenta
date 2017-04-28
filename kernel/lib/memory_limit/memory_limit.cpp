// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <iovec.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <lib/memory_limit.h>
#include <mxtl/algorithm.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <inttypes.h>


#define LOCAL_TRACE 1

/* Checks if a memory limit has been imposed on the system by the boot
 * command line. NO_ERROR indicates a valid limit is being returned,
 * whereas ERR_NOT_SUPPORTED indicates there is no such restriction
 * on the kernel.
 */
status_t mem_limit_get(uint64_t* limit) {
    uint64_t _limit;

    if (!limit) {
        return ERR_INVALID_ARGS;
    }

    _limit = cmdline_get_uint64("kernel.memory-limit", 0);

    if (_limit == 0) {
        return ERR_NOT_SUPPORTED;
    }

    *limit = _limit;
    return NO_ERROR;
}

// Minimal validation is done here because it isn't the responsibility of this
// library to ensure the kernel and ramdisk aren't overlapping in some manner.
status_t mem_limit_init(mem_limit_cfg_t* cfg) {
    if (!cfg) {
        return ERR_INVALID_ARGS;
    }

    status_t status = mem_limit_get(&cfg->memory_limit);
    if (status == NO_ERROR) {
        cfg->memory_limit *= MB; // Convert MB to B for later calls
    }

    return status;
}

/* This will take a contiguous range of memory and return io vectors
 * corresponding to the arenas that needed to be carved out due to placement of
 * the kernel, placement of the ramdisk, and any memory limits being imposed
 * upon the system. The size of the arena is subtracted from the value passed in
 * by 'limit'
 */
status_t mem_limit_apply(mem_limit_cfg_t* cfg,
                            uintptr_t range_base,
                            size_t range_size,
                            iovec_t iovs[],
                            size_t iov_cnt,
                            size_t* used_cnt) {

    // This correspond to the two ranges that might be built to represent
    // a pair of ranges that correspond to a kernel and a ramdisk. They're
    // used instead of iovs[] directly to avoid casting for (void*) math.
    uintptr_t low_base = 0, high_base = 0;
    size_t low_len = 0, high_len = 0;

    // We need at most 2 iovec_ts to handle both the kernel and ramdisk in any
    // memory layout within a single range if we grow/shrink sub-ranges.
    if (!cfg || !iovs || range_size == 0 || iov_cnt < 2 || !used_cnt) {
        return ERR_INVALID_ARGS;
    }

    if (cfg->memory_limit == 0) {
        /* If our limit has been reached this range can be skipped */
        *used_cnt = 0;
        return NO_ERROR;
    }

    LTRACEF("scanning range %" PRIxPTR " of size %zu, (kernel start %#" PRIxPTR  " limit %zu\n",
            range_base, range_size, cfg->kernel_base, cfg->memory_limit);
    // Convenience values for the offsets and sizes within the range.
    uintptr_t range_end = range_base + range_size;
    uintptr_t k_base = cfg->kernel_base;
    size_t k_size = cfg->kernel_size;
    uintptr_t k_end = k_base + k_size;
    uintptr_t r_base = cfg->ramdisk_base;
    size_t r_size = cfg->ramdisk_size;
    uintptr_t r_end = r_base + r_size;

    /* This is where things get more complicated if we found the kernel_iov. On both
     * x86 and ARM the kernel and ramdisk will exist in the same memory range.
     * On x86 this is the lowmem region below 4GB based on where UEFI's page
     * allocations placed it. For ARM, it depends on the platform's bootrom, but
     * the important detail is that they both should be in the same contiguous
     * block of DRAM. Either way, we know the kernel + bss needs to be included
     * in memory regardless so that's the first priority.
     *
     * If we booted in the first place then we can assume we have enough space
     * for ourselves. k_low/k_high/r_high represent spans as follows:
     * |base|<k_low>[kernel]<k_high>[ramdisk]<r_high>|_end|
     *
     * Alternatively, if there is no ramdisk then the situation looks more like:
     * |base|<k_low>[kernel]< k_high >[end]
     *
     * TODO: when kernel relocation exists this will need to handle the ramdisk
     * being before the kernel_iov, as well as them possibly being in different
     * ranges.
     */
    if (range_base <= k_base && k_base < range_end) {
        uint64_t k_low = 0, k_high = 0, r_high = 0, tmp = 0;

        // First set up the kernel
        k_low = k_base - range_base;
        k_high = range_end;
        low_base = k_base;
        low_len = k_size;
        cfg->memory_limit -= k_size;
        LTRACEF("kernel base %#" PRIxPTR " size %#" PRIxPTR "\n", k_base, k_size);

        // Add the ramdisk_iov. Truncate if we must and warn the user if it happens
        if (r_size) {
            LTRACEF("ramdisk base %" PRIxPTR " size %" PRIxPTR "\n", r_base, r_size);
            tmp = mxtl::min(cfg->memory_limit, r_size);
            if (tmp != r_size) {
                printf("Warning: ramdisk has been truncated from %zu to %zu"
                       "bytes due to memory limitations.\n",
                       r_size, cfg->memory_limit);
            }
            high_base = r_base;
            high_len = tmp;
            cfg->memory_limit -= tmp;

            // If a ramdisk is found then the kernel ends at the ramdisk's base
            // rather than at the end of the range
            k_high = r_base - k_end;
            r_high = range_end - r_end;
        }

        // We've created our kernel and ramdisk vecs, and now we expand them as
        // much as possible within the imposed limit, starting with the k_high
        // gap between the kernel and ramdisk_iov.
        tmp = mxtl::min(cfg->memory_limit, k_high);
        if (tmp) {
            LTRACEF("growing low iov by %zu bytes.\n", tmp);
            low_len += tmp;
            cfg->memory_limit -= tmp;
        }

        // Handle space between the start of the range and the kernel base
        tmp = mxtl::min(cfg->memory_limit, k_low);
        if (tmp) {
            low_base -= tmp;
            low_len += tmp;
            cfg->memory_limit -= tmp;
            LTRACEF("moving low iov base back by %zu to %#" PRIxPTR ".\n",
                    tmp, low_base);
        }

        // If we have no ramdisk then k_high will have encompassed this region,
        // but this is also accounted for by r_high being 0.
        tmp = mxtl::min(cfg->memory_limit, r_high);
        if (tmp) {
            LTRACEF("growing high iov by %zu bytes.\n", tmp);
            high_len += tmp;
            cfg->memory_limit -= tmp;
        }

        // Collapse the kernel and ramdisk into a single io vector if they're
        // adjacent to each other.
        if ((low_base + low_len) == high_base) {
            low_len += high_len;
            high_base = 0;
            high_len = 0;
            LTRACEF("Merging both iovs into a single iov base %#" PRIxPTR " size %zu\n",
                    low_base, low_len);
        }

        cfg->found_kernel = true;
        cfg->found_ramdisk = true;
    } else {

        size_t relative_limit = cfg->memory_limit;

        // Set the limit for the current range we're scanning based on whether we
        // have found the kernel and ramdisk yet. If we haven't, we need to set
        // aside space for them in future ranges.
        if (!cfg->found_kernel) {
            relative_limit -= mxtl::min(cfg->kernel_size, relative_limit);
        }

        if (!cfg->found_kernel && cfg->ramdisk_size) {
            relative_limit -= mxtl::min(cfg->ramdisk_size, relative_limit);
        }

        LTRACEF("relative limit of %zu being used (found_kernel: %d, found_ramdisk: %d)\n", relative_limit, cfg->found_kernel, cfg->found_ramdisk);
        // No kernel here, presumably no ramdisk. Just add what we can.
        uint64_t tmp = mxtl::min(relative_limit, range_size);
        low_base = range_base;
        low_len = tmp;
        cfg->memory_limit -= tmp;
        LTRACEF("using %zu bytes from base %#" PRIxPTR "\n", low_len, low_base);
    }

exit:
    // Build the iovs with the ranges figured out above
    memset(iovs, 0, sizeof(iovec_t) * iov_cnt);
    iovs[0].iov_base = reinterpret_cast<void*>(low_base);
    iovs[0].iov_len = ROUNDUP_PAGE_SIZE(low_len);
    iovs[1].iov_base = reinterpret_cast<void*>(high_base);
    iovs[1].iov_len = ROUNDUP_PAGE_SIZE(high_len);

    // Set the count to 0 through 2 depending on vectors used
    *used_cnt = !!(iovs[0].iov_len);
    *used_cnt += !!(iovs[1].iov_len);

    LTRACEF("used %zu iov%s remaining memory %zu bytes\n", *used_cnt, (*used_cnt == 1) ? "," : "s,", cfg->memory_limit);
    return NO_ERROR;
}
