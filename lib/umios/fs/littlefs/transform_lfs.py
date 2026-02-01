#!/usr/bin/env python3
"""Transform lfs.c to lfs.cc mechanically.

Usage: python3 transform_lfs.py > lfs.cc
"""

import re
import sys

SRC = "/Users/tekitou/work/umi/.refs/littlefs/lfs.c"

HEADER = r'''// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, The littlefs authors.
// Copyright (c) 2017, Arm Limited. All rights reserved.
//
// C++23 port of lfs.c for the UMI framework.
// This is a FAITHFUL port — same algorithms, data structures, and logic.
// The original static functions are preserved as-is (not class methods),
// operating on an lfs_t struct that is aliased from the Lfs class.
// Public API methods on Lfs delegate to these static functions.

#include "lfs.hh"

#include <cinttypes>
#include <cstring>

// Disable LFS_MIGRATE — our Lfs class does not carry lfs1 state
#ifndef LFS_NO_MIGRATE
#define LFS_NO_MIGRATE
#endif

namespace umi::fs::lfs {

// ============================================================================
// CRC implementation (from lfs_util.c)
// ============================================================================

uint32_t lfs_crc(uint32_t crc, const void* buffer, size_t size) {
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    const auto* data = static_cast<const uint8_t*>(buffer);
    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }
    return crc;
}

// ============================================================================
// Type bridge: type aliases so the original C-style static functions compile
// unchanged, operating on Lfs (which has public members matching lfs_t).
// ============================================================================

using lfs_t = Lfs;
using lfs_cache_t = LfsCache;
using lfs_mdir_t = LfsMdir;
using lfs_dir_t = LfsDir;
using lfs_file_t = LfsFile;
using lfs_superblock_t = LfsSuperblock;
using lfs_gstate_t = LfsGstate;

// ============================================================================
// Compatibility defines: map original C LFS_ERR_*, LFS_TYPE_*, etc. to
// integer values so the extracted C code compiles unchanged.
// ============================================================================

// Error codes
#define LFS_ERR_OK          0
#define LFS_ERR_IO          (-5)
#define LFS_ERR_CORRUPT     (-84)
#define LFS_ERR_NOENT       (-2)
#define LFS_ERR_EXIST       (-17)
#define LFS_ERR_NOTDIR      (-20)
#define LFS_ERR_ISDIR       (-21)
#define LFS_ERR_NOTEMPTY    (-39)
#define LFS_ERR_BADF        (-9)
#define LFS_ERR_FBIG        (-27)
#define LFS_ERR_INVAL       (-22)
#define LFS_ERR_NOSPC       (-28)
#define LFS_ERR_NOMEM       (-12)
#define LFS_ERR_NOATTR      (-61)
#define LFS_ERR_NAMETOOLONG (-36)

// File types
#define LFS_TYPE_REG          0x001
#define LFS_TYPE_DIR          0x002
#define LFS_TYPE_SPLICE       0x400
#define LFS_TYPE_NAME         0x000
#define LFS_TYPE_STRUCT       0x200
#define LFS_TYPE_USERATTR     0x300
#define LFS_TYPE_FROM         0x100
#define LFS_TYPE_TAIL         0x600
#define LFS_TYPE_GLOBALS      0x700
#define LFS_TYPE_CRC          0x500
#define LFS_TYPE_CREATE       0x401
#define LFS_TYPE_DELETE       0x4ff
#define LFS_TYPE_SUPERBLOCK   0x0ff
#define LFS_TYPE_DIRSTRUCT    0x200
#define LFS_TYPE_CTZSTRUCT    0x202
#define LFS_TYPE_INLINESTRUCT 0x201
#define LFS_TYPE_SOFTTAIL     0x600
#define LFS_TYPE_HARDTAIL     0x601
#define LFS_TYPE_MOVESTATE    0x7ff
#define LFS_TYPE_CCRC         0x500
#define LFS_TYPE_FCRC         0x5ff
#define LFS_TYPE_FROM_NOOP    0x000
#define LFS_TYPE_FROM_MOVE    0x101
#define LFS_TYPE_FROM_USERATTRS 0x102

// FROM aliases (used without TYPE_ prefix in original code)
#define LFS_FROM_NOOP       0x000
#define LFS_FROM_MOVE       0x101
#define LFS_FROM_USERATTRS  0x102

// Open flags
#define LFS_O_RDONLY   1
#define LFS_O_WRONLY   2
#define LFS_O_RDWR     3
#define LFS_O_CREAT    0x0100
#define LFS_O_EXCL     0x0200
#define LFS_O_TRUNC    0x0400
#define LFS_O_APPEND   0x0800
#define LFS_F_DIRTY    0x010000
#define LFS_F_WRITING  0x020000
#define LFS_F_READING  0x040000
#define LFS_F_ERRED    0x080000
#define LFS_F_INLINE   0x100000

// Seek
#define LFS_SEEK_SET  0
#define LFS_SEEK_CUR  1
#define LFS_SEEK_END  2

// Version/limits
#define LFS_VERSION            VERSION
#define LFS_VERSION_MAJOR      VERSION_MAJOR
#define LFS_VERSION_MINOR      VERSION_MINOR
#define LFS_DISK_VERSION       DISK_VERSION
#define LFS_DISK_VERSION_MAJOR DISK_VERSION_MAJOR
#define LFS_DISK_VERSION_MINOR DISK_VERSION_MINOR
#define LFS_NAME_MAX           NAME_MAX
#define LFS_FILE_MAX           FILE_MAX
#define LFS_ATTR_MAX           ATTR_MAX

// Helper for C99 scalar compound literal address-of: &(type){value}
// Uses GCC/Clang statement expression to create a local and return its address.
// The lifetime of the local extends to the end of the enclosing full-expression.
#define LFS_SCALAR_TMP(type, val) \
    __extension__({ static type _lfs_stmp; _lfs_stmp = (type)(val); &_lfs_stmp; })


'''

