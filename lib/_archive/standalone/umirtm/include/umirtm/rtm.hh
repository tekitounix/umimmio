// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Real-Time Monitor (RTM) ring-buffer transport for debug output.
/// @author Shota Moriguchi @tekitounix
///
/// Provides a compile-time configurable monitor with up/down ring buffers
/// compatible with SEGGER RTT protocol layout.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

namespace rt {

/// @brief Buffer operating mode when full.
enum class Mode : std::uint32_t {
    NoBlockSkip = 0,    ///< Drop entire write if buffer is full.
    NoBlockTrim = 1,    ///< Write as much as fits, discard excess.
    BlockIfFifoFull = 2 ///< Block until space is available.
};

/// @brief Static monitor class with configurable ring buffers for debug I/O.
/// @tparam UpBuffers   Number of host-readable (target→host) buffers.
/// @tparam DownBuffers Number of host-writable (host→target) buffers.
/// @tparam UpBufferSize   Size of each up buffer in bytes.
/// @tparam DownBufferSize Size of each down buffer in bytes.
/// @tparam DefaultMode    Default Mode for buffer overflow behaviour.
/// @note Not instantiable—all members are static.
template <std::size_t UpBuffers = 3,
          std::size_t DownBuffers = 3,
          std::size_t UpBufferSize = 1024,
          std::size_t DownBufferSize = 16,
          Mode DefaultMode = Mode::NoBlockSkip>
class Monitor {
    static_assert(UpBuffers > 0, "At least one up buffer required");
    static_assert(DownBuffers > 0, "At least one down buffer required");
    static_assert(UpBufferSize > 0, "Up buffer size must be positive");
    static_assert(DownBufferSize > 0, "Down buffer size must be positive");

  private:
    /// @brief RTM protocol per-buffer descriptor.
    struct rtm_buffer {
        const char* name;               ///< Buffer name pointer.
        char* buffer;                   ///< Data buffer pointer.
        unsigned size;                  ///< Buffer capacity in bytes.
        volatile unsigned write_offset; ///< Write position (producer).
        volatile unsigned read_offset;  ///< Read position (consumer).
        unsigned flags;                 ///< Mode flags.
    };

    /// @brief RTM protocol control block (discovered by host debugger).
    struct rtm_control_block_t {
        char id[16];                          ///< Magic ID string.
        int max_up_buffers;                   ///< Number of up buffers.
        int max_down_buffers;                 ///< Number of down buffers.
        rtm_buffer up_buffers[UpBuffers];     ///< Up (target→host) buffer array.
        rtm_buffer down_buffers[DownBuffers]; ///< Down (host→target) buffer array.
    };

    /// @brief Default buffer name for channel 0.
    static constexpr const char console_name[] = "Console";

    /// @brief Internal storage holding ring buffers and the control block.
    struct Storage {
        alignas(4) std::array<char, UpBufferSize> up_buffer{};
        alignas(4) std::array<char, DownBufferSize> down_buffer{};
        rtm_control_block_t cb{.id = {},
                               .max_up_buffers = UpBuffers,
                               .max_down_buffers = DownBuffers,
                               .up_buffers = {},
                               .down_buffers = {}};

        /// @brief Constexpr constructor that wires up buffer pointers (C++23).
        constexpr Storage() {
            cb.up_buffers[0] = {console_name, up_buffer.data(), UpBufferSize, 0, 0, std::to_underlying(DefaultMode)};
            cb.down_buffers[0] = {
                console_name, down_buffer.data(), DownBufferSize, 0, 0, std::to_underlying(DefaultMode)};
        }
    };

    inline static Storage storage{};

  public:
    /// @brief Deleted — Monitor is a static-only class.
    Monitor() = delete;

    /// @brief Get a pointer to the internal control block.
    /// @return Pointer to the rtm_control_block_t structure.
    /// @note Used by HostMonitor for shared memory export.
    static auto* get_control_block() noexcept { return &storage.cb; }

    /// @brief Get the size of the control block in bytes.
    static constexpr auto get_control_block_size() noexcept { return sizeof(rtm_control_block_t); }

    /// @brief Initialize the monitor with a control block ID.
    /// @param control_block_id ID string written into the control block (max 16 chars).
    /// @pre Must be called before any write/read operations.
    static void init(std::string_view control_block_id) noexcept {
        std::ranges::fill(storage.cb.id, '\0');
        auto id = control_block_id.substr(0, sizeof(storage.cb.id) - 1);
        std::ranges::copy(id, storage.cb.id);

        for (auto& buf : storage.cb.up_buffers) {
            buf.write_offset = 0;
            buf.read_offset = 0;
        }
        for (auto& buf : storage.cb.down_buffers) {
            buf.write_offset = 0;
            buf.read_offset = 0;
        }
    }

