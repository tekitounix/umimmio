#pragma once

#include "rtm.hh"
#include <iostream>
#include <thread>
#include <chrono>

#ifdef __APPLE__
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rt {

// ホスト環境用RTM実装
template <typename BaseMonitor = Monitor<>>
class HostMonitor {
public:
    // モード1: 標準出力へのブリッジ（最も簡単）
    static void bridge_to_stdout() {
        std::thread reader([]() {
            char buffer[1024];
            while (true) {
                auto* line = BaseMonitor::template read_line<0>(buffer, sizeof(buffer));
                if (line) {
                    std::cout << line << std::endl;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
        reader.detach();
    }
    
    // モード2: 共有メモリエクスポート（RTTビューア互換）
    static bool export_to_shared_memory(const char* name = "rtm_shared") {
#ifdef __APPLE__
        // macOSでの共有メモリ実装
        auto shm_name = std::string("/") + name;
        
        // RTM control blockのアドレスとサイズを取得
        auto* cb = get_control_block();
        auto cb_size = sizeof(*cb);
        
        // 共有メモリを作成
        int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) return false;
        
        // サイズを設定
        if (ftruncate(fd, cb_size) < 0) {
            close(fd);
            return false;
        }
        
        // メモリマップ
        void* mapped = mmap(nullptr, cb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            close(fd);
            return false;
        }
        
        // コントロールブロックをコピー
        std::memcpy(mapped, cb, cb_size);
        
        // 定期的に同期するスレッドを起動
        std::thread syncer([mapped, cb, cb_size]() {
            while (true) {
                std::memcpy(mapped, cb, cb_size);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        syncer.detach();
        
        std::cout << "RTM exported to shared memory: " << shm_name << std::endl;
        std::cout << "Use RTT viewer with memory address: " << mapped << std::endl;
        
        return true;
#else
        // 他のプラットフォーム用の実装
        return false;
#endif
    }
    
    // モード3: TCPサーバー（RTT over TCP）
    static bool start_tcp_server(int port = 19021) {
        // TCP実装は別途必要
        // JLink RTTサーバーと同じプロトコルを実装
        std::cout << "TCP server mode not implemented yet" << std::endl;
        return false;
    }
    
private:
    // コントロールブロックへのアクセス
    static auto* get_control_block() {
        // BaseMonitorのprivateメンバーにアクセスする方法が必要
        // friend宣言かpublicアクセサを追加する必要がある
        return nullptr; // 仮実装
    }
};

// ホスト環境用のRTMエイリアス
using rtm_host = HostMonitor<>;

} // namespace rt