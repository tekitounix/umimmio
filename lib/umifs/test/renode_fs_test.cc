// =============================================================================
// UMI-OS Renode Filesystem Test
// =============================================================================
// Runs littlefs and FATfs basic tests on Cortex-M4 via Renode emulation.
// Uses minimal UART output (no kernel/syscall dependencies).
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <common/vector_table.hh>

#include <umifs/little/lfs.hh>
#include <umifs/little/lfs_config.hh>
#include <umifs/fat/ff.hh>
#include <umifs/fat/ff_diskio.hh>

#include <cstdint>
#include <cstring>

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
// DWT Cycle Counter
// =============================================================================

static volatile uint32_t* const DWT_CTRL_REG = reinterpret_cast<volatile uint32_t*>(0xE0001000);
static volatile uint32_t* const DWT_CYCCNT_REG = reinterpret_cast<volatile uint32_t*>(0xE0001004);
static volatile uint32_t* const DEMCR_REG = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);

static void dwt_init() {
    *DEMCR_REG |= (1u << 24);
    *DWT_CYCCNT_REG = 0;
    *DWT_CTRL_REG |= 1u;
}

static uint32_t dwt_get() {
    return *DWT_CYCCNT_REG;
}

static void report_cycles(const char* op, uint32_t cycles) {
    print("  [cycles] ");
    print(op);
    print(": ");
    print_uint32(cycles);
    println("");
}

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
// RAM Block Device (shared between littlefs and FATfs tests)
// =============================================================================

static constexpr uint32_t LFS_BLOCK_SIZE = 256;
static constexpr uint32_t LFS_BLOCK_COUNT = 64;
static uint8_t lfs_storage[LFS_BLOCK_SIZE * LFS_BLOCK_COUNT];

struct LfsRamDev {
    int read(uint32_t block, uint32_t off, void* buf, uint32_t size) {
        if (block >= LFS_BLOCK_COUNT || off + size > LFS_BLOCK_SIZE) return -1;
        std::memcpy(buf, &lfs_storage[block * LFS_BLOCK_SIZE + off], size);
        return 0;
    }
    int write(uint32_t block, uint32_t off, const void* buf, uint32_t size) {
        if (block >= LFS_BLOCK_COUNT || off + size > LFS_BLOCK_SIZE) return -1;
        std::memcpy(&lfs_storage[block * LFS_BLOCK_SIZE + off], buf, size);
        return 0;
    }
    int erase(uint32_t block) {
        if (block >= LFS_BLOCK_COUNT) return -1;
        std::memset(&lfs_storage[block * LFS_BLOCK_SIZE], 0xFF, LFS_BLOCK_SIZE);
        return 0;
    }
    uint32_t block_size() const { return LFS_BLOCK_SIZE; }
    uint32_t block_count() const { return LFS_BLOCK_COUNT; }
};

static LfsRamDev lfs_dev;
static uint8_t lfs_read_buf[LFS_BLOCK_SIZE];
static uint8_t lfs_prog_buf[LFS_BLOCK_SIZE];
static uint8_t lfs_lookahead_buf[16];
static uint8_t lfs_file_buf[LFS_BLOCK_SIZE];

// =============================================================================
// littlefs Tests
// =============================================================================

