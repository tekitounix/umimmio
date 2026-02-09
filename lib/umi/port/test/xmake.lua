-- =====================================================================
-- UMI-Port Tests
-- =====================================================================
-- NOTE: Concept tests moved to lib/umihal/tests/
--       Driver tests moved to lib/umidevice/tests/

local test_dir = os.scriptdir()
local umiport_dir = path.directory(test_dir)
local lib_dir = path.directory(umiport_dir)
local root_dir = path.directory(lib_dir)

-- =====================================================================
-- Host unit tests
-- =====================================================================

target("test_port_hal_h7")
    add_rules("host.test")
    add_tests("default")
    set_group("tests/port")
    set_default(true)
    add_files(path.join(test_dir, "test_hal_stm32h7.cc"))
    add_includedirs(path.join(umiport_dir, "mcu/stm32h7"))
    add_deps("umitest", "umimmio")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

target("test_port_audio")
    add_rules("host.test")
    add_tests("default")
    set_group("tests/port")
    set_default(true)
    add_files(path.join(test_dir, "test_audio_driver.cc"))
    add_deps("umitest", "umihal")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
