// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Host-side RTM bridge utilities (stdout, shared memory).
/// @author Shota Moriguchi @tekitounix
///
/// Provides HostMonitor that bridges Monitor ring buffers to stdout or
/// shared memory (macOS) for use with RTT viewers on the host.
#pragma once

#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "rtm.hh"

#ifdef __APPLE__
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace rt {

/// @brief Host-side RTM bridge for debugging on desktop.
///
/// Bridges Monitor ring buffers to host-side I/O channels. Supports:
/// - stdout printing (bridge_to_stdout)
/// - POSIX shared memory export (macOS, export_to_shared_memory)
///
/// Lifecycle: construct to start, destroy to stop. Call stop() or
/// let the destructor handle cleanup.
///
/// @tparam BaseMonitor The Monitor specialization to bridge.
template <typename BaseMonitor = Monitor<>>
class HostMonitor {
  public:
    HostMonitor() = default;
    ~HostMonitor() { stop(); }

    HostMonitor(const HostMonitor&) = delete;
    HostMonitor& operator=(const HostMonitor&) = delete;
    HostMonitor(HostMonitor&&) = delete;
    HostMonitor& operator=(HostMonitor&&) = delete;

    /// @brief Start a background thread that reads from up buffer 0 and prints to stdout.
    /// @note Safe to call multiple times; subsequent calls are ignored if already running.
    void bridge_to_stdout() {
        if (reader.joinable()) {
            return; // already running
        }
        reader = std::thread([this]() {
            std::array<char, 1024> buffer{};
            while (!stopping.load(std::memory_order_relaxed)) {
                auto* line = BaseMonitor::template read_line<0>(buffer.data(), buffer.size());
                if (line) {
                    std::cout << line << '\n';
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }

    /// @brief Export the control block to POSIX shared memory.
    ///
    /// Creates a shared memory object and starts a background thread that
    /// periodically copies the control block into shared memory so that
    /// an external RTT viewer can read ring buffer contents.
    ///
    /// @param name Shared memory object name (prepended with '/').
    /// @return true on success, false on failure or unsupported platform.
    bool export_to_shared_memory(const char* name = "rtm_shared") {
#ifdef __APPLE__
        shm_name = std::string("/") + name;

        auto* cb = BaseMonitor::get_control_block();
        auto cb_size = BaseMonitor::get_control_block_size();

        int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) {
            return false;
        }

        if (ftruncate(fd, static_cast<off_t>(cb_size)) < 0) {
            close(fd);
            return false;
        }

        mapped = mmap(nullptr, cb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd); // fd no longer needed after mmap
        if (mapped == MAP_FAILED) {
            mapped = nullptr;
            return false;
        }

        mapped_size = cb_size;
        std::memcpy(mapped, cb, cb_size);

        syncer = std::thread([this, cb, cb_size]() {
            while (!stopping.load(std::memory_order_relaxed)) {
                std::memcpy(this->mapped, cb, cb_size);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        std::cout << "RTM exported to shared memory: " << shm_name << '\n';
        return true;
#else
        (void)name;
        return false;
#endif
    }

    /// @brief Stop all background threads and release shared memory.
    void stop() {
        stopping.store(true, std::memory_order_relaxed);
        if (reader.joinable()) {
            reader.join();
        }
        if (syncer.joinable()) {
            syncer.join();
        }
        cleanup_shm();
        stopping.store(false, std::memory_order_relaxed);
    }

  private:
    void cleanup_shm() {
#ifdef __APPLE__
        if (mapped != nullptr) {
            munmap(mapped, mapped_size);
            mapped = nullptr;
        }
        if (!shm_name.empty()) {
            shm_unlink(shm_name.c_str());
            shm_name.clear();
        }
#endif
    }

    std::atomic<bool> stopping{false};
    std::thread reader;
    std::thread syncer;
    void* mapped = nullptr;
    std::size_t mapped_size = 0;
    std::string shm_name;
};

/// @brief Convenience alias for the default HostMonitor configuration.
using rtm_host = HostMonitor<>;

} // namespace rt
