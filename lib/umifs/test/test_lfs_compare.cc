// SPDX-License-Identifier: MIT
// Comparison tests: C++23 port (umi::fs::lfs) vs reference littlefs C implementation
// Verifies functional equivalence and measures performance delta

#include "test_common.hh"

// --- C++23 port ---
#include <umifs/little/lfs.hh>
#include <umifs/little/lfs_config.hh>

// --- Reference C implementation ---
extern "C" {
#include "lfs.h"
#include "bd/lfs_rambd.h"
}

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Shared storage — both implementations operate on identical RAM images
// ============================================================================

static constexpr uint32_t BLOCK_SIZE = 256;
static constexpr uint32_t BLOCK_COUNT = 512; // 128KB
static constexpr uint32_t TOTAL_SIZE = BLOCK_SIZE * BLOCK_COUNT;

// Separate storage for each implementation so they don't interfere
static uint8_t storage_port[TOTAL_SIZE];
static uint8_t storage_ref[TOTAL_SIZE];

// ============================================================================
// C++23 port helpers (BlockDeviceLike)
// ============================================================================

struct PortRamDev {
    uint8_t* storage;

    int read(uint32_t block, uint32_t offset, void* buf, uint32_t size) {
        std::memcpy(buf, &storage[block * BLOCK_SIZE + offset], size);
        return 0;
    }
    int write(uint32_t block, uint32_t offset, const void* buf, uint32_t size) {
        std::memcpy(&storage[block * BLOCK_SIZE + offset], buf, size);
        return 0;
    }
    int erase(uint32_t block) {
        std::memset(&storage[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
        return 0;
    }
    uint32_t block_size() const { return BLOCK_SIZE; }
    uint32_t block_count() const { return BLOCK_COUNT; }
};

static PortRamDev port_dev{storage_port};
static uint8_t port_read_buf[BLOCK_SIZE];
static uint8_t port_prog_buf[BLOCK_SIZE];
static uint8_t port_la_buf[16];
static uint8_t port_file_buf[BLOCK_SIZE];

// ============================================================================
// Reference C implementation helpers
// ============================================================================

static uint8_t ref_read_buf[BLOCK_SIZE];
static uint8_t ref_prog_buf[BLOCK_SIZE];
static uint8_t ref_la_buf[16];
static uint8_t ref_file_buf[BLOCK_SIZE];

static int ref_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buf, lfs_size_t size) {
    auto* s = static_cast<uint8_t*>(c->context);
    std::memcpy(buf, &s[block * BLOCK_SIZE + off], size);
    return 0;
}

static int ref_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buf, lfs_size_t size) {
    auto* s = static_cast<uint8_t*>(c->context);
    std::memcpy(&s[block * BLOCK_SIZE + off], buf, size);
    return 0;
}

