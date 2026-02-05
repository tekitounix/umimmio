target("umibench_common")
    set_kind("headeronly")
    add_headerfiles("include/(umibench/**.hh)")
    add_includedirs("include", { public = true })

target("umibench_host")
    set_kind("headeronly")
    add_deps("umibench_common")
    add_defines("UMIBENCH_HOST")

target("umibench_embedded")
    set_kind("headeronly")
    add_deps("umibench_common")
    add_defines("UMIBENCH_EMBEDDED")

-- Host tests
includes("test")

-- Embedded targets (STM32F4 with Renode)
includes("target/stm32f4")
