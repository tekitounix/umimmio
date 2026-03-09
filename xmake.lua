target("umimmio")
    set_kind("headeronly")
    set_license("MIT")
    add_headerfiles("include/(umimmio/**.hh)")
    add_includedirs("include", {public = true})

includes("tests")
