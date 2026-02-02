// SPDX-License-Identifier: MIT
// UMI-OS Syscall Numbers — Kernel-side aliases
// Canonical definitions are in core/syscall_nr.hh.
// This header re-exports them with sys_* aliases for backward compatibility.

#pragma once

#include "../../core/syscall_nr.hh"

namespace umi::syscall {

// Kernel-side sys_* aliases (thin wrappers over nr::*)
inline constexpr uint8_t sys_exit             = static_cast<uint8_t>(nr::exit);
inline constexpr uint8_t sys_yield            = static_cast<uint8_t>(nr::yield);
inline constexpr uint8_t sys_register_proc    = static_cast<uint8_t>(nr::register_proc);
inline constexpr uint8_t sys_unregister_proc  = static_cast<uint8_t>(nr::unregister_proc);

inline constexpr uint8_t sys_wait_event       = static_cast<uint8_t>(nr::wait_event);
inline constexpr uint8_t sys_get_time         = static_cast<uint8_t>(nr::get_time);
inline constexpr uint8_t sys_sleep            = static_cast<uint8_t>(nr::sleep);

inline constexpr uint8_t sys_set_app_config     = static_cast<uint8_t>(nr::set_app_config);
inline constexpr uint8_t sys_set_route_table    = static_cast<uint8_t>(nr::set_route_table);
inline constexpr uint8_t sys_set_param_mapping  = static_cast<uint8_t>(nr::set_param_mapping);
inline constexpr uint8_t sys_set_input_mapping  = static_cast<uint8_t>(nr::set_input_mapping);
inline constexpr uint8_t sys_configure_input    = static_cast<uint8_t>(nr::configure_input);
inline constexpr uint8_t sys_send_param_request = static_cast<uint8_t>(nr::send_param_request);

inline constexpr uint8_t sys_get_shared       = static_cast<uint8_t>(nr::get_shared);

inline constexpr uint8_t sys_log              = static_cast<uint8_t>(nr::log);
inline constexpr uint8_t sys_panic            = static_cast<uint8_t>(nr::panic);

inline constexpr uint8_t sys_file_open        = static_cast<uint8_t>(nr::file_open);
inline constexpr uint8_t sys_file_read        = static_cast<uint8_t>(nr::file_read);
inline constexpr uint8_t sys_file_write       = static_cast<uint8_t>(nr::file_write);
inline constexpr uint8_t sys_file_close       = static_cast<uint8_t>(nr::file_close);
inline constexpr uint8_t sys_file_seek        = static_cast<uint8_t>(nr::file_seek);
inline constexpr uint8_t sys_file_tell        = static_cast<uint8_t>(nr::file_tell);
inline constexpr uint8_t sys_file_size        = static_cast<uint8_t>(nr::file_size);
inline constexpr uint8_t sys_file_truncate    = static_cast<uint8_t>(nr::file_truncate);
inline constexpr uint8_t sys_file_sync        = static_cast<uint8_t>(nr::file_sync);

inline constexpr uint8_t sys_dir_open         = static_cast<uint8_t>(nr::dir_open);
inline constexpr uint8_t sys_dir_read         = static_cast<uint8_t>(nr::dir_read);
inline constexpr uint8_t sys_dir_close        = static_cast<uint8_t>(nr::dir_close);
inline constexpr uint8_t sys_dir_seek         = static_cast<uint8_t>(nr::dir_seek);
inline constexpr uint8_t sys_dir_tell         = static_cast<uint8_t>(nr::dir_tell);

inline constexpr uint8_t sys_stat             = static_cast<uint8_t>(nr::stat);
inline constexpr uint8_t sys_fstat            = static_cast<uint8_t>(nr::fstat);
inline constexpr uint8_t sys_mkdir            = static_cast<uint8_t>(nr::mkdir);
inline constexpr uint8_t sys_remove           = static_cast<uint8_t>(nr::remove);
inline constexpr uint8_t sys_rename           = static_cast<uint8_t>(nr::rename);

inline constexpr uint8_t sys_getattr          = static_cast<uint8_t>(nr::getattr);
inline constexpr uint8_t sys_setattr          = static_cast<uint8_t>(nr::setattr);
inline constexpr uint8_t sys_removeattr       = static_cast<uint8_t>(nr::removeattr);

inline constexpr uint8_t sys_fs_stat          = static_cast<uint8_t>(nr::fs_stat);
inline constexpr uint8_t sys_fs_result        = static_cast<uint8_t>(nr::fs_result);

}  // namespace umi::syscall
