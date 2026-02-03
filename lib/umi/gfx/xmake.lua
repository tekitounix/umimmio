add_rules("mode.debug", "mode.release")

local target_name = "umi_gfx"
target(target_name)
    set_kind("static")
    add_rules("coding.umi_library")
    
    add_includedirs(".", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("*.js")
    add_headerfiles("skin/**")
    
target_end()
