# Mbed OS

**分類:** RTOS (EOL)
**概要:** ARM が開発した Cortex-M 向け RTOS。targets.json による JSON ベースの継承システムとして最も体系的な実装を持つ。ARM は 2025 年 7 月にサポート終了を発表しており、延長サポートは 2026 年まで。

---

## アーキテクチャ概要

### targets.json の `inherits` チェーン

Mbed OS のボード定義は JSON 継承チェーンで構造化される。

```json
{
  "Target": {
    "boot_stack_size": "0x400",
    "default_lib": "std"
  },
  "MCU_STM32": {
    "inherits": ["Target"],
    "device_has_add": ["SERIAL", "STDIO_MESSAGES", "FLASH"],
    "extra_labels_add": ["STM32"]
  },
  "MCU_STM32F4": {
    "inherits": ["MCU_STM32"],
    "core": "Cortex-M4F",
    "device_has_add": ["SERIAL_ASYNCH", "FLASH", "MPU"],
    "extra_labels_add": ["STM32F4"]
  },
  "MCU_STM32F411xE": {
    "inherits": ["MCU_STM32F4"],
    "device_has_add": ["TRNG"],
    "mbed_rom_start": "0x08000000",
    "mbed_rom_size": "0x80000",
    "mbed_ram_start": "0x20000000",
    "mbed_ram_size": "0x20000"
  },
  "NUCLEO_F411RE": {
    "inherits": ["MCU_STM32F411xE"],
    "detect_code": ["0740"],
    "device_name": "STM32F411RETx"
  }
}
```

継承チェーンの解決順序:

```
NUCLEO_F411RE → MCU_STM32F411xE → MCU_STM32F4 → MCU_STM32 → Target
```

---

## 主要メカニズム

### `_add` / `_remove` プロパティ修飾子

配列型プロパティの差分管理を可能にする修飾子。

| 修飾子 | 動作 | 例 |
|--------|------|---|
| `property_add` | 親の配列にアイテムを追加 | `"device_has_add": ["USB"]` |
| `property_remove` | 親の配列からアイテムを削除 | `"device_has_remove": ["TRNG"]` |
| `property` (無修飾) | 親の値を完全に上書き | `"mbed_rom_size": "0x40000"` |

```json
{
  "MY_BOARD": {
    "inherits": ["MCU_STM32F411xE"],
    "device_has_add": ["USB_DEVICE"],
    "device_has_remove": ["TRNG"],
    "macros_add": ["MY_BOARD=1"]
  }
}
```

### Python MRO 類似の解決順序

ターゲット解決は Python の Method Resolution Order (MRO) に類似した順序で行われる。多重継承も可能だが、解決順序の把握が困難になる。

```json
{
  "MY_DUAL_BOARD": {
    "inherits": ["MCU_STM32F4", "SOME_MIXIN"],
    "resolution_order_override": ["MCU_STM32F4", "SOME_MIXIN", "MCU_STM32", "Target"]
  }
}
```

### custom_targets.json

プロジェクトルートに配置することで、Mbed OS 本体を変更せずにカスタムターゲットを追加可能。

```json
{
  "MY_CUSTOM_BOARD": {
    "inherits": ["MCU_STM32F411xE"],
    "detect_code": ["FFFF"],
    "device_name": "STM32F411CEUx"
  }
}
```

### モノリシック JSON 問題

targets.json は **1 ファイルに全ターゲットを集約** する巨大モノリシック構造。ファイルが肥大化し、チェーンが深くなると値の出所の追跡が困難になる。

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| 継承メカニズム | ◎ | `inherits` + `_add`/`_remove` が直感的で強力 |
| ユーザー拡張 | ○ | custom_targets.json で本体変更不要 |
| Linter 検証 | ○ | 継承パターンの自動検証が可能 |
| 単一源泉 | ○ | 1 ファイルに集約だが巨大すぎる |
| メモリ定義 | △ | JSON 内の値とリンカのプリプロセッサマクロの二重管理 |
| 解決順序 | △ | MRO 類似の解決は非プログラマに直感的でない |
| 条件分岐 | △ | JSON には条件分岐がなく、複雑なロジック表現が不可能 |
| 保守性 | -- | 巨大モノリシック JSON。EOL により将来性なし |

---

## EOL ステータス

- **ARM は 2025 年 7 月にサポート終了を発表**
- 延長サポートは 2026 年まで
- 新規プロジェクトでの採用は非推奨
- ただし targets.json の設計パターンは学ぶ価値がある

---

## UMI への示唆

1. **`inherits` + `_add`/`_remove`** の差分管理パターンは最も直感的な継承方式。UMI の Lua テーブルマージでも同等の表現力が得られる
2. **モノリシック JSON の失敗** は重要な教訓。UMI はデバイス定義をファイル分割し、1 ファイル = 1 デバイスの原則を守るべき
3. **MRO 類似の複雑な解決順序** は避けるべき。UMI の 2 段継承（family → mcu）のシンプルさを維持する
4. **Linter による継承パターンの自動検証** は UMI でも導入を検討すべき。xmake の check ルールで実現可能

---

## 参照

- [Adding and Configuring Targets](https://os.mbed.com/docs/mbed-os/v6.16/program-setup/adding-and-configuring-targets.html)
- [Mbed OS targets.json (GitHub)](https://github.com/ARMmbed/mbed-os/blob/master/targets/targets.json)
