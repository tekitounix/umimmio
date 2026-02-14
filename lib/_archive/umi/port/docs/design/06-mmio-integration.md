# 06: umi_mmio 統合計画

## 概要

外部リポジトリ `umi_mmio` は、MCU内蔵ペリフェラルと外部デバイス（コーデック、センサ等）のレジスタアクセスを**同一API**で扱う型安全なレジスタ抽象ライブラリ。umiportのレイヤー設計と高い親和性を持つ。

コア部分（mmio.hh, transport/）を `lib/umimmio/` としてumiリポジトリに完全移植して使用する。HAL concepts（gpio, i2c, audio等）はumimmioには含めず、`lib/umiport/concepts/` に配置する。SVD等からのレジスタ定義自動生成ツールも存在するが現時点では未完成。コアヘッダはほぼ完成しており、Transport（元"バックエンド"、umiportの文脈では"port"と同義）を差し替えることで様々なバス・プロトコルに対応する。

## umi_mmio の核心

### 統一レジスタアクセス

```
MCU内蔵レジスタ (GPIOD::ODR @ 0x40020C14)
    → DirectTransport (volatile ポインタ直接アクセス)

外部I2Cデバイス (CS43L22::POWER_CTL1 @ 0x02)
    → I2cTransport (I2Cプロトコル経由)

↓ 同一API: write / read / modify / is / flip
```

### 構造

```
lib/umimmio/include/mmio/         ← レジスタ抽象コア (umimmioに移植)
├── mmio.hh                       # Device → Block → Register → Field → Value 階層
└── transport/
    ├── direct.hh                 # DirectTransport (MMIO)
    └── i2c.hh                    # I2cTransport (I2Cバス経由)

lib/umiport/concepts/             ← HAL concepts (umiportに配置)
├── result.hh                     # Result<T, ErrorCode>
├── gpio.hh                       # GpioPin / GpioPort concept
├── i2c.hh                        # I2cMaster concept
├── uart.hh                       # Uart concept
├── timer.hh                      # Timer / DelayTimer / PwmTimer concept
├── i2s.hh                        # I2sMaster concept
├── interrupt.hh                  # InterruptController concept
└── audio.hh                      # AudioDevice concept
```

### 技術的特徴

| 特徴 | 説明 |
|------|------|
| **CRTP + RegOps** | vtableなしでTransport（≒port）切り替え。ゼロオーバーヘッド |
| **Device-level Transport制約** | MCU内蔵→DirectTransportのみ、外部デバイス→I2C/SPI。コンパイル時検証 |
| **ByteAdapter** | I2C/SPIのバイトプロトコルとレジスタアクセスのブリッジ |
| **複数フィールド最適化** | 同一レジスタの複数フィールド変更→単一RMW操作 |
| **Access Policy** | RW/RO/WO をコンパイル時に検証。ROレジスタへのwriteはコンパイルエラー |
| **HAL Concepts** | C++23 concepts による型安全なHAL定義（umiport/concepts/に配置） |

## umiportレイヤーとの対応

### 現状の問題

現在のumiportのmcu層ファイル（`mcu/rcc.hh`, `mcu/gpio.hh`等）は生のvolatileポインタ操作でレジスタを読み書きしている。これは：

1. **型安全性がない** — アドレスやビットフィールドの誤りがコンパイル時に検出されない
2. **外部デバイスと異なるAPI** — CS43L22のような外部コーデックはI2C経由だが、MCUレジスタとは全く別の書き方
3. **RMW操作が手動** — ビットマスクの手計算によるバグリスク

### umi_mmio統合後のレイヤーマッピング

