# UMI-OS Structure

single process
fixed multi task

## umios lib
- rt-kernel
  - scheduler
  - notify
  - wait_block
  - context switch
- system service
  - loader
    - sign validator
  - updater
    - relocator
    - crc validator
  - file system
  - shell
  - diagnostics
  - 
  - midi
    - parser
      - realtime
      - sysex
        - stdio
  - audio
  - driver
- memory management
  - mpu
  - heap/stack monitor
  - fault handle
- security
  - crypto
    - sha256
    - sha512
    - ed25519

## umios structure
- task
  - audio[0]
    - audio device (usb, sai)
    - audio process
  - system[1]
    - service
      - loader
      - updater
      - shell
      - file system
      - driver
        - usb
        - i2s
  - control[2]
    - control process
  - idle[3]
    - wfi
- memory
  - layout
  - mpu
  - heap/stack
  - fault
  - shared
- port
  - isr
  - systick
  - timer

## umios abi
- syscall
  - exit[0]
  - yield[1]
  - wait_event[2]
  - get_time[3]
  - get_shared[4]
  - register_proc[5]
  - unregister_proc[6]
  - fopen[32]
  - fread[33]
  - fwrite[34]
  - fclose[35]
  - fseek[36]
  - fstat[37]
  - diropen[38]
  - dirread[39]
  - dirclose[40]
- shared memory
  - header
    - version
    - size
    - flags
  - payload
    - audio context
    - control state

## app format
- .umia
  - header
    - crc
    - sign
  - payload
    - entry
  
## RT-Kernel Port
- init_systick
- init_dwt
- get_dwt_cycles
- yield
- wfi
- request_ctx_switch
- delay
- tcb
- init_task
- start_scheduler
- svc_callback (svc_handler)
- ctx_switch_callback (pendsv_handler)
- tick_callback (systick_handler)
  
- enable_fpu
- enable_dwt

## OS Port
- exception

## BSP
- set_timer_absolute
- monotic_time_us
- enter_critical
- exit_critical
- trigger_ipi
- current_core
- request_ctx_switch
- save_fpu
- restore_fpu
- mute_audio
- write_backup
- read_backup
- configure_mpu
- system_reset
- wait_for_event
- wait_for_interrupt
- start_fast_task
- watchdog_init
- watchdog_feed
- get_cycle_count
- cycles_per_us
- get_unique_id
- on_systick

## UMI-OS API
### Coroutine Runtime

## UMI-BOOT
- auth
- loader
- updater
- 

## UMI-OS Startup

- early_init
- init_bss
- call_ctors
- init_vectors
- 

## UMI-OS App Startup

## C/C++ Port (syscalls.cc)
- _end
- _estack
- _end

- _sbrk
- _write
  - umistdio_write
- _read
  - umistdio_read
- _close
- _fstat
- _lseek
- _getpid
- _kill
- _isatty
- _exit
- _gettimeofday
- _abort
- __assert_func
- __cxa_pure_virtual
- __cxa_atexit
- __dso_handle
- __stack_chk_fail
- __aeabi_unwind_cpp_pr0

## UMIDI

## UMIDSP

## UMI-Shell

## UMI-FileSystem

## UMI-USB

## UMI-UI

## UMI-Event