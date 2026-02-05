target("umibench")
    set_kind("headeronly")
    add_headerfiles("include/(umibench/**.hh)")
    add_includedirs("include", { public = true })

-- Host tests
includes("test")

-- Embedded targets (STM32F4 with Renode)
includes("target/stm32f4")