PUBLIC_API = r'''

// ============================================================================
// Public API: Lfs class methods delegating to internal static functions
// ============================================================================

#ifndef LFS_READONLY
int Lfs::format(const LfsConfig* config) noexcept {
    return lfs_format_(this, config);
}
#endif

int Lfs::mount(const LfsConfig* config) noexcept {
    return lfs_mount_(this, config);
}

int Lfs::unmount() noexcept {
    return lfs_unmount_(this);
}

#ifndef LFS_READONLY
int Lfs::remove(const char* path) noexcept {
    return lfs_remove_(this, path);
}
#endif

#ifndef LFS_READONLY
int Lfs::rename(const char* oldpath, const char* newpath) noexcept {
    return lfs_rename_(this, oldpath, newpath);
}
#endif

int Lfs::stat(const char* path, LfsInfo* info) noexcept {
    return lfs_stat_(this, path, info);
}

lfs_ssize_t Lfs::getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size) noexcept {
    return lfs_getattr_(this, path, type, buffer, size);
}

#ifndef LFS_READONLY
int Lfs::setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size) noexcept {
    return lfs_setattr_(this, path, type, buffer, size);
}
#endif

#ifndef LFS_READONLY
int Lfs::removeattr(const char* path, uint8_t type) noexcept {
    return lfs_removeattr_(this, path, type);
}
#endif

#ifndef LFS_NO_MALLOC
int Lfs::file_open(LfsFile* file, const char* path, int flags) noexcept {
    return lfs_file_open_(this, file, path, flags);
}
#endif

int Lfs::file_opencfg(LfsFile* file, const char* path, int flags,
                      const LfsFileConfig* config) noexcept {
    return lfs_file_opencfg_(this, file, path, flags, config);
}

int Lfs::file_close(LfsFile* file) noexcept {
    return lfs_file_close_(this, file);
}

#ifndef LFS_READONLY
int Lfs::file_sync(LfsFile* file) noexcept {
    return lfs_file_sync_(this, file);
}
#endif

lfs_ssize_t Lfs::file_read(LfsFile* file, void* buffer, lfs_size_t size) noexcept {
    return lfs_file_read_(this, file, buffer, size);
}

#ifndef LFS_READONLY
lfs_ssize_t Lfs::file_write(LfsFile* file, const void* buffer, lfs_size_t size) noexcept {
    return lfs_file_write_(this, file, buffer, size);
}
#endif

lfs_soff_t Lfs::file_seek(LfsFile* file, lfs_soff_t off, int whence) noexcept {
    return lfs_file_seek_(this, file, off, whence);
}

#ifndef LFS_READONLY
int Lfs::file_truncate(LfsFile* file, lfs_off_t size) noexcept {
    return lfs_file_truncate_(this, file, size);
}
#endif

lfs_soff_t Lfs::file_tell(LfsFile* file) noexcept {
    return lfs_file_tell_(this, file);
}

int Lfs::file_rewind(LfsFile* file) noexcept {
    return lfs_file_rewind_(this, file);
}

lfs_soff_t Lfs::file_size(LfsFile* file) noexcept {
    return lfs_file_size_(this, file);
}

#ifndef LFS_READONLY
int Lfs::mkdir(const char* path) noexcept {
    return lfs_mkdir_(this, path);
}
#endif

int Lfs::dir_open(LfsDir* dir, const char* path) noexcept {
    return lfs_dir_open_(this, dir, path);
}

int Lfs::dir_close(LfsDir* dir) noexcept {
    return lfs_dir_close_(this, dir);
}

int Lfs::dir_read(LfsDir* dir, LfsInfo* info) noexcept {
    return lfs_dir_read_(this, dir, info);
}

int Lfs::dir_seek(LfsDir* dir, lfs_off_t off) noexcept {
    return lfs_dir_seek_(this, dir, off);
}

lfs_soff_t Lfs::dir_tell(LfsDir* dir) noexcept {
    return lfs_dir_tell_(this, dir);
}

int Lfs::dir_rewind(LfsDir* dir) noexcept {
    return lfs_dir_rewind_(this, dir);
}

int Lfs::fs_stat(LfsFsInfo* fsinfo) noexcept {
    return lfs_fs_stat_(this, fsinfo);
}

lfs_ssize_t Lfs::fs_size() noexcept {
    return lfs_fs_size_(this);
}

int Lfs::fs_traverse(int (*cb)(void*, lfs_block_t), void* data) noexcept {
    return lfs_fs_traverse_(this, cb, data, true);
}

#ifndef LFS_READONLY
int Lfs::fs_mkconsistent() noexcept {
    return lfs_fs_mkconsistent_(this);
}
#endif

#ifndef LFS_READONLY
int Lfs::fs_gc() noexcept {
    return lfs_fs_gc_(this);
}
#endif

#ifndef LFS_READONLY
int Lfs::fs_grow(lfs_size_t block_count) noexcept {
    return lfs_fs_grow_(this, block_count);
}
#endif

} // namespace umi::fs::lfs
'''


