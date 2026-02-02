# umifs クリーンルーム再実装計画

## 目的

littlefs / FATfs のコアロジックを、リファレンス実装（`.refs/fatfs/`, `.refs/littlefs/`）の
問題点を詳細に分析した上で、公開仕様のみに基づく独自のクリーンルーム実装に全面書き換える。

組み込みオーディオシステムでの使用を前提とし、安全性・堅牢性・電源遮断耐性・
クリティカルパス性能を最優先に設計する。

## リファレンス実装の欠陥分析

### FATfs R0.15 (.refs/fatfs/source/ff.c, 7249行)

| ID | 問題 | 重大度 | 詳細 |
|----|------|--------|------|
| F1 | f_rename クロスリンク窓 | **Critical** | 同一クラスタチェーンが2エントリから参照される時間窓あり。コード自体が `/* Start of critical section where an interruption can cause a cross-link */` と認識 |
| F2 | グローバル可変状態 | **Critical** | `FatFs[]`, `Fsid`, `CurrVol`, `LfnBuf[]`, `DirBuf[]`, `CodePage`, `ExCvt` 等がstatic global。複数インスタンス不可、スレッドセーフ不可 |
| F3 | disk_write戻り値無視 | **High** | 2nd FAT書き込み(1068行)、FSInfo書き込み(1129行)の戻り値が未チェック。冗長性が黙って失われる |
| F4 | 循環チェーン無限ループ | **High** | `remove_chain`, `f_lseek` のFATチェーン走査に循環検出なし。破損メディアで永久停止 |
| F5 | f_truncateサイズ不整合 | **Medium** | `remove_chain`エラー後も`objsize`を更新 → 不整合がsyncで永続化 |
| F6 | ヒープ割り当て | **Medium** | `FF_USE_LFN==3`で`ff_memalloc`使用。組み込みリアルタイムに不適 |
| F7 | f_unlinkロストクラスタ | **Medium** | ディレクトリエントリ削除後、チェーン解放前の電源断でロストクラスタ |
| F8 | FAT二重化不整合 | **Medium** | 1st FAT書き込み後、2nd FAT書き込み前の電源断でFAT不整合 |

### littlefs v2.9 (.refs/littlefs/lfs.c, 6549行)

| ID | 問題 | 重大度 | 詳細 |
|----|------|--------|------|
| L1 | ASSERT 70箇所がリリースで無効 | **High** | `LFS_NO_ASSERT`定義時（プロダクション）に破損データで未定義動作。特にbd_read/bd_progの境界チェック |
| L2 | traverse深度制限がASSERTのみ | **High** | `lfs_dir_traverse`の手動スタック深度がASSERTのみ。`LFS_NO_ASSERT`時にスタックバッファオーバーフロー |
| L3 | lfs_npw2の未定義動作 | **Medium** | `a==1`で`__builtin_clz(0)`呼び出し。未定義動作（アーキテクチャ依存で異なる結果） |
| L4 | lock/unlock NULL未検証 | **High** | `LFS_THREADSAFE`有効時、`lfs_init`にコールバックNULLチェックなし |
| L5 | rcacheバッファ暗黙的再利用 | **Medium** | truncate時に`rcache.buffer`を一時バッファとして借用。内部変更で壊れやすい |
| L6 | Cスタイルフラット構造 | **Low** | 6500行のstatic関数群。`using lfs_t = Lfs`でブリッジ。状態管理が不透明 |

## 設計方針

### 共通原則

| 原則 | 説明 |
|------|------|
| **仕様ベース実装** | FAT: Microsoft公開仕様、littlefs: SPEC.md。リファレンスは問題分析のみに参照 |
| **全てインスタンスメンバ** | グローバル可変状態を完全排除。複数インスタンス安全 |
| **ASSERT禁止** | 全チェックをエラーコード返却に。プロダクションでも防御的動作 |
| **ヒープ禁止** | 全バッファはユーザー提供 or インスタンス内静的配列 |
| **クリティカルパス最適化** | read/writeの関数呼び出し深度を最小化。オーディオ再生に耐える低レイテンシ |
| **電源遮断耐性** | 全書き込み操作で中断安全な順序を保証 |

### FATfs 設計

**FAT12/16/32仕様（Microsoft公開）に基づくゼロからの実装。**

