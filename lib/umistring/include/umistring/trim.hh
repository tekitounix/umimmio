// SPDX-License-Identifier: MIT
#pragma once

#include "string.hh"

namespace umistring {

/// 前後の空白を削除
[[nodiscard]] inline constexpr StringView trim(StringView sv) noexcept {
    size_t start = 0;
    while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\n' || sv[start] == '\r')) {
        ++start;
    }

    size_t end = sv.size();
    while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\n' || sv[end - 1] == '\r')) {
        --end;
    }

    return sv.substr(start, end - start);
}

} // namespace umistring
