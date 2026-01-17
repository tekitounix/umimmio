-- =====================================================================
-- Headless Web Host - UMI-OS Web Simulation
-- =====================================================================
-- A web-based simulation host for headless embedded applications.
-- Supports multiple backends:
--   - WASM: Direct WASM compilation (fastest, no server)
--   - Renode: Cycle-accurate hardware simulation (requires server)
--
-- Build:
--   cd examples/headless_webhost && xmake build
--   xmake run serve
--
-- Or from root:
--   xmake build headless_webhost
-- =====================================================================

set_project("headless_webhost")
set_version("0.1.0")
set_xmakever("2.8.0")

-- =====================================================================
-- Configuration
-- =====================================================================

set_languages("c++23")
add_rules("mode.debug", "mode.release")

-- Check for Emscripten
local has_emscripten = os.getenv("EMSDK") ~= nil
    or os.isfile("/opt/homebrew/bin/emcc")
    or os.isfile("/usr/local/bin/emcc")
    or os.isfile("/usr/bin/emcc")

if not has_emscripten then
    print("Warning: Emscripten not found. WASM targets will not be available.")
    print("Install: brew install emscripten (macOS) or visit https://emscripten.org")
end

-- =====================================================================
-- Include paths (relative to this xmake.lua)
-- =====================================================================

local project_root = path.absolute("../..")
local include_dirs = {
    path.join(project_root, "lib/umios/core"),
    path.join(project_root, "lib/umios/kernel"),
    path.join(project_root, "lib/umios/backend/wasm"),
    path.join(project_root, "lib/umidsp/include"),
    path.join(project_root, "lib"),
    "src"
}

-- =====================================================================
-- WASM Target
-- =====================================================================

if has_emscripten then

local exported_funcs = "[" .. table.concat({
    "'_malloc'", "'_free'",
    -- Simulation API
    "'_umi_sim_init'", "'_umi_sim_reset'", "'_umi_sim_process'",
    "'_umi_sim_note_on'", "'_umi_sim_note_off'", "'_umi_sim_cc'", "'_umi_sim_midi'",
    "'_umi_sim_load'", "'_umi_sim_position_lo'", "'_umi_sim_position_hi'",
    "'_umi_sim_get_name'", "'_umi_sim_get_vendor'", "'_umi_sim_get_version'",
    -- UMIM-compatible API
    "'_umi_create'", "'_umi_destroy'", "'_umi_process'",
    "'_umi_note_on'", "'_umi_note_off'",
    "'_umi_get_processor_name'", "'_umi_get_name'", "'_umi_get_vendor'", "'_umi_get_version'",
    "'_umi_get_type'", "'_umi_get_param_count'", "'_umi_set_param'", "'_umi_get_param'",
    "'_umi_get_param_name'", "'_umi_get_param_min'", "'_umi_get_param_max'",
    "'_umi_get_param_default'", "'_umi_get_param_curve'", "'_umi_get_param_id'",
    "'_umi_get_param_unit'", "'_umi_process_cc'"
}, ",") .. "]"

target("webhost_sim")
    set_kind("binary")
    set_default(true)
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_targetdir("build")
    set_filename("webhost_sim.js")

    add_files("src/synth_sim.cc")
    add_includedirs(include_dirs)

    add_cxflags("-fno-exceptions", "-fno-rtti", "-O3", {force = true})
    add_ldflags("-sWASM=1", "-sALLOW_MEMORY_GROWTH=0", {force = true})
    add_ldflags("-sSTACK_SIZE=65536", "-sINITIAL_MEMORY=1048576", {force = true})
    add_ldflags("-sEXPORTED_FUNCTIONS=" .. exported_funcs, {force = true})
    add_ldflags("-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPF32','HEAP8']", {force = true})
    add_ldflags("-sENVIRONMENT=web,worker", "-sWASM_ASYNC_COMPILATION=1", {force = true})
    add_ldflags("-sMODULARIZE=1", "-sEXPORT_NAME='createWebhostModule'", {force = true})
target_end()

end  -- if has_emscripten

-- =====================================================================
-- Tasks
-- =====================================================================

task("serve")
    set_category("action")
    on_run(function ()
        print("Building WASM module...")
        os.exec("xmake build webhost_sim")

        -- Copy WASM files to web directory
        os.cp("build/webhost_sim.js", "web/")
        os.cp("build/webhost_sim.wasm", "web/")

        print("\nStarting local server...")
        print("Open: http://localhost:8080/")
        os.exec("cd web && python3 -m http.server 8080")
    end)
    set_menu {usage = "xmake run serve", description = "Build and serve web host"}
task_end()

task("clean-all")
    set_category("action")
    on_run(function ()
        os.tryrm("build")
        os.tryrm(".xmake")
        os.tryrm("web/webhost_sim.js")
        os.tryrm("web/webhost_sim.wasm")
        print("Cleaned build artifacts")
    end)
    set_menu {usage = "xmake clean-all", description = "Remove all build artifacts"}
task_end()
