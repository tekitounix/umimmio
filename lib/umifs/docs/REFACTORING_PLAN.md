# FS リファクタリング計画

## 目的

littlefs / FATfs の C++23 ポートについて、コアロジック（元ライセンス維持）と
UMI 独自設計部分（UMI ライセンス）を明確に分離し、モダン C++23 で書き直す。

## 現状分析

### ファイル構成と行数

| ファイル | 行数 | 内容 |
|----------|------|------|
| `littlefs/lfs.cc` | 6254 | 全ロジック（C スタイル static 関数群） |
| `littlefs/lfs.hh` | 167 | Lfs クラス宣言（Public API） |
| `littlefs/lfs_types.hh` | 250 | 型定義、enum、構造体 |
| `littlefs/lfs_config.hh` | 119 | LfsConfig + make_lfs_config |
| `littlefs/lfs_util.hh` | 154 | ユーティリティ（CRC, endian, bit ops） |
| `fatfs/ff.cc` | 2306 | 全ロジック（FatFs メンバ関数群） |
| `fatfs/ff.hh` | 124 | FatFs クラス宣言（Public API） |
| `fatfs/ff_types.hh` | 182 | 型定義、enum、構造体 |
| `fatfs/ff_config.hh` | 64 | コンフィグ定数 |
| `fatfs/ff_diskio.hh` | 131 | DiskIo + make_diskio |
| `fatfs/ff_unicode.hh` | 20 | Unicode 変換宣言 |
| `fatfs/ff_unicode.cc` | 222 | CP437 ↔ Unicode テーブル + 大文字変換 |

### 現在の問題点

1. **ライセンス混在**: 全ファイルに元ライセンスヘッダが付いているが、型定義や設定は仕様から書ける
2. **C スタイル残存**: lfs.cc は `using lfs_t = Lfs` で型を橋渡しし、`#define` でC定数を再定義している
3. **Lfs クラスの二重構造**: Public API は Lfs メンバ関数だが、内部は全て static 関数で lfs_t* を受け取る。Lfs はメンバを全て public にして C 構造体として使っている
4. **FatFs は比較的整理済み**: クラスメンバ関数化されているが、ff.cc 内の定数定義が冗長

---

## コアロジック vs UMI 独自実装の分類

### littlefs — コアロジック（元ライセンス維持必須）

以下は littlefs のオンディスクフォーマット・アルゴリズムに直結し、独自実装が困難/危険:

| 機能 | lfs.cc 内の主要関数 | 理由 |
|------|---------------------|------|
| **ブロックキャッシュ** | `lfs_bd_read`, `lfs_bd_prog`, `lfs_bd_flush`, `lfs_bd_sync` | COW キャッシュ整合性 |
| **メタデータペア管理** | `lfs_dir_fetchmatch`, `lfs_dir_commit`, `lfs_dir_compact` | アトミック更新の根幹 |
| **タグシステム** | `lfs_dir_get`, `lfs_dir_getslice`, `lfs_dir_traverse` | ログ構造メタデータ |
| **ブロックアロケータ** | `lfs_alloc`, `lfs_alloc_scan`, `lfs_alloc_ckpoint` | ウェアレベリング |
| **CTZ スキップリスト** | `lfs_ctz_find`, `lfs_ctz_extend`, `lfs_ctz_traverse` | ファイルデータ管理 |
| **グローバルステート** | `lfs_gstate_*`, `lfs_fs_deorphan`, `lfs_fs_forceconsistency` | 電断リカバリ |
| **CRC** | `lfs_crc` | データ整合性検証 |
| **コミットCRC** | `lfs_dir_commitcrc`, `lfs_dir_commitattr` | アトミックコミット |

### littlefs — UMI 独自実装可能

| ファイル/機能 | 理由 |
|---------------|------|
| `lfs_types.hh` (型定義) | 仕様から書ける。enum class, 構造体レイアウトは独自設計 |
| `lfs_config.hh` (設定) | BlockDeviceLike アダプタは完全に UMI 設計 |
| `lfs_util.hh` (ユーティリティ) | min/max/align/endian/popcount は汎用。CRC 宣言のみ |
| `lfs.hh` (Public API) | クラスインターフェースは独自設計 |
| **エラーコード** | 値は POSIX 準拠で仕様由来。enum class 化は独自 |
| **lfs.cc 内の #define 群** | lfs.cc 冒頭の 160 行の互換 define はリファクタリングで消せる |
| **Lfs:: メンバ関数ラッパー** | lfs.cc 末尾の 160 行は単なる委譲 |

