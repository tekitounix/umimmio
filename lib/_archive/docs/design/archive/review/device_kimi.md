# umidevice 理想的設計レビュー

## 前提の明確化

本レビューは「現状のumiportアーキテクチャにどう組み込むか」ではなく、**「外部デバイスという概念の理想的な扱いとは何か」**という観点から考察する。

---

## 1. 外部デバイスの本質的特徴

### 1.1 何が"外部"なのか

```
┌─────────────────────────────────────────────────────────────┐
│  MCU内部                           外部デバイス領域          │
│  ┌──────────┐                     ┌──────────────┐          │
│  │ UART1    │────────────────────→│   BME280     │          │
│  │ (レジスタ)│  I2C/SPI/UART       │ (温度/湿度/気圧)│         │
│  └──────────┘                     └──────────────┘          │
│       ↑                               ↑                     │
│       │                               │                     │
│   MCU設計者が定義                メーカーが定義              │
│   アドレス固定                   プロトコルで抽象化          │
└─────────────────────────────────────────────────────────────┘
```

**本質的違い**:

| 観点 | MCU内蔵 | 外部デバイス |
|------|---------|-------------|
| **インターフェース** | レジスタマップ（メモリマップド） | 通信プロトコル（I2C/SPI/UART） |
| **状態管理** | レジスタ書き込み即反映 | 通信遅延、バッファ、応答待ち |
| **エラー** | 基本的に発生しない | 通信エラー、タイムアウト、応答なし |
| **アドレス** | 固定 | 選択可能（I2Cアドレスピン等） |
| **複数性** | シングルトン（1種類1つ） | マルチインスタンス（同型複数可） |
| **ライフサイクル** | 電源投入で常に存在 | 接続/切断の可能性あり |

### 1.2 デバイスドライバの責務

理想的なデバイスドライバは以下を隠蔽すべき：

1. **通信プロトコルの詳細**（I2C読み書きのシーケンス）
2. **レジスタアドレスとビットマスク**（データシートの詳細）
3. **初期化シーケンス**（何をどの順で書き込むか）
4. **エラー回復戦略**（NACK時のリトライなど）

**暴露すべき**:

1. **デバイスの能力**（測定可能な物理量、解像度、範囲）
2. **高レベル操作**（`measure_temperature()`、`set_volume()`）
3. **設定パラメータ**（サンプリングレート、フィルタ設定など）

---

## 2. 理想的な抽象化レベル

### 2.1 レイヤー構造

```
┌────────────────────────────────────────────────────────────┐
│ アプリケーション層                                          │
│  auto temp = sensor.read_temperature();                      │
│  display.show(temp);                                         │
├────────────────────────────────────────────────────────────┤
│ デバイス抽象層（Device Driver）                             │
│  - デバイスの能力を表現                                     │
│  - プロトコル詳細を隠蔽                                     │
├────────────────────────────────────────────────────────────┤
│ バス抽象層（Bus）                                           │
│  - I2C/SPI/UARTの共通インターフェース                       │
│  - トランザクション管理                                     │
├────────────────────────────────────────────────────────────┤
│ MCUポート層（umiport）                                      │
│  - レジスタ操作                                             │
│  - タイミング制御                                           │
└────────────────────────────────────────────────────────────┘
```

### 2.2 理想的なデバイスインターフェース

```cpp
// 理想: デバイスは「何ができるか」を表現
namespace device {

// 能力を型として表現
struct TemperatureSensor {
    using ValueType = std::chrono::temperature;  // C++23
    static constexpr auto accuracy = 0.1_deg_c;
    static constexpr auto range = std::pair{-40.0_deg_c, 85.0_deg_c};
};

struct HumiditySensor {
    using ValueType = percent;
    static constexpr auto accuracy = 1.0_percent;
};

// BME280は両方の能力を持つ
template<typename Bus>
class Bme280 : public TemperatureSensor, public HumiditySensor {
public:
    struct Config {
        Oversampling temperature_os = Oversampling::x1;
        Oversampling humidity_os = Oversampling::x1;
        Filter filter = Filter::off;
    };
    
    explicit Bme280(Bus& bus, uint8_t address, const Config& cfg = {});
    
    // 高レベルAPI—内部レジスタの詳細は完全に隠蔽
    Result<Temperature> read_temperature();
    Result<Humidity> read_humidity();
    Result<Pressure> read_pressure();
    
    // 一括読み出し（アトミック）
    Result<Measurement> read_all();
    
    // 非同期対応
    Task<Result<Temperature>> read_temperature_async();
    
private:
    Bus& bus_;
    uint8_t addr_;
    CalibrationData cal_;  // デバイス固有のキャリブレーション
};

}
```