| 項目 | 設計 |
|------|------|
| セクタウィンドウ | 512Bバッファ。セクタアラインのフルセクタはBD直接アクセス（ウィンドウバイパス） |
| FATテーブル | get_fat/put_fatの3分岐(12/16/32)。FAT12の1.5Bパッキングは仕様通り |
| 循環チェーン検出 | 全チェーン走査にカウンタ上限(`n_fatent`)。超過で`DISK_ERR`返却 |
| rename安全性 | 中間sync挿入でクロスリンク窓を最小化 |
| エラー伝搬 | 全disk_write/disk_read戻り値をチェック・上位に伝搬 |
| truncateの安全性 | remove_chainエラー時はobjsize更新をスキップ |
| LFN | `if constexpr(config::USE_LFN)`でコンパイル時除去可能。バッファはインスタンス内静的 |

### littlefs 設計

**SPEC.md (v2.1オンディスクフォーマット仕様) に基づくゼロからの実装。**

| 項目 | 設計 |
|------|------|
| メタデータペア | 2ブロック交互書き込み。コミットログ構造。CRC-32(0x04c11db7)検証 |
| タグXOR連鎖 | 32bitビッグエンディアンタグの隣接XOR。双方向イテレーション |
| CTZスキップリスト | `std::countr_zero`でポインタ数計算。O(log n)シーク |
| ブロックアロケータ | ルックアヘッドビットマップ。CRC-XORベースのランダムシード |
| gstate | XOR分散デルタ。マウント時全ペア走査で復元。deorphan処理 |
| コンパクション | 50%超で分割。有効エントリのみ再書き込み |
| インラインファイル | ブロックサイズ1/4以下の小ファイルはメタデータペア内格納 |
| 全境界チェック | ASSERT→if+エラーリターン。traverse深度は実行時チェック |

## ファイル構成

```
lib/umifs/
├── fat/
│   ├── fat_core.cc        ← 新規: FATfsクリーンルーム実装 (UMIライセンス)
│   ├── ff.hh              ← 公開API (FatFsクラス)
│   ├── ff_types.hh        ← 型定義、enum class
│   ├── ff_config.hh       ← コンパイル時設定
│   ├── ff_diskio.hh       ← DiskIo + BlockDeviceLikeアダプタ
│   ├── ff_unicode.cc      ← CP437テーブル + 大文字変換
│   └── ff_unicode.hh      ← Unicode変換宣言
├── little/
│   ├── lfs_core.cc        ← 新規: littlefsクリーンルーム実装 (UMIライセンス)
│   ├── lfs.hh             ← 公開API (Lfsクラス)
│   ├── lfs_types.hh       ← 型定義、enum class
│   ├── lfs_config.hh      ← LfsConfig + BlockDeviceLikeアダプタ
│   └── lfs_util.hh        ← ユーティリティ (CRC, endian, bit ops)
├── docs/
│   ├── REFACTORING_PLAN.md  ← このファイル
│   ├── FATFS_AUDIT.md       ← FATfsリファレンス監査結果
│   ├── LITTLEFS_AUDIT.md    ← littlefsリファレンス監査結果
│   ├── SAFETY_ANALYSIS.md   ← 安全性分析
│   └── RENODE_COMPARISON.md ← Renodeベンチマーク比較
├── test/
│   ├── test_lfs.cc          ← littlefs単体テスト (113テスト)
│   ├── test_fat.cc          ← FATfs単体テスト (96テスト)
│   ├── test_lfs_compare.cc  ← C++23 vs リファレンスC比較 (33テスト)
│   ├── test_fat_compare.cc  ← C++23 vs リファレンスC比較 (27テスト)
│   ├── renode_fs_test.cc    ← Renode統合テスト (C++23)
│   ├── renode_fs_test_ref.cc ← Renode統合テスト (リファレンスC)
│   ├── TEST_REPORT.md       ← テストレポート
│   └── xmake.lua            ← テスト用ビルド設定
└── xmake.lua                ← ライブラリビルド設定
```

## 実施フェーズ

### Phase 1: FATfs クリーンルーム再実装

`fat_core.cc` を Microsoft FAT 仕様に基づきゼロから書き直す。

**実装モジュール構成:**
1. BPB定数 (FAT仕様オフセット定義)
2. エンディアンヘルパー (ld_u16/ld_u32/st_u16/st_u32)
3. セクタウィンドウ管理 (sync_window, move_window)
4. FATテーブル操作 (get_fat, put_fat — FAT12/16/32)
5. クラスタチェーン (create_chain, remove_chain — 循環検出付き)
6. ディレクトリ操作 (dir_sdi, dir_next, dir_alloc, dir_read, dir_find, dir_register, dir_remove)
7. LFN処理 (lfn_cmp, lfn_pick, lfn_put, gen_numname, sfn_sum, create_name)
8. パス解析 (follow_path)
9. ボリューム管理 (check_fs, find_volume, mount_volume)
10. 公開API実装 (mount, open, read, write, close, mkdir, stat, rename, unlink, getfree)

