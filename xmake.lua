set_project("umi")
-- Version source of truth: git tags (vX.Y.Z).
-- This value is updated automatically by `xmake release --ver=X.Y.Z`.
set_version("0.2.0")
set_xmakever("2.8.0")

set_languages("c++23")
add_rules("mode.debug", "mode.release", "embedded.compdb")
add_rules("embedded.vscode", {target = "umirtm_example_stm32f4_disco"})
set_warnings("all", "extra", "error")

includes("lib/umihal")
includes("lib/umiport")
includes("lib/umimmio")
includes("lib/umitest")
includes("lib/umidevice")
includes("lib/umibench")
includes("lib/umirtm")

includes("tools/release.lua")
includes("tools/dev-sync.lua")
