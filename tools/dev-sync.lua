-- Sync local arm-embedded package source directly to ~/.xmake (bypasses package cache).
-- Use this during active development of xmake-repo/synthernet for instant feedback.
-- For release/CI, use `xmake require --force arm-embedded` instead.
--
-- Dynamic scan: walks source directories so new files are automatically included.
-- Only the rule_name_map needs updating if a rule's installed name differs from source.
-- Usage: xmake dev-sync

task("dev-sync")
    set_category("plugin")
    on_run(function ()
        import("core.base.global")

        local pkg_root = path.join(os.projectdir(), "xmake-repo", "synthernet",
                                   "packages", "a", "arm-embedded")
        if not os.isdir(pkg_root) then
            raise("Package source not found: %s", pkg_root)
        end

        local dest_root = global.directory()
        local total = 0

        -- Source rule name → installed rule name mapping
        local rule_name_map = {
            vscode = "embedded.vscode",
            compdb = "embedded.compdb"
        }

        -- Helper: recursively copy all files under src_dir to dest_dir
        local function sync_tree(src_dir, dest_dir)
            if not os.isdir(src_dir) then return 0 end
            local count = 0
            for _, f in ipairs(os.files(path.join(src_dir, "**"))) do
                local rel = path.relative(f, src_dir)
                local dest_file = path.join(dest_dir, rel)
                os.mkdir(path.directory(dest_file))
                io.writefile(dest_file, io.readfile(f))
                count = count + 1
            end
            return count
        end

        -- 0. Remove known legacy directories from previous versions.
        -- These were removed or consolidated in the current package structure.
        -- Keep this list until all developer environments have been updated.
        local legacy = {
            rules  = {"embedded.debugger"},                                     -- merged into embedded rule
            plugins = {"debug", "debugger", "deploy", "emulator", "project", "serve"}  -- unused/redundant
        }
        for _, name in ipairs(legacy.rules) do
            local d = path.join(dest_root, "rules", name)
            if os.isdir(d) then os.rmdir(d) end
        end
        for _, name in ipairs(legacy.plugins) do
            local d = path.join(dest_root, "plugins", name)
            if os.isdir(d) then os.rmdir(d) end
        end

        -- 1. Rules: clean destination then copy from source
        local rules_src = path.join(pkg_root, "rules")
        if os.isdir(rules_src) then
            for _, dir in ipairs(os.dirs(path.join(rules_src, "*"))) do
                local rule_name = path.filename(dir)
                local dest_name = rule_name_map[rule_name] or rule_name
                local dest_dir = path.join(dest_root, "rules", dest_name)
                if os.isdir(dest_dir) then
                    os.rmdir(dest_dir)
                end
                total = total + sync_tree(dir, dest_dir)
            end
        end

        -- 2. Plugins: clean destination then copy from source
        local plugins_src = path.join(pkg_root, "plugins")
        if os.isdir(plugins_src) then
            for _, dir in ipairs(os.dirs(path.join(plugins_src, "*"))) do
                local plugin_name = path.filename(dir)
                local dest_dir = path.join(dest_root, "plugins", plugin_name)
                if os.isdir(dest_dir) then
                    os.rmdir(dest_dir)
                end
                total = total + sync_tree(dir, dest_dir)
            end
        end

        print("dev-sync: %d files synced from xmake-repo/synthernet to %s", total, dest_root)
    end)
    set_menu {
        usage = "xmake dev-sync",
        description = "Sync local arm-embedded source to ~/.xmake (dev only)"
    }
