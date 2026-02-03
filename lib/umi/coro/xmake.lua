add_rules("mode.debug", "mode.release")

target("umi_coro")
    set_kind("static")
    add_includedirs(".", {public = true})
    add_headerfiles("*.hh")
    add_deps("umi_core")
target_end()
