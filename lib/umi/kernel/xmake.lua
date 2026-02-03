add_rules("mode.debug", "mode.release")

target("umi_kernel")
    set_kind("static")
    add_includedirs(".", {public = true})

    -- Source files
    add_files("*.cc")

    -- Header files
    add_headerfiles("*.hh")
    add_headerfiles("syscall/*.hh")

    -- Dependencies
    add_deps("umi_core", "umi_runtime", "umi_port", "umi_service")
target_end()
