// =============================================================================
// UMI-OS Renode Filesystem Test — Reference C Implementation
// =============================================================================
// Same tests as renode_fs_test.cc but using original C littlefs and FatFs.
// Used for size/performance comparison against C++23 port.
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <umios/backend/cm/common/vector_table.hh>

extern "C" {
#include "lfs.h"
#include "ff.h"
#include "diskio.h"
}

#include <cstdint>
#include <cstring>

// =============================================================================
// Linker symbol aliases (picolibc expects __heap_start/__heap_end)
// =============================================================================

extern "C" {
extern char _heap_start;
extern char _heap_end;
__attribute__((used)) char* __heap_start = &_heap_start;
__attribute__((used)) char* __heap_end = &_heap_end;
}

// =============================================================================
// UART output (provided by syscalls.cc)
// =============================================================================

extern "C" int _write(int, const void*, int);

namespace {

void print(const char* s) {
    while (*s) {
        _write(1, s, 1);
        ++s;
    }
}

void println(const char* s) {
    print(s);
    print("\r\n");
}

void print_int(int val) {
    if (val < 0) { print("-"); val = -val; }
    if (val == 0) { print("0"); return; }
    char buf[12];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) { char c[2] = {buf[--i], 0}; print(c); }
}

void print_uint32(uint32_t val) {
    if (val == 0) { print("0"); return; }
    char buf[12];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) { char c[2] = {buf[--i], 0}; print(c); }
}

} // namespace

// =============================================================================
// Test Framework (minimal)
// =============================================================================

namespace test {

int tests_run = 0;
int tests_passed = 0;

void run(const char* name, bool result) {
    ++tests_run;
    if (result) {
        ++tests_passed;
        print("[PASS] ");
    } else {
        print("[FAIL] ");
    }
    println(name);
}

void summary() {
    println("");
    println("========================================");
    print("Tests: ");
    print_int(tests_passed);
    print("/");
    print_int(tests_run);
    println(" passed");
    if (tests_passed == tests_run) {
        println("*** ALL TESTS PASSED ***");
    } else {
        println("SOME TESTS FAILED");
    }
    println("========================================");
    println("TEST_COMPLETE");
}

} // namespace test

// =============================================================================
// DWT Cycle Counter
// =============================================================================

static volatile uint32_t* const DWT_CTRL = reinterpret_cast<volatile uint32_t*>(0xE0001000);
static volatile uint32_t* const DWT_CYCCNT = reinterpret_cast<volatile uint32_t*>(0xE0001004);
static volatile uint32_t* const CoreDebug_DEMCR = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);

static void dwt_init() {
    *CoreDebug_DEMCR |= (1u << 24);  // TRCENA
    *DWT_CYCCNT = 0;
    *DWT_CTRL |= 1u;                  // CYCCNTENA
}

static uint32_t dwt_get() {
    return *DWT_CYCCNT;
}

static void report_cycles(const char* op, uint32_t cycles) {
    print("  [cycles] ");
    print(op);
    print(": ");
    print_uint32(cycles);
    println("");
}

// =============================================================================
// RAM Block Device for littlefs (reference C)
// =============================================================================

static constexpr uint32_t LFS_BLOCK_SIZE = 256;
static constexpr uint32_t LFS_BLOCK_COUNT = 64;
static uint8_t lfs_storage[LFS_BLOCK_SIZE * LFS_BLOCK_COUNT];

static int ram_read(const struct lfs_config* c, lfs_block_t block,
                    lfs_off_t off, void* buffer, lfs_size_t size) {
    (void)c;
    if (block >= LFS_BLOCK_COUNT || off + size > LFS_BLOCK_SIZE) return LFS_ERR_IO;
    std::memcpy(buffer, &lfs_storage[block * LFS_BLOCK_SIZE + off], size);
    return 0;
}

static int ram_prog(const struct lfs_config* c, lfs_block_t block,
                    lfs_off_t off, const void* buffer, lfs_size_t size) {
    (void)c;
    if (block >= LFS_BLOCK_COUNT || off + size > LFS_BLOCK_SIZE) return LFS_ERR_IO;
    std::memcpy(&lfs_storage[block * LFS_BLOCK_SIZE + off], buffer, size);
    return 0;
}

static int ram_erase(const struct lfs_config* c, lfs_block_t block) {
    (void)c;
    if (block >= LFS_BLOCK_COUNT) return LFS_ERR_IO;
    std::memset(&lfs_storage[block * LFS_BLOCK_SIZE], 0xFF, LFS_BLOCK_SIZE);
    return 0;
}