static int ref_erase(const struct lfs_config* c, lfs_block_t block) {
    auto* s = static_cast<uint8_t*>(c->context);
    std::memset(&s[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
    return 0;
}

static int ref_sync(const struct lfs_config* /*c*/) { return 0; }

static lfs_config make_ref_config() {
    lfs_config cfg{};
    cfg.context = storage_ref;
    cfg.read = ref_read;
    cfg.prog = ref_prog;
    cfg.erase = ref_erase;
    cfg.sync = ref_sync;
    cfg.read_size = 16;
    cfg.prog_size = 16;
    cfg.block_size = BLOCK_SIZE;
    cfg.block_count = BLOCK_COUNT;
    cfg.cache_size = BLOCK_SIZE;
    cfg.lookahead_size = 16;
    cfg.block_cycles = 500;
    cfg.read_buffer = ref_read_buf;
    cfg.prog_buffer = ref_prog_buf;
    cfg.lookahead_buffer = ref_la_buf;
    return cfg;
}

// ============================================================================
// Timer utility
// ============================================================================

struct Timer {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start;

    Timer() : start(clock::now()) {}

    double elapsed_us() const {
        auto end = clock::now();
        return std::chrono::duration<double, std::micro>(end - start).count();
    }
};

static void report_perf(const char* op, double port_us, double ref_us) {
    double ratio = (ref_us > 0.0) ? port_us / ref_us : 0.0;
    std::printf("  [perf] %-30s  port: %8.1f us  ref: %8.1f us  ratio: %.2fx\n", op, port_us, ref_us, ratio);
}

// ============================================================================
// Test: format + mount — functional equivalence
// ============================================================================

static void test_format_mount_compare() {
    SECTION("Compare: Format/Mount");

    // Port
    std::memset(storage_port, 0xFF, TOTAL_SIZE);
    umi::fs::Lfs plfs{};
    auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
    int perr = plfs.format(&pcfg);
    CHECK(perr == 0, "port format");
    perr = plfs.mount(&pcfg);
    CHECK(perr == 0, "port mount");

    // Reference
    std::memset(storage_ref, 0xFF, TOTAL_SIZE);
    lfs_t rlfs{};
    auto rcfg = make_ref_config();
    int rerr = lfs_format(&rlfs, &rcfg);
    CHECK(rerr == 0, "ref format");
    rerr = lfs_mount(&rlfs, &rcfg);
    CHECK(rerr == 0, "ref mount");

    // Both should report similar fs_size (blocks used by metadata)
    auto psz = plfs.fs_size();
    auto rsz = lfs_fs_size(&rlfs);
    CHECK(psz == rsz, "fs_size matches after format");

    plfs.unmount();
    lfs_unmount(&rlfs);
}

// ============================================================================
// Test: file write/read — data equivalence
// ============================================================================

static void test_file_write_read_compare() {
    SECTION("Compare: File Write/Read");

    // === Port ===
    std::memset(storage_port, 0xFF, TOTAL_SIZE);
    umi::fs::Lfs plfs{};
    auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
    plfs.format(&pcfg);
    plfs.mount(&pcfg);

    umi::fs::LfsFile pfile{};
    umi::fs::LfsFileConfig pfcfg{};
    pfcfg.buffer = port_file_buf;
    plfs.file_opencfg(&pfile, "test.bin",
        static_cast<int>(umi::fs::LfsOpenFlags::RDWR | umi::fs::LfsOpenFlags::CREAT), &pfcfg);

    const char* data = "ABCDEFGHIJKLMNOP";
    plfs.file_write(&pfile, data, 16);
    plfs.file_close(&pfile);

    // Read back
    plfs.file_opencfg(&pfile, "test.bin", static_cast<int>(umi::fs::LfsOpenFlags::RDONLY), &pfcfg);
    char pbuf[16]{};
    plfs.file_read(&pfile, pbuf, 16);
    plfs.file_close(&pfile);

    // === Reference ===
    std::memset(storage_ref, 0xFF, TOTAL_SIZE);
    lfs_t rlfs{};
    auto rcfg = make_ref_config();
    lfs_format(&rlfs, &rcfg);
    lfs_mount(&rlfs, &rcfg);

    lfs_file_t rfile{};
    lfs_file_config rfcfg{};
    rfcfg.buffer = ref_file_buf;
    lfs_file_opencfg(&rlfs, &rfile, "test.bin", LFS_O_RDWR | LFS_O_CREAT, &rfcfg);
    lfs_file_write(&rlfs, &rfile, data, 16);
    lfs_file_close(&rlfs, &rfile);

    lfs_file_opencfg(&rlfs, &rfile, "test.bin", LFS_O_RDONLY, &rfcfg);
    char rbuf[16]{};
    lfs_file_read(&rlfs, &rfile, rbuf, 16);
    lfs_file_close(&rlfs, &rfile);

    // Compare
    CHECK(std::memcmp(pbuf, rbuf, 16) == 0, "read data matches between port and ref");
    CHECK(std::memcmp(pbuf, data, 16) == 0, "data is correct");

    plfs.unmount();
    lfs_unmount(&rlfs);
}

// ============================================================================
// Test: on-disk format compatibility
// Write with port, read with reference (and vice versa)
// ============================================================================

static void test_cross_read_port_to_ref() {
    SECTION("Compare: Cross-read (port write → ref read)");

    // Write with port
    std::memset(storage_port, 0xFF, TOTAL_SIZE);
    umi::fs::Lfs plfs{};
    auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
    plfs.format(&pcfg);
    plfs.mount(&pcfg);

    umi::fs::LfsFile pfile{};
    umi::fs::LfsFileConfig pfcfg{};
    pfcfg.buffer = port_file_buf;
    plfs.file_opencfg(&pfile, "cross.bin",
        static_cast<int>(umi::fs::LfsOpenFlags::WRONLY | umi::fs::LfsOpenFlags::CREAT), &pfcfg);
    const char* msg = "cross-read-test";
    plfs.file_write(&pfile, msg, std::strlen(msg));
    plfs.file_close(&pfile);
    plfs.unmount();

    // Copy storage to ref
    std::memcpy(storage_ref, storage_port, TOTAL_SIZE);

    // Read with reference C implementation
    lfs_t rlfs{};
    auto rcfg = make_ref_config();
    int err = lfs_mount(&rlfs, &rcfg);
    CHECK(err == 0, "ref mounts port-formatted image");

    lfs_file_t rfile{};
    lfs_file_config rfcfg{};
    rfcfg.buffer = ref_file_buf;
    err = lfs_file_opencfg(&rlfs, &rfile, "cross.bin", LFS_O_RDONLY, &rfcfg);
    CHECK(err == 0, "ref opens port-created file");

    char rbuf[32]{};
    lfs_ssize_t n = lfs_file_read(&rlfs, &rfile, rbuf, sizeof(rbuf));
    CHECK(n == static_cast<lfs_ssize_t>(std::strlen(msg)), "ref reads correct size");
    CHECK(std::memcmp(rbuf, msg, std::strlen(msg)) == 0, "ref reads correct data");

    lfs_file_close(&rlfs, &rfile);
    lfs_unmount(&rlfs);
}

static void test_cross_read_ref_to_port() {
    SECTION("Compare: Cross-read (ref write → port read)");

    // Write with reference
    std::memset(storage_ref, 0xFF, TOTAL_SIZE);
    lfs_t rlfs{};
    auto rcfg = make_ref_config();
    lfs_format(&rlfs, &rcfg);
    lfs_mount(&rlfs, &rcfg);

    lfs_file_t rfile{};
    lfs_file_config rfcfg{};
    rfcfg.buffer = ref_file_buf;
    lfs_file_opencfg(&rlfs, &rfile, "cross.bin", LFS_O_WRONLY | LFS_O_CREAT, &rfcfg);
    const char* msg = "ref-to-port-test";
    lfs_file_write(&rlfs, &rfile, msg, std::strlen(msg));
    lfs_file_close(&rlfs, &rfile);
    lfs_unmount(&rlfs);

    // Copy storage to port
    std::memcpy(storage_port, storage_ref, TOTAL_SIZE);

    // Read with port
    umi::fs::Lfs plfs{};
    auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
    int err = plfs.mount(&pcfg);
    CHECK(err == 0, "port mounts ref-formatted image");

    umi::fs::LfsFile pfile{};
    umi::fs::LfsFileConfig pfcfg{};
    pfcfg.buffer = port_file_buf;
    err = plfs.file_opencfg(&pfile, "cross.bin", static_cast<int>(umi::fs::LfsOpenFlags::RDONLY), &pfcfg);
    CHECK(err == 0, "port opens ref-created file");

    char pbuf[32]{};
    auto n = plfs.file_read(&pfile, pbuf, sizeof(pbuf));
    CHECK(n == static_cast<umi::fs::lfs_ssize_t>(std::strlen(msg)), "port reads correct size");
    CHECK(std::memcmp(pbuf, msg, std::strlen(msg)) == 0, "port reads correct data");

    plfs.file_close(&pfile);
    plfs.unmount();
}

// ============================================================================
// Test: directory operations — structural equivalence
// ============================================================================

static void test_dir_compare() {
    SECTION("Compare: Directory operations");

    // === Port ===
    std::memset(storage_port, 0xFF, TOTAL_SIZE);
    umi::fs::Lfs plfs{};
    auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
    plfs.format(&pcfg);
    plfs.mount(&pcfg);
    plfs.mkdir("dir1");
    plfs.mkdir("dir2");
    {
        umi::fs::LfsFile f{};
        umi::fs::LfsFileConfig fc{};
        fc.buffer = port_file_buf;
        plfs.file_opencfg(&f, "file.txt",
            static_cast<int>(umi::fs::LfsOpenFlags::WRONLY | umi::fs::LfsOpenFlags::CREAT), &fc);
        plfs.file_close(&f);
    }

    constexpr int EXPECT_ENTRIES = 5; // ".", "..", "dir1", "dir2", "file.txt"
    char pnames[EXPECT_ENTRIES][64]{};
    uint8_t ptypes[EXPECT_ENTRIES]{};
    int pcount = 0;
    {
        umi::fs::LfsDir d{};
        plfs.dir_open(&d, "/");
        umi::fs::LfsInfo info{};
        while (plfs.dir_read(&d, &info) > 0 && pcount < EXPECT_ENTRIES) {
            std::strncpy(pnames[pcount], info.name, 63);
            ptypes[pcount] = info.type;
            pcount++;
        }
        plfs.dir_close(&d);
    }

    // === Reference ===
    std::memset(storage_ref, 0xFF, TOTAL_SIZE);
    lfs_t rlfs{};
    auto rcfg = make_ref_config();
    lfs_format(&rlfs, &rcfg);
    lfs_mount(&rlfs, &rcfg);
    lfs_mkdir(&rlfs, "dir1");
    lfs_mkdir(&rlfs, "dir2");
    {
        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = ref_file_buf;
        lfs_file_opencfg(&rlfs, &f, "file.txt", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        lfs_file_close(&rlfs, &f);
    }

    char rnames[EXPECT_ENTRIES][64]{};
    uint8_t rtypes[EXPECT_ENTRIES]{};
    int rcount = 0;
    {
        lfs_dir_t d{};
        lfs_dir_open(&rlfs, &d, "/");
        lfs_info info{};
        while (lfs_dir_read(&rlfs, &d, &info) > 0 && rcount < EXPECT_ENTRIES) {
            std::strncpy(rnames[rcount], info.name, 63);
            rtypes[rcount] = info.type;
            rcount++;
        }
        lfs_dir_close(&rlfs, &d);
    }

    CHECK(pcount == rcount, "directory entry count matches");
    CHECK(pcount == EXPECT_ENTRIES, "both have 5 entries");
    for (int i = 0; i < pcount && i < rcount; i++) {
        CHECK(std::strcmp(pnames[i], rnames[i]) == 0, "entry name matches");
        CHECK(ptypes[i] == rtypes[i], "entry type matches");
    }

    plfs.unmount();
    lfs_unmount(&rlfs);
}

// ============================================================================
// Performance: large file write benchmark
// ============================================================================

static void test_perf_large_write() {
    SECTION("Perf: Large file write");

    constexpr int ITERATIONS = 30;
    constexpr int WARMUP = 2;
    constexpr int CHUNK_SIZE = 128;
    constexpr int CHUNKS = 8; // 1KB total
    uint8_t pattern[CHUNK_SIZE];
    std::srand(42);
    for (int i = 0; i < CHUNK_SIZE; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // === Port ===
    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        std::memset(storage_port, 0xFF, TOTAL_SIZE);
        umi::fs::Lfs plfs{};
        auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
        plfs.format(&pcfg);
        plfs.mount(&pcfg);

        umi::fs::LfsFile f{};
        umi::fs::LfsFileConfig fc{};
        fc.buffer = port_file_buf;

        Timer t;
        plfs.file_opencfg(&f, "bench.bin",
            static_cast<int>(umi::fs::LfsOpenFlags::WRONLY | umi::fs::LfsOpenFlags::CREAT), &fc);
        for (int c = 0; c < CHUNKS; c++) plfs.file_write(&f, pattern, CHUNK_SIZE);
        plfs.file_close(&f);
        if (iter >= WARMUP) port_total += t.elapsed_us();

        plfs.unmount();
    }

    // === Reference ===
    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        std::memset(storage_ref, 0xFF, TOTAL_SIZE);
        lfs_t rlfs{};
        auto rcfg = make_ref_config();
        lfs_format(&rlfs, &rcfg);
        lfs_mount(&rlfs, &rcfg);

        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = ref_file_buf;

        Timer t;
        lfs_file_opencfg(&rlfs, &f, "bench.bin", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        for (int c = 0; c < CHUNKS; c++) lfs_file_write(&rlfs, &f, pattern, CHUNK_SIZE);
        lfs_file_close(&rlfs, &f);
        if (iter >= WARMUP) ref_total += t.elapsed_us();

        lfs_unmount(&rlfs);
    }

    report_perf("write 1KB", port_total / ITERATIONS, ref_total / ITERATIONS);

    double ratio = port_total / ref_total;
    CHECK(ratio < 2.0, "port write not more than 2x slower than ref");
}

// ============================================================================
// Performance: large file read benchmark
// ============================================================================

static void test_perf_large_read() {
    SECTION("Perf: Large file read");

    constexpr int ITERATIONS = 30;
    constexpr int WARMUP = 2;
    constexpr int CHUNK_SIZE = 128;
    constexpr int CHUNKS = 8;
    uint8_t pattern[CHUNK_SIZE];
    std::srand(123);
    for (int i = 0; i < CHUNK_SIZE; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // Prepare port
    std::memset(storage_port, 0xFF, TOTAL_SIZE);
    umi::fs::Lfs plfs{};
    auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
    plfs.format(&pcfg);
    plfs.mount(&pcfg);
    {
        umi::fs::LfsFile f{};
        umi::fs::LfsFileConfig fc{};
        fc.buffer = port_file_buf;
        plfs.file_opencfg(&f, "bench.bin",
            static_cast<int>(umi::fs::LfsOpenFlags::WRONLY | umi::fs::LfsOpenFlags::CREAT), &fc);
        for (int c = 0; c < CHUNKS; c++) plfs.file_write(&f, pattern, CHUNK_SIZE);
        plfs.file_close(&f);
    }

    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        umi::fs::LfsFile f{};
        umi::fs::LfsFileConfig fc{};
        fc.buffer = port_file_buf;
        plfs.file_opencfg(&f, "bench.bin", static_cast<int>(umi::fs::LfsOpenFlags::RDONLY), &fc);
        Timer t;
        uint8_t buf[CHUNK_SIZE];
        for (int c = 0; c < CHUNKS; c++) plfs.file_read(&f, buf, CHUNK_SIZE);
        if (iter >= WARMUP) port_total += t.elapsed_us();
        plfs.file_close(&f);
    }
    plfs.unmount();

    // Prepare ref
    std::memset(storage_ref, 0xFF, TOTAL_SIZE);
    lfs_t rlfs{};
    auto rcfg = make_ref_config();
    lfs_format(&rlfs, &rcfg);
    lfs_mount(&rlfs, &rcfg);
    {
        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = ref_file_buf;
        lfs_file_opencfg(&rlfs, &f, "bench.bin", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        for (int c = 0; c < CHUNKS; c++) lfs_file_write(&rlfs, &f, pattern, CHUNK_SIZE);
        lfs_file_close(&rlfs, &f);
    }

    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = ref_file_buf;
        lfs_file_opencfg(&rlfs, &f, "bench.bin", LFS_O_RDONLY, &fc);
        Timer t;
        uint8_t buf[CHUNK_SIZE];
        for (int c = 0; c < CHUNKS; c++) lfs_file_read(&rlfs, &f, buf, CHUNK_SIZE);
        if (iter >= WARMUP) ref_total += t.elapsed_us();
        lfs_file_close(&rlfs, &f);
    }
    lfs_unmount(&rlfs);

    report_perf("read 1KB", port_total / ITERATIONS, ref_total / ITERATIONS);

    double ratio = port_total / ref_total;
    CHECK(ratio < 2.0, "port read not more than 2x slower than ref");
}

// ============================================================================
// Performance: format + mount benchmark
// ============================================================================

static void test_perf_format_mount() {
    SECTION("Perf: Format/Mount");

    constexpr int ITERATIONS = 100;

    // Port
    double port_total = 0;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        std::memset(storage_port, 0xFF, TOTAL_SIZE);
        umi::fs::Lfs plfs{};
        auto pcfg =
            umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
        Timer t;
        plfs.format(&pcfg);
        plfs.mount(&pcfg);
        port_total += t.elapsed_us();
        plfs.unmount();
    }

    // Ref
    double ref_total = 0;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        std::memset(storage_ref, 0xFF, TOTAL_SIZE);
        lfs_t rlfs{};
        auto rcfg = make_ref_config();
        Timer t;
        lfs_format(&rlfs, &rcfg);
        lfs_mount(&rlfs, &rcfg);
        ref_total += t.elapsed_us();
        lfs_unmount(&rlfs);
    }

    report_perf("format+mount", port_total / ITERATIONS, ref_total / ITERATIONS);

    double ratio = port_total / ref_total;
    CHECK(ratio < 2.0, "port format+mount not more than 2x slower than ref");
}

// ============================================================================
// Performance: mkdir + stat benchmark
// ============================================================================

static void test_perf_mkdir_stat() {
    SECTION("Perf: Mkdir/Stat");

    constexpr int ITERATIONS = 30;
    constexpr int WARMUP = 2;
    constexpr int N_DIRS = 5;

    // Port
    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        std::memset(storage_port, 0xFF, TOTAL_SIZE);
        umi::fs::Lfs plfs{};
        auto pcfg =
            umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
        plfs.format(&pcfg);
        plfs.mount(&pcfg);

        Timer t;
        for (int i = 0; i < N_DIRS; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "d%d", i);
            plfs.mkdir(name);
        }
        for (int i = 0; i < N_DIRS; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "d%d", i);
            umi::fs::LfsInfo info{};
            plfs.stat(name, &info);
        }
        if (iter >= WARMUP) port_total += t.elapsed_us();
        plfs.unmount();
    }

    // Ref
    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        std::memset(storage_ref, 0xFF, TOTAL_SIZE);
        lfs_t rlfs{};
        auto rcfg = make_ref_config();
        lfs_format(&rlfs, &rcfg);
        lfs_mount(&rlfs, &rcfg);

        Timer t;
        for (int i = 0; i < N_DIRS; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "d%d", i);
            lfs_mkdir(&rlfs, name);
        }
        for (int i = 0; i < N_DIRS; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "d%d", i);
            lfs_info info{};
            lfs_stat(&rlfs, name, &info);
        }
        if (iter >= WARMUP) ref_total += t.elapsed_us();
        lfs_unmount(&rlfs);
    }

    report_perf("mkdir+stat x5", port_total / ITERATIONS, ref_total / ITERATIONS);

    double ratio = port_total / ref_total;
    CHECK(ratio < 2.0, "port mkdir+stat not more than 2x slower than ref");
}

