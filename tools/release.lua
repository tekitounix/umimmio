-- UMI release task
-- Usage: xmake release --ver=0.3.0 [--libs umimmio,umitest] [--dry-run]
-- Config: release_config.lua (project root) controls which libraries are publishable

task("release")
    set_category("action")

    on_run(function()
        import("core.base.option")

        local version = option.get("ver")
        if not version then
            raise("--ver is required (e.g., xmake release --ver=0.3.0)")
        end
        if not version:match("^%d+%.%d+%.%d+$") then
            raise("version must be semver format: MAJOR.MINOR.PATCH (got: %s)", version)
        end

        local dry_run    = option.get("dry-run")
        local no_test    = option.get("no-test")
        local no_tag     = option.get("no-tag")
        local no_archive = option.get("no-archive")

        -- load release config
        local project_root = os.projectdir()
        local config_path = path.join(project_root, "release_config.lua")
        local config = io.load(config_path)
        if not config then
            raise("failed to load release config: %s", config_path)
        end

        local proj    = config.project or {}
        local lib_dir = proj.lib_dir or "lib"
        local out_dir = proj.output_dir or "build/packages"
        local repo_dir = proj.xmake_repo or "xmake-repo/synthernet/packages"
        local commit_fmt       = proj.commit_message or "release: v%s"
        local archive_files    = proj.archive_files or {"VERSION", "LICENSE", "README.md"}
        local archive_dirs     = proj.archive_dirs or {"include", "platforms", "src", "renode"}
        local lib_config       = config.libs or {}
        local pkg_config       = config.packages or {}

        -- resolve library list
        local libs
        local libs_opt = option.get("libs")
        if libs_opt then
            -- explicit --libs: validate each name exists in config
            libs = {}
            for name in libs_opt:gmatch("[^,]+") do
                name = name:match("^%s*(.-)%s*$") -- trim
                if not lib_config[name] then
                    raise("unknown library: %s (not in release_config.lua)", name)
                end
                table.insert(libs, name)
            end
        else
            -- default: all libraries with publish = true
            libs = {}
            local skipped = {}
            for name, entry in pairs(lib_config) do
                if entry.publish then
                    table.insert(libs, name)
                else
                    table.insert(skipped, name)
                end
            end
            table.sort(libs)
            table.sort(skipped)
            if #skipped > 0 then
                print("Skipping unpublished libraries: %s", table.concat(skipped, ", "))
            end
        end

        if #libs == 0 then
            raise("no libraries selected for release")
        end

        -- validate library directories exist
        for _, name in ipairs(libs) do
            local dir = path.join(project_root, lib_dir, name)
            if not os.isdir(dir) then
                raise("library not found: %s (expected at %s)", name, dir)
            end
        end

        local date = os.date("%Y-%m-%d")
        local prefix = dry_run and "[dry-run] " or ""
        local total_steps = 7

        print("%s=== Releasing v%s ===", prefix, version)
        print("%sLibraries: %s", prefix, table.concat(libs, ", "))
        print("")

        -- step 1: pre-checks (skip in dry-run)
        if not dry_run and not no_tag then
            print("Step 1/%d: Pre-checks...", total_steps)
            local ok1 = os.execv("git", {"diff", "--quiet"}, {try = true})
            local ok2 = os.execv("git", {"diff", "--cached", "--quiet"}, {try = true})
            if ok1 ~= 0 or ok2 ~= 0 then
                raise("uncommitted changes detected. Commit or stash first.")
            end
            print("  ok: working tree clean")
        else
            print("%sStep 1/%d: Pre-checks (skipped)", prefix, total_steps)
        end

        -- step 2: tests
        if not no_test then
            print("%sStep 2/%d: Running tests...", prefix, total_steps)
            if not dry_run then
                local ok = os.execv("xmake", {"test"}, {try = true})
                if ok ~= 0 then
                    raise("tests failed")
                end
            end
            print("  ok: tests passed")
        else
            print("%sStep 2/%d: Tests (skipped)", prefix, total_steps)
        end

        -- step 3: update version references
        print("%sStep 3/%d: Updating version references...", prefix, total_steps)

        -- root xmake.lua: set_version
        local root_xmake = path.join(project_root, "xmake.lua")
        local xmake_content = io.readfile(root_xmake)
        local new_xmake = xmake_content:gsub('set_version%(".-"%)', 'set_version("' .. version .. '")')
        if new_xmake ~= xmake_content then
            print("  %s%s (set_version)", prefix, root_xmake)
            if not dry_run then
                io.writefile(root_xmake, new_xmake)
            end
        end

        -- per-library Doxyfile
        for _, name in ipairs(libs) do
            local ldir = path.join(project_root, lib_dir, name)

            -- Doxyfile: PROJECT_NUMBER
            local doxyfile = path.join(ldir, "Doxyfile")
            if os.isfile(doxyfile) then
                local content = io.readfile(doxyfile)
                local updated = content:gsub("PROJECT_NUMBER%s*=%s*[^\n]+",
                    "PROJECT_NUMBER         = " .. version)
                if updated ~= content then
                    print("  %s%s (PROJECT_NUMBER)", prefix, doxyfile)
                    if not dry_run then
                        io.writefile(doxyfile, updated)
                    end
                end
            end
        end

        -- step 4: generate archives
        local sha_results = {}
        if not no_archive then
            print("%sStep 4/%d: Generating archives...", prefix, total_steps)
            local pkg_dir = path.join(project_root, out_dir)
            if not dry_run then
                os.mkdir(pkg_dir)
            end

            for _, name in ipairs(libs) do
                local ldir = path.join(project_root, lib_dir, name)
                local archive_name = name .. "-" .. version
                local staging_dir = path.join(pkg_dir, archive_name)
                local archive_path = path.join(pkg_dir, archive_name .. ".tar.gz")

                print("  %s%s", prefix, archive_path)

                if not dry_run then
                    -- clean staging
                    os.tryrm(staging_dir)
                    os.mkdir(staging_dir)

                    -- copy files (with root fallback for LICENSE, dynamic generation for VERSION)
                    for _, f in ipairs(archive_files) do
                        local src = path.join(ldir, f)
                        if os.isfile(src) then
                            os.cp(src, path.join(staging_dir, f))
                        elseif f == "LICENSE" then
                            local root_src = path.join(project_root, "LICENSE")
                            if os.isfile(root_src) then
                                os.cp(root_src, path.join(staging_dir, f))
                            end
                        elseif f == "VERSION" then
                            io.writefile(path.join(staging_dir, "VERSION"), version .. "\n")
                        end
                    end

                    -- copy directories
                    for _, d in ipairs(archive_dirs) do
                        local src = path.join(ldir, d)
                        if os.isdir(src) then
                            os.cp(src, path.join(staging_dir, d))
                        end
                    end

                    -- create tar.gz with relative paths
                    local old_dir = os.cd(pkg_dir)
                    os.execv("tar", {"czf", archive_name .. ".tar.gz", archive_name})
                    os.cd(old_dir)

                    -- compute sha256
                    local sha = hash.sha256(archive_path)
                    sha_results[name] = sha

                    -- write sha256 file
                    io.writefile(archive_path .. ".sha256", sha .. "  " .. archive_name .. ".tar.gz\n")

                    -- clean staging
                    os.tryrm(staging_dir)
                end
            end
        else
            print("%sStep 4/%d: Archives (skipped)", prefix, total_steps)
        end

        -- step 5: update xmake-repo package definitions
        print("%sStep 5/%d: Updating xmake-repo package definitions...", prefix, total_steps)

        local homepage = "https://github.com/tekitounix/umi"

        -- generate package xmake.lua from config
        local function generate_package_lua(name, pkg)
            local lines = {}
            table.insert(lines, string.format('package("%s")', name))
            table.insert(lines, string.format('    set_homepage("%s")', homepage))
            table.insert(lines, string.format('    set_description("%s")', pkg.description or ""))
            table.insert(lines, '    set_license("MIT")')
            table.insert(lines, "")

            local lentry = lib_config[name] or {}
            if lentry.headeronly then
                table.insert(lines, '    set_kind("library", {headeronly = true})')
            else
                table.insert(lines, '    set_kind("library", {headeronly = false})')
            end
            table.insert(lines, "")

            table.insert(lines, string.format(
                '    add_urls("https://github.com/tekitounix/umi/releases/download/v$(version)/%s-$(version).tar.gz")',
                name))

            -- dev version pointing to local
            -- count path segments: repo_dir + first_letter + name = segments + 2
            local _, slash_count = repo_dir:gsub("/", "")
            local depth = slash_count + 2
            local rel_prefix = string.rep("../", depth)
            table.insert(lines, string.format('    add_versions("dev", "git:%s%s/%s")', rel_prefix, lib_dir, name))
            table.insert(lines, "")

            -- custom configs
            if pkg.configs then
                table.insert(lines, pkg.configs)
                table.insert(lines, "")
            end

            -- deps
            if pkg.deps and #pkg.deps > 0 then
                for _, dep in ipairs(pkg.deps) do
                    table.insert(lines, string.format('    add_deps("%s")', dep))
                end
                table.insert(lines, "")
            end

            -- on_load
            if pkg.on_load then
                table.insert(lines, "    on_load(function(package)")
                table.insert(lines, pkg.on_load)
                table.insert(lines, "    end)")
                table.insert(lines, "")
            end

            -- on_install
            local install_dirs = pkg.install_dirs or {"include"}
            table.insert(lines, "    on_install(function(package)")
            for _, d in ipairs(install_dirs) do
                table.insert(lines, string.format('        os.cp("%s", package:installdir())', d))
            end
            table.insert(lines, "    end)")
            table.insert(lines, "")

            -- on_test
            local test_mode = pkg.test_mode or "snippet"
            table.insert(lines, "    on_test(function(package)")
            if test_mode == "file" then
                local test_files = pkg.test_files or {}
                for _, f in ipairs(test_files) do
                    table.insert(lines, string.format(
                        '        assert(os.isfile(path.join(package:installdir(), "%s")))', f))
                end
            else
                -- snippet test
                local header = pkg.main_header
                if header then
                    local init = pkg.test_init or ""
                    if init ~= "" then
                        init = "\n                " .. init .. "\n            "
                    end
                    table.insert(lines, string.format(
                        '        assert(package:check_cxxsnippets({test = [[\n'
                        .. '            #include <%s>\n'
                        .. '            void test() {%s}\n'
                        .. '        ]]}, {configs = {languages = "c++23"}}))',
                        header, init))
                end
            end
            table.insert(lines, "    end)")

            table.insert(lines, "package_end()")
            table.insert(lines, "")
            return table.concat(lines, "\n")
        end

        -- append add_versions line to existing package file
        local function append_version(pkg_xmake_path, name, ver, sha)
            local content = io.readfile(pkg_xmake_path)

            -- check if this version already exists
            local ver_pattern = string.format('add_versions%%("%s"', ver:gsub("%.", "%%."))
            if content:find(ver_pattern) then
                print("  %s (v%s already exists, skipping)", name, ver)
                return
            end

            -- insert after the last add_versions line
            local last_pos = nil
            local search_start = 1
            while true do
                local s = content:find("add_versions%(", search_start)
                if not s then break end
                -- find end of this line
                local line_end = content:find("\n", s)
                if line_end then
                    last_pos = line_end
                    search_start = line_end + 1
                else
                    last_pos = #content
                    break
                end
            end

            if last_pos then
                local new_line = string.format('    add_versions("%s", "%s")\n', ver, sha)
                local updated = content:sub(1, last_pos) .. new_line .. content:sub(last_pos + 1)
                io.writefile(pkg_xmake_path, updated)
                print("  %s: add_versions(\"%s\", \"%s\")", name, ver, sha)
            end
        end

        for _, name in ipairs(libs) do
            local pkg = pkg_config[name]
            if not pkg then
                print("  %s: no package config, skipping", name)
                goto continue_pkg
            end

            local first_letter = name:sub(1, 1)
            local pkg_dir_path = path.join(project_root, repo_dir, first_letter, name)
            local pkg_xmake = path.join(pkg_dir_path, "xmake.lua")

            -- generate/update package xmake.lua
            print("  %s%s", prefix, pkg_xmake)
            if not dry_run then
                os.mkdir(pkg_dir_path)
                local content = generate_package_lua(name, pkg)
                io.writefile(pkg_xmake, content)
            end

            -- append add_versions with SHA256 (only if archive was generated)
            local sha = sha_results[name]
            if sha and not dry_run then
                append_version(pkg_xmake, name, version, sha)
            elseif sha then
                print("  %s%s: add_versions(\"%s\", \"%s\")", prefix, name, version, sha)
            end

            ::continue_pkg::
        end

        -- step 6: git commit + tags
        local commit_msg = string.format(commit_fmt, version)
        if not no_tag then
            print("%sStep 6/%d: Git commit and tags...", prefix, total_steps)
            if not dry_run then
                os.execv("git", {"add", "-A"})
                os.execv("git", {"commit", "-m", commit_msg})
                os.execv("git", {"tag", "v" .. version})
                for _, name in ipairs(libs) do
                    local tag = name .. "/v" .. version
                    os.execv("git", {"tag", tag})
                    print("  tag: %s", tag)
                end
            else
                print("  %scommit: %s", prefix, commit_msg)
                print("  %stag: v%s", prefix, version)
                for _, name in ipairs(libs) do
                    print("  %stag: %s/v%s", prefix, name, version)
                end
            end
        else
            print("%sStep 6/%d: Git (skipped)", prefix, total_steps)
        end

        -- step 7: summary
        print("")
        print("%s=== Release v%s complete ===", prefix, version)
        print("")

        if not no_archive and not dry_run then
            print("Archives:")
            for _, name in ipairs(libs) do
                local archive_name = name .. "-" .. version .. ".tar.gz"
                print("  %s/%s", out_dir, archive_name)
            end
            print("")
            print("SHA256:")
            for _, name in ipairs(libs) do
                local sha = sha_results[name]
                if sha then
                    print("  %s: %s", name, sha)
                end
            end
            print("")
        end

        if not no_tag then
            if dry_run then
                print("Next steps (after real run):")
            else
                print("Next steps:")
            end
            print("  git push origin main --tags")
        end
    end)

    set_menu {
        usage = "xmake release --ver=0.3.0 [options]",
        description = "Release UMI libraries (version bump, archive, tag, package update)",
        options = {
            {nil, "ver",        "kv", nil, "Version to release (e.g., 0.3.0)"},
            {'l', "libs",       "kv", nil, "Comma-separated library list (default: all publishable)"},
            {'d', "dry-run",    "k",  nil, "Show changes without executing"},
            {nil, "no-test",    "k",  nil, "Skip test execution"},
            {nil, "no-tag",     "k",  nil, "Skip git commit and tag creation"},
            {nil, "no-archive", "k",  nil, "Skip archive generation"},
        }
    }
task_end()
