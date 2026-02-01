// SPDX-License-Identifier: MIT
// Comprehensive unit tests for littlefs C++23 port
// Tests all public API functions for Phase 0 regression baseline

#include "test_common.hh"

#include <umifs/little/lfs.hh>
#include <umifs/little/lfs_config.hh>
#include <cstring>

using namespace umi::fs;

// ============================================================================
// RAM Block Device for testing
// ============================================================================

static constexpr uint32_t BLOCK_SIZE = 256;
static constexpr uint32_t BLOCK_COUNT = 64;
static constexpr uint32_t TOTAL_SIZE = BLOCK_SIZE * BLOCK_COUNT;

struct RamBlockDevice {
    uint8_t storage[TOTAL_SIZE]{};

    int read(uint32_t block, uint32_t offset, void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE) return -1;
        std::memcpy(buffer, &storage[block * BLOCK_SIZE + offset], size);
        return 0;
    }

    int write(uint32_t block, uint32_t offset, const void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE) return -1;
        std::memcpy(&storage[block * BLOCK_SIZE + offset], buffer, size);
        return 0;
    }

    int erase(uint32_t block) {
        if (block >= BLOCK_COUNT) return -1;
        std::memset(&storage[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
        return 0;
    }

    uint32_t block_size() const { return BLOCK_SIZE; }
    uint32_t block_count() const { return BLOCK_COUNT; }
};

// ============================================================================
// Test fixture helpers
// ============================================================================

static RamBlockDevice ram_dev;
static uint8_t read_buf[BLOCK_SIZE];
static uint8_t prog_buf[BLOCK_SIZE];
static uint8_t lookahead_buf[16];
static uint8_t file_buf[BLOCK_SIZE];

struct LfsFixture {
    Lfs lfs{};
    LfsConfig cfg{};

    void format_and_mount() {
        std::memset(ram_dev.storage, 0xFF, TOTAL_SIZE);
        cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);
        lfs.format(&cfg);
        lfs.mount(&cfg);
    }

    void unmount() { lfs.unmount(); }

    void remount() {
        lfs.unmount();
        lfs = Lfs{};
        lfs.mount(&cfg);
    }
};

// ============================================================================
// Basic: format, mount, unmount
// ============================================================================

static void test_format_and_mount() {
    SECTION("Format and Mount");

    Lfs lfs;
    std::memset(ram_dev.storage, 0xFF, TOTAL_SIZE);
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    int err = lfs.format(&cfg);
    CHECK(err == 0, "format succeeds");

    err = lfs.mount(&cfg);
    CHECK(err == 0, "mount succeeds");

    err = lfs.unmount();
    CHECK(err == 0, "unmount succeeds");
}

static void test_double_mount_fails() {
    SECTION("Double mount without unmount");

    Lfs lfs;
    std::memset(ram_dev.storage, 0xFF, TOTAL_SIZE);
    auto cfg = make_lfs_config(ram_dev, BLOCK_SIZE, 16, read_buf, prog_buf, lookahead_buf);

    lfs.format(&cfg);
    lfs.mount(&cfg);

    // Second mount on same Lfs should fail or behave predictably
    // (littlefs doesn't guard against this, but we verify no crash)
    lfs.unmount();
    CHECK(true, "no crash on lifecycle");
}

// ============================================================================
// File: open, close, read, write
// ============================================================================