// ============================================================================
// Performance: 32KB write with random data
// ============================================================================

static void test_perf_write_32k() {
    SECTION("Perf: Write 32KB random");

    constexpr int ITERATIONS = 10;
    constexpr int WARMUP = 2;
    constexpr int CHUNK_SIZE = 256;
    constexpr int CHUNKS = 128; // 32KB total
    uint8_t pattern[CHUNK_SIZE];
    std::srand(777);
    for (int i = 0; i < CHUNK_SIZE; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // Port
    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        std::memset(storage_port, 0xFF, TOTAL_SIZE);
        umi::fs::Lfs plfs{};
        auto pcfg =
            umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
        plfs.format(&pcfg);
        plfs.mount(&pcfg);

        umi::fs::LfsFile f{};
        umi::fs::LfsFileConfig fc{};
        fc.buffer = port_file_buf;

        Timer t;
        plfs.file_opencfg(&f, "big.bin",
            static_cast<int>(umi::fs::LfsOpenFlags::WRONLY | umi::fs::LfsOpenFlags::CREAT), &fc);
        for (int c = 0; c < CHUNKS; c++) plfs.file_write(&f, pattern, CHUNK_SIZE);
        plfs.file_close(&f);
        if (iter >= WARMUP) port_total += t.elapsed_us();

        plfs.unmount();
    }

    // Reference
    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        std::memset(storage_ref, 0xFF, TOTAL_SIZE);
        lfs_t rlfs{};
        auto rcfg = make_ref_config();
        lfs_format(&rlfs, &rcfg);
        lfs_mount(&rlfs, &rcfg);

        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = ref_file_buf;

        Timer t;
        lfs_file_opencfg(&rlfs, &f, "big.bin", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        for (int c = 0; c < CHUNKS; c++) lfs_file_write(&rlfs, &f, pattern, CHUNK_SIZE);
        lfs_file_close(&rlfs, &f);
        if (iter >= WARMUP) ref_total += t.elapsed_us();

        lfs_unmount(&rlfs);
    }

    report_perf("write 32KB random", port_total / ITERATIONS, ref_total / ITERATIONS);
    double ratio = port_total / ref_total;
    CHECK(ratio < 2.0, "port write 32KB not more than 2x slower than ref");
}

