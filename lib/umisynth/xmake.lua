-- UMI Synth Library
-- Common synthesizer implementation shared between embedded and WASM builds

target("umi.synth")
    set_kind("headeronly")
    add_headerfiles("include/*.hh")
    add_includedirs("include", {public = true})
    add_deps("umi.dsp", {public = true})