    /// @brief Write raw byte data to an up buffer.
    /// @tparam BufferIndex Target buffer index.
    /// @return Number of bytes actually written.
    template <std::size_t BufferIndex = 0>
    [[nodiscard]] static auto write(std::span<const std::byte> data) noexcept -> std::size_t {
        return write_up_buffer(BufferIndex, data);
    }

    /// @brief Write a string to an up buffer.
    /// @return Number of bytes actually written.
    template <std::size_t BufferIndex = 0>
    [[nodiscard]] static auto write(std::string_view str) noexcept -> std::size_t {
        return write(std::span{reinterpret_cast<const std::byte*>(str.data()), str.size()});
    }

    /// @brief Write a string, discarding the return value.
    template <std::size_t BufferIndex = 0>
    static void log(std::string_view str) noexcept {
        std::ignore = write<BufferIndex>(str);
    }

    /// @brief Read raw bytes from a down buffer.
    /// @return Number of bytes read.
    template <std::size_t BufferIndex = 0>
    static auto read(std::span<std::byte> buffer) noexcept -> std::size_t {
        return read_down_buffer(BufferIndex, buffer);
    }

    /// @brief Read a single byte from a down buffer.
    /// @return Byte value (0–255), or -1 if empty.
    template <std::size_t BufferIndex = 0>
    static auto read_byte() noexcept -> int {
        if constexpr (BufferIndex >= DownBuffers) {
            return -1;
        }

        auto& buffer = storage.cb.down_buffers[BufferIndex];
        const auto write_offset = buffer.write_offset;

        std::atomic_thread_fence(std::memory_order_acquire);

        const auto read_offset = buffer.read_offset;
        if (read_offset == write_offset) {
            return -1;
        }

        const auto data = static_cast<unsigned char>(buffer.buffer[read_offset]);
        buffer.read_offset = (read_offset + 1) % buffer.size;

        return data;
    }

    /// @brief Get the number of unread bytes in an up buffer.
    template <std::size_t BufferIndex = 0>
    static auto get_available() noexcept -> std::size_t {
        if constexpr (BufferIndex >= UpBuffers) {
            return 0;
        }

        const auto& buffer = storage.cb.up_buffers[BufferIndex];
        const auto write_offset = buffer.write_offset;
        const auto read_offset = buffer.read_offset;

        if (write_offset >= read_offset) {
            return write_offset - read_offset;
        }
        return buffer.size - read_offset + write_offset;
    }

    /// @brief Get the free space in an up buffer.
    template <std::size_t BufferIndex = 0>
    static auto get_free_space() noexcept -> std::size_t {
        if constexpr (BufferIndex >= UpBuffers) {
            return 0;
        }

        const auto& buffer = storage.cb.up_buffers[BufferIndex];
        return buffer.size - get_available<BufferIndex>() - 1;
    }

    /// @brief Read a newline-terminated line from a down buffer.
    /// @param line_buffer Output buffer for the line.
    /// @param max_len Maximum characters to read (including NUL).
    /// @return Pointer to line_buffer on success, nullptr if no data.
    template <std::size_t BufferIndex = 0>
    static auto read_line(char* line_buffer, std::size_t max_len) noexcept -> char* {
        if (max_len == 0)
            return nullptr;

        std::size_t i = 0;
        while (i < max_len - 1) {
            const auto byte = read_byte<BufferIndex>();
            if (byte < 0)
                break;

            const auto ch = static_cast<char>(byte);
            if (ch == '\n') {
                line_buffer[i] = '\0';
                return line_buffer;
            }
            line_buffer[i++] = ch;
        }

        if (i > 0) {
            line_buffer[i] = '\0';
            return line_buffer;
        }

        return nullptr;
    }

