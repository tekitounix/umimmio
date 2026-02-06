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

// 動作モード
enum class Mode : std::uint32_t {
    NoBlockSkip = 0,    // バッファフル時はスキップ
    NoBlockTrim = 1,    // 入るだけ書き込む
    BlockIfFifoFull = 2 // 空きができるまでブロック
};

// メインのモニタークラス（staticメンバのみ）
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
    // RTMプロトコルのバッファ構造体
    struct rtm_buffer {
        const char* name;               // バッファ名へのポインタ
        char* buffer;                   // バッファへのポインタ
        unsigned size;                  // バッファサイズ
        volatile unsigned write_offset; // 書き込みオフセット
        volatile unsigned read_offset;  // 読み取りオフセット
        unsigned flags;                 // フラグ
    };

    // RTMプロトコルのコントロールブロック
    struct rtm_control_block_t {
        char id[16];                               // "RT MONITOR" + padding
        int max_up_buffers;                        // 最大アップバッファ数
        int max_down_buffers;                      // 最大ダウンバッファ数
        rtm_buffer up_buffers[UpBuffers];     // アップバッファ配列
        rtm_buffer down_buffers[DownBuffers]; // ダウンバッファ配列
    };

    // バッファ名
    static constexpr const char console_name[] = "Console";
    
    // C++23: 集約初期化とstd::arrayを活用
    struct Storage {
        alignas(4) std::array<char, UpBufferSize> up_buffer{};
        alignas(4) std::array<char, DownBufferSize> down_buffer{};
        rtm_control_block_t cb{
            .id = {},
            .max_up_buffers = UpBuffers,
            .max_down_buffers = DownBuffers,
            .up_buffers = {},
            .down_buffers = {}
        };
        
        // C++23のconstexprコンストラクタで初期化
        constexpr Storage() {
            cb.up_buffers[0] = {console_name, up_buffer.data(), UpBufferSize, 0, 0, std::to_underlying(DefaultMode)};
            cb.down_buffers[0] = {console_name, down_buffer.data(), DownBufferSize, 0, 0, std::to_underlying(DefaultMode)};
        }
    };
    
    inline static Storage storage{};

  public:
    // コンストラクタを削除（インスタンス化不可）
    Monitor() = delete;

    // 初期化（カスタムID）
    static void init(std::string_view control_block_id) noexcept {
        // IDをコントロールブロックに設定
        std::ranges::fill(storage.cb.id, '\0');
        std::ranges::copy(control_block_id.substr(0, sizeof(storage.cb.id)), storage.cb.id);
    }

    // バイトデータ書き込み
    template <std::size_t BufferIndex = 0>
    [[nodiscard]] static auto write(std::span<const std::byte> data) noexcept -> std::size_t {
        return write_up_buffer(BufferIndex, data);
    }

    // 文字列書き込み（戻り値チェック推奨）
    template <std::size_t BufferIndex = 0>
    [[nodiscard]] static auto write(std::string_view str) noexcept -> std::size_t {
        return write(std::span{reinterpret_cast<const std::byte*>(str.data()), str.size()});
    }

    // 文字列書き込み（戻り値無視可能）
    template <std::size_t BufferIndex = 0> static void log(std::string_view str) noexcept {
        std::ignore = write<BufferIndex>(str);
    }

    // バイトデータ読み込み
    template <std::size_t BufferIndex = 0>
    static auto read(std::span<std::byte> buffer) noexcept -> std::size_t {
        return read_down_buffer(BufferIndex, buffer);
    }

    // ダウンバッファから1バイト読み取り
    template <std::size_t BufferIndex = 0> static auto read_byte() noexcept -> int {
        if constexpr (BufferIndex >= DownBuffers) {
            return -1;
        }

        auto& buffer = storage.cb.down_buffers[BufferIndex];
        const auto write_offset = buffer.write_offset;

        std::atomic_thread_fence(std::memory_order_acquire);

        const auto read_offset = buffer.read_offset;
        if (read_offset == write_offset) {
            return -1; // バッファが空
        }

        const auto data = static_cast<unsigned char>(buffer.buffer[read_offset]);
        buffer.read_offset = (read_offset + 1) % buffer.size;

        return data;
    }

    // 保留中のデータ量を取得
    template <std::size_t BufferIndex = 0> static auto get_available() noexcept -> std::size_t {
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

    // アップバッファの空き容量を取得
    template <std::size_t BufferIndex = 0> static auto get_free_space() noexcept -> std::size_t {
        if constexpr (BufferIndex >= UpBuffers) {
            return 0;
        }

        const auto& buffer = storage.cb.up_buffers[BufferIndex];
        return buffer.size - get_available<BufferIndex>() - 1;
    }

    // ダウンバッファからラインを読み取る
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
    // アップバッファへの書き込み（ホスト→ターゲット）
    static auto write_up_buffer(std::size_t buffer_index, std::span<const std::byte> data) noexcept
        -> std::size_t {
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

        // 空き容量を計算
        const auto free_space = (write_offset >= read_offset)
                                    ? (size - write_offset + read_offset - 1)
                                    : (read_offset - write_offset - 1);

        if (free_space == 0) {
            return 0; // バッファフル
        }

        const auto to_write = std::min(data.size(), static_cast<std::size_t>(free_space));

        // 環状バッファへの書き込み
        const auto until_end = size - write_offset;
        if (to_write <= until_end) {
            // 一度に書き込める
            std::memcpy(buffer.buffer + write_offset, data.data(), to_write);
        } else {
            // 2回に分けて書き込む
            std::memcpy(buffer.buffer + write_offset, data.data(), until_end);
            std::memcpy(buffer.buffer, data.data() + until_end, to_write - until_end);
        }

        // メモリバリアで書き込み完了を保証
        std::atomic_thread_fence(std::memory_order_release);

        // 書き込みポインタを更新
        buffer.write_offset = (write_offset + to_write) % size;

        return to_write;
    }

    // ダウンバッファからの読み込み（ターゲット→ホスト）
    static auto read_down_buffer(std::size_t buffer_index, std::span<std::byte> data) noexcept
        -> std::size_t {
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

        // 利用可能なデータ量を計算
        const auto available = (write_offset >= read_offset) ? (write_offset - read_offset)
                                                             : (size - read_offset + write_offset);

        if (available == 0) {
            return 0; // データなし
        }

        const auto to_read = std::min(data.size(), static_cast<std::size_t>(available));

        // 環状バッファからの読み込み
        const auto until_end = size - read_offset;
        if (to_read <= until_end) {
            // 一度に読み込める
            std::memcpy(data.data(), buffer.buffer + read_offset, to_read);
        } else {
            // 2回に分けて読み込む
            std::memcpy(data.data(), buffer.buffer + read_offset, until_end);
            std::memcpy(data.data() + until_end, buffer.buffer, to_read - until_end);
        }

        // 読み込みポインタを更新
        buffer.read_offset = (read_offset + to_read) % size;

        return to_read;
    }
};

// ターミナル制御シーケンス
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

// グローバル名前空間でのエイリアス
using rtm = rt::Monitor<>;