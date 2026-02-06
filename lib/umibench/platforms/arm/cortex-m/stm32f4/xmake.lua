-- STM32F4 Renode Test Target for umibench

-- Clang-ARM version
target("umibench_stm32f4_renode")
    set_kind("binary")
    set_default(false)
    add_rules("embedded")
    
    -- MCU Configuration
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "clang-arm")
    
    -- Source files
    add_files("startup.cc")
    add_files("syscalls.cc")
    add_files("../../../../tests/test_*.cc")
    
    -- Linker script
    set_values("embedded.linker_script", path.join(os.scriptdir(), "linker.ld"))
    
    -- Dependencies
    add_deps("umibench_embedded")
    add_deps("umitest")
    
    -- Include paths (public API + Cortex-M shared + STM32F4 board)
    add_includedirs("..", {public = false})
    add_includedirs(os.scriptdir(), {public = false})
    
    -- Renode run task
    on_run(function(target)
        import("core.base.option")
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
        if not os.isfile(renode) then
            renode = "renode"
        end
        local resc = path.join(os.scriptdir(), "renode", "bench_stm32f4.resc")
        os.execv(renode, {"--console", "--disable-xwt", resc})
    end)
target_end()

-- GCC-ARM version
target("umibench_stm32f4_renode_gcc")
    set_kind("binary")
    set_default(false)
    add_rules("embedded")
    
    -- MCU Configuration
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "gcc-arm")
    
    -- Source files
    add_files("startup.cc")
    add_files("syscalls.cc")
    add_files("../../../../tests/test_*.cc")
    
    -- Linker script
    set_values("embedded.linker_script", path.join(os.scriptdir(), "linker.ld"))
    
    -- Dependencies
    add_deps("umibench_embedded")
    add_deps("umitest")
    
    -- Include paths (public API + Cortex-M shared + STM32F4 board)
    add_includedirs("..", {public = false})
    add_includedirs(os.scriptdir(), {public = false})
target_end()
