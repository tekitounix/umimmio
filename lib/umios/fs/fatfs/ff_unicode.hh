// SPDX-License-Identifier: FatFs
// Copyright (C) 2025, ChaN, all right reserved.
// C++23 port for UMI framework — Unicode conversion (CP437 only)

#pragma once

#include <cstdint>

namespace umi::fs::fat {

/// OEM code to Unicode conversion (CP437)
uint16_t ff_oem2uni(uint16_t oem, uint16_t cp);

/// Unicode to OEM code conversion (CP437)
uint16_t ff_uni2oem(uint32_t uni, uint16_t cp);

/// Unicode upper-case conversion
uint32_t ff_wtoupper(uint32_t uni);

} // namespace umi::fs::fat