  private:
    /// @brief Write data into an up (target-to-host) ring buffer.
    /// @param buffer_index Target buffer index.
    /// @param data Byte span to write.
    /// @return Number of bytes actually written.
    static auto write_up_buffer(std::size_t buffer_index, std::span<const std::byte> data) noexcept -> std::size_t {
        if (buffer_index >= UpBuffers || data.empty()) {
            return 0;
        }

        auto& buffer = storage.cb.up_buffers[buffer_index];
        if (!buffer.buffer) {
            return 0;
        }

        const auto size = buffer.size;
        const auto write_offset = buffer.write_offset;
        const auto read_offset = buffer.read_offset;

        const auto free_space =
            (write_offset >= read_offset) ? (size - write_offset + read_offset - 1) : (read_offset - write_offset - 1);

        if (free_space == 0) {
            return 0;
        }

        const auto to_write = std::min(data.size(), static_cast<std::size_t>(free_space));

        const auto until_end = size - write_offset;
        if (to_write <= until_end) {
            std::memcpy(buffer.buffer + write_offset, data.data(), to_write);
        } else {
            std::memcpy(buffer.buffer + write_offset, data.data(), until_end);
            std::memcpy(buffer.buffer, data.data() + until_end, to_write - until_end);
        }

        std::atomic_thread_fence(std::memory_order_release);
        buffer.write_offset = (write_offset + to_write) % size;

        return to_write;
    }

    /// @brief Read data from a down (host-to-target) ring buffer.
    /// @param buffer_index Source buffer index.
    /// @param data Destination byte span.
    /// @return Number of bytes actually read.
    static auto read_down_buffer(std::size_t buffer_index, std::span<std::byte> data) noexcept -> std::size_t {
        if (buffer_index >= DownBuffers || data.empty()) {
            return 0;
        }

        auto& buffer = storage.cb.down_buffers[buffer_index];
        if (!buffer.buffer) {
            return 0;
        }

        const auto size = buffer.size;
        const auto write_offset = buffer.write_offset;
        const auto read_offset = buffer.read_offset;

        std::atomic_thread_fence(std::memory_order_acquire);

        const auto available =
            (write_offset >= read_offset) ? (write_offset - read_offset) : (size - read_offset + write_offset);

        if (available == 0) {
            return 0;
        }

        const auto to_read = std::min(data.size(), static_cast<std::size_t>(available));

        const auto until_end = size - read_offset;
        if (to_read <= until_end) {
            std::memcpy(data.data(), buffer.buffer + read_offset, to_read);
        } else {
            std::memcpy(data.data(), buffer.buffer + read_offset, until_end);
            std::memcpy(data.data() + until_end, buffer.buffer, to_read - until_end);
        }

        buffer.read_offset = (read_offset + to_read) % size;

        return to_read;
    }
};

/// @brief Terminal ANSI escape sequences for colored output.
namespace terminal {
constexpr const char* reset = "\x1B[0m";
constexpr const char* clear = "\x1B[2J";

namespace text {
constexpr const char* black = "\x1B[2;30m";
constexpr const char* red = "\x1B[2;31m";
constexpr const char* green = "\x1B[2;32m";
constexpr const char* yellow = "\x1B[2;33m";
constexpr const char* blue = "\x1B[2;34m";
constexpr const char* magenta = "\x1B[2;35m";
constexpr const char* cyan = "\x1B[2;36m";
constexpr const char* white = "\x1B[2;37m";

constexpr const char* bright_black = "\x1B[1;30m";
constexpr const char* bright_red = "\x1B[1;31m";
constexpr const char* bright_green = "\x1B[1;32m";
constexpr const char* bright_yellow = "\x1B[1;33m";
constexpr const char* bright_blue = "\x1B[1;34m";
constexpr const char* bright_magenta = "\x1B[1;35m";
constexpr const char* bright_cyan = "\x1B[1;36m";
constexpr const char* bright_white = "\x1B[1;37m";
} // namespace text

namespace background {
constexpr const char* black = "\x1B[24;40m";
constexpr const char* red = "\x1B[24;41m";
constexpr const char* green = "\x1B[24;42m";
constexpr const char* yellow = "\x1B[24;43m";
constexpr const char* blue = "\x1B[24;44m";
constexpr const char* magenta = "\x1B[24;45m";
constexpr const char* cyan = "\x1B[24;46m";
constexpr const char* white = "\x1B[24;47m";

constexpr const char* bright_black = "\x1B[4;40m";
constexpr const char* bright_red = "\x1B[4;41m";
constexpr const char* bright_green = "\x1B[4;42m";
constexpr const char* bright_yellow = "\x1B[4;43m";
constexpr const char* bright_blue = "\x1B[4;44m";
constexpr const char* bright_magenta = "\x1B[4;45m";
constexpr const char* bright_cyan = "\x1B[4;46m";
constexpr const char* bright_white = "\x1B[4;47m";
} // namespace background
} // namespace terminal

} // namespace rt

/// @brief Convenience alias for the default Monitor configuration.
using rtm = rt::Monitor<>;