static void test_lfs() {
    using namespace umi::fs;

    println("--- littlefs Tests ---");

    std::memset(lfs_storage, 0xFF, sizeof(lfs_storage));
    auto cfg = make_lfs_config(lfs_dev, LFS_BLOCK_SIZE, 16,
                               lfs_read_buf, lfs_prog_buf, lfs_lookahead_buf);

    Lfs lfs{};

    // Format and mount
    int err = lfs.format(&cfg);
    test::run("lfs: format", err == 0);

    err = lfs.mount(&cfg);
    test::run("lfs: mount", err == 0);

    // Write file
    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = lfs_file_buf;
    err = lfs.file_opencfg(&file, "hello.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT | LfsOpenFlags::TRUNC), &fcfg);
    test::run("lfs: file open write", err == 0);

    const char* msg = "Hello from Cortex-M4!";
    auto len = static_cast<lfs_size_t>(std::strlen(msg));
    lfs_ssize_t written = lfs.file_write(&file, msg, len);
    test::run("lfs: file write", written == static_cast<lfs_ssize_t>(len));
    lfs.file_close(&file);

    // Read back
    LfsFile file2{};
    LfsFileConfig fcfg2{};
    fcfg2.buffer = lfs_file_buf;
    err = lfs.file_opencfg(&file2, "hello.txt", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg2);
    test::run("lfs: file open read", err == 0);

    char buf[64]{};
    lfs_ssize_t nread = lfs.file_read(&file2, buf, sizeof(buf));
    test::run("lfs: file read size", nread == static_cast<lfs_ssize_t>(len));
    test::run("lfs: file content", std::memcmp(buf, msg, len) == 0);
    lfs.file_close(&file2);

    // Mkdir and stat
    err = lfs.mkdir("testdir");
    test::run("lfs: mkdir", err == 0);

    LfsInfo info{};
    err = lfs.stat("testdir", &info);
    test::run("lfs: stat dir", err == 0 && info.type == static_cast<uint8_t>(LfsType::DIR));

    // Rename
    err = lfs.rename("hello.txt", "renamed.txt");
    test::run("lfs: rename", err == 0);
    test::run("lfs: old gone", lfs.stat("hello.txt", &info) != 0);
    test::run("lfs: new exists", lfs.stat("renamed.txt", &info) == 0);

    // Remove
    err = lfs.remove("renamed.txt");
    test::run("lfs: remove file", err == 0);

    // Persistence: unmount and remount
    lfs.unmount();
    lfs = Lfs{};
    err = lfs.mount(&cfg);
    test::run("lfs: remount", err == 0);

    err = lfs.stat("testdir", &info);
    test::run("lfs: dir persists", err == 0);

    lfs.unmount();
}

// =============================================================================
// FATfs RAM Block Device
// =============================================================================

static constexpr uint32_t FAT_BLOCK_SIZE = 512;
static constexpr uint32_t FAT_BLOCK_COUNT = 128;  // 64KB — fits in 128KB SRAM
static uint8_t fat_storage[FAT_BLOCK_SIZE * FAT_BLOCK_COUNT];

struct FatRamDev {
    int read(uint32_t block, uint32_t off, void* buf, uint32_t size) {
        if (block >= FAT_BLOCK_COUNT || off + size > FAT_BLOCK_SIZE) return -1;
        std::memcpy(buf, &fat_storage[block * FAT_BLOCK_SIZE + off], size);
        return 0;
    }
    int write(uint32_t block, uint32_t off, const void* buf, uint32_t size) {
        if (block >= FAT_BLOCK_COUNT || off + size > FAT_BLOCK_SIZE) return -1;
        std::memcpy(&fat_storage[block * FAT_BLOCK_SIZE + off], buf, size);
        return 0;
    }
    int erase(uint32_t block) {
        if (block >= FAT_BLOCK_COUNT) return -1;
        std::memset(&fat_storage[block * FAT_BLOCK_SIZE], 0xFF, FAT_BLOCK_SIZE);
        return 0;
    }
    uint32_t block_size() const { return FAT_BLOCK_SIZE; }
    uint32_t block_count() const { return FAT_BLOCK_COUNT; }
};

static FatRamDev fat_dev;

static void format_fat12() {
    // FAT12 image for 128 sectors (64KB)
    // Layout: BPB(1) + FAT1(1) + FAT2(1) + RootDir(4) + Data(121)
    std::memset(fat_storage, 0, sizeof(fat_storage));
    uint8_t* bs = fat_storage;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    bs[11] = 0x00; bs[12] = 0x02;  // 512 bytes/sector
    bs[13] = 1;                      // 1 sector/cluster
    bs[14] = 1; bs[15] = 0;         // 1 reserved sector
    bs[16] = 2;                      // 2 FATs
    bs[17] = 0x40; bs[18] = 0x00;   // root entries = 64
    bs[19] = 128; bs[20] = 0;       // total sectors = 128
    bs[21] = 0xF8;
    bs[22] = 1; bs[23] = 0;         // 1 sector per FAT
    bs[24] = 0x3F; bs[25] = 0;
    bs[26] = 0xFF; bs[27] = 0;
    bs[38] = 0x29;
    bs[39] = 0x12; bs[40] = 0x34; bs[41] = 0x56; bs[42] = 0x78;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT12   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    // FAT tables (FAT12: first 2 entries reserved)
    uint8_t* fat1 = &fat_storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF;
    uint8_t* fat2 = &fat_storage[512 * 2];
    fat2[0] = 0xF8; fat2[1] = 0xFF; fat2[2] = 0xFF;
}

// =============================================================================
// FATfs Tests
// =============================================================================

static void test_fat() {
    using namespace umi::fs;

    println("--- FATfs Tests ---");

    format_fat12();
    auto diskio = make_diskio(fat_dev);
    FatFs fs{};
    fs.set_diskio(&diskio);
    FatFsVolume vol{};

    FatResult res = fs.mount(&vol, "", 1);
    test::run("fat: mount", res == FatResult::OK);

    // Write file
    FatFile fp{};
    uint32_t bw = 0;
    res = fs.open(&fp, "TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    test::run("fat: file create", res == FatResult::OK);

    const char* msg = "Hello from Cortex-M4!";
    res = fs.write(&fp, msg, std::strlen(msg), &bw);
    test::run("fat: file write", res == FatResult::OK && bw == std::strlen(msg));
    fs.close(&fp);

    // Read back
    FatFile fp2{};
    res = fs.open(&fp2, "TEST.TXT", FA_READ);
    test::run("fat: file open read", res == FatResult::OK);

    char buf[64]{};
    uint32_t br = 0;
    res = fs.read(&fp2, buf, sizeof(buf), &br);
    test::run("fat: file read", res == FatResult::OK && br == std::strlen(msg));
    test::run("fat: file content", std::memcmp(buf, msg, std::strlen(msg)) == 0);
    fs.close(&fp2);

    // Mkdir and stat
    res = fs.mkdir("MYDIR");
    test::run("fat: mkdir", res == FatResult::OK);

    FatFileInfo fno{};
    res = fs.stat("MYDIR", &fno);
    test::run("fat: stat dir", res == FatResult::OK && (fno.fattrib & AM_DIR) != 0);

    // Rename
    res = fs.rename("TEST.TXT", "NEW.TXT");
    test::run("fat: rename", res == FatResult::OK);
    test::run("fat: old gone", fs.stat("TEST.TXT", &fno) != FatResult::OK);
    test::run("fat: new exists", fs.stat("NEW.TXT", &fno) == FatResult::OK);

    // Unlink
    res = fs.unlink("NEW.TXT");
    test::run("fat: unlink", res == FatResult::OK);

    // Getfree
    uint32_t nclst = 0;
    FatFsVolume* vol_ptr = nullptr;
    res = fs.getfree("", &nclst, &vol_ptr);
    test::run("fat: getfree", res == FatResult::OK && nclst > 0);

    fs.unmount("");
}

// =============================================================================
// Benchmarks
// =============================================================================

static void bench_lfs() {
    using namespace umi::fs;

    println("");
    println("--- littlefs Benchmarks (C++23 port) ---");

    std::memset(lfs_storage, 0xFF, sizeof(lfs_storage));
    auto cfg = make_lfs_config(lfs_dev, LFS_BLOCK_SIZE, 16,
                               lfs_read_buf, lfs_prog_buf, lfs_lookahead_buf);

    Lfs lfs{};
    uint32_t t0, t1;

    // Format + mount
    t0 = dwt_get();
    lfs.format(&cfg);
    lfs.mount(&cfg);
    t1 = dwt_get();
    report_cycles("format+mount", t1 - t0);

    // Write 1KB
    {
        LfsFile f{};
        LfsFileConfig fc{};
        fc.buffer = lfs_file_buf;
        char data[256];
        std::memset(data, 'A', sizeof(data));

        lfs.file_opencfg(&f, "bench.dat",
            static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT | LfsOpenFlags::TRUNC), &fc);

        t0 = dwt_get();
        for (int i = 0; i < 4; i++) {
            lfs.file_write(&f, data, sizeof(data));
        }
        lfs.file_close(&f);
        t1 = dwt_get();
        report_cycles("write 1KB", t1 - t0);
    }

    // Read 1KB
    {
        LfsFile f{};
        LfsFileConfig fc{};
        fc.buffer = lfs_file_buf;
        char data[256];

        lfs.file_opencfg(&f, "bench.dat", static_cast<int>(LfsOpenFlags::RDONLY), &fc);

        t0 = dwt_get();
        for (int i = 0; i < 4; i++) {
            lfs.file_read(&f, data, sizeof(data));
        }
        lfs.file_close(&f);
        t1 = dwt_get();
        report_cycles("read 1KB", t1 - t0);
    }

    // mkdir + stat x5
    {
        t0 = dwt_get();
        lfs.mkdir("d1");
        lfs.mkdir("d2");
        lfs.mkdir("d3");
        lfs.mkdir("d4");
        lfs.mkdir("d5");
        LfsInfo info{};
        lfs.stat("d1", &info);
        lfs.stat("d2", &info);
        lfs.stat("d3", &info);
        lfs.stat("d4", &info);
        lfs.stat("d5", &info);
        t1 = dwt_get();
        report_cycles("mkdir+stat x5", t1 - t0);
    }

    lfs.unmount();
}

static void bench_fat() {
    using namespace umi::fs;

    println("");
    println("--- FATfs Benchmarks (C++23 port) ---");

    format_fat12();
    auto diskio = make_diskio(fat_dev);
    FatFs fs{};
    fs.set_diskio(&diskio);
    FatFsVolume vol{};
    fs.mount(&vol, "", 1);

    uint32_t t0, t1;

    // Write 4KB
    {
        FatFile fp{};
        fs.open(&fp, "BENCH.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        char data[512];
        std::memset(data, 'A', sizeof(data));

        t0 = dwt_get();
        for (int i = 0; i < 8; i++) {
            uint32_t bw;
            fs.write(&fp, data, sizeof(data), &bw);
        }
        fs.close(&fp);
        t1 = dwt_get();
        report_cycles("write 4KB", t1 - t0);
    }

    // Read 4KB
    {
        FatFile fp{};
        fs.open(&fp, "BENCH.DAT", FA_READ);
        char data[512];

        t0 = dwt_get();
        for (int i = 0; i < 8; i++) {
            uint32_t br;
            fs.read(&fp, data, sizeof(data), &br);
        }
        fs.close(&fp);
        t1 = dwt_get();
        report_cycles("read 4KB", t1 - t0);
    }

    // mkdir + stat x3
    {
        t0 = dwt_get();
        fs.mkdir("D1");
        fs.mkdir("D2");
        fs.mkdir("D3");
        FatFileInfo fno{};
        fs.stat("D1", &fno);
        fs.stat("D2", &fno);
        fs.stat("D3", &fno);
        t1 = dwt_get();
        report_cycles("mkdir+stat x3", t1 - t0);
    }

    fs.unmount("");
}

// =============================================================================
// Main
// =============================================================================

extern "C" [[noreturn]] void _start() {
    dwt_init();

    println("");
    println("================================");
    println("  UMI-OS FS Renode Test (C++23)");
    println("================================");

    test_lfs();
    test_fat();

    bench_lfs();
    bench_fat();

    test::summary();

    // Halt
    while (true) { __asm volatile("wfi"); }
}
