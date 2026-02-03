add_rules("mode.debug", "mode.release")

local target_name = "umi_test"
target(target_name)
    set_kind("static")
    add_rules("coding.umi_library")
    
    add_includedirs(".", {public = true})
    add_includedirs("include", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("include/*.hh")
    
target_end()
