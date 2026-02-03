-- =====================================================================
-- UMI-Port: Hardware Abstraction Layer
-- =====================================================================
-- Layer structure (each subdirectory is an include root):
--   concepts/    - C++23 Concept contracts (always included)
--   common/      - Cortex-M common (NVIC, SCB, SysTick, DWT, etc.)
--   arch/<arch>/ - CPU core (cm4, cm7) context switch, cache, FPU
--   mcu/<mcu>/   - SoC peripherals (stm32f4, stm32h7)
--   board/<brd>/ - Board-level drivers (stm32f4_disco, daisy_seed)
--   platform/<p>/ - Execution environment (embedded, wasm)
-- =====================================================================

add_rules("mode.debug", "mode.release")

target("umi_port")
    set_kind("static")
    add_includedirs(".", {public = true})

    -- Core include directories (always included)
    add_includedirs("concepts", {public = true})
    add_includedirs("common", {public = true})

    -- Architecture-specific (Cortex-M4)
    add_includedirs("arch/cm4", {public = true})

    -- MCU-specific (STM32F4)
    add_includedirs("mcu/stm32f4", {public = true})

    -- Board-specific (STM32F4 Discovery)
    add_includedirs("board/stm32f4_disco", {public = true})

    -- Platform-specific (embedded)
    add_includedirs("platform/embedded", {public = true})

    -- Source files
    add_files("arch/cm4/**/*.cc")
    add_files("common/**/*.cc")
    add_files("mcu/stm32f4/**/*.cc")

    -- Header files
    add_headerfiles("concepts/**/*.hh")
    add_headerfiles("common/**/*.hh")
    add_headerfiles("arch/**/*.hh")
    add_headerfiles("mcu/**/*.hh")
    add_headerfiles("board/**/*.hh")
    add_headerfiles("device/**/*.hh")
    add_headerfiles("platform/**/*.hh")

target_end()