### 2.3 理想的なバスインターフェース

```cpp
// バスは「転送手段」を抽象化
namespace bus {

// 最小限のトランザクションインターフェース
template<typename T>
concept I2CBus = requires(T& bus, uint8_t addr, std::span<std::byte> data) {
    { bus.write(addr, data) } -> std::same_as<Result<void>>;
    { bus.read(addr, data) } -> std::same_as<Result<void>>;
    { bus.write_then_read(addr, write_data, read_data) } -> std::same_as<Result<void>>;
};

// デバイスアダプタ—アドレスをバインド
class I2CDevice {
public:
    I2CDevice(I2CBus auto& bus, uint8_t addr) : bus_(bus), addr_(addr) {}
    
    Result<void> write(std::span<const std::byte> data) {
        return bus_.write(addr_, data);
    }
    
    Result<void> read(std::span<std::byte> data) {
        return bus_.read(addr_, data);
    }
    
    // レジスタアクセス—デバイスドライバに便利
    Result<void> write_register(uint8_t reg, std::span<const std::byte> data);
    Result<void> read_register(uint8_t reg, std::span<std::byte> data);
    
private:
    I2CBus auto& bus_;
    uint8_t addr_;
};

}
```

---

## 3. 合成による設計（Composition over Inheritance）

### 3.1 問題: 継承ベースの設計

```cpp
// 悪い例: 継承階層が深くなる
template<typename Bus>
class I2CDevice { /* ... */ };

template<typename Bus>
class Sensor : public I2CDevice<Bus> { /* ... */ };

template<typename Bus>
class TemperatureSensor : public Sensor<Bus> { /* ... */ };

template<typename Bus>
class Bme280 : public TemperatureSensor<Bus> { /* ... */ };
// 問題: Busがテンプレートパラメータとして蔓延
```

### 3.2 理想的: 合成による構成

```cpp
// 良い例: バスはメンバとして保持、テンプレートは最小限
namespace device {

// Busは型消去またはConceptで抽象化
template<I2CBus Bus>
class Bme280 {
public:
    explicit Bme280(Bus& bus, uint8_t address) 
        : i2c_(bus, address) {}
    
    Result<Temperature> read_temperature() {
        // レジスタ読み書きはI2CDeviceに委譲
        auto data = TRY(i2c_.read_register(kTempMsbReg, 3));
        return convert_temperature(data, cal_);
    }
    
private:
    bus::I2CDevice i2c_;  // 合成
    CalibrationData cal_;
};

// 別のデバイスも同じパターン
template<I2CBus Bus>
class Ssd1306 {
public:
    explicit Ssd1306(Bus& bus, uint8_t address)
        : i2c_(bus, address) {}
    
    Result<void> display(const FrameBuffer& fb) {
        TRY(i2c_.write_command(kSetColumnAddr));
        TRY(i2c_.write_data(fb.data()));
        return {};
    }
    
private:
    bus::I2CDevice i2c_;
};

}
```

---

## 4. 状態管理とライフサイクル

### 4.1 理想的な状態モデル

```cpp
namespace device {

// 明示的な状態管理
template<I2CBus Bus>
class Bme280 {
public:
    enum class State {
        Uninitialized,  // 構築直後
        Initializing,   // init()呼び出し中
        Ready,          // 使用可能
        Error,          // エラー状態
        Sleep           // 低消費電力モード
    };
    
    explicit Bme280(Bus& bus, uint8_t addr);
    
    // 2フェーズ初期化—コンストラクタでは通信しない
    Result<void> init(const Config& cfg);
    
    // 状態遷移を明示
    Result<void> enter_sleep();
    Result<void> wake_up();
    Result<void> reset();
    
    // 各操作で状態チェック
    Result<Temperature> read_temperature() {
        if (state_ != State::Ready) {
            return Error::NotReady;
        }
        // ...
    }
    
    State state() const { return state_; }
    
private:
    bus::I2CDevice i2c_;
    State state_ = State::Uninitialized;
    CalibrationData cal_;
};

}
```

