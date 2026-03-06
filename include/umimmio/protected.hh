#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file protected.hh
/// @brief Backward-compatibility redirect — use <umisync/protected.hh> directly.
/// @deprecated Include <umisync/protected.hh> instead. This header will be removed.
/// @author Shota Moriguchi @tekitounix

#include <umisync/protected.hh>

namespace umi::mmio {

// Backward-compatibility aliases — will be removed in a future release.
using umi::sync::Guard;
using umi::sync::MutexPolicy;
using umi::sync::NoLockPolicy;
using umi::sync::Protected;

} // namespace umi::mmio
