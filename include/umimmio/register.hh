#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file register.hh
/// @brief UMI Memory-mapped I/O register abstractions — umbrella header.
///
/// Includes the complete umimmio type system:
///   - policy.hh:  Access policies, transport tags, error policies, bit constants
///   - region.hh:  Device, Register, Field, Value types with concepts
///   - ops.hh:     RegOps, ByteAdapter — type-safe register operations
///
/// Existing code can continue to `#include <umimmio/register.hh>`.
/// New code may include individual headers for finer granularity.
///
/// @author Shota Moriguchi @tekitounix

#include "ops.hh" // IWYU pragma: export
