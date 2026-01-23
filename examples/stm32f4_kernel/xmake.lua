-- STM32F4-Discovery Kernel
-- Separate kernel binary that loads .umiapp applications
-- Uses embedded rule from arm-embedded package

target("stm32f4_kernel")
    set_group("firmware")
    set_default(false)
    
    -- Use embedded rule from arm-embedded package
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", path.join(os.scriptdir(), "kernel.ld"))
    set_values("embedded.optimize", "size")  -- -Os for kernel
    
    -- Source files
    add_files("src/*.cc")
    add_files("$(projectdir)/lib/bsp/stm32f4-disco/syscalls.cc")
    add_files("$(projectdir)/lib/umios/kernel/loader.cc")
    add_files("$(projectdir)/lib/umios/backend/cm/common/irq.cc")
    
    -- Dependencies
    add_deps("umi.embedded.full")
    
    -- Include paths
    add_includedirs("src")
    add_includedirs("$(projectdir)/lib/umios/kernel")
    
    -- Defines
    add_defines("UMIOS_KERNEL=1")
    add_defines("STM32F4", "BOARD_STM32F4")
    
    -- Note: embedded rule handles .bin/.hex/.map generation automatically
