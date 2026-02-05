target("umistring")
    set_kind("headeronly")
    add_headerfiles("include/(umistring/**.hh)")
    add_includedirs("include", { public = true })