```
umiport レイヤー          umi_mmio の役割
──────────────────────────────────────────────────────
concepts/                  umi_mmioのhal/ concepts をここに配置
                           (GpioPin, I2cMaster, AudioDevice 等)
                           umimmioはレジスタ抽象のみ、HALはumiportが所有

common/                    共通レジスタ定義 (NVIC, SCB, SysTick, DWT)
                           → mmio Device/Block/Register で定義
                           → DirectTransport でアクセス

arch/cm4/                  CM4固有操作 (cortex_m4.hh)
                           → インラインasm中心のため mmio対象外

mcu/stm32f4/               MCUペリフェラル定義
                           → mmio Device<RW> + DirectTransport
                           例: STM32F4::RCC, STM32F4::GPIO, STM32F4::I2S

board/stm32f4_disco/       外部デバイスドライバ + ボード構成
                           → CS43L22: mmio Device<RW, I2CTransportTag>
                           → I2cTransport でアクセス
                           → bsp.hh: MCUピン設定 + デバイスインスタンス構成

platform/                  ソフトウェア抽象（syscall, privilege）
                           → mmio対象外
```

### 外部デバイスの抽象化パターン

CS43L22を例に、外部デバイスの正しい構造:

```
lib/umiport/
├── concepts/
│   └── codec.hh              ← AudioCodec concept定義
│
├── device/                   ← 新レイヤー: 外部デバイスドライバ
│   └── cs43l22/
│       ├── cs43l22_regs.hh   ← mmio Register定義 (Device<RW, I2CTransportTag>)
│       └── cs43l22.hh        ← AudioCodec conceptの実装
│
├── mcu/stm32f4/mcu/
│   ├── i2c.hh                ← I2Cペリフェラル (mmio DirectTransport)
│   └── gpio.hh               ← GPIO (mmio DirectTransport)
│
└── board/stm32f4_disco/board/
    └── bsp.hh                ← CS43L22をI2C1 + PD4(reset)で接続する構成
```

**ポイント:**
- `device/cs43l22/` はどのボードにも依存しない（I2cMaster conceptのみ要求）
- `board/` がMCUのI2Cペリフェラルとデバイスドライバを結びつける
- 同じCS43L22ドライバがSTM32F4-DiscoでもDaisy PodでもI2C接続なら再利用可能

### 全外部デバイスに共通のパターン

```cpp
// device/cs43l22/cs43l22_regs.hh
namespace umi::device::cs43l22 {

struct CS43L22 : mmio::Device<mmio::RW, mmio::I2CTransportTag> {
    static constexpr mmio::Addr base_address = 0;  // I2Cデバイスはオフセット0

    using ID         = mmio::Register<CS43L22, 0x01, mmio::bits8, mmio::RO>;
    using PowerCtl1  = mmio::Register<CS43L22, 0x02, mmio::bits8>;
    using PowerCtl2  = mmio::Register<CS43L22, 0x04, mmio::bits8>;
    // ...
    struct PowerCtl2Fields {
        using HeadphoneA = mmio::Field<PowerCtl2, 6, 2>;
        using HeadphoneB = mmio::Field<PowerCtl2, 4, 2>;
        using SpeakerA   = mmio::Field<PowerCtl2, 2, 2>;
        using SpeakerB   = mmio::Field<PowerCtl2, 0, 2>;
    };
};

}  // namespace umi::device::cs43l22
```

```cpp
// device/cs43l22/cs43l22.hh
namespace umi::device::cs43l22 {

template <concepts::I2cMaster I2C>
class Cs43l22Driver {
    mmio::I2cTransport<I2C> transport;

public:
    explicit Cs43l22Driver(I2C& i2c, uint8_t addr = 0x94)
        : transport(i2c, addr) {}

    void init() {
        transport.write(CS43L22::PowerCtl1::value(0x01));
        // ...
    }

    void set_volume(uint8_t vol) {
        transport.write(CS43L22::MasterVolA::value(vol));
        transport.write(CS43L22::MasterVolB::value(vol));
    }
};

}  // namespace umi::device::cs43l22
```

```cpp
// board/stm32f4_disco/board/bsp.hh — ボード固有の結線
namespace umi::board::stm32f4_disco {

struct Bsp {
    // MCUペリフェラルインスタンス
    static auto& i2c1() { /* STM32F4 I2C1 instance */ }
    static auto& codec() {
        static device::cs43l22::Cs43l22Driver driver(i2c1(), 0x94);
        return driver;
    }
};

}  // namespace umi::board::stm32f4_disco
```

## umi_mmioの統合方法

### 選択肢

**方針: lib/umimmio/ としてコア部分を完全移植**

