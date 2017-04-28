#include <unittest.h>
#include <sys/types.h>
#include <lib/memory_limit.h>
#include <err.h>

typedef struct {
    uintptr_t base;
    size_t size;
} test_range_t;

// Memory map provided by EFI 5/3/17
static test_range_t nuc_ranges[] = {
    { 0, 0x58000, },
    { 0x59000, 0x45000 },
    { 0x100000, 0x85d8b000 },
    { 0x85eb6000, 0x4375000 },
    { 0x8b2ff000, 0x1000 },
    { 0x100000000, 0x36f000000 },
};

static mem_limit_cfg_t nuc_cfg = {
    .kernel_base = 0x100000,
    .kernel_size = 4 * MB,
    .ramdisk_base = 0x818e4000,
    .ramdisk_size = 4 *MB,
    .memory_limit = 0,
    .found_kernel = 0,
    .found_ramdisk = 0,
};

/* // Memory map provided by EFI 5/3/17
__UNUSED static test_range_t acer12[] = {
    { 0, 0x58000 },
    { 0x59000, 0x2d000 },
    { 0x100000, 0x7359d000 },
    { 0x736c8000, 0xb1000 },
    { 0x74079000, 0x16215000 },
    { 0x8aefe000, 0x1000 },
    { 0x100000000, 0x6f000000 },
};

static bool ml_test_args(void* context) {
    BEGIN_TEST;
    uintptr_t base = 0x0;
    size_t mem_limit = 16 * MB;
    size_t size = 128 * MB;
    size_t k_size = 16 * MB;
    iovec_t vecs[2];

    mem_limit_cfg_t cfg = {
        .kernel_base = (base + size) - k_size,
        .kernel_size 4 * MB,
        .ramdisk_base = 0,
        .ramdisk_size = 0,
        .memory_limit = mem_limit,
       };

    [>EXPECT_EQ(ERR_INVALID_ARGS, mem_limit_apply(&cfg, base, 0, vecs, 2), "range size 0");
    EXPECT_EQ(ERR_INVALID_ARGS, mem_limit_apply(&cfg, base, size, (iovec_t*)nullptr, 2), "invalid iovecs");
    EXPECT_EQ(ERR_INVALID_ARGS, mem_limit_apply(&cfg, base, size, vecs, 1), "not enough iovecs");
    cfg.memory_limit = 0;
    EXPECT_EQ(ERR_NO_MEMORY, mem_limit_apply(&cfg, base, size, vecs, 2), "check no limit");<]

    END_TEST;
} */

// Unit tests
static bool ml_test_nuc(void* context) {
    BEGIN_TEST;
    iovec_t vecs[2];
    status_t status;
    size_t used;

    // This test closely matches observed layout booting magenta on a skylake
    // NUC with the kernel and ramdisk in the lower 4 GB.
    // We should end up with two vectors. The first is the kernel + an expanded range
    // to fill out the ~122 MB not used by the kernel and ramdisk. The second should
    // just be the ramdisk itself.

    for (size_t memory_limit = 2 * GB; memory_limit >= 2 * GB; memory_limit /= 2) {
        size_t size = 0;
        nuc_cfg.memory_limit = memory_limit;
        nuc_cfg.found_kernel = false;
        nuc_cfg.found_ramdisk = false;


        for (auto range : nuc_ranges) {
            status = mem_limit_apply(&nuc_cfg, range.base, range.size, vecs, countof(vecs), &used);
            EXPECT_EQ(status, NO_ERROR, "checking status");
            size += vecs[0].iov_len + vecs[1].iov_len;
        }

        EXPECT_EQ(nuc_cfg.found_kernel, true, "checking kernel");
        EXPECT_EQ(nuc_cfg.found_ramdisk, true, "checking ramdisk");
        EXPECT_EQ(size, memory_limit, "comparing limit");
    }

    END_TEST;
}

