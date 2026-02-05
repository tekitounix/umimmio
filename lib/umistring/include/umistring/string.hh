// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace umistring {

/// 文字列ビュー
class StringView {
    const char* data_ = nullptr;
    size_t len_ = 0;

  public:
    constexpr StringView() noexcept = default;

    constexpr StringView(const char* data, size_t len) noexcept : data_(data), len_(len) {}

    constexpr StringView(const char* str) noexcept : data_(str), len_(0) {
        while (str && *str++)
            ++len_;
    }

    [[nodiscard]] constexpr const char* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return len_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return len_ == 0; }

    [[nodiscard]] constexpr char operator[](size_t i) const noexcept { return (i < len_) ? data_[i] : '\0'; }

    [[nodiscard]] constexpr StringView substr(size_t pos, size_t count) const noexcept {
        if (pos >= len_)
            return StringView();
        const auto actual_count = (pos + count > len_) ? (len_ - pos) : count;
        return StringView(data_ + pos, actual_count);
    }
};

} // namespace umistring