static int ram_sync(const struct lfs_config* c) {
    (void)c;
    return 0;
}

static uint8_t lfs_read_buf[LFS_BLOCK_SIZE];
static uint8_t lfs_prog_buf[LFS_BLOCK_SIZE];
static uint8_t lfs_lookahead_buf[16];
static uint8_t lfs_file_buf[LFS_BLOCK_SIZE];

// =============================================================================
// littlefs Tests (reference C)
// =============================================================================

static void test_lfs() {
    println("--- littlefs Tests (ref C) ---");

    std::memset(lfs_storage, 0xFF, sizeof(lfs_storage));

    struct lfs_config cfg{};
    cfg.read = ram_read;
    cfg.prog = ram_prog;
    cfg.erase = ram_erase;
    cfg.sync = ram_sync;
    cfg.read_size = LFS_BLOCK_SIZE;
    cfg.prog_size = LFS_BLOCK_SIZE;
    cfg.block_size = LFS_BLOCK_SIZE;
    cfg.block_count = LFS_BLOCK_COUNT;
    cfg.cache_size = LFS_BLOCK_SIZE;
    cfg.lookahead_size = 16;
    cfg.read_buffer = lfs_read_buf;
    cfg.prog_buffer = lfs_prog_buf;
    cfg.lookahead_buffer = lfs_lookahead_buf;

    lfs_t lfs{};

    // Format and mount
    int err = lfs_format(&lfs, &cfg);
    test::run("lfs: format", err == 0);

    err = lfs_mount(&lfs, &cfg);
    test::run("lfs: mount", err == 0);

    // Write file
    lfs_file_t file{};
    struct lfs_file_config fcfg{};
    fcfg.buffer = lfs_file_buf;
    err = lfs_file_opencfg(&lfs, &file, "hello.txt",
        LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fcfg);
    test::run("lfs: file open write", err == 0);

    const char* msg = "Hello from Cortex-M4!";
    auto len = static_cast<lfs_size_t>(std::strlen(msg));
    lfs_ssize_t written = lfs_file_write(&lfs, &file, msg, len);
    test::run("lfs: file write", written == static_cast<lfs_ssize_t>(len));
    lfs_file_close(&lfs, &file);

    // Read back
    lfs_file_t file2{};
    struct lfs_file_config fcfg2{};
    fcfg2.buffer = lfs_file_buf;
    err = lfs_file_opencfg(&lfs, &file2, "hello.txt", LFS_O_RDONLY, &fcfg2);
    test::run("lfs: file open read", err == 0);

    char buf[64]{};
    lfs_ssize_t nread = lfs_file_read(&lfs, &file2, buf, sizeof(buf));
    test::run("lfs: file read size", nread == static_cast<lfs_ssize_t>(len));
    test::run("lfs: file content", std::memcmp(buf, msg, len) == 0);
    lfs_file_close(&lfs, &file2);

    // Mkdir and stat
    err = lfs_mkdir(&lfs, "testdir");
    test::run("lfs: mkdir", err == 0);

    struct lfs_info info{};
    err = lfs_stat(&lfs, "testdir", &info);
    test::run("lfs: stat dir", err == 0 && info.type == LFS_TYPE_DIR);

    // Rename
    err = lfs_rename(&lfs, "hello.txt", "renamed.txt");
    test::run("lfs: rename", err == 0);
    test::run("lfs: old gone", lfs_stat(&lfs, "hello.txt", &info) != 0);
    test::run("lfs: new exists", lfs_stat(&lfs, "renamed.txt", &info) == 0);

    // Remove
    err = lfs_remove(&lfs, "renamed.txt");
    test::run("lfs: remove file", err == 0);

    // Persistence: unmount and remount
    lfs_unmount(&lfs);
    lfs = lfs_t{};
    err = lfs_mount(&lfs, &cfg);
    test::run("lfs: remount", err == 0);

    err = lfs_stat(&lfs, "testdir", &info);
    test::run("lfs: dir persists", err == 0);

    lfs_unmount(&lfs);
}

// =============================================================================
// FATfs RAM Block Device (reference C)
// =============================================================================

static constexpr uint32_t FAT_BLOCK_SIZE = 512;
static constexpr uint32_t FAT_BLOCK_COUNT = 128;
static uint8_t fat_storage[FAT_BLOCK_SIZE * FAT_BLOCK_COUNT];