/* static bool ml_kernel_rpi3(void* context) {
    BEGIN_TEST;

    // Values obtained from magenta booting on a standard rpi3.
    uintptr_t base = 0xffff000000000000;
    uintptr_t kernel_load_offset = 0;
    size_t size = 0x20000000;
    size_t mem_limit = 0;
    size_t used;
    iovec_t vecs[2];

    mem_limit_cfg_t cfg = {
        .kernel_base = base + kernel_load_offset,
        .kernel_end = 0xffff0000005a1000,
        .ramdisk_base = 0xffff000007d44000,
        .ramdisk_size = 2801664,
        .memory_limit = 0,
    };

    // Call with 512 MB
    cfg.memory_limit = mem_limit = 512 * MB;
    EXPECT_EQ(NO_ERROR, mem_limit_apply(&cfg, base, size, vecs, countof(vecs), &used), "rpi3 512 apply");
    // Test results
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[0].iov_base), cfg.kernel_base, "rpi3 512 vec0 base");
    EXPECT_EQ(vecs[0].iov_len, mem_limit, "rpi3 512 vec0 len");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[1].iov_base), 0u, "rpi3 512 vec1 base");
    EXPECT_EQ(vecs[1].iov_len, 0u, "rpi3 512 vec1 len");

    // Call with 256 MB
    cfg.memory_limit = mem_limit = 256 * MB;
    EXPECT_EQ(NO_ERROR, mem_limit_apply(&cfg, base, size, vecs, countof(vecs), &used), "rpi3 256 apply");
    // Test results
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[0].iov_base), cfg.kernel_base, "rpi3 256 vec0 base");
    EXPECT_EQ(vecs[0].iov_len, mem_limit, "rpi3 256 vec0 len");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[1].iov_base), 0u, "rpi3 256 vec1 base");
    EXPECT_EQ(vecs[1].iov_len, 0u, "rpi3 256 vec1 len");

    // Call with 128 MB
    cfg.memory_limit = mem_limit = 128 * MB;
    EXPECT_EQ(NO_ERROR, mem_limit_apply(&cfg, base, size, vecs, countof(vecs), &used), "rpi3 128 apply");
    // Test results
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[0].iov_base), cfg.kernel_base, "rpi3 128 vec0 base");
    EXPECT_EQ(vecs[0].iov_len, mem_limit, "rpi3 128 vec0 len");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[1].iov_base), 0u, "rpi3 128 vec1 base");
    EXPECT_EQ(vecs[1].iov_len, 0u, "rpi3 128 vec1 len");

    // Call with 64 MB
    // This is the case where a split should be seen since the ramdisk base is outside the limit
    // of a contiguous region of 64 MB
    cfg.memory_limit = mem_limit = 64 * MB;
    EXPECT_EQ(NO_ERROR, mem_limit_apply(&cfg, base, size, vecs, countof(vecs), &used), "rpi3 64 apply");
    // Test results
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[0].iov_base), cfg.kernel_base, "rpi3 64 vec0 base");
    EXPECT_EQ(vecs[0].iov_len, mem_limit - cfg.ramdisk_size, "rpi3 64 vec0 len");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[1].iov_base), cfg.ramdisk_base, "rpi3 64 vec1 base");
    EXPECT_EQ(vecs[1].iov_len, cfg.ramdisk_size, "rpi3 64 vec1 len");
    END_TEST;
} */
/* 
static bool ml_eor(void* context) {
    BEGIN_TEST;

    uintptr_t base = 0x0;
    size_t size = 128 * MB;
    size_t k_size = 2 * MB;
    size_t mem_limit = 8 * MB;
    size_t used;
    iovec_t vecs[2];


    mem_limit_cfg_t cfg = {
        .kernel_base = (base + size) - k_size,
        .kernel_end = (base + size),
        .ramdisk_base = 0,
        .ramdisk_size = 0,
        .memory_limit = mem_limit,
    };

    status_t status = mem_limit_apply(&cfg, base, size, vecs, 2, &used);
    EXPECT_EQ(NO_ERROR, status, "apply");
    EXPECT_EQ(vecs[0].iov_len, mem_limit, "check size");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vecs[1].iov_base), 0u, "No second vector");
    EXPECT_EQ(vecs[1].iov_len, 0u, "No second vector");

    END_TEST;
} */

#define ML_UNITTEST(fname) UNITTEST(#fname, fname)
UNITTEST_START_TESTCASE(memlimit_tests)
ML_UNITTEST(ml_test_nuc)
UNITTEST_END_TESTCASE(memlimit_tests, "memlim_tests", "Memory limit tests", nullptr, nullptr);
#undef ML_UNITTEST
