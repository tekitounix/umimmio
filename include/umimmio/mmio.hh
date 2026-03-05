#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file mmio.hh
/// @brief UMI Memory-mapped I/O library — unified header.
/// @author Shota Moriguchi @tekitounix

#include "ops.hh"                   // IWYU pragma: export
#include "protected.hh"             // IWYU pragma: export
#include "transport/bitbang_i2c.hh" // IWYU pragma: export
#include "transport/bitbang_spi.hh" // IWYU pragma: export
#include "transport/direct.hh"      // IWYU pragma: export
#include "transport/i2c.hh"         // IWYU pragma: export
#include "transport/spi.hh"         // IWYU pragma: export
