# CMSIS-Pack

**分類:** ARM 業界標準
**概要:** ARM が規定するデバイス・ソフトウェアパッケージの業界標準フォーマット。PDSC (Pack Description) XML で 4 階層のデバイスヒエラルキーを定義し、メモリ・デバッグ・フラッシュアルゴリズムを包括的に管理する。

---

## アーキテクチャ概要

### 4 階層デバイスヒエラルキー

CMSIS-Pack は 4 段階の階層でデバイスを定義する。各階層でプロパティが累積的に継承され、下位で上書き可能。

```
Dvendor (STMicroelectronics)
  └── Dfamily (STM32F4)
      └── DsubFamily (STM32F407)
          └── Dname (STM32F407VGTx)
              └── Dvariant (任意)
```

### PDSC XML 構造

```xml
<package schemaVersion="1.7.28">
  <vendor>STMicroelectronics</vendor>
  <name>STM32F4xx_DFP</name>

  <devices>
    <family Dfamily="STM32F4" Dvendor="STMicroelectronics:13">
      <processor Dcore="Cortex-M4" DcoreVersion="r0p1"
                 Dfpu="SP_FPU" Dendian="Little-endian"
                 Dmpu="MPU" Dclock="180000000"/>

      <subFamily DsubFamily="STM32F407">
        <processor Dclock="168000000"/>

        <device Dname="STM32F407VGTx">
          <memory name="IROM1" access="rx"
                  start="0x08000000" size="0x100000"
                  startup="1" default="1"/>
          <memory name="IRAM1" access="rwx"
                  start="0x20000000" size="0x20000" default="1"/>
          <memory name="IRAM2" access="rwx"
                  start="0x10000000" size="0x10000"/>
          <algorithm name="Flash/STM32F4xx_1024.FLM"
                     start="0x08000000" size="0x100000" default="1"/>
          <debug svd="SVD/STM32F407.svd"/>
        </device>
      </subFamily>
    </family>
  </devices>
</package>
```

---

## 主要メカニズム

### メモリ定義

構造化されたメモリ記述。各メモリ領域に詳細な属性を付与する。

| 属性 | 意味 | 値 |
|------|------|---|
| `name` | メモリ領域名 | IROM1, IRAM1, IRAM2 等 |
| `access` | アクセス権限 | `r`(read), `w`(write), `x`(execute), `s`(secure), `n`(non-secure), `p`(privileged) |
| `start` | 開始アドレス | 16 進数 |
| `size` | サイズ | 16 進数 |
| `startup` | 起動メモリか | `1` / `0` |
| `default` | デフォルト配置先か | `1` / `0` |

### フラッシュアルゴリズム

```xml
<algorithm name="Flash/STM32F4xx_1024.FLM"
           start="0x08000000" size="0x100000"
           RAMstart="0x20000000" RAMsize="0x1000"
           default="1"/>
```

`.FLM` ファイルはフラッシュ書き込み用の ELF バイナリ。pyOCD, probe-rs, Keil, SEGGER 等のデバッグツールが共通で使用する業界標準。

### DFP vs BSP パック

| パック種別 | 内容 | 提供者 |
|-----------|------|--------|
| DFP (Device Family Pack) | デバイス定義、SVD、フラッシュアルゴリズム、スタートアップ | シリコンベンダー |
| BSP (Board Support Pack) | ボード固有の初期化、ピン設定、デフォルトコンフィグ | ボードベンダー |

BSP パックは DFP パックの特定デバイスを参照し、ボード固有の設定を追加する。デバイスとボードの責務が明確に分離されている。

### CMSIS-Toolbox — オープンソース化

従来 Keil 中心だった CMSIS エコシステムがオープンソース方向に移行中。

```yaml
# csolution.yml — 新世代 CMSIS プロジェクト記述
solution:
  packs:
    - pack: Keil::STM32F4xx_DFP@2.17.0
  target-types:
    - type: STM32F407-Discovery
      device: STM32F407VGTx
```

CMSIS-Toolbox は GCC/Clang/IAR/AC6 の全ツールチェーンに対応し、csolution.yml による宣言的プロジェクト記述を可能にする。

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| 4 階層継承 | ◎ | family → subFamily → device → variant の属性累積 |
| メモリ定義 | ◎ | XML 内に構造化されたメモリ記述。アクセス権限まで詳細 |
| ツール統合 | ◎ | Keil, IAR, pyOCD, probe-rs, SEGGER 等が共通対応 |
| デバイス-ボード分離 | ◎ | DFP と BSP の明確な責務分離 |
| リンカスクリプト | ○ | ツールチェーン別で Pack 内に提供 |
| 人間可読性 | △ | XML が冗長 |
| オープンソース親和性 | △ → ○ | 従来は Keil 中心、CMSIS-Toolbox でオープン化が進行中 |

---

## UMI への示唆

1. **4 階層の属性累積** は UMI の継承設計の参考になる。ただし UMI は 2-3 段に簡素化すべき
2. **DFP/BSP の分離** は UMI のデバイス定義とボード定義の分離に直接対応する。全成熟システムがこの分離を採用している
3. **メモリ定義の構造化** (アクセス権限、起動メモリ、デフォルト配置先) は UMI の Lua データベースでも同等の詳細度を持つべき
4. **`.FLM` フラッシュアルゴリズム** は pyOCD/probe-rs で直接利用可能。UMI のデバッグ設定でこれを参照する設計が合理的
5. **SVD ファイル** をデバイス定義に含めることで、レジスタビューアとの統合が容易になる

---

## 参照

- [Open-CMSIS-Pack Spec](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_family_pg.html)
- [CMSIS-Toolbox](https://github.com/Open-CMSIS-Pack/cmsis-toolbox)
- [Memfault: Peeking Inside CMSIS-Packs](https://interrupt.memfault.com/blog/cmsis-packs)
- [CMSIS-Driver 2.11.0](https://arm-software.github.io/CMSIS_6/latest/Driver/)
