target("umimmio")
    set_kind("headeronly")
    set_license("MIT")
    add_headerfiles("include/(umimmio/**.hh)")
    add_includedirs("include", {public = true})

    set_values("publish", true)
    set_values("publish.description", "Type-safe memory-mapped I/O abstraction library")
    set_values("publish.remote", "umimmio-public")
    set_values("publish.main_header", "umimmio/mmio.hh")

includes("tests", "examples")
