# ジッター補正設計

受信・送信の両方向で、メッセージのタイミング精度を確保する仕組み。

---

## 概要

| 手法 | 方向 | 精度 | 条件 |
|------|------|------|------|
| HW タイムスタンプ (内部) | 受信 | サンプル精度 | 常時利用可 |
| JR Timestamp (MIDI 2.0) | 受信 | ~32μs | 送信側が JR 対応の場合 |
| JR Timestamp (MIDI 2.0) | 送信 | ~32μs | 受信側が JR 対応の場合 |
| タイマー駆動送出 | 送信 | バッファ境界精度 | 常時利用可 |

---

## 1. 受信側: 内部 HW タイムスタンプ

### 問題

System Service がメッセージを処理するタイミングは「ISR が RawInputQueue に入れた時刻」ではなく「System Service が読み出した時刻」。この差がジッターになる。

```
実際の受信時刻     System が処理する時刻    差 = ジッター
──────┼────────────────┼──────────────────
      t0               t0 + Δ              Δ = 0〜数ms
```

### 解決

RawInputQueue の各要素にハードウェアタイマー値を記録する。
RawInput の定義は [EVENT_SYSTEM_DESIGN.md](EVENT_SYSTEM_DESIGN.md) を参照。

ISR で記録:
```cpp
void uart_midi_isr() {
    RawInput input;
    input.hw_timestamp = DWT->CYCCNT;  // サイクルカウンタ
    // ... parse byte, fill payload ...
    raw_input_queue.push(input);
}
```

### System Service での sample_offset 算出

```cpp
uint32_t calc_sample_offset(uint32_t hw_timestamp) {
    uint32_t now_cycles = DWT->CYCCNT;
    uint32_t elapsed_cycles = now_cycles - hw_timestamp;
    float elapsed_seconds = float(elapsed_cycles) / cpu_frequency;

    // DMA バッファの現在書き込み位置（= 現在のサンプル位置）
    uint32_t current_sample = get_dma_sample_position();

    // 受信時点のサンプル位置を逆算
    uint32_t elapsed_samples = uint32_t(elapsed_seconds * sample_rate);
    int32_t sample_offset = int32_t(current_sample) - int32_t(elapsed_samples);

    // 前のバッファに属する場合はクランプ
    if (sample_offset < 0) sample_offset = 0;

    return uint32_t(sample_offset);
}
```

### 精度

DWT サイクルカウンタ (168MHz) で 1 サイクル = ~6ns。
サンプル精度 (48kHz = ~20.8μs) に対して十分。

### 適用対象

| 入力ソース | HW タイムスタンプ取得方法 |
|-----------|----------------------|
| UART MIDI | RXNE 割り込み or DMA + NDTR 差分から推定 |
| USB MIDI | SOF カウンタ + パケット受信タイミング |
| GPIO (ボタン) | EXTI 割り込み |
| ADC (ノブ/CV) | DMA 完了割り込み（バッチ処理のため精度は粗い） |

UART の DMA ポーリングモードでは個々のバイトの受信時刻が取れないため、
ポーリング時点のタイムスタンプを使う（最悪 1ms の誤差 = ポーリング周期）。
これは MIDI 1.0 の伝送遅延 (~1ms/message) と同等なので実用上問題ない。

---

## 2. 受信側: MIDI 2.0 JR Timestamp

### JR Timestamp とは

MIDI 2.0 UMP で規定された Jitter Reduction Timestamp。
MT=0x0, Status=0x0020 の Utility Message。

```
JR Timestamp UMP (32bit):
  [MT=0:4][Group=0:4][Status=0x20:8][Timestamp:16]

Timestamp: 1 tick = 1/31250 秒 (= 32μs)
  16bit → 最大 2.097 秒の範囲（ラップアラウンド）
```

送信側が「このメッセージはこの時刻に発生した」という意図を伝える仕組み。
受信側はこのタイムスタンプを使って、トランスポート遅延やバッファリングのジッターを補正する。

### 受信処理

