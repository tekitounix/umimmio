-- =====================================================================
-- UMI Filesystem Tests
-- =====================================================================

local test_dir = os.scriptdir()
local umifs_dir = path.directory(test_dir)
local umi_dir = path.directory(umifs_dir)
local lib_dir = path.directory(umi_dir)
local root_dir = path.directory(lib_dir)

-- =====================================================================
-- Host unit tests
-- =====================================================================

target("test_fs_fat")
    add_rules("host.test")
    add_tests("default")
    set_default(true)
    add_deps("umi.fs.fatfs")
    add_files(path.join(test_dir, "test_fat.cc"))
    add_deps("umitest")
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("test_fs_slim")
    add_rules("host.test")
    set_group("tests/fs")
    set_default(true)
    add_deps("umi.fs.slimfs")
    add_files(path.join(test_dir, "test_slim.cc"))
    add_deps("umitest")
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- =====================================================================
-- Comparison tests (C++23 port vs reference C implementation) — legacy
-- =====================================================================

target("test_fs_fat_compare")
    add_rules("host.test")
    set_group("tests/fs")
    set_default(false)
    add_deps("umi.fs.fatfs")
    add_files(path.join(test_dir, "test_fat_compare.cc"))
    -- Reference FATfs C implementation
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_deps("umitest")
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    -- Suppress warnings in reference C code
    add_cflags(
        "-Wno-missing-field-initializers",
        "-Wno-unused-function",
        "-Wno-sign-compare",
        "-Wno-implicit-fallthrough",
        {force = true})
target_end()

-- =====================================================================
-- Unified benchmark tests
-- =====================================================================

-- Unified benchmark: lfs(ref), fat(cr), slim — 3 FS side-by-side
target("test_fs_bench")
    add_rules("host.test")
    set_default(false)
    add_deps("umi.fs.fatfs", "umi.fs.slimfs")
    add_files(path.join(test_dir, "test_bench.cc"))
    -- Reference littlefs C implementation
    add_files(path.join(root_dir, ".refs/littlefs/lfs.c"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs_util.c"))
    add_includedirs(path.join(root_dir, ".refs/littlefs"))
    add_deps("umitest")
    add_includedirs(test_dir)
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    add_cflags(
        "-Wno-missing-field-initializers",
        "-Wno-unused-function",
        "-Wno-sign-compare",
        {force = true})
target_end()

-- FATfs reference C benchmark (separate binary due to global symbol conflicts)
target("test_fs_bench_fat_ref")
    add_rules("host.test")
    set_default(false)
    add_deps("umi.fs.fatfs")
    add_files(path.join(test_dir, "test_bench_fat_ref.cc"))
    -- Reference FATfs C implementation
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_deps("umitest")
    add_includedirs(test_dir)
    add_includedirs(lib_dir)
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    add_cflags(
        "-Wno-missing-field-initializers",
        "-Wno-unused-function",
        "-Wno-sign-compare",
        "-Wno-implicit-fallthrough",
        {force = true})
target_end()

-- =====================================================================
-- Renode embedded tests (Cortex-M4 STM32F407)
-- =====================================================================

local stm32f4_linker = path.join(umi_dir, "port/mcu/stm32f4/linker.ld")
local stm32f4_syscalls = path.join(umi_dir, "port/mcu/stm32f4/syscalls.cc")

target("renode_fs_test")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "renode_fs_test.cc"))
    add_files(path.join(umifs_dir, "fat/fat_core.cc"))
    add_files(path.join(umifs_dir, "fat/ff_unicode.cc"))
    add_files(path.join(umifs_dir, "slim/slim_core.cc"))
    add_includedirs(path.join(umifs_dir, "fat"))
    add_includedirs(lib_dir)
    add_cxxflags(
        "-Wno-address-of-temporary",
        "-Wno-c99-designator",
        "-Wno-c++11-narrowing",
        "-Wno-missing-field-initializers",
        "-Wno-reserved-user-defined-literal",
        "-Wno-unused-function",
        {force = true})
    on_run(function(target)
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"--console", "--disable-xwt", "-e",
            "include @" .. path.join(test_dir, "fs_test.resc")})
    end)
