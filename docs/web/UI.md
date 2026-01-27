# Web Audio UI 設計ガイド

## 1. ゴールと非ゴール

### ゴール

- VST/AU/CLAP っぽい操作感のタイル UI
- ノブ・スライダー中心、セクション分割、密度高め
- Web Audio と Web MIDI で動作
- モバイルからデスクトップまで同一設計で破綻しない
- 将来 Max/Pd 風ノード UI も同一基盤で共存

### 非ゴール

- 最初から DAW レベルの編集機能、タイムライン、Undo 完全実装
- 最初から全ブラウザで同一動作保証
- 最初から AudioWorklet WASM の最適解を決め打ち

---

## 2. アーキテクチャの柱

### 2.1 状態・ロジック・描画の分離

レイヤを明確に分ける。依存方向も固定する。

| レイヤ | 責務 |
|--------|------|
| `domain` | 変換、単位、スケール、範囲、量子化、MIDI マッピング規則、プリセットのスキーマ |
| `engine` | `AudioContext`、`AudioWorklet`、`Worker`、`AudioParam` への適用とスムージング |
| `ui_headless` | 入力ジェスチャ、フォーカス、キーバインド、a11y、値の更新頻度制御、ドラッグスケーリング |
| `ui_styled` | 見た目のみ、tokens のみ参照、SVG/Canvas/HTML の描画 |

> **重要**: UI は engine を直接触らない。`Param` を通す。

### 2.2 headless と styled の分離

同じノブを別スキンで差し替える前提。

**headless knob:**
- pointer 操作
- fine モード
- wheel
- double click reset
- keyboard 操作

**styled knob:**
- 表示のみ
- SVG か Canvas

> **重要**: headless が UI フレームワーク依存を持たないこと。

### 2.3 Design Tokens を単一ソース

token は設計の契約。コードの都合で増やさない。

**最低限のカテゴリ:**
- `color`
- `space`
- `radius`
- `border`
- `shadow`
- `typography`
- `motion`
- `z`

CSS Variables は出力形式。入力は tokens JSON を推奨。

---

## 3. UI 基盤の選択

### A. Web Components 中核

**向いている条件:**
- ノブやメータなど特殊 UI を複数プロジェクトで再利用したい
- フレームワークを変える可能性がある
- Shadow DOM でスタイル衝突を避けたい

**設計方針:**
- tokens は host から CSS Variables で注入
- 共有スタイルは `adoptedStyleSheets`

### B. フレームワーク中心

**向いている条件:**
- 画面の状態が複雑
- ノード UI を含めた編集機能が大きい
- DX を重視

**設計方針:**
- headless を純 TS として外出し
- UI は薄くする

### 結論

寿命を伸ばすなら Web Components か headless 純 TS を必ず入れる。

---

## 4. レスポンシブ戦略

### Container Queries を基本

viewport 基準はコンポーネント再利用を壊す。入れ物基準にする。

```css
/* tile を container として定義 */
.tile { container-type: inline-size; }

/* tile 内のレイアウトは container query で分岐 */
@container (min-width: 520px) {
  .kit { grid-template-columns: repeat(4, minmax(120px, 1fr)); }
}
```

### CSS 詳細度の破綻を避ける

Cascade Layers を使い、層で固定。

**推奨レイヤ:**
1. `reset`
2. `tokens` / `theme`
3. `components`
4. `utilities`

---

## 5. プラグイン風 UI の構成

### セクション Tile

**責務:**
- 見出し
- バイパス
- プリセット
- 折りたたみ

### パラメータ UI

**必須の部品:**
- `Knob`
- `Slider`
- `Toggle`
- `Value readout`
- `Unit formatter`

**必須の挙動:**
- double click → default
- shift → fine
- wheel
- keyboard
- value の量子化対応
- modified 状態表示

### 表示系

- `Scope`
- `Spectrum`
- `Meter`

> Waveform は要件が増えるので別枠。

---

## 6. Param 設計

### 正規化を唯一の真実にする

`Param` 内部は `0..1` のみ。実値は変換で得る。

**Param が持つもの:**

```typescript
interface Param {
  id: string;
  default01: number;
  toValue(v01: number): number;  // 0..1 → 実値
  to01(value: number): number;   // 実値 → 0..1
  format(value: number): string; // 表示用文字列
  apply(value: number): void;    // engine へ適用
}
```

`apply` は engine 側へ委譲し、`AudioParam` は smoothing 前提。

### 更新頻度の規約

- UI の `pointermove` はそのまま apply しない
- UI は 60fps でも、engine にはレート制限する
- `AudioParam` は `setTargetAtTime` か `setValueAtTime` を規約化

---

## 7. ノブ UI の実装規約

> **ここが今回の破綻点。**

### 原則

**表示は単調でなければならない**

値に応じて SVG arc の解が切り替わる方式は禁止。

### 禁止例

```javascript
// ❌ 始点と終点だけで A コマンドを生成し、largeArc を value で切り替える
const largeArc = value > 0.5 ? 1 : 0;
arc.setAttribute('d', `M ${p0.x} ${p0.y} A ${r} ${r} 0 ${largeArc} ${sweep} ${p1.x} ${p1.y}`);
```

### 推奨例

```javascript
// ✅ リングは固定描画、インジケータだけ回転させる
indicator.style.transform = `rotate(${angle}deg)`;

// ✅ もしくは stroke-dasharray / dashoffset
arc.setAttribute('stroke-dashoffset', String(circumference * (1 - value)));
```

この方式にすると「途中で膨らむ」問題は構造的に発生しない。

---

## 8. Web Audio と UI の接続

### スレッド設計

- UI は軽くする
- 重い処理は `Worker`
- 実時間処理は `AudioWorklet`

### Audio 入出力デバイス

```javascript
// デバイス一覧取得
const devices = await navigator.mediaDevices.enumerateDevices();
const inputs = devices.filter(d => d.kind === 'audioinput');

// デバイス指定で取得
const stream = await navigator.mediaDevices.getUserMedia({
  audio: { deviceId: { exact: deviceId } }
});
```

> **注意**: `AudioContext` の起動はユーザー操作イベントから。

---

## 9. MIDI

### MIDI Learn

1. Learn on
2. 次の入力で binding
3. CC 相対方式も想定した抽象を用意

### 保存対象

- binding の一覧
- Param の `0..1` 値

---

## 10. ノード UI との共存設計

### 重要な分離

- **UI graph** と **audio graph** は別物
- UI graph は保存と編集のためのデータ
- audio graph は実行時にコンパイル生成される

### 実装方針

1. graph JSON を正規化して保存
2. compile で Web Audio ノードへ変換
3. 将来は AudioWorklet 内に本体を移す

---

## 11. 推奨ディレクトリ構成

```
domain/
  param.ts
  scale.ts
  units.ts
  preset.ts
  midi_map.ts
engine/
  engine.ts
  worklet/
ui_headless/
  knob_logic.ts
  slider_logic.ts
  learn.ts
ui_components/
  knob_view_svg.ts
  meter_canvas.ts
  tile.ts
app/
  screens/
  state/
```

Web Components を使うなら `ui_components` が custom elements になる。

---

## 12. 実装チェックリスト

- [ ] Param は `0..1` が唯一
- [ ] UI は Param の `set01` しか呼ばない
- [ ] AudioParam は smoothing 規約に従う
- [ ] ノブ表示は rotate 方式か dashoffset 方式
- [ ] レイアウトは container query 優先
- [ ] tokens 以外の固定値をコンポーネント内に持たない
- [ ] MIDI Learn は binding を永続化できる
- [ ] ノード UI は JSON を正規化できる