### 4.2 エラー処理の統一

```cpp
namespace device {

// デバイス層固有のエラー
template<typename T>
using Result = std::expected<T, Error>;

enum class Error {
    // 通信エラー
    BusError,           // I2C/SPIの通信失敗
    Timeout,            // 応答待ちタイムアウト
    Nack,               // アドレスNACK（デバイス未接続？）
    
    // デバイスエラー
    InvalidId,          // 予期しないデバイスID
    CalibrationFailed,  // キャリブレーションデータ破損
    SelfTestFailed,     // デバイス自己診断エラー
    
    // 状態エラー
    NotInitialized,     // init()が呼ばれていない
    NotReady,           // 初期化中またはスリープ中
    Busy,               // 変換/処理中
    
    // 設定エラー
    InvalidConfig,      // サポートされていない設定値
    OutOfRange,         // 測定範囲外
};

// エラー情報の詳細化
struct ErrorInfo {
    Error code;
    const char* operation;  // どの操作で発生
    uint32_t context;       // デバイス固有のコンテキスト（レジスタ値など）
};

}
```

---

## 5. 複数インスタンスとアドレシング

### 5.1 同型デバイスの複数接続

```cpp
// 理想的: 同型デバイスを複数簡単に扱える
void example() {
    using I2C = umiport::mcu::stm32f4::I2C1;
    
    // 同じI2Cバスに複数の同型デバイス
    device::Bme280 sensor1{i2c, 0x76};  // SDO=GND
    device::Bme280 sensor2{i2c, 0x77};  // SDO=VCC
    
    TRY(sensor1.init({}));
    TRY(sensor2.init({}));
    
    auto temp1 = TRY(sensor1.read_temperature());
    auto temp2 = TRY(sensor2.read_temperature());
}
```

### 5.2 デバイス探索と自動設定

```cpp
namespace device {

// バスに接続されているデバイスを発見
template<I2CBus Bus>
class DeviceScanner {
public:
    explicit DeviceScanner(Bus& bus) : bus_(bus) {}
    
    // デバイスIDレジスタを読んで機種を特定
    Result<std::variant<Bme280, Mpu6050, Unknown>> detect(uint8_t address) {
        auto id = TRY(bus_.read_register(address, kIdRegister));
        switch (id) {
            case 0x60: return Bme280{bus_, address};
            case 0x68: return Mpu6050{bus_, address};
            default: return Unknown{address, id};
        }
    }
    
    // 全アドレスをスキャン
    std::vector<DiscoveredDevice> scan();
    
private:
    Bus& bus_;
};

}
```

---

## 6. テストとモック

### 6.1 理想的なテスト容易性

```cpp
// バスをモック化してデバイスドライバをテスト
class MockI2C {
public:
    // 期待されるトランザクションを事前登録
    void expect_write(uint8_t addr, std::vector<std::byte> data);
    void expect_read(uint8_t addr, std::vector<std::byte> response);
    
    // 実際の呼び出しを記録
    std::vector<Transaction> recorded_transactions;
    
    // I2CBus conceptを満たす
    Result<void> write(uint8_t addr, std::span<const std::byte> data);
    Result<void> read(uint8_t addr, std::span<std::byte> data);
};

TEST(Bme280Test, ReadTemperature) {
    MockI2C mock;
    
    // 初期化シーケンス
    mock.expect_write(0x76, {0xF4, 0x27});  // 制御レジスタ設定
    mock.expect_read(0x76, {0x60});          // デバイスID確認
    
    // 温度読み出し
    mock.expect_write(0x76, {0xFA});         // 温度MSBレジスタ
    mock.expect_read(0x76, {0x65, 0x5A, 0xC0});
    
    device::Bme280 sensor{mock, 0x76};
    EXPECT_TRUE(sensor.init({}));
    
    auto temp = sensor.read_temperature();
    EXPECT_NEAR(temp.celsius(), 25.0, 0.1);
    
    mock.verify();  // 全期待が満たされたか確認
}
```

---

## 7. 理想的なアーキテクチャ全体像

### 7.1 パッケージ構成

