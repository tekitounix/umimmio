// SPDX-License-Identifier: FatFs
// Copyright (C) 2025, ChaN, all right reserved.
// C++23 port for UMI framework — configuration (replaces ffconf.h)

#pragma once

#include <cstdint>

namespace umi::fs::fat::config {

// ============================================================================
// Function Configurations
// ============================================================================

inline constexpr int FS_READONLY = 0;       ///< 0: Read/Write, 1: Read-only
inline constexpr int FS_MINIMIZE = 0;       ///< 0: Full API
inline constexpr int USE_FIND = 0;          ///< 0: Disable f_findfirst/f_findnext
inline constexpr int USE_MKFS = 0;          ///< 0: Disable f_mkfs
inline constexpr int USE_FASTSEEK = 0;      ///< 0: Disable fast seek
inline constexpr int USE_EXPAND = 0;        ///< 0: Disable f_expand
inline constexpr int USE_CHMOD = 0;         ///< 0: Disable f_chmod/f_utime
inline constexpr int USE_LABEL = 0;         ///< 0: Disable f_getlabel/f_setlabel
inline constexpr int USE_FORWARD = 0;       ///< 0: Disable f_forward
inline constexpr int USE_STRFUNC = 0;       ///< 0: Disable string functions

// ============================================================================
// Locale and Namespace
// ============================================================================

inline constexpr uint16_t CODE_PAGE = 437;  ///< US ASCII
inline constexpr int USE_LFN = 1;           ///< 1: LFN with static BSS buffer
inline constexpr int MAX_LFN = 255;         ///< Max LFN length in UTF-16 code units
inline constexpr int LFN_UNICODE = 0;       ///< 0: ANSI/OEM (TCHAR = char)
inline constexpr int LFN_BUF = 255;
inline constexpr int SFN_BUF = 12;
inline constexpr int FS_RPATH = 0;          ///< 0: Disable relative path

// ============================================================================
// Drive/Volume
// ============================================================================

inline constexpr int VOLUMES = 1;           ///< Single volume (SD card)
inline constexpr int STR_VOLUME_ID = 0;     ///< 0: No string volume IDs
inline constexpr int MULTI_PARTITION = 0;   ///< 0: Single partition per drive
inline constexpr uint32_t MIN_SS = 512;
inline constexpr uint32_t MAX_SS = 512;     ///< Fixed sector size
inline constexpr int LBA64 = 0;             ///< 0: 32-bit LBA
inline constexpr int USE_TRIM = 0;          ///< 0: No TRIM

// ============================================================================
// System
// ============================================================================

inline constexpr int FS_TINY = 0;           ///< 0: Normal buffer configuration
inline constexpr int FS_EXFAT = 0;          ///< 0: No exFAT
inline constexpr int FS_NORTC = 1;          ///< 1: No RTC — fixed timestamp
inline constexpr int NORTC_MON = 1;
inline constexpr int NORTC_MDAY = 1;
inline constexpr int NORTC_YEAR = 2025;
inline constexpr int FS_NOFSINFO = 0;
inline constexpr int FS_LOCK = 0;           ///< 0: No file lock
inline constexpr int FS_REENTRANT = 0;      ///< 0: Not reentrant

} // namespace umi::fs::fat::config
