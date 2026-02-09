# umiport と umiport-boards の分離評価

## 問い

`umiport` と `umiport-boards` の2パッケージ分離は正しい設計と言えるか？

---

## 結論

**基本正しいが、境界の定義に改善の余地あり**

依存関係の方向性（ボード→MCU）は正しく、分離の意図も妥当。ただし、両者の責務境界がやや曖昧な箇所があり、より厳格な分離が可能。

---

## 1. 分離が正しい理由

### 1.1 依存関係の自然さ

```
umiport (MCUレジスタ操作)
    ↑
umiport-boards (ボード統合)
```

この方向性は自然—ボードがMCUを知るのは妥当だが、逆は不自然。

### 1.2 startup.cc の位置づけ

startup.cc が `umiport-boards` にある正当性：

```cpp
// startup.cc の中身を見ると...
#include "platform.hh"  // ← ボード固有のヘッダをインクルード
void Reset_Handler() {
    // ...
    umi::port::Platform::init();  // ← ボードのPlatformに依存
}
```

**startup.cc は厳密には「MCUコード」ではなく「ボード統合コード」**：
- ベクタテーブルはMCU固有
- しかし初期化シーケンスはボードのPlatformに依存
- 同じSTM32F4でも、クロック設定はボードによって異なる

### 1.3 共有の効率性

```
umiport-boards/src/stm32f4/
├── startup.cc      ← 同一MCUの全ボードで共有
├── syscalls.cc     ← 同一MCUの全ボードで共有
└── linker.ld       ← 同一MCUの全ボードで共有

umiport-boards/include/board/
├── stm32f4-renode/
│   ├── platform.hh  ← ボード固有
│   └── board.hh     ← ボード固有
└── stm32f4-disco/
    ├── platform.hh  ← ボード固有
    └── board.hh     ← ボード固有
```

同一MCUの別ボードを追加する際、startup/syscalls/linker を再利用できる設計は**効率的**。

---

## 2. 境界の曖昧さ（改善が必要）

### 2.1 問題: umiport の責務が揺らいでいる

```
umiport/
├── include/umiport/
│   ├── arm/cortex-m/     ← アーキ共通（責務明確）
│   │   └── dwt.hh
│   └── mcu/
│       └── stm32f4/      ← MCUレジスタ操作（責務明確）
│           ├── rcc.hh
│           └── uart.hh   ← この中で _write() を使う？
```

**疑問**: `uart.hh` での出力は？
- `UartOutput::putc()` はボードの出力経路と同じ？
- それとも独立？

### 2.2 問題: 出力経路の二重定義

```cpp
// umiport/include/umiport/mcu/stm32f4/uart.hh
struct UartOutput {
    static void putc(char c) {
        // 直接レジスタに書き込む？
        USART1->DR = c;
    }
};

// umiport-boards/include/board/stm32f4-renode/platform.hh
struct Platform {
    using Output = stm32f4::UartOutput;  // これは同じ？
};
```

ここで `UartOutput` が `umiport` にあることの是非：
- ✅ MCU固有のレジスタ操作なので `umiport` にあるべき
- ❓ しかし出力先の選択はボードの責務

---

## 3. より良い境界案

### 案A: 現状維持（推奨度: 中）

```
umiport: MCUレジスタ操作 + レジスタベース出力
umiport-boards: ボード統合 + startup + 出力経路の最終決定
```

**問題**: `UartOutput` はボードでラップされるが、実質的に同じ機能

---

### 案B: より厳格な分離（推奨度: 高）

```
umiport: 純粋なレジスタ操作のみ（出力概念なし）
umiport-boards: すべての出力関連 + ボード統合 + startup
```

```cpp
// umiport: レジスタ操作のみ
struct USART1 {
    static void write_dr(uint8_t data);  // 生のレジスタ操作
    static bool tx_ready();
};

// umiport-boards: 出力ポリシーとして定義
struct UartOutput {
    static void putc(char c) {
        while (!USART1::tx_ready()) {}
        USART1::write_dr(c);
    }
};
```

**利点**: 
- umiport は純粋に「レジスタをどう読み書きするか」のみ
- 出力のセマンティクス（ブロッキング/ノンブロッキング、改行変換など）はボード層

---

### 案C: 統合案（推奨度: 低）

```
umiport: すべて統合（startup含む）
umiport-boards: 削除、board定義のみを残す
```

**問題**: 
- startup が platform.hh に依存する問題が残る
- umiport が「ボード統合」を含むことになり責務が曖昧に

---

## 4. 最終評価

| 評価項目 | 現在の設計 | 改善案B |
|---------|-----------|---------|
| **責務の明確さ** | △ 出力概念が両方にまたがる | ○ 明確に分離 |
| **再利用性** | ○ 同一MCUのボード間で共有 | ○ 同様 |
| **テスト容易性** | △ 出力経路が混在 | ○ レジスタ層は純粋 |
| **拡張性** | ○ 新MCU追加が容易 | ○ 同様 |

---

## 5. 推奨: 軽微な修正で現設計を維持

**現在の分離は「方向性として正しい」**が、以下の微調整を推奨：

### 5.1 umiport は「レジスタアクセス」のみと明確化

- `UartOutput` ではなく `USART1` レジスタ操作のみ提供
- 出力ポリシーは全て boards 層に移動

### 5.2 startup/syscalls/linker の位置は正しい

- platform.hh 依存があるため boards 層が適切

### 5.3 device 統合時の指針

```cpp
// 理想的な統合（将来の umidevice 統合時）
// umiport-boards/include/board/stm32f4-disco/devices.hh
#include <umiport/mcu/stm32f4/i2c.hh>   // レジスタ操作
#include <umibus/i2c_device.hh>          // バス抽象
#include <umidevice/sensor/bme280.hh>    // デバイスドライバ

struct Devices {
    umiport::mcu::stm32f4::I2C1 i2c;           // 生のI2C
    umibus::I2CDevice i2c_bus{i2c, 0x76};      // バス抽象
    umidevice::Bme280 sensor{i2c_bus};         // デバイス
};
```

---

## 6. まとめ

| 問い | 回答 |
|------|------|
| **分離自体は正しいか** | ✅ はい、依存方向が正しい |
| **境界は適切か** | △ やや曖昧—出力概念の整理が欲しい |
| **改善すべきか** | 🟡 軽微な整理（案B方向）を推奨 |
| **全面的な見直しは必要か** | ❌ いいえ、現在の設計は採用価値が高い |

**結論**: umiport と umiport-boards の分離は正しい設計判断。ただし、umiport 内の「出力機能」を boards 層に移すことで、責務がさらに明確になる。
