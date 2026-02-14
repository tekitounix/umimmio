# C11: セキュリティ / 保護

---

## 1. 概要

セキュリティ / 保護カテゴリは、MCU が提供するハードウェアセキュリティ機能を定義する。
フラッシュ保護、デバッグアクセス制御、セキュアブート、暗号エンジン、
OTP (One-Time Programmable) / eFuse などが含まれる。

組み込みオーディオデバイスにおいても、ファームウェアの不正読み出し防止や
セキュアなファームウェアアップデートは重要な要件であり、
PAL レイヤでプラットフォーム固有の保護メカニズムを統一的に表現する。

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L2 (コアプロファイル固有) | MPU リージョン設定、SAU (TrustZone 搭載コア) |
| L3 (MCU 固有) | フラッシュ保護機構、セキュアブート、暗号エンジン構成 |
| L4 (デバイス固有) | OTP ビット割り当て、利用可能な保護オプション |

---

## 2. 構成要素

### 2.1 TrustZone / セキュア実行環境

ARMv8-M (Cortex-M23/M33) および RP2350 で利用可能。
セキュアワールドとノンセキュアワールドのハードウェア分離を提供する。

### 2.2 フラッシュ保護

| 保護種別 | 説明 |
|---------|------|
| 読み出し保護 (RDP/ROP) | デバッガからのフラッシュ読み出しを防止 |
| 書き込み保護 (WRP) | 特定セクタへの書き込みを防止 |
| Proprietary Code Readout Protection (PCROP) | コード実行は可能だが読み出し不可 |

### 2.3 JTAG / SWD 保護

デバッグインターフェースの無効化またはアクセス制限。
多くの MCU で不可逆的な無効化オプションが存在する。

### 2.4 セキュアブート

ファームウェアの正当性を暗号的に検証してから実行するメカニズム。

### 2.5 OTP / eFuse

ワンタイムプログラマブルメモリ。セキュリティ設定やデバイス固有鍵の格納に使用される。
一度書き込むと変更不可能。

### 2.6 暗号エンジン

ハードウェアアクセラレータによる暗号処理 (AES, SHA, RSA, ECC 等)。

---

## 3. プラットフォーム差異

| プラットフォーム | フラッシュ保護 | デバッグ保護 | セキュアブート | TrustZone | 暗号エンジン | OTP/eFuse |
|----------------|-------------|------------|-------------|-----------|------------|----------|
| STM32F4 | RDP L0/L1/L2 + WRP | RDP L1 で SWD ブロック | -- | -- | -- (ソフトウェアのみ) | Option bytes |
| STM32H7 | RDP + WRP + PCROP + Secure area | RDP + TZEN | SBSFU (ST 提供) | M33 コア搭載モデルのみ | AES, HASH, RNG | Option bytes |
| RP2040 | -- (外部フラッシュ) | SWD 無効化 (不可逆) | -- | -- | -- | OTP (limited) |
| RP2350 | -- (外部フラッシュ) | SWD 無効化 + Secure debug | Signed boot (SHA-256 + ECDSA) | ARM コア: TrustZone 対応 | SHA-256, AES-256 | OTP 8KB (ページ単位) |
| ESP32-S3 | Flash encryption (AES-256-XTS) | JTAG 無効化 (eFuse) | Secure Boot v2 (RSA-PSS / ECDSA) | -- | AES, SHA, RSA, ECC, HMAC | eFuse 4096 bits |
| ESP32-P4 | Flash encryption | JTAG 無効化 (eFuse) | Secure Boot v2 | -- | AES, SHA, RSA, ECC, HMAC, DS | eFuse (拡張) |
| i.MX RT | BEE (Bus Encryption Engine) + XIP | JTAG 無効化 (SJC) | HAB (High Assurance Boot) | -- | DCP (AES-128), TRNG | eFuse (OCOTP) |

---

## 4. 生成ヘッダのコード例