// FatFs reference C needs global disk_* functions
extern "C" {

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    for (UINT i = 0; i < count; i++) {
        if (sector + i >= FAT_BLOCK_COUNT) return RES_PARERR;
        std::memcpy(buff + i * FAT_BLOCK_SIZE,
                     &fat_storage[(sector + i) * FAT_BLOCK_SIZE],
                     FAT_BLOCK_SIZE);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    for (UINT i = 0; i < count; i++) {
        if (sector + i >= FAT_BLOCK_COUNT) return RES_PARERR;
        std::memcpy(&fat_storage[(sector + i) * FAT_BLOCK_SIZE],
                     buff + i * FAT_BLOCK_SIZE,
                     FAT_BLOCK_SIZE);
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    (void)pdrv;
    (void)buff;
    if (cmd == CTRL_SYNC) return RES_OK;
    return RES_PARERR;
}

DWORD get_fattime(void) {
    return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

} // extern "C"

static void format_fat12() {
    std::memset(fat_storage, 0, sizeof(fat_storage));
    uint8_t* bs = fat_storage;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    bs[11] = 0x00; bs[12] = 0x02;
    bs[13] = 1;
    bs[14] = 1; bs[15] = 0;
    bs[16] = 2;
    bs[17] = 0x40; bs[18] = 0x00;
    bs[19] = 128; bs[20] = 0;
    bs[21] = 0xF8;
    bs[22] = 1; bs[23] = 0;
    bs[24] = 0x3F; bs[25] = 0;
    bs[26] = 0xFF; bs[27] = 0;
    bs[38] = 0x29;
    bs[39] = 0x12; bs[40] = 0x34; bs[41] = 0x56; bs[42] = 0x78;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT12   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    uint8_t* fat1 = &fat_storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF;
    uint8_t* fat2 = &fat_storage[512 * 2];
    fat2[0] = 0xF8; fat2[1] = 0xFF; fat2[2] = 0xFF;
}

// =============================================================================
// FATfs Tests (reference C)
// =============================================================================

static void test_fat() {
    println("--- FATfs Tests (ref C) ---");

    format_fat12();

    FATFS fs{};
    FRESULT res = f_mount(&fs, "", 1);
    test::run("fat: mount", res == FR_OK);

    // Write file
    FIL fp{};
    UINT bw = 0;
    res = f_open(&fp, "TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    test::run("fat: file create", res == FR_OK);

    const char* msg = "Hello from Cortex-M4!";
    res = f_write(&fp, msg, std::strlen(msg), &bw);
    test::run("fat: file write", res == FR_OK && bw == std::strlen(msg));
    f_close(&fp);

    // Read back
    FIL fp2{};
    res = f_open(&fp2, "TEST.TXT", FA_READ);
    test::run("fat: file open read", res == FR_OK);

    char buf[64]{};
    UINT br = 0;
    res = f_read(&fp2, buf, sizeof(buf), &br);
    test::run("fat: file read", res == FR_OK && br == std::strlen(msg));
    test::run("fat: file content", std::memcmp(buf, msg, std::strlen(msg)) == 0);
    f_close(&fp2);

    // Mkdir and stat
    res = f_mkdir("MYDIR");
    test::run("fat: mkdir", res == FR_OK);

    FILINFO fno{};
    res = f_stat("MYDIR", &fno);
    test::run("fat: stat dir", res == FR_OK && (fno.fattrib & AM_DIR) != 0);

    // Rename
    res = f_rename("TEST.TXT", "NEW.TXT");
    test::run("fat: rename", res == FR_OK);
    test::run("fat: old gone", f_stat("TEST.TXT", &fno) != FR_OK);
    test::run("fat: new exists", f_stat("NEW.TXT", &fno) == FR_OK);

    // Unlink
    res = f_unlink("NEW.TXT");
    test::run("fat: unlink", res == FR_OK);

    // Getfree
    DWORD nclst = 0;
    FATFS* fs_ptr = nullptr;
    res = f_getfree("", &nclst, &fs_ptr);
    test::run("fat: getfree", res == FR_OK && nclst > 0);

    f_mount(nullptr, "", 0);
}

// =============================================================================
// Benchmarks
// =============================================================================

static void bench_lfs() {
    println("");
    println("--- littlefs Benchmarks (ref C) ---");

    struct lfs_config cfg{};
    cfg.read = ram_read;
    cfg.prog = ram_prog;
    cfg.erase = ram_erase;
    cfg.sync = ram_sync;
    cfg.read_size = LFS_BLOCK_SIZE;
    cfg.prog_size = LFS_BLOCK_SIZE;
    cfg.block_size = LFS_BLOCK_SIZE;
    cfg.block_count = LFS_BLOCK_COUNT;
    cfg.cache_size = LFS_BLOCK_SIZE;
    cfg.lookahead_size = 16;
    cfg.read_buffer = lfs_read_buf;
    cfg.prog_buffer = lfs_prog_buf;
    cfg.lookahead_buffer = lfs_lookahead_buf;

    lfs_t lfs{};
    uint32_t t0, t1;

    // Format + mount
    std::memset(lfs_storage, 0xFF, sizeof(lfs_storage));
    t0 = dwt_get();
    lfs_format(&lfs, &cfg);
    lfs_mount(&lfs, &cfg);
    t1 = dwt_get();
    report_cycles("format+mount", t1 - t0);

    // Write 1KB
    {
        lfs_file_t f{};
        struct lfs_file_config fc{};
        fc.buffer = lfs_file_buf;
        char data[256];
        std::memset(data, 'A', sizeof(data));

        lfs_file_opencfg(&lfs, &f, "bench.dat",
            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fc);

        t0 = dwt_get();
        for (int i = 0; i < 4; i++) {
            lfs_file_write(&lfs, &f, data, sizeof(data));
        }
        lfs_file_close(&lfs, &f);
        t1 = dwt_get();
        report_cycles("write 1KB", t1 - t0);
    }

    // Read 1KB
    {
        lfs_file_t f{};
        struct lfs_file_config fc{};
        fc.buffer = lfs_file_buf;
        char data[256];

        lfs_file_opencfg(&lfs, &f, "bench.dat", LFS_O_RDONLY, &fc);

        t0 = dwt_get();
        for (int i = 0; i < 4; i++) {
            lfs_file_read(&lfs, &f, data, sizeof(data));
        }
        lfs_file_close(&lfs, &f);
        t1 = dwt_get();
        report_cycles("read 1KB", t1 - t0);
    }

    // mkdir + stat x5
    {
        t0 = dwt_get();
        lfs_mkdir(&lfs, "d1");
        lfs_mkdir(&lfs, "d2");
        lfs_mkdir(&lfs, "d3");
        lfs_mkdir(&lfs, "d4");
        lfs_mkdir(&lfs, "d5");
        struct lfs_info info;
        lfs_stat(&lfs, "d1", &info);
        lfs_stat(&lfs, "d2", &info);
        lfs_stat(&lfs, "d3", &info);
        lfs_stat(&lfs, "d4", &info);
        lfs_stat(&lfs, "d5", &info);
        t1 = dwt_get();
        report_cycles("mkdir+stat x5", t1 - t0);
    }

    lfs_unmount(&lfs);
}

static void bench_fat() {
    println("");
    println("--- FATfs Benchmarks (ref C) ---");

    format_fat12();
    FATFS fs{};
    f_mount(&fs, "", 1);

    uint32_t t0, t1;

    // Write 4KB
    {
        FIL fp{};
        f_open(&fp, "BENCH.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        char data[512];
        std::memset(data, 'A', sizeof(data));

        t0 = dwt_get();
        for (int i = 0; i < 8; i++) {
            UINT bw;
            f_write(&fp, data, sizeof(data), &bw);
        }
        f_close(&fp);
        t1 = dwt_get();
        report_cycles("write 4KB", t1 - t0);
    }

    // Read 4KB
    {
        FIL fp{};
        f_open(&fp, "BENCH.DAT", FA_READ);
        char data[512];

        t0 = dwt_get();
        for (int i = 0; i < 8; i++) {
            UINT br;
            f_read(&fp, data, sizeof(data), &br);
        }
        f_close(&fp);
        t1 = dwt_get();
        report_cycles("read 4KB", t1 - t0);
    }

    // mkdir + stat
    {
        t0 = dwt_get();
        f_mkdir("D1");
        f_mkdir("D2");
        f_mkdir("D3");
        FILINFO fno;
        f_stat("D1", &fno);
        f_stat("D2", &fno);
        f_stat("D3", &fno);
        t1 = dwt_get();
        report_cycles("mkdir+stat x3", t1 - t0);
    }

    f_mount(nullptr, "", 0);
}

// =============================================================================
// Main
// =============================================================================

extern "C" [[noreturn]] void _start() {
    dwt_init();

    println("");
    println("================================");
    println("  UMI-OS FS Renode Test (ref C)");
    println("================================");

    test_lfs();
    test_fat();

    bench_lfs();
    bench_fat();

    test::summary();

    while (true) { __asm volatile("wfi"); }
}
