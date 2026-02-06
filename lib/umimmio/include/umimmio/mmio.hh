/// @file mmio.hh
/// @brief UMI Memory-mapped I/O library - unified header
/// @author Shota Moriguchi @tekitounix
/// @date 2025
/// @license MIT

#pragma once

#include "register.hh"              // IWYU pragma: export
#include "transport/bitbang_i2c.hh" // IWYU pragma: export
#include "transport/bitbang_spi.hh" // IWYU pragma: export
#include "transport/direct.hh"      // IWYU pragma: export
#include "transport/i2c.hh"         // IWYU pragma: export
#include "transport/spi.hh"         // IWYU pragma: export
