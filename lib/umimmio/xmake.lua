target("umimmio")
    set_kind("headeronly")
    add_headerfiles("include/(umimmio/**.hh)")
    add_includedirs("include", { public = true })
