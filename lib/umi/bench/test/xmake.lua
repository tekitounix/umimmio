-- SPDX-License-Identifier: MIT
-- bench library tests

target("test_bench")
    add_rules("host.test")
    set_default(true)
    add_files("test_bench.cc")
    add_deps("umi.test")
    add_includedirs("$(projectdir)/lib/umi/bench/include")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