### 4.1 STM32F4 セキュリティ

```cpp
// pal/mcu/stm32f4/security.hh
#pragma once
#include <cstdint>

namespace umi::pal::stm32f4::security {

/// @brief 読み出し保護レベル (RDP)
/// @warning LEVEL_2 は不可逆 — 設定すると二度と解除できない
enum class RdpLevel : uint8_t {
    LEVEL_0 = 0xAA,  // 保護なし — 全アクセス許可
    LEVEL_1 = 0x00,  // 読み出し保護 — デバッグからのフラッシュ読み出しブロック
    LEVEL_2 = 0xCC,  // 永久保護 (不可逆!) — SWD 完全無効化
};

/// @brief フラッシュ書き込み保護セクタ定義
namespace flash_wp {
    constexpr uint8_t sector_count = 12; // STM32F407: セクタ 0-11

    // セクタサイズ定義
    // セクタ 0-3: 16KB each (0x0800'0000 - 0x0800'FFFF)
    // セクタ 4:   64KB      (0x0801'0000 - 0x0801'FFFF)
    // セクタ 5-11: 128KB each (0x0802'0000 - 0x080F'FFFF)

    /// @brief セクタ開始アドレス
    constexpr uint32_t sector_addr[] = {
        0x0800'0000, // Sector 0  (16KB)
        0x0800'4000, // Sector 1  (16KB)
        0x0800'8000, // Sector 2  (16KB)
        0x0800'C000, // Sector 3  (16KB)
        0x0801'0000, // Sector 4  (64KB)
        0x0802'0000, // Sector 5  (128KB)
        0x0804'0000, // Sector 6  (128KB)
        0x0806'0000, // Sector 7  (128KB)
        0x0808'0000, // Sector 8  (128KB)
        0x080A'0000, // Sector 9  (128KB)
        0x080C'0000, // Sector 10 (128KB)
        0x080E'0000, // Sector 11 (128KB)
    };
}

/// @brief Option bytes 設定
namespace option_bytes {
    constexpr uint32_t base_addr = 0x1FFF'C000;
    constexpr uint8_t bor_level_count = 4; // BOR Level 0-3
}

} // namespace umi::pal::stm32f4::security
```

### 4.2 RP2350 セキュリティ (TrustZone + OTP + Secure Boot)

```cpp
// pal/mcu/rp2350/security.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2350::security {

/// @brief TrustZone 構成 (ARM コアモードのみ)
namespace trustzone {
    constexpr bool available = true; // ARM コア選択時のみ
    constexpr uint8_t sau_region_count = 8;
    // RISC-V コア選択時は PMP で保護
}

/// @brief OTP メモリ (8KB, ページ単位で書き込み)
namespace otp {
    constexpr uint32_t total_size = 8 * 1024;
    constexpr uint32_t page_size = 64;          // 64 バイト/ページ
    constexpr uint32_t page_count = 128;

    /// @brief OTP ページの用途区分
    namespace pages {
        constexpr uint8_t BOOT_KEY_START = 0;   // Secure Boot 公開鍵ハッシュ格納
        constexpr uint8_t BOOT_FLAGS = 16;      // ブートフラグ
        constexpr uint8_t USER_START = 48;      // ユーザー利用可能領域
    }
}

/// @brief セキュアブート
namespace secure_boot {
    constexpr bool sha256_verify = true;
    constexpr bool ecdsa_verify = true; // NIST P-256
    // 署名されたイメージのみブート可能 (OTP で有効化後)
}

/// @brief デバッグ保護
namespace debug {
    constexpr bool secure_debug_available = true;
    // Secure debug: 認証されたデバッガのみアクセス許可
    // Permanent disable: OTP で不可逆無効化
}

} // namespace umi::pal::rp2350::security
```

### 4.3 ESP32-S3 セキュリティ (eFuse + Flash Encryption + Secure Boot)

