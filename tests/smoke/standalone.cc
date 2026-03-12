// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Smoke test — verify all public headers compile standalone.
/// @author Shota Moriguchi @tekitounix

#include <umimmio/mmio.hh>             // IWYU pragma: keep
#include <umimmio/ops.hh>              // IWYU pragma: keep
#include <umimmio/policy.hh>           // IWYU pragma: keep
#include <umimmio/region.hh>           // IWYU pragma: keep
#include <umimmio/transport/detail.hh> // IWYU pragma: keep
#include <umimmio/transport/direct.hh> // IWYU pragma: keep
#include <umimmio/transport/i2c.hh>    // IWYU pragma: keep
#include <umimmio/transport/spi.hh>    // IWYU pragma: keep
