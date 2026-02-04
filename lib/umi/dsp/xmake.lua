add_rules("mode.debug", "mode.release")

target("umi.dsp")
    set_kind("headeronly")
    add_includedirs("include", {public = true})

    -- Header files
    add_headerfiles("include/(**.hh)")

    -- Dependencies
    add_deps("umi.core")
target_end()

-- DSP test (using umitest framework)
target("test_dsp")
    add_rules("host.test")
    add_tests("default")
    set_group("tests/dsp")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files("test/test_dsp.cc")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- =====================================================================
-- Python Bindings (pybind11)
-- =====================================================================
-- TB-303 WaveShaper Python module
-- Build: xmake build tb303_waveshaper_py
-- Install: cp build/.../tb303_waveshaper.*.so docs/dsp/tb303/vco/test/

local has_python = os.getenv("PYTHON") ~= nil
    or os.isfile("/usr/bin/python3")
    or os.isfile("/opt/homebrew/bin/python3")
    or os.isfile("/usr/local/bin/python3")
    or os.which("python3") ~= nil

if has_python then

target("tb303_waveshaper_py")
    set_kind("shared")
    set_group("python")
    set_default(false)
    set_languages("c++17")
    add_files("../../../docs/dsp/tb303/vco/code/waveshaper_pybind.cpp")
    add_includedirs("../../../docs/dsp/tb303/vco/code")

    -- Python extension settings
    set_prefixname("")  -- Remove lib prefix

    on_load(function (target)
        local python = os.getenv("PYTHON") or "python3"

        -- Get Python extension suffix
        local suffix = os.iorun(python .. " -c \"import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))\"")
        if suffix then
            suffix = suffix:gsub("%s+", "")
            target:set("extension", suffix)
        else
            -- Fallback
            if is_plat("macosx") then
                target:set("extension", ".cpython-311-darwin.so")
            else
                target:set("extension", ".so")
            end
        end

        -- Get Python include path
        local includes = os.iorun(python .. " -c \"import sysconfig; print(sysconfig.get_path('include'))\"")
        if includes then
            includes = includes:gsub("%s+", "")
            target:add("includedirs", includes)
        end

        -- Get pybind11 include path (installed via pip)
        try {
            function()
                local pybind_inc = os.iorun(python .. " -c \"import pybind11; print(pybind11.get_include())\"")
                if pybind_inc then
                    pybind_inc = pybind_inc:gsub("%s+", "")
                    target:add("includedirs", pybind_inc)
                end
            end,
            catch {
                function(e)
                    -- pybind11 not installed, skip
                end
            }
        }
    end)

    -- Don't add Python library linkage on macOS (undefined dynamic lookup)
    if is_plat("macosx") then
        add_ldflags("-undefined", "dynamic_lookup", {force = true})
    end
target_end()

task("waveshaper-py")
    set_category("action")
    on_run(function ()
        print("Building TB-303 WaveShaper Python module...")
        os.exec("xmake build tb303_waveshaper_py")

        -- Find and copy the built module
        import("core.project.project")
        local target = project.target("tb303_waveshaper_py")
        local targetfile = target:targetfile()
        local destdir = "docs/dsp/tb303/vco/test"

        if os.isfile(targetfile) then
            os.cp(targetfile, destdir .. "/")
            print("\n" .. string.rep("=", 60))
            print("Python module built successfully!")
            print("Copied to: " .. destdir)
            print("\nUsage:")
            print("  cd " .. destdir)
            print("  python3 -c \"import tb303_waveshaper as ws; print(ws.V_T)\"")
            print(string.rep("=", 60))
        else
            print("ERROR: Build failed, target file not found: " .. targetfile)
        end
    end)
    set_menu {usage = "xmake waveshaper-py", description = "Build TB-303 WaveShaper Python module"}
task_end()

end  -- if has_python