```
lib/
├── umihal/                    # 概念定義（変更なし）
│   └── concept/
│       ├── gpio.hh
│       ├── uart.hh
│       └── ...
│
├── umibus/                    # バス抽象（新規—理想設計）
│   └── include/umibus/
│       ├── concept/
│       │   └── bus.hh         # I2CBus, SPIBus concept
│       ├── i2c_device.hh      # I2CDevice（アドレスバインド）
│       ├── spi_device.hh      # SPIDevice（CSバインド）
│       └── transaction.hh     # トランザクション管理
│
├── umidevice/                 # デバイスドライバ（新規—理想設計）
│   └── include/umidevice/
│       ├── concept/
│       │   └── sensor.hh      # Sensor, Display, Codec concept
│       ├── sensor/
│       │   ├── bme280.hh
│       │   └── mpu6050.hh
│       ├── display/
│       │   ├── ssd1306.hh
│       │   └── st7789.hh
│       └── audio/
│           └── pcm5102.hh
│
├── umiport/                   # MCUポート層（変更なし）
│   └── mcu/
│       └── stm32f4/
│           ├── i2c.hh         # umibus::I2CBus conceptを満たす
│           └── spi.hh
│
└── umiport-boards/            # ボード統合（変更あり）
    └── include/board/
        └── stm32f4-disco/
            └── devices.hh      # ボード上のデバイス構成
```

### 7.2 依存関係（理想）

```
umihal (concept)
    ↑
umibus (bus abstraction)
    ↑ (uses)
umidevice (device driver)
    ↑ (uses)
umiport (MCU port—satisfies bus concept)
    ↑ (binds)
umiport-boards (board configuration)
    ↑
application
```

### 7.3 ボードでの統合例

```cpp
// board/stm32f4-disco/devices.hh
#pragma once
#include <umibus/i2c_device.hh>
#include <umidevice/sensor/bme280.hh>
#include <umiport/mcu/stm32f4/i2c.hh>

namespace umi::board {

// ボード上のI2Cバス
using I2C1 = umiport::mcu::stm32f4::I2C1;

// ボード上のデバイス構成
struct Devices {
    // バスインスタンス
    I2C1 i2c1;
    
    // I2Cバス上のデバイスアドレスを定義
    static constexpr uint8_t kAudioCodecAddr = 0x4A;
    static constexpr uint8_t kAccelerometerAddr = 0x32;
    
    // デバイスインスタンス（オプション—必要に応じて）
    // device::Cs43l22 audio_codec{i2c1, kAudioCodecAddr};
};

// またはファクトリ関数
inline auto make_audio_codec(I2C1& i2c) {
    return device::Cs43l22{i2c, Devices::kAudioCodecAddr};
}

}

// アプリケーション側
void app() {
    umi::board::Devices devices;
    devices.i2c1.init();
    
    auto codec = umi::board::make_audio_codec(devices.i2c1);
    codec.init({});
    codec.set_volume(0.5);
}
```

---

## 8. まとめ: 理想と現実のトレードオフ

### 理想的設計の核心

| 項目 | 理想 | 理由 |
|------|------|------|
| **バス抽象化** | `umibus` 独立パッケージ | I2C/SPIの共通パターンを分離 |
| **デバイス層** | `umidevice` 独立パッケージ | MCU/ボード非依存の再利用 |
| **状態管理** | 明示的な2フェーズ初期化 | コンストラクタでの通信を避ける |
| **エラー処理** | `std::expected` ベース | エラー型の階層化 |
| **インスタンス化** | 自由（同型複数可） | アドレス/CSは実行時パラメータ |
| **テスト** | バスモックで完全テスト | ハードウェアなしで検証 |

### 現実的な妥協点

| 理想 | 現実的妥協 | 理由 |
|------|-----------|------|
| `std::expected` | 独自の`Result<T>`型 | 組み込みC++20/23未対応 |
| 完全な型消去 | テンプレート + Concept | ゼロオーバーヘッド維持 |
| 動的ポリモーフィズム | 静的ポリモーフィズム | vtable回避の設計原則 |
| 独立パッケージ | `umiport-boards`内統合 | パッケージ数抑制 |

### 最終推奨

**短期（現在）**: `umiport-boards/include/udev/` として実装
- 理想のAPI設計は維持
- バス抽象は `umibus::I2CDevice` として簡易実装

**長期（将来）**: `umibus` と `umidevice` の独立パッケージ化
- デバイス種類が10種類以上になったら検討
- 再利用性が実証された後に分離