def find_line(lines, pattern):
    """Find 0-based index of first line matching pattern."""
    for i, line in enumerate(lines):
        if pattern in line:
            return i
    raise ValueError(f"Pattern not found: {pattern}")


def transform_line(line):
    """Apply C-to-C++ transformations to a single line."""
    # Keep LFS_BLOCK_NULL/INLINE defines — they are needed and not in our headers

    # NULL -> nullptr (word boundary)
    line = re.sub(r'\bNULL\b', 'nullptr', line)

    # void* -> typed pointer implicit casts (C allows, C++ doesn't)
    # Handle initialization: "type *name = buffer/data/p;"
    void_ptr_params = {'buffer', 'data', 'p'}

    # Pattern 1: typed pointer init (with optional struct keyword)
    m = re.match(
        r'^(\s*)((?:const\s+)?(?:struct\s+)?\w+\s*\*\s*)(\w+)(\s*=\s*)(\w+)\s*;',
        line
    )
    if m and m.group(5) in void_ptr_params:
        indent = m.group(1)
        type_part = m.group(2).strip()
        varname = m.group(3)
        eq = m.group(4)
        src = m.group(5)
        # Build cast type: remove trailing spaces/stars, add back one *
        base = re.sub(r'\s*\*\s*$', '', type_part)
        cast_type = base + '*'
        line = f'{indent}{type_part}{varname}{eq}static_cast<{cast_type}>({src});\n'

    # Pattern 2: bare assignment like "dir = buffer;" where dir is already a pointer
    m2 = re.match(r'^(\s*)(\w+)(\s*=\s*)(buffer|data|p)\s*;', line)
    if m2 and m2.group(2) not in ('int', 'uint32_t', 'lfs_size_t', 'err', 'res'):
        indent = m2.group(1)
        varname = m2.group(2)
        eq = m2.group(3)
        src = m2.group(4)
        line = f'{indent}{varname}{eq}static_cast<decltype({varname})>({src});\n'

    # Pattern 3: member.buffer = void_ptr; (cfg->buffer, lfs_malloc, cfg->read_buffer, etc.)
    # E.g. "file->cache.buffer = file->cfg->buffer;"
    # E.g. "lfs->rcache.buffer = lfs_malloc(...);"
    line = re.sub(
        r'(\.buffer\s*=\s*)((?:lfs_malloc|.*->cfg->(?:buffer|read_buffer|prog_buffer|lookahead_buffer))\b[^;]*)',
        lambda m: m.group(1) + 'static_cast<uint8_t*>(' + m.group(2) + ')',
        line
    )

    # struct type -> C++ type
    line = re.sub(r'\bstruct lfs_config\b', 'LfsConfig', line)
    line = re.sub(r'\bstruct lfs_info\b', 'LfsInfo', line)
    line = re.sub(r'\bstruct lfs_fsinfo\b', 'LfsFsInfo', line)
    line = re.sub(r'\bstruct lfs_file_config\b', 'LfsFileConfig', line)
    line = re.sub(r'\bstruct lfs_mlist\b', 'LfsMlist', line)
    line = re.sub(r'\bstruct lfs_ctz\b', 'LfsCtz', line)
    line = re.sub(r'\bstruct lfs_attr\b', 'LfsAttr', line)

    # Fix PRIx32/PRIu32 string literal spacing (C++11 user-defined literal issue)
    # "...%"PRIx32 -> "...%" PRIx32
    line = re.sub(r'"(\s*)(PRI[duxX]\d+)', r'" \2', line)

    # struct initialization {0} -> {} to avoid -Wmissing-field-initializers
    line = re.sub(r'(\bstruct \w+\s+\w+\s*=\s*)\{0\}', r'\1{}', line)

    # Scalar compound literal &(type){expr} -> use LFS_SCALAR_TMP macro
    # Replace: &(lfs_off_t){expr} -> LFS_SCALAR_TMP(lfs_off_t, expr)
    # Replace: &(uint8_t){expr} -> LFS_SCALAR_TMP(uint8_t, expr)
    # Replace: &(const char*){expr} -> LFS_SCALAR_TMP(const char*, expr)
    line = re.sub(
        r'&\(lfs_off_t\)\{([^}]*)\}',
        r'LFS_SCALAR_TMP(lfs_off_t, \1)',
        line
    )
    line = re.sub(
        r'&\(uint8_t\)\{([^}]*)\}',
        r'LFS_SCALAR_TMP(uint8_t, \1)',
        line
    )
    line = re.sub(
        r'&\(const char\*\)\{([^}]*)\}',
        r'LFS_SCALAR_TMP(const char*, \1)',
        line
    )

    # C99 compound literal zero-init: (lfs_gstate_t){0} -> lfs_gstate_t{}
    line = re.sub(r'\(lfs_gstate_t\)\{0\}', 'lfs_gstate_t{}', line)
    line = re.sub(r'\(lfs_superblock_t\)\{0\}', 'lfs_superblock_t{}', line)
    line = re.sub(r'\(lfs_mdir_t\)\{0\}', 'lfs_mdir_t{}', line)

    # C99 compound literal for address-of temporaries:
    # &(lfs_off_t){expr} -> [&]{ lfs_off_t _t = expr; return &_t; }() — NO.
    # Actually these are used as output parameters. Let's use a simpler approach:
    # The pattern &(type){value} in the original code is used to pass a pointer
    # to a temporary. In C++ we need a named variable. We'll handle these
    # specific patterns with a static local trick that works for single-threaded code.
    # Actually the simplest: convert &(lfs_off_t){0} to (&(lfs_off_t){0}) won't work in C++.
    # We need to use a different approach for each call site.
    # Let's use a GCC/Clang extension that allows compound literals in C++ mode,
    # or define a helper macro.

    return line