- umi_mmio自体がumiプロジェクトのために作られたライブラリ
- 他のlib/と同じ規則（xmake.lua, test/, docs/）に従う
- コア（mmio.hh, transport/）のみ移植。hal/はumiport/concepts/へ。STM32F4テストターゲット等は移植しない
- SVD自動生成ツールは成熟後に別途統合を検討

### 統合手順

```
lib/umimmio/                       ← レジスタ抽象のみ (HALなし)
├── xmake.lua
├── include/
│   └── mmio/
│       ├── mmio.hh                # Device/Block/Register/Field/Value/RegOps/ByteAdapter
│       └── transport/
│           ├── direct.hh          # DirectTransport (MMIO)
│           └── i2c.hh             # I2cTransport (I2Cバス経由)
├── docs/
└── test/
    ├── xmake.lua
    └── test_*.cc
```

### umiportからの依存

```lua
-- lib/umiport/xmake.lua
target("umi.port.embedded.stm32f4_disco")
    add_deps("umi.mmio")  -- mmioライブラリへの依存
    -- ...
```

## umiportへの影響

### 新レイヤー: device/

外部デバイスドライバを格納する新レイヤーを追加:

```
lib/umiport/
├── concepts/          # HAL concepts (umi_mmioのhal/と統合・拡張)
├── device/            # ← 新規: 外部デバイスドライバ (transport非依存)
│   ├── cs43l22/       # Audio DAC
│   ├── wm8731/        # Audio CODEC (Daisy)
│   └── pcm3060/       # Audio CODEC (Daisy Pod v2)
├── common/            # Cortex-M共通レジスタ → mmio化
├── arch/              # CPUコア固有 (変更なし)
├── mcu/               # MCUペリフェラル → mmio Device定義に移行
├── board/             # ボード構成 (MCU + device を結線)
└── platform/          # ソフトウェア抽象 (変更なし)
```

### Concept配置

umimmioにはHAL conceptsを置かない。全てumiport/concepts/に配置する:

```
lib/umiport/concepts/
├── result.hh            # Result<T, ErrorCode> — 共通エラー型
├── gpio.hh              # GpioPin, GpioPort (umi_mmio hal/gpio.hh 由来)
├── i2c.hh               # I2cMaster (umi_mmio hal/i2c.hh 由来)
├── uart.hh              # Uart (umi_mmio hal/uart.hh 由来)
├── timer.hh             # Timer, DelayTimer, PwmTimer (umi_mmio hal/timer.hh 由来)
├── i2s.hh               # I2sMaster (umi_mmio hal/i2s.hh 由来)
├── interrupt.hh         # InterruptController (umi_mmio hal/interrupt.hh 由来)
├── audio.hh             # AudioDevice (umi_mmio hal/audio.hh 由来)
├── codec.hh             # AudioCodec — umiport固有
├── board.hh             # BoardSpec, McuInit — umiport固有
└── fault.hh             # FaultReport — umiport固有
```

**責務分離:**
- **umimmio**: レジスタアクセス抽象のみ（Device/Register/Field + Transport）
- **umiport/concepts/**: ハードウェア抽象のConcept定義（何ができるか）
- **umiport/mcu,board,device/**: Concept実装（どう実現するか）

### MCUレジスタ定義のmmio化

現在のmcu層ファイルを段階的にmmio形式に移行:

```
Phase 0 (現在): 生volatile操作
Phase 1: mmio Device/Register 定義を追加（旧コードと共存）
Phase 2: ドライバコードをmmio API経由に変更
Phase 3: 旧volatile操作コードを削除
```

**優先度**: Daisy Pod実装に必要なペリフェラルから着手
- GPIO → SAI(I2S後継) → I2C → DMA → USB

## まとめ

umi_mmioは umiportの設計思想と完全に合致する:

1. **MCUレジスタと外部デバイスが同一API** → mcu/とdevice/の境界がクリーンに
2. **Transport抽象** → board/が「どのバスでどう接続するか」を定義する役割と一致
3. **HAL Concepts** → umi_mmio由来のconceptsをumiport/concepts/に配置
4. **ゼロオーバーヘッド** → リアルタイム制約に適合
5. **コンパイル時安全性** → #ifdef排除の設計思想と同方向
