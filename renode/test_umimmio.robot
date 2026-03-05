*** Settings ***
Library           Renode
Suite Setup       Setup
Suite Teardown    Teardown
Test Timeout      30 seconds

*** Keywords ***
Setup
    Execute Command    mach create "umimmio_test"
    Execute Command    machine LoadPlatformDescription @lib/umimmio/renode/stm32f4_test.repl
    Execute Command    sysbus LoadELF @build/umimmio_stm32f4_renode/release/umimmio_stm32f4_renode.elf
    Execute Command    sysbus WriteDoubleWord 0xE000ED08 0x08000000

Teardown
    Execute Command    quit

*** Test Cases ***
MMIO Tests Complete On ARM
    [Documentation]    Verify all umimmio tests pass on Cortex-M4 (ARM execution)
    Execute Command    start
    Wait For Line On Uart    passed    usart2    timeout=15

MMIO Tests No Failures
    [Documentation]    Verify no test failures reported
    Execute Command    start
    Should Not Be Found On Uart    FAILED    usart2    timeout=15