```cpp
// pal/mcu/esp32s3/security.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3::security {

/// @brief eFuse ブロック定義
namespace efuse {
    constexpr uint16_t total_bits = 4096;
    constexpr uint8_t block_count = 11; // BLOCK0-BLOCK10

    /// @brief eFuse ブロックの用途
    namespace block {
        constexpr uint8_t SYSTEM_DATA = 0;      // システムパラメータ (MAC, チップ情報)
        constexpr uint8_t KEY0 = 4;             // Flash encryption key / Secure Boot key
        constexpr uint8_t KEY1 = 5;
        constexpr uint8_t KEY2 = 6;
        constexpr uint8_t KEY3 = 7;
        constexpr uint8_t KEY4 = 8;
        constexpr uint8_t KEY5 = 9;
    }
}

/// @brief フラッシュ暗号化
namespace flash_encryption {
    constexpr bool available = true;
    constexpr auto algorithm = "AES-256-XTS"; // XTS モードによるインプレース暗号化
    // 開発モード: 暗号化有効、再書き込み可能
    // リリースモード: 暗号化有効、再書き込み回数制限 (eFuse による)
}

/// @brief セキュアブート v2
namespace secure_boot {
    constexpr bool available = true;
    constexpr bool rsa_pss = true;     // RSA-PSS 3072-bit
    constexpr bool ecdsa = true;       // ECDSA P-256
    constexpr uint8_t key_slot_count = 3; // 最大 3 つの鍵で署名可能
}

/// @brief JTAG 保護
namespace jtag {
    // eFuse で JTAG 無効化 (不可逆)
    // HMAC ベースの JTAG 再有効化も可能 (eFuse 設定依存)
    constexpr bool soft_disable_available = true;  // ソフトウェアで一時無効化
    constexpr bool hard_disable_available = true;  // eFuse で永久無効化
}

/// @brief ハードウェア暗号エンジン
namespace crypto {
    constexpr bool aes = true;          // AES-128/256
    constexpr bool sha = true;          // SHA-224/256
    constexpr bool rsa = true;          // RSA-1024/2048/3072/4096
    constexpr bool ecc = true;          // ECDSA P-192/P-256
    constexpr bool hmac = true;         // HMAC-SHA-256
    constexpr bool digital_signature = true; // DS (Digital Signature peripheral)
}

} // namespace umi::pal::esp32s3::security
```

---

## 5. データソース

| プラットフォーム | ドキュメント | 入手先 |
|----------------|------------|--------|
| STM32F4 | STM32F4xx Reference Manual (RM0090) — Chapter: Flash, Option bytes | st.com |
| STM32F4 | AN4701: STM32F4 proprietary code read-out protection (PCROP) | st.com |
| STM32H7 | STM32H7xx Reference Manual — Security chapter, SBSFU | st.com |
| RP2040 | RP2040 Datasheet — Section: SWD | raspberrypi.com |
| RP2350 | RP2350 Datasheet — Chapter: OTP, Secure Boot, TrustZone | raspberrypi.com |
| ESP32-S3 | ESP32-S3 Technical Reference Manual — Chapter: eFuse Controller | espressif.com |
| ESP32-S3 | ESP-IDF Security Guide — Flash Encryption, Secure Boot | docs.espressif.com |
| ESP32-P4 | ESP32-P4 Technical Reference Manual — Security chapters | espressif.com |
| i.MX RT | i.MX RT1060 Security Reference Manual — HAB, BEE, DCP | nxp.com |

**注記**: セキュリティ設定の多くは不可逆的な操作 (OTP 書き込み、RDP Level 2 等) を含む。
PAL レイヤでは静的な構成情報とオプションの定義のみを提供し、
実際のセキュリティ設定操作は専用ツール (STM32CubeProgrammer, espefuse 等) を介して行う。
コード例内の `RdpLevel::LEVEL_2` のような危険な操作には明示的な警告コメントを付与する。
