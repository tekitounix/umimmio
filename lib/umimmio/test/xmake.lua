-- MMIO Library Tests

local test_dir = os.scriptdir()
local lib_dir = path.directory(path.directory(test_dir))

local function mmio_test(name, file)
    target(name)
        add_rules("host.test")
        set_default(false)
        add_deps("umi.mmio", "umitest")
        add_files(path.join(test_dir, file))
        add_cxxflags("-Wno-unused-variable", "-Wno-unused-but-set-variable", {force = true})
    target_end()
end

mmio_test("test_mmio", "test.cc")
-- Assert test: source file uses #undef NDEBUG to keep assert() active
mmio_test("test_mmio_assert", "test_assert.cc")
mmio_test("test_mmio_register_value", "test_register_value.cc")
mmio_test("test_mmio_value_get", "test_value_get.cc")

-- CS43L22 device driver test (needs umiport/device/ include path)
target("test_mmio_cs43l22")
    add_rules("host.test")
    set_default(false)
    add_deps("umi.mmio", "umitest")
    add_includedirs(path.join(lib_dir, "umiport/device"), {public = false})
    add_files(path.join(test_dir, "test_cs43l22.cc"))
    add_cxxflags("-Wno-unused-variable", "-Wno-unused-but-set-variable", {force = true})
target_end()
