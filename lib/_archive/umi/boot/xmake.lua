add_rules("mode.debug", "mode.release")

local target_name = "umi.boot"
target(target_name)
    set_kind("static")
    
    add_includedirs(".", {public = true})
    add_includedirs("include", {public = true})
    
    add_headerfiles("*.hh")
    add_headerfiles("include/*.hh")
    
    add_deps("umi.crypto")
    
target_end()

-- umiboot library tests
for _, test in ipairs({
    {"umiboot_test_auth", "test/test_auth.cc"},
    {"umiboot_test_firmware", "test/test_firmware.cc"},
    {"umiboot_test_session", "test/test_session.cc"},
}) do
    target(test[1])
        add_rules("host.test")
        add_tests("default")
        set_group("tests/umiboot")
        add_deps("umi.boot", "umitest")
        add_files(test[2])
        add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
    target_end()
end
