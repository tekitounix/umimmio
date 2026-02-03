add_rules("mode.debug", "mode.release")

target("umi_service")
    set_kind("static")
    add_includedirs(".", {public = true})
    
    -- Source files
    add_files("loader/*.cc")
    add_headerfiles("loader/*.hh")
    add_headerfiles("shell/*.hh")
    add_headerfiles("audio/*.hh")
    add_headerfiles("midi/*.hh")
    add_headerfiles("storage/*.hh")
    
    -- Dependencies
    add_deps("umi_core", "umi_runtime", "umi_fs")
target_end()
