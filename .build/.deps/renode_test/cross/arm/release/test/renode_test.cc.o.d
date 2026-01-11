{
    files = {
        "test/renode_test.cc"
    },
    depfiles = "renode_test.o: test/renode_test.cc test/../core/umi_kernel.hh  test/../core/umi_expected.hh test/../core/umi_monitor.hh  test/../port/arm/cortex-m/common/vector_table.hh  test/../port/arm/cortex-m/common/scb.hh test/../include/umi/types.hh  test/../include/umi/time.hh test/../include/umi/event.hh  test/../include/umi/audio_context.hh test/../include/umi/processor.hh  test/../include/umi/triple_buffer.hh test/../adapter/embedded/adapter.hh\
",
    depfiles_format = "gcc",
    values = {
        "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-g++",
        {
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O3",
            "-std=c++23",
            "-I.",
            "-Icore",
            "-Iport",
            "-Iinclude",
            "-DSTM32F4",
            "-DBOARD_STM32F4",
            "-Os",
            "-g",
            "-mcpu=cortex-m4",
            "-mthumb",
            "-mfloat-abi=hard",
            "-mfpu=fpv4-sp-d16",
            "-fno-exceptions",
            "-fno-rtti",
            "-ffunction-sections",
            "-fdata-sections",
            "-DNDEBUG"
        }
    }
}