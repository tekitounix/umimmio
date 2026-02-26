-- Host tests
target("test_umimmio")
    add_rules("host.test")
    add_tests("default")
    set_default(true)
    add_files("test_main.cc", "test_register_field.cc", "test_transport.cc", "test_access_policy.cc", "test_spi_bitbang.cc")
    add_deps("umimmio", "umiport_host")
    umimmio_add_umitest_dep()

target("test_umimmio_compile_fail")
    set_kind("phony")
    set_default(false)
    add_tests("write_ro", "read_wo")

    on_test(function()
        import("lib.detect.find_tool")

        local cxx = find_tool("c++") or find_tool("g++") or find_tool("clang++")
        assert(cxx and cxx.program, "no host C++ compiler found for compile-fail test")

        local include_dir = path.join(os.scriptdir(), "..", "include")
        local umitest_include = path.join(os.scriptdir(), "..", "..", "umitest", "include")

        local test_cases = {"write_ro", "read_wo"}
        for _, name in ipairs(test_cases) do
            local source = path.join(os.scriptdir(), "compile_fail", name .. ".cc")
            local object = os.tmpfile() .. ".o"

            local ok = false
            try {
                function()
                    os.iorunv(cxx.program, {"-std=c++23", "-I" .. include_dir, "-I" .. umitest_include, "-c", source, "-o", object})
                    ok = true
                end,
                catch {
                    function() end
                }
            }

            os.tryrm(object)

            if ok then
                raise("compile-fail test '%s' failed: compiled successfully", name)
            end
        end

        return true
    end)

-- STM32F4 Renode ARM targets (requires umiport — Phase 2+)
if os.isdir(path.join(os.projectdir(), "lib", "umiport")) then
    target("umimmio_stm32f4_renode")
        set_kind("binary")
        set_default(false)
        add_rules("embedded", "umiport.board")

        set_values("embedded.mcu", "stm32f407vg")
        set_values("embedded.optimize", "size")
        set_values("embedded.toolchain", "clang-arm")
        set_values("umiport.board", "stm32f4-renode")

        add_files("test_*.cc")

        add_deps("umimmio", "umiport", "umiport_stm32f4_renode")
        umimmio_add_umitest_dep()
    target_end()

    target("umimmio_stm32f4_renode_gcc")
        set_kind("binary")
        set_default(false)
        add_rules("embedded", "umiport.board")

        set_values("embedded.mcu", "stm32f407vg")
        set_values("embedded.optimize", "size")
        set_values("embedded.toolchain", "gcc-arm")
        set_values("umiport.board", "stm32f4-renode")

        add_files("test_*.cc")

        add_deps("umimmio", "umiport", "umiport_stm32f4_renode")
        umimmio_add_umitest_dep()
    target_end()
end