// ============================================================================
// Performance: 32KB read with random data
// ============================================================================

static void test_perf_read_32k() {
    SECTION("Perf: Read 32KB random");

    constexpr int ITERATIONS = 10;
    constexpr int WARMUP = 2;
    constexpr int CHUNK_SIZE = 256;
    constexpr int CHUNKS = 128; // 32KB total
    uint8_t pattern[CHUNK_SIZE];
    std::srand(777);
    for (int i = 0; i < CHUNK_SIZE; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // Prepare port
    std::memset(storage_port, 0xFF, TOTAL_SIZE);
    umi::fs::Lfs plfs{};
    auto pcfg = umi::fs::make_lfs_config(port_dev, BLOCK_SIZE, 16, port_read_buf, port_prog_buf, port_la_buf, 16, 16);
    plfs.format(&pcfg);
    plfs.mount(&pcfg);
    {
        umi::fs::LfsFile f{};
        umi::fs::LfsFileConfig fc{};
        fc.buffer = port_file_buf;
        plfs.file_opencfg(&f, "big.bin",
            static_cast<int>(umi::fs::LfsOpenFlags::WRONLY | umi::fs::LfsOpenFlags::CREAT), &fc);
        for (int c = 0; c < CHUNKS; c++) plfs.file_write(&f, pattern, CHUNK_SIZE);
        plfs.file_close(&f);
    }

    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        umi::fs::LfsFile f{};
        umi::fs::LfsFileConfig fc{};
        fc.buffer = port_file_buf;
        plfs.file_opencfg(&f, "big.bin", static_cast<int>(umi::fs::LfsOpenFlags::RDONLY), &fc);
        Timer t;
        uint8_t buf[CHUNK_SIZE];
        for (int c = 0; c < CHUNKS; c++) plfs.file_read(&f, buf, CHUNK_SIZE);
        if (iter >= WARMUP) port_total += t.elapsed_us();
        plfs.file_close(&f);
    }
    plfs.unmount();

    // Prepare ref
    std::memset(storage_ref, 0xFF, TOTAL_SIZE);
    lfs_t rlfs{};
    auto rcfg = make_ref_config();
    lfs_format(&rlfs, &rcfg);
    lfs_mount(&rlfs, &rcfg);
    {
        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = ref_file_buf;
        lfs_file_opencfg(&rlfs, &f, "big.bin", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        for (int c = 0; c < CHUNKS; c++) lfs_file_write(&rlfs, &f, pattern, CHUNK_SIZE);
        lfs_file_close(&rlfs, &f);
    }

    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = ref_file_buf;
        lfs_file_opencfg(&rlfs, &f, "big.bin", LFS_O_RDONLY, &fc);
        Timer t;
        uint8_t buf[CHUNK_SIZE];
        for (int c = 0; c < CHUNKS; c++) lfs_file_read(&rlfs, &f, buf, CHUNK_SIZE);
        if (iter >= WARMUP) ref_total += t.elapsed_us();
        lfs_file_close(&rlfs, &f);
    }
    lfs_unmount(&rlfs);

    report_perf("read 32KB random", port_total / ITERATIONS, ref_total / ITERATIONS);
    double ratio = port_total / ref_total;
    CHECK(ratio < 2.0, "port read 32KB not more than 2x slower than ref");
}

// ============================================================================
// Entry point
// ============================================================================

int main() {
    std::printf("\n=== littlefs: C++23 port vs reference C implementation ===\n\n");

    test_format_mount_compare();
    test_file_write_read_compare();
    test_cross_read_port_to_ref();
    test_cross_read_ref_to_port();
    test_dir_compare();
    test_perf_large_write();
    test_perf_large_read();
    test_perf_write_32k();
    test_perf_read_32k();
    test_perf_format_mount();
    test_perf_mkdir_stat();

    std::printf("\n");
    TEST_SUMMARY();
}