target_end()

-- =====================================================================
-- Binary size comparison targets (ARM Cortex-M4, -Oz)
-- Each links exactly one FS implementation for isolated size measurement.
-- Usage: xmake build size_fs_fat ... then arm-none-eabi-size
-- =====================================================================

local size_cxxflags = {
    "-Wno-address-of-temporary",
    "-Wno-c99-designator",
    "-Wno-c++11-narrowing",
    "-Wno-missing-field-initializers",
    "-Wno-reserved-user-defined-literal",
    "-Wno-unused-function",
    "-Wno-unused-variable",
}
local size_cflags = {
    "-Wno-missing-field-initializers",
    "-Wno-unused-function",
    "-Wno-sign-compare",
    "-Wno-implicit-fallthrough",
    "-Wno-unused-parameter",
    "-Wno-old-style-definition",
}

-- FATfs cleanroom
target("size_fs_fat")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_fat.cc"))
    add_files(path.join(umifs_dir, "fat/fat_core.cc"))
    add_files(path.join(umifs_dir, "fat/ff_unicode.cc"))
    add_includedirs(lib_dir)
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- slimfs
target("size_fs_slim")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_slim.cc"))
    add_files(path.join(umifs_dir, "slim/slim_core.cc"))
    add_includedirs(lib_dir)
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- littlefs reference C
target("size_fs_lfs_ref")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_defines("LFS_NO_DEBUG", "LFS_NO_WARN", "LFS_NO_ERROR", "LFS_NO_ASSERT", "LFS_NO_MALLOC")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_lfs_ref.cc"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs.c"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs_util.c"))
    add_includedirs(path.join(root_dir, ".refs/littlefs"))
    add_includedirs(lib_dir)
    add_cflags(table.unpack(size_cflags))
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- FATfs reference C
target("size_fs_fat_ref")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "size_fat_ref.cc"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(test_dir)   -- for ffconf.h
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_includedirs(lib_dir)
    add_cflags(table.unpack(size_cflags))
    add_cxxflags(table.unpack(size_cxxflags))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("renode_fs_test_ref")
    set_group("firmware")
    set_default(false)
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", stm32f4_linker)
    set_values("embedded.optimize", "size")
    add_deps("umi.embedded.full")
    add_defines("STM32F4", "BOARD_STM32F4")
    add_defines("LFS_NO_DEBUG", "LFS_NO_WARN", "LFS_NO_ERROR", "LFS_NO_ASSERT")
    add_files(stm32f4_syscalls)
    add_files(path.join(test_dir, "renode_fs_test_ref.cc"))
    -- Reference C littlefs
    add_files(path.join(root_dir, ".refs/littlefs/lfs.c"))
    add_files(path.join(root_dir, ".refs/littlefs/lfs_util.c"))
    add_includedirs(path.join(root_dir, ".refs/littlefs"))
    -- Reference C FATfs (with custom ffconf.h from test/)
    add_files(path.join(root_dir, ".refs/fatfs/source/ff.c"))
    add_files(path.join(root_dir, ".refs/fatfs/source/ffunicode.c"))
    add_includedirs(test_dir)   -- for ffconf.h
    add_includedirs(path.join(root_dir, ".refs/fatfs/source"))
    add_includedirs(lib_dir)
    add_cflags(
        "-Wno-unused-parameter",
        "-Wno-sign-compare",
        "-Wno-implicit-fallthrough",
        "-Wno-missing-field-initializers",
        "-Wno-old-style-definition",
        "-Wno-unused-function",
        {force = true})
    add_cxxflags(
        "-Wno-address-of-temporary",
        "-Wno-c99-designator",
        "-Wno-c++11-narrowing",
        "-Wno-missing-field-initializers",
        "-Wno-reserved-user-defined-literal",
        "-Wno-unused-function",
        {force = true})
target_end()

-- =====================================================================
-- Filesystem check: tests + benchmarks + size comparison
-- =====================================================================

