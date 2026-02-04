-- SPDX-License-Identifier: MIT
-- UMI bench framework

local bench_dir = os.scriptdir()
local include_dir = path.join(bench_dir, "include")
local stm32f4_linker = path.join(bench_dir, "target/stm32f4/linker.ld")

-- Header-only framework (include/bench/...)
target("umi.bench")
    set_kind("headeronly")
    set_group("umi")
    add_includedirs(include_dir, {public = true})
target_end()

includes("test")

-- =============================================================================
-- Examples (STM32F4 embedded targets)
-- =============================================================================

local function stm32f4_example(name, opts)
    opts = opts or {}
    target(name)
        set_group("bench_examples")
        set_default(false)
        add_rules("embedded")
        set_values("embedded.mcu", "stm32f407vg")
        set_values("embedded.linker_script", stm32f4_linker)
        if opts.optimize then
            set_values("embedded.optimize", opts.optimize)
        end
        add_includedirs(include_dir)
        add_files(path.join(bench_dir, "target/stm32f4/startup.cc"))
        add_files(opts.source)
        if opts.renode_script then
            on_run(function(target)
                local renode = "/Applications/Renode.app/Contents/MacOS/Renode"
                if not os.isfile(renode) then
                    renode = "renode"
                end
                os.execv(renode, {"--console", "--disable-xwt", "-e", "include @" .. opts.renode_script})
            end)
        end
    target_end()
end

stm32f4_example("bench_instruction_stm32f4", {
    source = path.join(bench_dir, "examples/instruction_bench_stm32f4.cc"),
    optimize = "fast",
    renode_script = "lib/umi/bench/target/stm32f4/renode/bench_stm32f4.resc"
})