**検証:**
```bash
xmake build test_fs_fat && xmake run test_fs_fat            # 96テスト
xmake build test_fs_fat_compare && xmake run test_fs_fat_compare  # 27テスト (オンディスク互換)
```

### Phase 2: littlefs クリーンルーム再実装

`lfs_core.cc` を SPEC.md に基づきゼロから書き直す。

**実装モジュール構成:**
1. CRC-32 (多項式0x04c11db7, 4bitテーブル)
2. BD操作 (bd_read, bd_prog, bd_erase — キャッシュ付き)
3. ブロックアロケータ (alloc_scan, alloc)
4. タグ操作 (tag_type, tag_id, tag_size, make_tag)
5. メタデータペア読み取り (dir_fetch, dir_get, dir_getinfo)
6. メタデータペア書き込み (dir_commit, dir_compact)
7. CTZスキップリスト (ctz_index, ctz_find, ctz_extend, ctz_traverse)
8. ファイル操作 (file_open, file_close, file_sync, file_read, file_write, file_seek, file_truncate)
9. ディレクトリ操作 (dir_open, dir_close, dir_read, dir_seek, dir_rewind)
10. パス解析 (path_next, dir_find_entry)
11. FS操作 (mkdir, remove, rename, stat, attr操作)
12. gstate管理 (gstate_xor, deorphan)
13. 公開API (format, mount, unmount + ラッパー)

**検証:**
```bash
xmake build test_fs_lfs && xmake run test_fs_lfs            # 113テスト
xmake build test_fs_lfs_compare && xmake run test_fs_lfs_compare  # 33テスト (オンディスク互換)
```

### Phase 3: サイズ・性能最適化

**サイズ目標: Flash < 69,768 B（リファレンスC以下）**

手法:
- `if constexpr` で不使用機能を除去
- LFN処理のCP437テーブル最小化
- 共通CRCテーブル統合
- littlefsのfs_gc/fs_growをconstexprフラグで除外可能に

**性能目標 (Renode Cortex-M4, DWT_CYCCNT):**

| 操作 | リファレンスC | 目標 |
|------|-------------|------|
| LFS format+mount | 88,896 cyc | < 88,896 |
| LFS write 1KB | 217,486 cyc | < 217,486 |
| LFS read 1KB | 40,574 cyc | < 40,574 |
| FAT write 4KB | 91,688 cyc | < 91,688 |
| FAT read 4KB | 57,337 cyc | < 57,337 |
| FAT mkdir+stat x3 | 162,691 cyc | < 162,691 |

手法:
- readパスの関数呼び出し深度削減
- セクタアラインのバルクread/writeでウィンドウバイパス
- CTZスキップリストのジャンプ最大化
- `[[gnu::always_inline]]` をホットパスに適用

### Phase 4: 旧コード削除・ライセンス更新・最終検証

1. 旧実装ファイル (`fat_core_v2.cc` 等) を削除
2. 全ファイルのライセンスヘッダーをUMI独自に更新
3. ドキュメント更新 (SAFETY_ANALYSIS, RENODE_COMPARISON, TEST_REPORT)
4. 全テスト + Renodeテスト実行
5. サイズ・ベンチマーク確認

## 検証基準

| 項目 | 基準 |
|------|------|
| test_fs_fat | 96/96 passed |
| test_fs_lfs | 113/113 passed |
| test_fs_fat_compare | 27/27 passed (オンディスク互換) |
| test_fs_lfs_compare | 33/33 passed (オンディスク互換) |
| renode_fs_test | 28/28 passed |
| Flash size | < 69,768 B |
| 全操作サイクル数 | リファレンスC以下 |

## リスク管理

| リスク | 対策 |
|--------|------|
| FAT12の1.5Bパッキングバグ | compareテストでリファレンスと同一イメージをバイト比較 |
| littlefs gstateリカバリ不備 | write途中中断 → remount の電断シミュレーションテスト |
| CTZスキップリスト計算ミス | ファイルサイズ別 (0B, 1B, block_size-1, 10KB) のread-backテスト |
| サイズ目標未達 | Phase 3で段階的に最適化 |
| コンパイラ最適化差異 | `-Os`ビルドを検証。ホットパスに`always_inline` |