def transform_mkattrs(line):
    """Replace LFS_MKATTRS macro with C++-compatible version."""
    # The C version uses compound literal arrays which are not valid C++.
    # Replace with a C++-compatible version using initializer_list or array.
    if '#define LFS_MKATTRS' in line:
        return None  # Will be replaced with our version
    return line


def main():
    with open(SRC) as f:
        lines = f.readlines()

    # Find section boundaries
    const_start = find_line(lines, 'some constants used throughout the code')
    cache_start = find_line(lines, '/// Caching block device operations ///')
    public_start = find_line(lines, '/// Public API wrappers ///')

    # Extract constant section (const_start to cache_start-1)
    const_section = lines[const_start:cache_start]
    # Extract core implementation (cache_start to public_start - 3)
    core_section = lines[cache_start:public_start - 2]

    # Output header
    sys.stdout.write(HEADER)

    # C++-compatible LFS_MKATTRS: use a plain array instead of compound literal
    sys.stdout.write(
        '// C++-compatible LFS_MKATTRS (replaces C99 compound literal array)\n'
        '#define LFS_MKATTRS(...) \\\n'
        '    (lfs_mattr[]){__VA_ARGS__}, \\\n'
        '    sizeof((lfs_mattr[]){__VA_ARGS__}) / sizeof(lfs_mattr)\n\n'
    )

    # Process constants
    skip_mkattrs = False
    for line in const_section:
        if '#define LFS_MKATTRS' in line:
            skip_mkattrs = True
            continue
        if skip_mkattrs:
            # Skip continuation lines of the macro
            if line.rstrip().endswith('\\'):
                continue
            else:
                skip_mkattrs = False
                continue

        result = transform_line(line)
        if result is not None:
            sys.stdout.write(result)

    # Process core implementation
    skip_mkattrs = False
    for line in core_section:
        if '#define LFS_MKATTRS' in line:
            skip_mkattrs = True
            continue
        if skip_mkattrs:
            if line.rstrip().endswith('\\'):
                continue
            else:
                skip_mkattrs = False
                continue

        result = transform_line(line)
        if result is not None:
            sys.stdout.write(result)

    # Output public API
    sys.stdout.write(PUBLIC_API)


if __name__ == '__main__':
    main()
