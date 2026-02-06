target("umirtm")
    set_kind("headeronly")
    add_headerfiles("include/(umirtm/**.hh)")
    add_includedirs("include", { public = true })

includes("test")
