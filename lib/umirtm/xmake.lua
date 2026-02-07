local standalone_repo = os.projectdir() == os.scriptdir()
UMIRTM_STANDALONE_REPO = standalone_repo

if standalone_repo then
    set_project("umirtm")
    set_version("0.1.0")
    set_xmakever("2.8.0")

    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")

    add_requires("arm-embedded", {optional = true})
    add_requires("umitest", {optional = true})
    add_requires("umimmio", {optional = true})
end

function umirtm_add_umimmio_dep()
    if standalone_repo then
        add_packages("umimmio")
    else
        add_deps("umimmio")
    end
end

function umirtm_add_umitest_dep()
    if standalone_repo then
        add_packages("umitest")
    else
        add_deps("umitest")
    end
end

target("umirtm")
    set_kind("headeronly")
    add_headerfiles("include/(umirtm/**.hh)")
    add_includedirs("include", { public = true })

-- Host tests
includes("tests")

-- WASM target
includes("platforms/wasm")

-- Embedded targets (STM32F4 with Renode)
includes("platforms/arm/cortex-m/stm32f4")
