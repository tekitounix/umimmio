-- Release configuration for UMI libraries
--
-- [project]  : project-wide settings
-- [libs]     : per-library release settings
--   publish    : include in release (version bump, archive, tag)
--   headeronly : true for header-only libs, false if src/ must be packaged
--
-- [packages] : xmake-repo package generation metadata
--   description  : package description
--   main_header  : primary header for compile test (e.g., "umibench/bench.hh")
--   install_dirs : directories to install (default: {"include"})
--   deps         : package dependencies (default: {})
--   test_init    : extra code inside test function (default: "")
--   configs      : custom xmake package configs (raw lua string, optional)
--   on_load      : custom on_load body (raw lua string, optional)
--   test_mode    : "snippet" (default) or "file"
--   test_files   : for test_mode="file", list of files to check
--
-- To add a new library: add an entry to [libs] and [packages], ensure lib/<name>/ exists.
-- To exclude from release: set publish = false.
{
    project = {
        -- directory containing library subdirectories (relative to project root)
        lib_dir = "lib",
        -- output directory for archives (relative to project root)
        output_dir = "build/packages",
        -- xmake-repo path (relative to project root)
        xmake_repo = "xmake-repo/synthernet/packages",
        -- git commit message format (%s is replaced with version)
        commit_message = "release: v%s",
        -- files to include in archive (VERSION/LICENSE auto-injected if missing)
        archive_files = {"VERSION", "LICENSE", "README.md"},
        -- directories to include in archive (if they exist in each library)
        archive_dirs = {"include", "platforms", "src", "renode"}
    },
    libs = {
        umitest = {
            publish = true,
            headeronly = true
        },
        umimmio = {
            publish = true,
            headeronly = true
        },
        umirtm = {
            publish = true,
            headeronly = true
        },
        umibench = {
            publish = true,
            headeronly = true
        },
        umiport = {
            publish = false,
            headeronly = false
        }
    },
    packages = {
        umitest = {
            description = "UMI zero-macro lightweight test framework for C++23",
            main_header = "umitest/test.hh",
            test_init = 'umi::test::Suite s("pkg_test");'
        },
        umimmio = {
            description = "UMI type-safe memory-mapped I/O abstraction library",
            main_header = "umimmio/mmio.hh"
        },
        umirtm = {
            description = "UMI SEGGER RTT-compatible monitor library",
            main_header = "umirtm/rtm.hh"
        },
        umibench = {
            description = "UMI cross-target micro-benchmark library",
            main_header = "umibench/bench.hh",
            install_dirs = {"include", "platforms"},
            configs = [[
    add_configs("backend", {
        description = "Target backend",
        default = "host",
        values = {"host", "wasm", "embedded"}
    })]],
            on_load = [[
        if package:config("backend") == "embedded" then
            package:add("deps", "arm-embedded")
            package:add("deps", "umimmio")
        end]]
        },
        umiport = {
            description = "UMI shared platform infrastructure (STM32F4 startup, linker, UART)",
            main_header = nil,
            install_dirs = {"include", "src", "renode"},
            deps = {"umimmio"},
            test_mode = "file",
            test_files = {"src/stm32f4/startup.cc", "renode/stm32f4_test.repl"}
        }
    }
}
