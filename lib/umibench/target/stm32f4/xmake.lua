-- STM32F4 Renode Test Target for umibench

target("umibench_stm32f4_renode")
    set_kind("binary")
    set_group("firmware/umibench")
    add_rules("embedded")
    
    -- MCU Configuration
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "clang-arm")
    
    -- Source files
    add_files("startup.cc")
    add_files("test_renode.cc")  -- TODO: Create test file
    
    -- Linker script
    set_values("embedded.linker_script", path.join(os.scriptdir(), "linker.ld"))
    
    -- Dependencies
    add_deps("umibench_embedded")
    
    -- Include paths
    add_includedirs("../..", {public = false})
    
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
