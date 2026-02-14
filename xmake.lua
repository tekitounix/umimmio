set_project("umi")
set_version("0.3.0")   -- クリーンスレート: 0.2.0 → 0.3.0
set_xmakever("2.8.0")

set_languages("c++23")
add_rules("mode.debug", "mode.release")
set_warnings("all", "extra", "error")

-- Phase 1 で順次追加
-- includes("lib/umitest")
-- includes("lib/umibench")
-- includes("lib/umirtm")
-- includes("lib/umimmio")
-- includes("lib/umicore")
-- includes("lib/umihal")

includes("tools/release.lua")
includes("tools/dev-sync.lua")