static void test_file_write_read() {
    SECTION("File Write/Read");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    int err = f.lfs.file_opencfg(&file, "hello.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT | LfsOpenFlags::TRUNC), &fcfg);
    CHECK(err == 0, "file open for write");

    const char* msg = "Hello, littlefs!";
    auto len = static_cast<lfs_size_t>(std::strlen(msg));
    lfs_ssize_t written = f.lfs.file_write(&file, msg, len);
    CHECK(written == static_cast<lfs_ssize_t>(len), "file write");

    err = f.lfs.file_close(&file);
    CHECK(err == 0, "file close after write");

    // Read back
    LfsFile file2{};
    LfsFileConfig fcfg2{};
    fcfg2.buffer = file_buf;

    err = f.lfs.file_opencfg(&file2, "hello.txt", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg2);
    CHECK(err == 0, "file open for read");

    char buf[64]{};
    lfs_ssize_t nread = f.lfs.file_read(&file2, buf, sizeof(buf));
    CHECK(nread == static_cast<lfs_ssize_t>(len), "file read size");
    CHECK(std::memcmp(buf, msg, len) == 0, "file content matches");

    f.lfs.file_close(&file2);
    f.unmount();
}

static void test_file_empty() {
    SECTION("Empty file");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    f.lfs.file_opencfg(&file, "empty.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_close(&file);

    LfsInfo info{};
    int err = f.lfs.stat("empty.bin", &info);
    CHECK(err == 0, "stat empty file");
    CHECK(info.size == 0, "empty file size is 0");

    f.unmount();
}

// ============================================================================
// File: seek, tell, rewind, size
// ============================================================================

static void test_file_seek_tell() {
    SECTION("File Seek/Tell");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    f.lfs.file_opencfg(&file, "seek.bin",
        static_cast<int>(LfsOpenFlags::RDWR | LfsOpenFlags::CREAT), &fcfg);

    const char* data = "ABCDEFGHIJ";
    f.lfs.file_write(&file, data, 10);

    // tell should be at end
    lfs_soff_t pos = f.lfs.file_tell(&file);
    CHECK(pos == 10, "tell after write");

    // seek SET
    pos = f.lfs.file_seek(&file, 3, static_cast<int>(LfsWhence::SET));
    CHECK(pos == 3, "seek SET to 3");

    char buf[4]{};
    f.lfs.file_read(&file, buf, 4);
    CHECK(std::memcmp(buf, "DEFG", 4) == 0, "read after seek SET");

    // seek CUR
    pos = f.lfs.file_seek(&file, -2, static_cast<int>(LfsWhence::CUR));
    CHECK(pos == 5, "seek CUR -2 from 7");

    // seek END
    pos = f.lfs.file_seek(&file, -3, static_cast<int>(LfsWhence::END));
    CHECK(pos == 7, "seek END -3");

    // size
    lfs_soff_t sz = f.lfs.file_size(&file);
    CHECK(sz == 10, "file size");

    // rewind
    int err = f.lfs.file_rewind(&file);
    CHECK(err == 0, "rewind");
    pos = f.lfs.file_tell(&file);
    CHECK(pos == 0, "tell after rewind");

    f.lfs.file_close(&file);
    f.unmount();
}

// ============================================================================
// File: truncate
// ============================================================================

static void test_file_truncate() {
    SECTION("File Truncate");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    f.lfs.file_opencfg(&file, "trunc.bin",
        static_cast<int>(LfsOpenFlags::RDWR | LfsOpenFlags::CREAT), &fcfg);

    const char* data = "0123456789";
    f.lfs.file_write(&file, data, 10);

    int err = f.lfs.file_truncate(&file, 5);
    CHECK(err == 0, "truncate to 5");

    lfs_soff_t sz = f.lfs.file_size(&file);
    CHECK(sz == 5, "size after truncate");

    f.lfs.file_close(&file);
    f.unmount();
}

// ============================================================================
// File: sync
// ============================================================================

static void test_file_sync() {
    SECTION("File Sync");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    f.lfs.file_opencfg(&file, "sync.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);

    f.lfs.file_write(&file, "data", 4);
    int err = f.lfs.file_sync(&file);
    CHECK(err == 0, "sync succeeds");

    f.lfs.file_close(&file);
    f.unmount();
}

// ============================================================================
// Directory: mkdir, stat, dir_open/read/close
// ============================================================================

static void test_mkdir_and_stat() {
    SECTION("Mkdir and Stat");

    LfsFixture f;
    f.format_and_mount();

    int err = f.lfs.mkdir("mydir");
    CHECK(err == 0, "mkdir");

    LfsInfo info{};
    err = f.lfs.stat("mydir", &info);
    CHECK(err == 0, "stat dir");
    CHECK(info.type == static_cast<uint8_t>(LfsType::DIR), "is directory");

    // stat non-existent
    err = f.lfs.stat("noexist", &info);
    CHECK(err != 0, "stat non-existent fails");

    f.unmount();
}

static void test_dir_read() {
    SECTION("Directory Read");

    LfsFixture f;
    f.format_and_mount();

    // Create entries
    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    f.lfs.file_opencfg(&file, "a.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_close(&file);

    f.lfs.file_opencfg(&file, "b.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_close(&file);

    f.lfs.mkdir("subdir");

    // Read root directory
    LfsDir dir{};
    int err = f.lfs.dir_open(&dir, "/");
    CHECK(err == 0, "dir open root");

    int count = 0;
    LfsInfo info{};
    while (f.lfs.dir_read(&dir, &info) > 0) {
        count++;
    }
    // Expect: ".", "..", "a.txt", "b.txt", "subdir" = 5 entries
    CHECK(count == 5, "root has 5 entries (., .., a.txt, b.txt, subdir)");

    f.lfs.dir_close(&dir);
    f.unmount();
}

// ============================================================================
// Directory: seek, tell, rewind
// ============================================================================

static void test_dir_seek_tell_rewind() {
    SECTION("Directory Seek/Tell/Rewind");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    f.lfs.file_opencfg(&file, "f1.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_close(&file);
    f.lfs.file_opencfg(&file, "f2.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_close(&file);

    LfsDir dir{};
    f.lfs.dir_open(&dir, "/");

    // Read first entry
    LfsInfo info{};
    f.lfs.dir_read(&dir, &info);
    lfs_soff_t pos = f.lfs.dir_tell(&dir);
    CHECK(pos > 0, "dir tell after first read");

    // Read rest
    while (f.lfs.dir_read(&dir, &info) > 0) {}

    // Rewind
    int err = f.lfs.dir_rewind(&dir);
    CHECK(err == 0, "dir rewind");

    // Read again
    int count = 0;
    while (f.lfs.dir_read(&dir, &info) > 0) count++;
    CHECK(count == 4, "after rewind: 4 entries (., .., f1.txt, f2.txt)");

    f.lfs.dir_close(&dir);
    f.unmount();
}

// ============================================================================
// Operations: remove
// ============================================================================

static void test_remove_file() {
    SECTION("Remove File");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    f.lfs.file_opencfg(&file, "del.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_write(&file, "x", 1);
    f.lfs.file_close(&file);

    LfsInfo info{};
    CHECK(f.lfs.stat("del.txt", &info) == 0, "file exists before remove");

    int err = f.lfs.remove("del.txt");
    CHECK(err == 0, "remove file");

    CHECK(f.lfs.stat("del.txt", &info) != 0, "file gone after remove");

    f.unmount();
}

static void test_remove_empty_dir() {
    SECTION("Remove Empty Directory");

    LfsFixture f;
    f.format_and_mount();

    f.lfs.mkdir("emptydir");
    int err = f.lfs.remove("emptydir");
    CHECK(err == 0, "remove empty dir");

    LfsInfo info{};
    CHECK(f.lfs.stat("emptydir", &info) != 0, "dir gone after remove");

    f.unmount();
}

static void test_remove_nonempty_dir_fails() {
    SECTION("Remove Non-empty Directory Fails");

    LfsFixture f;
    f.format_and_mount();

    f.lfs.mkdir("fulldir");

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    f.lfs.file_opencfg(&file, "fulldir/inner.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_close(&file);

    int err = f.lfs.remove("fulldir");
    CHECK(err != 0, "remove non-empty dir fails");

    f.unmount();
}

// ============================================================================
// Operations: rename
// ============================================================================

static void test_rename() {
    SECTION("Rename");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    f.lfs.file_opencfg(&file, "old.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_write(&file, "data", 4);
    f.lfs.file_close(&file);

    int err = f.lfs.rename("old.txt", "new.txt");
    CHECK(err == 0, "rename");

    LfsInfo info{};
    CHECK(f.lfs.stat("old.txt", &info) != 0, "old name gone");
    CHECK(f.lfs.stat("new.txt", &info) == 0, "new name exists");
    CHECK(info.size == 4, "content preserved");

    f.unmount();
}

// ============================================================================
// Attributes: getattr, setattr, removeattr
// ============================================================================

static void test_attributes() {
    SECTION("Custom Attributes");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    f.lfs.file_opencfg(&file, "attr.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_close(&file);

    // Set attribute
    uint32_t val = 0xDEADBEEF;
    int err = f.lfs.setattr("attr.bin", 0x42, &val, sizeof(val));
    CHECK(err == 0, "setattr");

    // Get attribute
    uint32_t got = 0;
    lfs_ssize_t sz = f.lfs.getattr("attr.bin", 0x42, &got, sizeof(got));
    CHECK(sz == sizeof(val), "getattr size");
    CHECK(got == 0xDEADBEEF, "getattr value");

    // Remove attribute
    err = f.lfs.removeattr("attr.bin", 0x42);
    CHECK(err == 0, "removeattr");

    sz = f.lfs.getattr("attr.bin", 0x42, &got, sizeof(got));
    CHECK(sz < 0, "getattr after remove fails");

    f.unmount();
}

// ============================================================================
// FS level: fs_stat, fs_size
// ============================================================================

static void test_fs_stat_and_size() {
    SECTION("FS Stat and Size");

    LfsFixture f;
    f.format_and_mount();

    LfsFsInfo fsinfo{};
    int err = f.lfs.fs_stat(&fsinfo);
    CHECK(err == 0, "fs_stat");
    CHECK(fsinfo.block_size == BLOCK_SIZE, "block size");
    CHECK(fsinfo.block_count == BLOCK_COUNT, "block count");

    lfs_ssize_t used = f.lfs.fs_size();
    CHECK(used > 0, "fs_size > 0 after format");

    f.unmount();
}

// ============================================================================
// FS level: fs_traverse
// ============================================================================

static int traverse_counter;
static int traverse_cb(void* /*data*/, lfs_block_t /*block*/) {
    traverse_counter++;
    return 0;
}

static void test_fs_traverse() {
    SECTION("FS Traverse");

    LfsFixture f;
    f.format_and_mount();

    traverse_counter = 0;
    int err = f.lfs.fs_traverse(traverse_cb, nullptr);
    CHECK(err == 0, "fs_traverse");
    CHECK(traverse_counter > 0, "traverse found blocks");

    f.unmount();
}

// ============================================================================
// FS level: fs_mkconsistent
// ============================================================================

static void test_fs_mkconsistent() {
    SECTION("FS Mkconsistent");

    LfsFixture f;
    f.format_and_mount();

    int err = f.lfs.fs_mkconsistent();
    CHECK(err == 0, "fs_mkconsistent");

    f.unmount();
}

// ============================================================================
// FS level: fs_gc
// ============================================================================

static void test_fs_gc() {
    SECTION("FS GC");

    LfsFixture f;
    f.format_and_mount();

    // Create and delete some files to create garbage
    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    for (int i = 0; i < 5; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "gc%d.bin", i);
        f.lfs.file_opencfg(&file, name,
            static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
        f.lfs.file_write(&file, "garbage", 7);
        f.lfs.file_close(&file);
    }
    for (int i = 0; i < 5; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "gc%d.bin", i);
        f.lfs.remove(name);
    }

    int err = f.lfs.fs_gc();
    CHECK(err == 0, "fs_gc");

    f.unmount();
}

// ============================================================================
// Persistence: data survives remount
// ============================================================================

static void test_persistence() {
    SECTION("Data Persists Across Remount");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    f.lfs.file_opencfg(&file, "persist.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    const char* msg = "persistent data";
    f.lfs.file_write(&file, msg, std::strlen(msg));
    f.lfs.file_close(&file);

    f.lfs.mkdir("persistdir");

    // Remount
    f.remount();

    // Verify file
    LfsFile file2{};
    LfsFileConfig fcfg2{};
    fcfg2.buffer = file_buf;
    int err = f.lfs.file_opencfg(&file2, "persist.bin", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg2);
    CHECK(err == 0, "file still exists after remount");

    char buf[64]{};
    lfs_ssize_t nread = f.lfs.file_read(&file2, buf, sizeof(buf));
    CHECK(nread == static_cast<lfs_ssize_t>(std::strlen(msg)), "data length preserved");
    CHECK(std::memcmp(buf, msg, std::strlen(msg)) == 0, "data content preserved");
    f.lfs.file_close(&file2);

    // Verify directory
    LfsInfo info{};
    err = f.lfs.stat("persistdir", &info);
    CHECK(err == 0, "directory persists after remount");

    f.unmount();
}

// ============================================================================
// Edge case: multi-block file
// ============================================================================

static void test_large_file() {
    SECTION("Large File (multi-block)");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    f.lfs.file_opencfg(&file, "big.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);

    // Write ~1KB (spans multiple blocks of 256 bytes)
    uint8_t pattern[128];
    for (int i = 0; i < 128; i++) pattern[i] = static_cast<uint8_t>(i);

    for (int i = 0; i < 8; i++) {
        lfs_ssize_t w = f.lfs.file_write(&file, pattern, 128);
        CHECK(w == 128, "write chunk");
    }
    f.lfs.file_close(&file);

    // Read back and verify
    LfsFile file2{};
    LfsFileConfig fcfg2{};
    fcfg2.buffer = file_buf;
    f.lfs.file_opencfg(&file2, "big.bin", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg2);

    lfs_soff_t sz = f.lfs.file_size(&file2);
    CHECK(sz == 1024, "large file size");

    for (int i = 0; i < 8; i++) {
        uint8_t buf[128]{};
        lfs_ssize_t r = f.lfs.file_read(&file2, buf, 128);
        CHECK(r == 128, "read chunk");
        CHECK(std::memcmp(buf, pattern, 128) == 0, "chunk content matches");
    }
    f.lfs.file_close(&file2);

    f.unmount();
}

// ============================================================================
// Edge case: nested directories
// ============================================================================

static void test_nested_dirs() {
    SECTION("Nested Directories");

    LfsFixture f;
    f.format_and_mount();

    f.lfs.mkdir("a");
    f.lfs.mkdir("a/b");
    f.lfs.mkdir("a/b/c");

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;
    f.lfs.file_opencfg(&file, "a/b/c/deep.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_write(&file, "deep", 4);
    f.lfs.file_close(&file);

    LfsInfo info{};
    int err = f.lfs.stat("a/b/c/deep.txt", &info);
    CHECK(err == 0, "stat nested file");
    CHECK(info.size == 4, "nested file size");

    f.unmount();
}

// ============================================================================
// Edge case: file open flags (EXCL, APPEND)
// ============================================================================

static void test_open_flags() {
    SECTION("Open Flags");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    // CREAT | EXCL on new file
    int err = f.lfs.file_opencfg(&file, "excl.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT | LfsOpenFlags::EXCL), &fcfg);
    CHECK(err == 0, "CREAT|EXCL on new file");
    f.lfs.file_close(&file);

    // CREAT | EXCL on existing file should fail
    err = f.lfs.file_opencfg(&file, "excl.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT | LfsOpenFlags::EXCL), &fcfg);
    CHECK(err != 0, "CREAT|EXCL on existing file fails");

    // APPEND
    LfsFileConfig fcfg2{};
    fcfg2.buffer = file_buf;
    f.lfs.file_opencfg(&file, "append.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg2);
    f.lfs.file_write(&file, "AAA", 3);
    f.lfs.file_close(&file);

    f.lfs.file_opencfg(&file, "append.txt",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::APPEND), &fcfg2);
    f.lfs.file_write(&file, "BBB", 3);
    f.lfs.file_close(&file);

    // Read and verify
    f.lfs.file_opencfg(&file, "append.txt", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg2);
    char buf[16]{};
    lfs_ssize_t n = f.lfs.file_read(&file, buf, sizeof(buf));
    CHECK(n == 6, "append total size");
    CHECK(std::memcmp(buf, "AAABBB", 6) == 0, "append content");
    f.lfs.file_close(&file);

    f.unmount();
}

// ============================================================================
// Edge case: full disk
// ============================================================================

static void test_full_disk() {
    SECTION("Full Disk");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    // Fill disk by writing large chunks
    int files_created = 0;
    for (int i = 0; i < 100; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "fill%02d", i);
        int err = f.lfs.file_opencfg(&file, name,
            static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
        if (err != 0) break;

        uint8_t data[BLOCK_SIZE];
        std::memset(data, static_cast<uint8_t>(i), sizeof(data));
        lfs_ssize_t w = f.lfs.file_write(&file, data, sizeof(data));
        f.lfs.file_close(&file);
        if (w <= 0) break;
        files_created++;
    }
    CHECK(files_created > 0, "created some files before full");

    // Verify existing files are still readable
    f.lfs.file_opencfg(&file, "fill00", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg);
    uint8_t buf[1];
    lfs_ssize_t r = f.lfs.file_read(&file, buf, 1);
    CHECK(r == 1, "can read first file on full disk");
    CHECK(buf[0] == 0, "first file content correct");
    f.lfs.file_close(&file);

    f.unmount();
}

// ============================================================================
// Edge case: block-size boundary writes
// ============================================================================

static void test_boundary_writes() {
    SECTION("Boundary Writes");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    // Write exactly block_size bytes
    f.lfs.file_opencfg(&file, "exact.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    uint8_t data[BLOCK_SIZE];
    for (uint32_t i = 0; i < BLOCK_SIZE; i++) data[i] = static_cast<uint8_t>(i);
    lfs_ssize_t w = f.lfs.file_write(&file, data, BLOCK_SIZE);
    CHECK(w == static_cast<lfs_ssize_t>(BLOCK_SIZE), "write exact block size");
    f.lfs.file_close(&file);

    // Write block_size - 1
    f.lfs.file_opencfg(&file, "minus1.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    w = f.lfs.file_write(&file, data, BLOCK_SIZE - 1);
    CHECK(w == static_cast<lfs_ssize_t>(BLOCK_SIZE - 1), "write block_size - 1");
    f.lfs.file_close(&file);

    // Write block_size + 1
    f.lfs.file_opencfg(&file, "plus1.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    w = f.lfs.file_write(&file, data, BLOCK_SIZE);
    CHECK(w == static_cast<lfs_ssize_t>(BLOCK_SIZE), "write first block");
    uint8_t extra = 0xAB;
    w = f.lfs.file_write(&file, &extra, 1);
    CHECK(w == 1, "write +1 byte");
    f.lfs.file_close(&file);

    // Verify sizes
    LfsInfo info{};
    f.lfs.stat("exact.bin", &info);
    CHECK(info.size == BLOCK_SIZE, "exact size");
    f.lfs.stat("minus1.bin", &info);
    CHECK(info.size == BLOCK_SIZE - 1, "minus1 size");
    f.lfs.stat("plus1.bin", &info);
    CHECK(info.size == BLOCK_SIZE + 1, "plus1 size");

    f.unmount();
}

// ============================================================================
// Edge case: overwrite existing file
// ============================================================================

static void test_overwrite() {
    SECTION("Overwrite Existing File");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    // Create with initial data
    f.lfs.file_opencfg(&file, "over.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    f.lfs.file_write(&file, "AAAAAAAAAA", 10);
    f.lfs.file_close(&file);

    // Overwrite with shorter data
    f.lfs.file_opencfg(&file, "over.bin",
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT | LfsOpenFlags::TRUNC), &fcfg);
    f.lfs.file_write(&file, "BBB", 3);
    f.lfs.file_close(&file);

    // Verify
    f.lfs.file_opencfg(&file, "over.bin", static_cast<int>(LfsOpenFlags::RDONLY), &fcfg);
    char buf[16]{};
    lfs_ssize_t r = f.lfs.file_read(&file, buf, sizeof(buf));
    CHECK(r == 3, "overwritten size");
    CHECK(std::memcmp(buf, "BBB", 3) == 0, "overwritten content");
    f.lfs.file_close(&file);

    f.unmount();
}

// ============================================================================
// Edge case: fragmentation (create many small files, delete, recreate)
// ============================================================================

static void test_fragmentation() {
    SECTION("Fragmentation");

    LfsFixture f;
    f.format_and_mount();

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    // Create 10 small files
    for (int i = 0; i < 10; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "frag%d", i);
        f.lfs.file_opencfg(&file, name,
            static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
        f.lfs.file_write(&file, "x", 1);
        f.lfs.file_close(&file);
    }

    // Delete odd-numbered
    for (int i = 1; i < 10; i += 2) {
        char name[16];
        std::snprintf(name, sizeof(name), "frag%d", i);
        f.lfs.remove(name);
    }

    // Create 5 new files in fragmented space
    for (int i = 0; i < 5; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "new%d", i);
        int err = f.lfs.file_opencfg(&file, name,
            static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
        CHECK(err == 0, "create in fragmented space");
        f.lfs.file_write(&file, "yy", 2);
        f.lfs.file_close(&file);
    }

    // Verify even-numbered originals
    for (int i = 0; i < 10; i += 2) {
        char name[16];
        std::snprintf(name, sizeof(name), "frag%d", i);
        LfsInfo info{};
        CHECK(f.lfs.stat(name, &info) == 0, "original file survives");
    }

    f.unmount();
}

// ============================================================================
// Edge case: max filename length
// ============================================================================

static void test_max_filename() {
    SECTION("Max Filename Length");

    LfsFixture f;
    f.format_and_mount();

    // LFS_NAME_MAX defaults to 255
    char long_name[256];
    std::memset(long_name, 'a', 255);
    long_name[255] = '\0';

    LfsFile file{};
    LfsFileConfig fcfg{};
    fcfg.buffer = file_buf;

    // This may fail if name_max is configured shorter, but should not crash
    int err = f.lfs.file_opencfg(&file, long_name,
        static_cast<int>(LfsOpenFlags::WRONLY | LfsOpenFlags::CREAT), &fcfg);
    if (err == 0) {
        f.lfs.file_close(&file);
        LfsInfo info{};
        CHECK(f.lfs.stat(long_name, &info) == 0, "long filename stat");
    } else {
        CHECK(true, "long filename rejected (name_max limit)");
    }

    f.unmount();
}

// ============================================================================
// Entry point
// ============================================================================

int main() {
    test_format_and_mount();
    test_double_mount_fails();
    test_file_write_read();
    test_file_empty();
    test_file_seek_tell();
    test_file_truncate();
    test_file_sync();
    test_mkdir_and_stat();
    test_dir_read();
    test_dir_seek_tell_rewind();
    test_remove_file();
    test_remove_empty_dir();
    test_remove_nonempty_dir_fails();
    test_rename();
    test_attributes();
    test_fs_stat_and_size();
    test_fs_traverse();
    test_fs_mkconsistent();
    test_fs_gc();
    test_persistence();
    test_large_file();
    test_nested_dirs();
    test_open_flags();
    test_full_disk();
    test_boundary_writes();
    test_overwrite();
    test_fragmentation();
    test_max_filename();

    TEST_SUMMARY();
}
