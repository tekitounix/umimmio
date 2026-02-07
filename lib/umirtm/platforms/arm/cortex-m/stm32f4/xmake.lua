-- STM32F4 Renode Test Targets for umirtm

-- umiport shared platform infrastructure
local umiport_stm32f4 = path.join(os.scriptdir(), "../../../../../umiport/src/stm32f4")
local umiport_include = path.join(os.scriptdir(), "../../../../../umiport/include")

-- Clang-ARM version
target("umirtm_stm32f4_renode")
    set_kind("binary")
    set_default(false)
    add_rules("embedded")

    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "clang-arm")

    add_files(path.join(umiport_stm32f4, "startup.cc"))
    add_files(path.join(umiport_stm32f4, "syscalls.cc"))
    add_files("../../../../tests/test_*.cc")

    set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))

    add_deps("umirtm")
    umirtm_add_umitest_dep()
    umirtm_add_umimmio_dep()

    add_includedirs(os.scriptdir(), {public = false})
    add_includedirs(umiport_include, {public = false})

    on_run(function(target)
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then
            renode = "renode"
        end
        local resc = path.join(os.scriptdir(), "renode", "umirtm_stm32f4.resc")
        os.execv(renode, {"--console", "--disable-xwt", resc})
    end)
target_end()

-- GCC-ARM version
target("umirtm_stm32f4_renode_gcc")
    set_kind("binary")
    set_default(false)
    add_rules("embedded")

    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "gcc-arm")

    add_files(path.join(umiport_stm32f4, "startup.cc"))
    add_files(path.join(umiport_stm32f4, "syscalls.cc"))
    add_files("../../../../tests/test_*.cc")

    set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))

    add_deps("umirtm")
    umirtm_add_umitest_dep()
    umirtm_add_umimmio_dep()

    add_includedirs(os.scriptdir(), {public = false})
    add_includedirs(umiport_include, {public = false})
target_end()
