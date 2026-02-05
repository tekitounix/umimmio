// SPDX-License-Identifier: MIT
#pragma once

#include <array>

#include "string.hh"

namespace umistring {

constexpr size_t max_split_parts = 16;

/// 文字列分割結果
struct SplitResult {
    std::array<StringView, max_split_parts> parts;
    size_t count = 0;
};

/// 文字列を分割
inline constexpr SplitResult split(StringView sv, char delimiter) noexcept {
    SplitResult result;

    size_t start = 0;
    for (size_t i = 0; i < sv.size() && result.count < max_split_parts; ++i) {
        if (sv[i] == delimiter) {
            result.parts[result.count++] = sv.substr(start, i - start);
            start = i + 1;
        }
    }

    // 最後の部分
    if (start < sv.size() && result.count < max_split_parts) {
        result.parts[result.count++] = sv.substr(start, sv.size() - start);
    }

    return result;
}

} // namespace umistring
