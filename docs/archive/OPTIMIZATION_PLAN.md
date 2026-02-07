# UMI-OS 最適化方針（現行）

**更新日**: 2026-01-29
**対象**: stm32f4_kernel / USB / Shell

本書は「実装済み」「採用方針」「未実施」を明確にした**現行方針**である。

## 1. 現状（要点）

- **LTO**: 効果は実測済みだが、**デフォルトは無効**。
- **コマンドディスパッチ**: `if` 連鎖のまま（ハッシュ化未導入）。
- **セクションGC**: stm32f4_kernelでは未設定。
- **DMAリングバッファ**: USB側には存在するが、カーネルのオーディオ処理パスには未反映。

## 2. LTO方針

### 2.1 方針

- **Releaseのみ有効化**（Debugは無効のまま）。
- 理由: サイズ/性能の改善が確認済みで、Debugでのデバッグ性低下を避けるため。

### 2.2 実測結果（thin LTO）

| 指標 | LTOなし | LTO (thin) | 削減 |
|------|---------|------------|------|
| text (Flash) | 41,692 B | 40,344 B | -1,348 B (3.2%) |
| data | 84 B | 20 B | -64 B |
| bss (RAM) | 77,736 B | 72,616 B | -5,120 B (6.6%) |

### 2.3 注意点

- アセンブリから参照されるシンボルに `[[gnu::used]]` が必要。
- 現状は [examples/stm32f4_kernel/src/arch.cc](../../examples/stm32f4_kernel/src/arch.cc) に付与済み。

### 2.4 設定位置

- LTO有効化スイッチ: [examples/stm32f4_kernel/xmake.lua](../../examples/stm32f4_kernel/xmake.lua)

## 3. Shellコマンド最適化

### 3.1 方針

- コマンド数が増えた場合のみハッシュテーブル化を検討。

### 3.2 現状

- `ShellCommands::execute()` は`if`連鎖のまま。
- 変更対象: [lib/umi/kernel/shell_commands.hh](../../lib/umi/kernel/shell_commands.hh)

## 4. セクションGC

### 4.1 方針

- **Releaseのみ有効化**（Debugは無効のまま）。

### 4.2 現状

- stm32f4_kernelでは **有効化**。

### 4.3 注意事項（必ず確認）

- **割り込みベクタ/例外ハンドラ/アセンブリ参照シンボル**は削除対象になり得る。
- 対策:
	- `[[gnu::used]]` でシンボルを保持
	- リンカスクリプト側で `KEEP()` を使ってセクションを保持
	- 参照経路が静的に見えないもの（間接参照、名前参照）は特に注意

## 5. DMAリングバッファ

### 5.1 方針

- USB側のリングバッファは既存資産として活用。
- カーネル側オーディオ処理へ導入するかは別途設計判断。

### 5.2 現状

- USB側: [lib/umiusb/include/audio_interface.hh](../../lib/umiusb/include/audio_interface.hh)
- カーネル側（stm32f4_kernel）: 未適用

## 6. 非対象（当面維持）

- **キャッシュラインアライメント削除**: false sharing防止のため維持。
- **TimerQueue O(1)**: MaxTimersが小さいため維持。
- **Hw<Impl>共通化**: 効果薄のため維持。

## 7. 運用ガイド（最小）

1. **ReleaseビルドのみLTO有効化**
2. サイズ問題が顕在化したら、セクションGCを検討
3. Shellコマンド数が増えたらハッシュ化を検討

## 8. 参照

- LTO設定: [examples/stm32f4_kernel/xmake.lua](../../examples/stm32f4_kernel/xmake.lua)
- LTO保護シンボル: [examples/stm32f4_kernel/src/arch.cc](../../examples/stm32f4_kernel/src/arch.cc)
- Shell実装: [lib/umi/kernel/shell_commands.hh](../../lib/umi/kernel/shell_commands.hh)