### FATfs — コアロジック（元ライセンス維持必須）

| 機能 | ff.cc 内の主要関数 | 理由 |
|------|---------------------|------|
| **FAT テーブル操作** | `get_fat`, `put_fat` | FAT12/16/32 テーブル読み書き |
| **クラスタチェーン管理** | `create_chain`, `remove_chain` | ファイル割り当て |
| **ディレクトリエントリ解析** | `dir_read`, `dir_find`, `dir_register`, `dir_remove` | FAT ディレクトリ構造 |
| **LFN 処理** | `cmp_lfn`, `pick_lfn`, `put_lfn`, `gen_numname`, `sum_sfn` | Long File Name 仕様実装 |
| **パス解析** | `follow_path`, `create_name` | SFN/LFN パス変換 |
| **ボリュームマウント** | `mount_volume`, `check_fs`, `find_volume` | BPB/FAT 解析 |
| **ウィンドウ管理** | `sync_window`, `move_window` | セクタバッファ管理 |
| **FS 同期** | `sync_fs` | FSInfo 更新 |

### FATfs — UMI 独自実装可能

| ファイル/機能 | 理由 |
|---------------|------|
| `ff_types.hh` (型定義) | FAT 仕様から書ける |
| `ff_config.hh` (設定) | 純粋な定数定義 |
| `ff_diskio.hh` (DiskIo) | BlockDeviceLike アダプタは完全に UMI 設計 |
| `ff.hh` (Public API) | クラスインターフェースは独自設計 |
| `ff_unicode.cc/hh` | CP437 テーブルは Unicode 仕様由来。大文字変換テーブルも仕様 |
| **ff.cc 内の定数定義** | BPB オフセット等は FAT 仕様由来。#define → constexpr 化は独自 |
| **ff.cc のヘルパー** | `ld_16`, `ld_32`, `st_16`, `st_32`, 文字判定等は汎用 |
| **Disk I/O ラッパー** | `disk_initialize` 等は DiskIo 委譲 |

---

## リファクタリング後のファイル構成

```
lib/umios/fs/
  little/
    lfs.hh              ← UMI license — Public API (Lfs class)
    lfs_types.hh         ← UMI license — 型定義、enum class
    lfs_config.hh        ← UMI license — LfsConfig + make_lfs_config
    lfs_util.hh          ← UMI license — ユーティリティ
    lfs_core.cc          ← BSD-3-Clause — コアアルゴリズム（COW, メタデータ, アロケータ等）
  fat/
    fat.hh               ← UMI license — Public API (FatFs class)
    fat_types.hh         ← UMI license — 型定義、enum class
    fat_config.hh        ← UMI license — コンフィグ
    fat_diskio.hh        ← UMI license — DiskIo + make_diskio
    fat_unicode.hh       ← UMI license — Unicode 変換（仕様準拠で独自実装）
    fat_unicode.cc       ← UMI license — CP437 テーブル + 大文字変換
    fat_core.cc          ← FatFs license — コアアルゴリズム（FAT テーブル, ディレクトリ等）
  docs/
    REFACTORING_PLAN.md  ← このファイル
  test/
    test_lfs.cc          ← 網羅テスト（littlefs）
    test_fat.cc          ← 網羅テスト（FATfs）
    test_lfs_compare.cc  ← C++23ポート vs リファレンス実装 比較テスト
    test_fat_compare.cc  ← C++23ポート vs リファレンス実装 比較テスト
```

---

## 実施フェーズ

### Phase 0: テスト基盤構築（現在）

既存テスト（`tests/test_littlefs.cc`, `tests/test_fatfs.cc`）のカバレッジを拡充し、
`fs/test/` に配置。リファクタリング前後で全テストが通ることを保証する。

**テスト項目（littlefs）:**