```cpp
// System Service 内の JR Timestamp 状態
struct JrTimestampState {
    uint16_t base_tick = 0;          // 最新の JR Timestamp 値
    uint32_t base_sample = 0;        // その受信時のサンプル位置
    bool valid = false;              // JR Timestamp を受信済みか

    static constexpr float TICKS_PER_SECOND = 31250.0f;
};

JrTimestampState jr_state;

void handle_jr_timestamp(const umidi::UMP32& ump) {
    jr_state.base_tick = ump.data() & 0xFFFF;  // 下位 16bit
    jr_state.base_sample = get_current_sample_position();
    jr_state.valid = true;
}

// JR Timestamp 付きメッセージの sample_offset 算出
uint32_t apply_jr_correction(uint16_t jr_tick) {
    if (!jr_state.valid) {
        return 0;  // JR 未受信ならフォールバック
    }

    // JR Timestamp からの経過 tick（ラップアラウンド考慮）
    int16_t delta_ticks = int16_t(jr_tick - jr_state.base_tick);

    // tick → サンプル数変換
    // samples_per_tick = sample_rate / 31250
    // 48000 / 31250 = 1.536
    float delta_samples = float(delta_ticks) * samples_per_jr_tick;

    int32_t sample_offset = int32_t(jr_state.base_sample) + int32_t(delta_samples);

    // バッファ範囲にクランプ
    if (sample_offset < 0) sample_offset = 0;
    if (sample_offset >= int32_t(buffer_size)) sample_offset = buffer_size - 1;

    return uint32_t(sample_offset);
}
```

### HW タイムスタンプとの使い分け

```
JR Timestamp あり → JR Timestamp を優先（送信側の意図を反映）
JR Timestamp なし → HW タイムスタンプでフォールバック
```

JR Timestamp は**オプショナル**。対応しない機器からのメッセージでも
HW タイムスタンプにより内部ジッターは補正される。

---

## 3. 送信側: タイマー駆動送出

### 問題

process() 内で生成されたイベント（NoteOn 等）は、バッファ内の任意のサンプル位置に紐づく。
これを物理的に正確なタイミングで送出したい。

### UART MIDI の場合

31250bps で 1 バイト = 320μs。3 バイトメッセージ = ~1ms。
サンプル精度（~21μs）の送出は物理的に不可能。

実用的な方式: **バッファ境界で一括送出**。

```
process() がイベントを output_events に書く
  → Audio ブロック完了
  → System Service: output_events を読み出し
  → sample_offset 順にソート
  → UART 送信キューに入れて DMA 送出
```

バッファサイズ 256 samples / 48kHz = ~5.3ms の粒度。
MIDI 1.0 の伝送遅延 (~1ms) を考えると、これ以上の精度は送信側で保証しても
受信側で相殺される。

### USB MIDI の場合

USB MIDI は SOF (1ms) 単位のパケット送信。
Audio バッファ（~5ms）内の複数メッセージを 1 USB パケットにまとめて送出。

sample_offset 情報はパケット内では失われるが、
JR Timestamp を付与することで受信側が復元できる。

---

## 4. 送信側: JR Timestamp 付与

MIDI 2.0 対応ホスト（DAW 等）に送信する場合、
メッセージの前に JR Timestamp UMP を挿入してサンプル精度の時刻情報を伝える。

```cpp
void send_with_jr_timestamp(const umidi::UMP32& ump, uint32_t sample_offset) {
    // sample_offset → JR tick 変換
    // jr_tick = sample_offset / samples_per_jr_tick
    uint16_t jr_tick = uint16_t(
        float(sample_offset) / samples_per_jr_tick + jr_base_tick
    );

    // JR Timestamp UMP を先に送出
    umidi::UMP32 jr_ump;
    jr_ump.set_raw((0x00 << 24) | (0x0020 << 8) | 0);  // MT=0, Status=0x20
    jr_ump.set_timestamp(jr_tick);
    usb_midi_enqueue(jr_ump);

    // 本体メッセージを送出
    usb_midi_enqueue(ump);
}
```

### JR Timestamp 基準の管理

```cpp
// フレーム先頭で JR base を更新
void on_audio_block_start() {
    jr_base_tick += uint16_t(float(buffer_size) / samples_per_jr_tick);
    // 16bit ラップアラウンドは自然に発生（仕様通り）
}
```

### 送信の有効化

JR Timestamp 付与はオプション。
MIDI CI Protocol Negotiation で相手が MIDI 2.0 + JR 対応であると確認された場合のみ有効化。
非対応機器には従来通り JR なしで送出。

---

## 設計上の位置づけ

```
受信:
  ISR → hw_timestamp 記録 → RawInputQueue
    → System Service:
        JR Timestamp あり → JR 基準で sample_offset 算出
        JR Timestamp なし → HW timestamp 基準で sample_offset 算出
    → AudioEventQueue / ControlEventQueue / SharedState

送信:
  process() → output_events (sample_offset 付き)
    → System Service:
        JR 有効 → JR Timestamp UMP 挿入 → 送信キュー
        JR 無効 → そのまま送信キュー
    → USB/UART 送出
```

ジッター補正は全て System Service 内で完結する。
Processor / Controller はサンプル位置だけを扱い、補正の詳細を知る必要がない。
