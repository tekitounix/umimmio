-- A standalone project uses the released test framework package.
-- An enclosing project supplies the test framework target.
local standalone = os.projectdir() == path.directory(os.scriptdir())
if standalone then
    add_repositories("synthernet https://github.com/tekitounix/synthernet-xmake-repo.git main")
    add_requires("umitest")
end

target("test_umimmio")
    set_kind("binary")
    set_default(false)
    add_files("test_*.cc")
    add_deps("umimmio")
    if standalone then
        add_packages("umitest")
    else
        add_deps("umitest")
    end
    add_tests("default")

    for _, f in ipairs(os.files(path.join(os.scriptdir(), "smoke", "*.cc"))) do
        local name = "smoke_" .. path.basename(f)
        add_tests(name, {files = path.join("smoke", path.filename(f)), build_should_pass = true})
    end

    for _, f in ipairs(os.files(path.join(os.scriptdir(), "compile_fail", "*.cc"))) do
        local name = "fail_" .. path.basename(f)
        add_tests(name, {files = path.join("compile_fail", path.filename(f)), build_should_fail = true})
    end

target_end()