task("fs-check")
    set_category("action")
    on_run(function ()
        import("core.project.project")
        local sep = string.rep("=", 70)

        -- ---------------------------------------------------------------
        -- 1. Host unit tests
        -- ---------------------------------------------------------------
        local tests = {"test_fs_fat", "test_fs_slim"}
        for _, name in ipairs(tests) do
            os.exec("xmake build " .. name)
        end
        print("\n" .. sep)
        print("  1. Host Unit Tests")
        print(sep .. "\n")
        local failed = {}
        for _, name in ipairs(tests) do
            print(">>> " .. name)
            local ok = os.execv("xmake", {"run", name}, {try = true})
            if ok ~= 0 then table.insert(failed, name) end
            print("")
        end
        if #failed > 0 then
            print("FAILED: " .. table.concat(failed, ", "))
            os.exit(1)
        end
        print("All unit tests passed!\n")

        -- ---------------------------------------------------------------
        -- 2. Renode benchmarks (slim + fat_cr, then lfs_ref + fat_ref)
        -- ---------------------------------------------------------------
        print(sep)
        print("  2. Renode Benchmarks (DWT cycles, ARM Cortex-M4)")
        print(sep .. "\n")

        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then renode = "renode" end
        local resc = path.join(test_dir, "fs_test.resc")
        local log = "build/renode_fs_uart.log"

        -- slim + fat(cr)
        os.exec("xmake build renode_fs_test")
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @" .. resc})
        local log_cr = ""
        if os.isfile(log) then
            log_cr = io.readfile(log)
        end

        -- lfs(ref) + fat(ref)
        os.exec("xmake build renode_fs_test_ref")
        -- Generate .resc pointing to ref ELF
        local resc_ref = "build/fs_test_ref.resc"
        local resc_content = io.readfile(resc)
        resc_content = resc_content:gsub(
            "renode_fs_test/release/renode_fs_test%.elf",
            "renode_fs_test_ref/release/renode_fs_test_ref.elf")
        io.writefile(resc_ref, resc_content)
        os.execv(renode, {"--console", "--disable-xwt", "-e", "include @" .. resc_ref})
        local log_ref = ""
        if os.isfile(log) then
            log_ref = io.readfile(log)
        end

        -- Print both results
        if #log_cr > 0 then
            print("\n--- fat(cr) + slim ---")
            print(log_cr)
        end
        if #log_ref > 0 then
            print("--- lfs(ref) + fat(ref) ---")
            print(log_ref)
        end

        -- ---------------------------------------------------------------
        -- 3. ARM binary size comparison
        -- ---------------------------------------------------------------
        print("\n" .. sep)
        print("  3. ARM Binary Size Comparison (Cortex-M4, -Oz)")
        print(sep .. "\n")

        local size_targets = {"size_fs_slim", "size_fs_lfs_ref", "size_fs_fat", "size_fs_fat_ref"}
        for _, name in ipairs(size_targets) do
            os.exec("xmake build " .. name)
        end

        local size_cmd = "arm-none-eabi-size"
        local labels = {
            size_fs_slim    = "slim",
            size_fs_lfs_ref = "lfs(ref)",
            size_fs_fat     = "fat(cr)",
            size_fs_fat_ref = "fat(ref)",
        }
        -- Print header
        printf("  %-10s %8s %8s %8s %8s\n", "", ".text", ".data", ".bss", "total")
        printf("  %-10s %8s %8s %8s %8s\n", "----------", "--------", "--------", "--------", "--------")
        for _, name in ipairs(size_targets) do
            local target = project.target(name)
            local elf = target:targetfile()
            local output = os.iorunv(size_cmd, {elf})
            -- Parse second line: text data bss dec hex filename
            for line in output:gmatch("[^\n]+") do
                local t, d, b, dec = line:match("^%s*(%d+)%s+(%d+)%s+(%d+)%s+(%d+)")
                if t then
                    printf("  %-10s %8s %8s %8s %8s\n", labels[name], t, d, b, dec)
                end
            end
        end

        -- ---------------------------------------------------------------
        -- Summary
        -- ---------------------------------------------------------------
        print("\n" .. sep)
        print("  fs-check complete")
        print(sep)
    end)
    set_menu {usage = "xmake fs-check", description = "Run FS tests, Renode benchmarks, and ARM size comparison"}
task_end()
