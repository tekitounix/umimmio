-- =====================================================================
-- UMI Filesystem Library (umifs)
-- =====================================================================
-- Usage:
--   includes("lib/umifs")
--   target("my_app")
--       add_deps("umi.fs.littlefs")  -- littlefs C++23 port
--       add_deps("umi.fs.fatfs")     -- FATfs C++23 port
-- =====================================================================

local umifs_dir = os.scriptdir()
local lib_dir = path.directory(umifs_dir)

-- =====================================================================
-- littlefs C++23 port (static library)
-- =====================================================================

target("umi.fs.littlefs")
    set_kind("static")
    set_group("umi")
    add_deps("umi.kernel")

    add_files(path.join(umifs_dir, "little/lfs_core.cc"))
    add_includedirs(path.join(umifs_dir, "little"), {public = true})
    add_includedirs(lib_dir, {public = true})
    -- Suppress warnings for C99 compound literals used in the faithful C port
    add_cxxflags(
        "-Wno-address-of-temporary",
        "-Wno-c++11-narrowing",
        "-Wno-missing-field-initializers",
        "-Wno-reserved-user-defined-literal",
        "-Wno-unused-function",
        {force = true})
target_end()

-- =====================================================================
-- FATfs C++23 port (static library)
-- =====================================================================

target("umi.fs.fatfs")
    set_kind("static")
    set_group("umi")
    add_deps("umi.kernel")

    add_files(path.join(umifs_dir, "fat/fat_core.cc"))
    add_files(path.join(umifs_dir, "fat/ff_unicode.cc"))
    add_includedirs(path.join(umifs_dir, "fat"), {public = true})
    add_includedirs(lib_dir, {public = true})
target_end()
