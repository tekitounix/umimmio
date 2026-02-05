// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace umistring {

/// 整数を文字列に変換（10進数）
/// @return 書き込んだ文字数
inline constexpr size_t to_string(int32_t value, char* buf, size_t buf_size) noexcept {
    if (buf_size == 0)
        return 0;

    size_t pos = 0;

    // 負数処理
    if (value < 0) {
        if (pos < buf_size)
            buf[pos++] = '-';
        value = -value;
    }

    // 数字を逆順に書く
    char temp[12];
    size_t temp_pos = 0;

    do {
        temp[temp_pos++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0 && temp_pos < 11);

    // 正順にコピー
    while (temp_pos > 0 && pos < buf_size) {
        buf[pos++] = temp[--temp_pos];
    }

    // 終端ヌル文字
    if (pos < buf_size)
        buf[pos] = '\0';

    return pos;
}

} // namespace umistring
