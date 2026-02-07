# SysEx / MIDI CI 経路設計

## 問題

SysEx は可変長メッセージ。UMP32 (MT=3, SysEx7) は 1 パケットで最大 3 データバイトしか運べない。
長い SysEx（数百〜数千バイト）は複数 UMP に分割される。

現在のイベントシステムの経路はすべて固定長（8B）前提:

| 経路 | サイズ | SysEx に適するか |
|------|--------|----------------|
| AudioEventQueue | umidi::Event 8B | 不適（断片は無意味） |
| ControlEventQueue | ControlEvent 8B | 不適（同上） |
| Stream | 未定義 | 可能だが生 UMP の羅列 |

SysEx と MIDI CI には専用の経路が必要。

---

## SysEx の性質

| 性質 | 通常メッセージ | SysEx / MIDI CI |
|------|-------------|----------------|
| サイズ | 固定 (3B 以下) | 可変 (数B〜数KB) |
| レイテンシ要求 | サンプル精度〜ms | 数十ms〜数百ms で十分 |
| 処理主体 | Processor / Controller | System (OS) または Controller |
| 頻度 | 高 | 低（設定変更、問い合わせ） |

---

## 経路設計

### SysExBuffer（共有メモリ上の可変長リングバッファ）

System が UMP SysEx パケットを再組み立て → 完全なメッセージとして書き込み。
Controller が読み出し → アプリ固有の解釈。

```
┌──────────────────────────────────┐
│ SysExBuffer (共有メモリ)           │
│                                    │
│ ┌─────┬─────┬─────────────────┐  │
│ │ len │ src │ data[0..len-1]  │  │ ← 1メッセージ
│ ├─────┼─────┼─────────────────┤  │
│ │ len │ src │ data[0..len-1]  │  │ ← 次のメッセージ
│ └─────┴─────┴─────────────────┘  │
│                                    │
│ 可変長リングバッファ                  │
│ System のみ write / Controller のみ read │
└──────────────────────────────────┘
```

```cpp
struct SysExBuffer {
    static constexpr size_t CAPACITY = 512;  // 共有メモリから確保

    uint8_t data[CAPACITY] = {};
    uint16_t write_pos = 0;
    uint16_t read_pos = 0;

    // メッセージフォーマット: [len:2B][source:1B][data:len B]
    // len = 0 はリング末尾の折り返しマーカー
};
```

### System 側の処理

```
UMP SysEx7 受信 (MT=3):
  Status = 0x00: Complete (1パケットで完結)
  Status = 0x10: Start
  Status = 0x20: Continue
  Status = 0x30: End

System Service:
  1. SysEx 再組み立てバッファ（OS RAM、per-source）に蓄積
  2. End または Complete を受信 → SysExBuffer に完全メッセージとして書き込み
  3. MIDI CI メッセージの場合は OS が先に検査（後述）
```

### 再組み立てバッファ

```cpp
struct SysExAssembler {
    static constexpr size_t MAX_SYSEX = 256;  // 1メッセージの最大長

    uint8_t buf[MAX_SYSEX] = {};
    uint16_t pos = 0;
    bool active = false;

    void start() noexcept { pos = 0; active = true; }

    bool append(const uint8_t* data, uint8_t count) noexcept {
        if (!active || pos + count > MAX_SYSEX) {
            active = false;  // オーバーフロー → 破棄
            return false;
        }
        memcpy(&buf[pos], data, count);
        pos += count;
        return true;
    }

    // End 受信時: SysExBuffer に書き込み
    bool complete(SysExBuffer& out, uint8_t source) noexcept {
        if (!active || pos == 0) return false;
        // ... write to ring buffer ...
        active = false;
        return true;
    }
};
```

OS RAM に配置（共有メモリではない）。ソースごとに 1 つ（USB, UART 等）。

---

## MIDI CI の責務分離

MIDI CI は SysEx の上位プロトコル（Universal SysEx ID = 0x7E, Sub-ID = 0x0D）。
同じ SysExBuffer を通るが、一部は OS が直接応答すべきもの。

| MIDI CI カテゴリ | 処理主体 | 理由 |
|-----------------|---------|------|
| Discovery | System (OS) | デバイス情報は OS が保持 |
| Profile Configuration | System (OS) | プロファイル切替は OS の責務 |
| Protocol Negotiation | System (OS) | MIDI 2.0 プロトコル切替 |
| Property Exchange | Controller | アプリ固有パラメータ |

### 処理フロー

```
SysEx 受信
  → System: 再組み立て
  → System: MIDI CI ヘッダ検査
    ├─ Discovery / Profile / Protocol → OS が直接応答
    │   └─ 必要に応じて Controller にもコピー（通知目的）
    │
    └─ Property Exchange / その他 → SysExBuffer に書き込み
       └─ Controller が読み出して処理
```

### RouteTable との関係

RouteTable の `system[0x0]`（= 0xF0, SysEx Start）で制御:

| RouteFlags | 動作 |
|-----------|------|
| `ROUTE_NONE` | OS が完全に処理（MIDI CI Discovery 等） |
| `ROUTE_CONTROL` | OS が再組み立て後 SysExBuffer 経由で Controller に渡す |
| 両方 | OS が処理しつつ Controller にもコピー |

デフォルト: `ROUTE_NONE`（OS が処理、必要な MIDI CI のみ Controller に転送）。
MIDI ルーターアプリなど全 SysEx を見たい場合は `ROUTE_CONTROL` を設定。

---

## Syscall

```
nr::read_sysex (17): SysExBuffer から次のメッセージを読み出し

int read_sysex(uint8_t* buf, uint16_t max_len, uint8_t* source) noexcept;
  戻り値: 読み出したバイト数（0 = 空）
```

---

## メモリ使用量

| コンポーネント | 場所 | サイズ | 備考 |
|---------------|------|--------|------|
| SysExBuffer | 共有メモリ | 516B | CAPACITY=512 + write/read pos |
| SysExAssembler × N | OS RAM | 260B × N | ソースごと（USB, UART = 2なら 520B） |

---

## SysEx 送信

Controller が SysEx を送信する場合:

```
Controller → syscall::send_sysex(data, len, destination)
  → System: UMP SysEx7 パケットに分割
  → System: 送信キューに入れて送出
```

```
nr::send_sysex (18): SysEx メッセージを送信

int send_sysex(const uint8_t* data, uint16_t len, uint8_t destination) noexcept;
  destination: 送信先ポート ID
  戻り値: 0 = 成功, エラーコード
```
