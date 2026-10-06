#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief UMI Memory-mapped I/O library — unified header.
/// @author Shota Moriguchi @tekitounix

#include "ops.hh"                     // IWYU pragma: export
#include "policy.hh"                  // IWYU pragma: export
#include "region.hh"                  // IWYU pragma: export
#include "transport/atomic.hh"        // IWYU pragma: export
#include "transport/atomic_direct.hh" // IWYU pragma: export
#include "transport/csr.hh"           // IWYU pragma: export
#include "transport/direct.hh"        // IWYU pragma: export
#include "transport/i2c.hh"           // IWYU pragma: export
#include "transport/spi.hh"           // IWYU pragma: export