| カテゴリ | テスト |
|----------|--------|
| 基本 | format, mount, unmount |
| ファイル | open, close, read, write, seek, tell, truncate, rewind, size |
| ディレクトリ | mkdir, dir_open, dir_close, dir_read, dir_seek, dir_tell, dir_rewind |
| 操作 | remove (ファイル), remove (ディレクトリ), rename |
| 属性 | stat, getattr, setattr, removeattr |
| FS レベル | fs_stat, fs_size, fs_traverse, fs_mkconsistent, fs_gc, fs_grow |
| エッジケース | 大ファイル（マルチブロック）, 深いディレクトリ, 長いファイル名, ディスクフル |
| 永続性 | unmount → remount でデータ保持 |

**テスト項目（FATfs）:**

| カテゴリ | テスト |
|----------|--------|
| 基本 | mount, unmount |
| ファイル | open (各モード), close, read, write, lseek, truncate, sync |
| ディレクトリ | opendir, closedir, readdir, mkdir |
| 操作 | unlink (ファイル), unlink (ディレクトリ), rename, stat |
| ボリューム | getfree |
| LFN | 長いファイル名の作成・読み取り |
| エッジケース | 大ファイル（マルチクラスタ）, 多ファイル, ディスクフル |

### Phase 1: ファイル分離・リネーム

- `lfs.cc` → `lfs_core.cc`（ライセンスヘッダ更新）
- `ff.cc` → `fat_core.cc`（ライセンスヘッダ更新）
- 型定義・設定・ユーティリティファイルのライセンスヘッダを UMI に変更
- `ff_*.hh/cc` を `fat_*.hh/cc` にリネーム（命名統一）
- **各ステップ後にテスト実行して動作確認**

### Phase 2: UMI 独自ヘッダの書き直し

型定義・設定・ユーティリティを C++23 モダンスタイルで再実装:

- `std::bit_ceil`, `std::countl_zero`, `std::popcount` 等の C++20/23 機能を活用
- `inline constexpr` → `constexpr` のみに統一（C++17 以降は暗黙的に inline）
- `lfs_types.hh`: `using` エイリアス → C++23 型に合わせた見直し
- `fat_types.hh`: 同様
- enum class のビット演算をテンプレートで統一
- 命名規則を CODING_STYLE.md に準拠させる（lower_case 関数名、CamelCase 型名、UPPER_CASE enum 値、lower_case constexpr定数）
- **各ステップ後にテスト実行して動作確認**

### Phase 3: コアファイルの内部リファクタリング

`lfs_core.cc`, `fat_core.cc` 内部を段階的にモダン化:

- `#define` → `constexpr` / `enum`（コンパイル可能な範囲で）
- C キャスト → `static_cast`（既にほぼ完了）
- `using lfs_t = Lfs` ブリッジの段階的解消
- `goto` → ループ構造化（可能な範囲で、アルゴリズム変更なし）
- **元のライセンスヘッダは維持**
- **各ステップ後にテスト実行して動作確認**

### Phase 4: Unicode 独自実装

`fat_unicode.cc/hh` を Unicode 仕様に基づいて独自実装:

- CP437 ↔ Unicode テーブルは Unicode コンソーシアムの公開マッピングデータから生成
- 大文字変換テーブルは UnicodeData.txt から生成
- **UMI ライセンスに変更**

---

## リスク管理

| リスク | 対策 |
|--------|------|
| リファクタリングでバグ混入 | Phase 0 で網羅テスト構築。各ステップ後にテスト実行 |
| オンディスクフォーマット互換性破壊 | コアロジックのアルゴリズムは変更しない。定数値も保持 |
| ライセンス判断ミス | コアロジック（COW, FAT テーブル操作等）は保守的に元ライセンス維持 |
| ビルド破壊 | ファイル分離・リネーム時は xmake.lua の更新を忘れない |

---

## 完了条件

1. 全テスト（Phase 0 で作成した網羅テスト）が通る
2. コアロジックファイルに元ライセンスが正しく残っている
3. UMI 独自ファイルに元ライセンスが含まれていない
4. C++23 コーディングスタイル（CLAUDE.md 準拠）が適用されている
5. `xmake test` が全て通る
