# ARM Embedded Plugins 再設計計画

## 概要

arm-embedded パッケージのプラグイン群を、理想的な状態に再設計するための計画ドキュメント。
現状の問題点を洗い出し、一貫性のある設計原則に基づいた改善案を提示する。

**調査実施日**: 2026-02-04  
**最終更新日**: 2026-02-04

---

## クロスプラットフォーム対応状況（調査結果）

### パッケージ配布状況

GitHub Releases APIで**全リリース**を調査した結果：

#### OpenOCD (xpack-dev-tools)

v0.12.0-7 ～ v0.11.0-3 の全10リリースを確認。

| プラットフォーム | アセット名 | サイズ | 状態 |
|------------------|-----------|--------|------|
| macOS arm64 | `xpack-openocd-0.12.0-7-darwin-arm64.tar.gz` | 2.3MB | ✅ |
| macOS x64 | `xpack-openocd-0.12.0-7-darwin-x64.tar.gz` | 2.4MB | ✅ |
| Linux arm64 | `xpack-openocd-0.12.0-7-linux-arm64.tar.gz` | 2.6MB | ✅ |
| Linux x64 | `xpack-openocd-0.12.0-7-linux-x64.tar.gz` | 2.7MB | ✅ |
| Linux arm (32-bit) | `xpack-openocd-0.12.0-6-linux-arm.tar.gz` | - | ✅ v0.12.0-6以前 |
| Windows x64 | `xpack-openocd-0.12.0-7-win32-x64.zip` | 3.1MB | ✅ |
| Windows arm64 | - | - | ❌ **全リリースで未提供** |

**結論**: Windows arm64ビルドは全リリース（v0.12.0-7～v0.11.0-3）において提供されていない。

**動作確認済み** (macOS arm64):
```
$ /tmp/openocd_test/xpack-openocd-0.12.0-7/bin/openocd --version
xPack Open On-Chip Debugger 0.12.0+dev-02228-ge5888bda3-dirty (2025-10-04-22:45)
```

#### Renode

v1.16.0 ～ v1.13.0 の全10リリースを確認。

| プラットフォーム | アセット名 | サイズ | 状態 | 備考 |
|------------------|-----------|--------|------|------|
| macOS arm64 | `renode-1.16.0-dotnet.osx-arm64-portable.dmg` | 78MB | ✅ | **v1.16.0で初めて追加** |
| macOS x64 | `renode_1.16.0.dmg` | 30MB | ✅ (Mono版) | 全バージョン |
| Linux arm64 | `renode-1.16.0.linux-arm64-portable-dotnet.tar.gz` | 70MB | ✅ | **v1.16.0で初めて追加** |
| Linux x64 | `renode-1.16.0.linux-portable.tar.gz` | 48MB | ✅ | 全バージョン |
| Linux x64 (dotnet) | `renode-1.16.0.linux-portable-dotnet.tar.gz` | 72MB | ✅ | 全バージョン |
| Windows x64 | `renode-1.16.0.windows-portable-dotnet.zip` | 111MB | ✅ | 全バージョン |
| Windows arm64 | - | - | ❌ **全リリースで未提供** | |

**結論**: 
- Windows arm64ビルドは全リリース（v1.16.0～v1.13.0）において提供されていない
- macOS/Linux arm64は v1.16.0 (2025年8月) で初めて追加された
- v1.15.x以前はx86_64のみ

**動作確認済み** (macOS arm64):
```
$ /tmp/renode_mount/Renode.app/Contents/MacOS/renode --version
Renode v1.16.0.1525
  build: 20ad06d9-202508030050
  build type: Release
  runtime: .NET 8.0.18
```

#### ディレクトリ構造

**OpenOCD**:
```
xpack-openocd-0.12.0-7/
├── bin/
│   └── openocd              # 実行ファイル
└── openocd/
    ├── contrib/
    └── scripts/             # 設定スクリプト
        ├── interface/       # stlink.cfg 等
        ├── target/          # stm32f4x.cfg 等
        └── board/
```

**Renode** (Linux portable):
```
renode_1.16.0_portable/
├── renode                   # 実行ファイル (ELF)
├── renode-test              # テストランナー
├── platforms/
├── scripts/
└── tests/
```

**Renode** (macOS DMG):
```
Renode.app/Contents/MacOS/
├── renode                   # 実行ファイル (Mach-O arm64)
├── renode-test
├── libcoreclr.dylib
└── ...
```

### Windows arm64 対応について

**調査結論**:
- OpenOCD: **xpack-dev-toolsの全リリース（v0.12.0-7～v0.11.0-3）でWindows arm64ビルドは存在しない**
- Renode: **全リリース（v1.16.0～v1.13.0）でWindows arm64ビルドは存在しない**

**今後の方針**:
- 現時点でWindows arm64は対応不可（公式ビルドが存在しないため）
- Windows arm64ユーザーはx64エミュレーションでの動作を検討（性能低下の可能性あり）
- 将来的に公式ビルドが提供されれば対応を追加

---

## ESP32 対応検討

> **詳細**: [esp32-support-investigation.md](esp32-support-investigation.md) を参照

### 概要

- **ツールチェーン**: `xtensa-esp-elf` (Xtensa), `riscv32-esp-elf` (RISC-V)
- **ベアメタル開発可能**: ESP-IDF無しでGCCツールチェーンのみで開発可能
- **全プラットフォーム対応**: macOS/Linux/Windows (arm64含む)

### パッケージ実装計画

| パッケージ | 優先度 | 説明 |
|------------|--------|------|
| `esp-toolchain` | 高 | xtensa-esp-elf, riscv32-esp-elf ツールチェーン |
| `esp-hal-components` | 中 | ESP-IDFから抽出したHAL/LLレイヤー（オプション） |

---

## 設計原則

### 1. 単一責任原則
各プラグインは明確に定義された一つの責務を持つ。

### 2. 一貫したインターフェース
すべてのプラグインは同じパターンでオプションを受け取り、同じ形式で出力する。

### 3. 設定より規約（Convention over Configuration）
デフォルトで「正しく動く」。カスタマイズは target values で行う。

### 4. ツール非依存
特定のツール（PyOCD等）に依存せず、複数のツールチェーンをサポート。

### 5. 汎用性
プロジェクト固有のタスクはプラグインとして汎用化し、再利用可能にする。

---

## パッケージ実装状況

| パッケージ | 状態 | 実装ファイル | 備考 |
|------------|------|-------------|------|
| pyocd | ✅ 実装済み | `packages/p/pyocd/xmake.lua` | pip install方式 |
| python3 | ✅ 実装済み | `packages/p/python3/xmake.lua` | venv方式 |
| openocd | ✅ **実装済み** | `packages/o/openocd/xmake.lua` | xpack配布 |
| renode | ✅ **実装済み** | `packages/r/renode/xmake.lua` | GitHub releases |
| jlink | ❌ 対象外 | - | ライセンス制約 |
| qemu | ❌ 対象外 | - | システムインストール前提 |

---

## 1. Flash プラグイン

### 現状の問題点

#### 1.1 ツール依存
- PyOCDのみサポート
- OpenOCD, J-Link, ST-Link Utility 未対応

#### 1.2 データベース設計（flash-targets.json）の問題

現在の構造 (147行):
```json
{
  "FLASH_TARGETS": {
    "builtin": { "targets": { "stm32f051": {...}, ... } },
    "pack_required": { "targets": { "stm32f407vg": {...}, ... } },
    "target_aliases": { "aliases": {...} }
  },
  "PACK_MANAGEMENT": {...},
  "ERROR_MESSAGES": {...}
}
```

**問題点**:
- 個別MCUごとに全設定をハードコード
- 新MCU追加のたびにJSON更新が必要
- 同じファミリーのMCUで設定が重複

#### 1.3 ビルド統合
- `os.execv("xmake", {"build", ...})` で外部プロセスとしてビルド
- xmake の `task.run("build", ...)` を使うべき

#### 1.4 オプション設計の問題
現在のオプション:
```
-t, --target    : ターゲット名
-d, --device    : MCUデバイス名
-a, --address   : ベースアドレス
-f, --frequency : SWD周波数
-e, --erase     : チップ消去
-r, --reset     : リセット
-n, --no-reset  : リセット無効
-p, --probe     : プローブ指定
--connect       : 接続モード
-y, --yes       : 確認スキップ
```

**不足している重要オプション**:
- `--verify`: 書き込み後の検証
- `--unlock`: 読み出し保護解除
- `--format`: バイナリフォーマット指定
- `--backend`: ツール選択
- `--dry-run`: デバッグ用

### 理想的な設計

#### コマンドインターフェース

```
xmake flash [options] [target...]

Options:
  -t, --target=TARGET      ターゲット名（複数指定可）
  -d, --device=MCU         MCUデバイス名（自動検出をオーバーライド）
  -b, --backend=BACKEND    書き込みツール [pyocd|openocd] (default: auto)
  -a, --address=ADDR       ベースアドレス
  -f, --file=FILE          バイナリファイル直接指定
      --format=FMT         ファイルフォーマット [elf|bin|hex] (default: auto)
  -e, --erase=MODE         消去モード [chip|sector|none] (default: sector)
  -v, --verify             書き込み後に検証
  -r, --reset=MODE         リセットモード [hw|sw|none] (default: hw)
  -p, --probe=ID           プローブUID/シリアル番号
  -s, --speed=FREQ         通信速度 (e.g., 4M, 1000k)
      --connect=MODE       接続モード [halt|pre-reset|under-reset]
      --unlock             読み出し保護を解除
  -y, --yes                確認プロンプトをスキップ
      --dry-run            実際には書き込まない（コマンド表示のみ）

Multi-image:
  xmake flash -t kernel -t app              # 順次書き込み
  xmake flash --file=boot.bin:0x08000000 --file=app.bin:0x08010000
```

#### target values 設計

```lua
target("firmware")
    add_rules("embedded")
    
    -- Flash設定
    set_values("flash.device", "stm32f407vg")
    set_values("flash.backend", "pyocd")        -- or "openocd"
    set_values("flash.address", "0x08000000")
    set_values("flash.probe", "0669FF37...")    -- 固定プローブ
    set_values("flash.speed", "4M")
    set_values("flash.verify", true)
    set_values("flash.erase", "sector")         -- or "chip", "none"
    set_values("flash.reset", "hw")             -- or "sw", "none"
```

#### バックエンド抽象化

```lua
-- plugins/flash/backends/pyocd.lua
local M = {}

function M.flash(config)
    local tool = require("utils.tool_registry").require("pyocd")
    
    local args = {"flash", "-t", config.device, "--format", "elf"}
    
    if config.verify then table.insert(args, "--verify") end
    if config.speed then 
        table.insert(args, "-f")
        table.insert(args, config.speed)
    end
    if config.erase == "chip" then 
        table.insert(args, "-e")
        table.insert(args, "chip")
    end
    if config.probe then
        table.insert(args, "--probe")
        table.insert(args, config.probe)
    end
    
    table.insert(args, config.file)
    
    return os.execv(tool.program, args)
end

function M.check_device_pack(device)
    -- デバイスパックの確認・自動インストール
end

return M

-- plugins/flash/backends/openocd.lua
local M = {}

function M.flash(config)
    local tool = require("utils.tool_registry").require("openocd")
    
    -- OpenOCD設定ファイルを決定
    local interface_cfg = config.interface or detect_interface(config.probe)
    local target_cfg = config.openocd_target or detect_target(config.device)
    
    local args = {
        "-f", "interface/" .. interface_cfg,
        "-f", "target/" .. target_cfg,
        "-c", string.format("program %s verify reset exit", config.file)
    }
    
    return os.execv(tool.program, args)
end

return M
```

#### データベース設計の改善（flash-targets.json v2）

```json
{
  "version": "2.0",
  "mcu_families": {
    "stm32f4": {
      "vendor": "STMicroelectronics",
      "pack": "stm32f4",
      "pack_auto_install": true,
      "flash_base": "0x08000000",
      "ram_base": "0x20000000",
      "default_speed": "4M",
      "openocd_target": "stm32f4x.cfg",
      "openocd_interface": "stlink.cfg"
    },
    "stm32h5": {
      "vendor": "STMicroelectronics",
      "pack": "stm32h5",
      "pack_auto_install": true,
      "flash_base": "0x08000000",
      "default_speed": "4M",
      "openocd_target": "stm32h5x.cfg"
    },
    "nrf52": {
      "vendor": "Nordic",
      "pack": "nrf52",
      "pack_auto_install": true,
      "flash_base": "0x00000000",
      "default_speed": "4M",
      "openocd_target": "nrf52.cfg"
    }
  },
  "mcu_overrides": {
    "stm32h533re": {
      "pyocd_target": "stm32h533retx",
      "note": "PyOCD requires 'tx' suffix"
    }
  },
  "aliases": {
    "stm32f407vg": "stm32f407vgtx",
    "stm32h533re": "stm32h533retx"
  }
}
```

#### 設定解決ロジック

```lua
function resolve_mcu_config(device, target_values, cmdline_opts)
    local config = {}
    
    -- 1. エイリアス解決
    device = ALIASES[device] or device
    
    -- 2. ファミリーを特定 (stm32f407vg -> stm32f4)
    local family = detect_family(device)
    
    -- 3. ファミリー設定をロード
    if family and MCU_FAMILIES[family] then
        config = table.copy(MCU_FAMILIES[family])
    end
    
    -- 4. MCUオーバーライドを適用
    if MCU_OVERRIDES[device] then
        table.merge(config, MCU_OVERRIDES[device])
    end
    
    -- 5. target values を適用
    for key, value in pairs(target_values) do
        config[key:gsub("^flash%.", "")] = value
    end
    
    -- 6. コマンドラインオプションを適用 (最優先)
    for key, value in pairs(cmdline_opts) do
        config[key] = value
    end
    
    return config
end

function detect_family(device)
    local patterns = {
        {"^stm32f0", "stm32f0"},
        {"^stm32f1", "stm32f1"},
        {"^stm32f4", "stm32f4"},
        {"^stm32h5", "stm32h5"},
        {"^stm32l4", "stm32l4"},
        {"^stm32g0", "stm32g0"},
        {"^nrf52", "nrf52"},
        {"^lpc", "lpc"},
    }
    for _, p in ipairs(patterns) do
        if device:lower():match(p[1]) then return p[2] end
    end
    return nil
end
```

### 不足機能まとめ

| 機能 | 優先度 | 説明 |
|------|--------|------|
| OpenOCD対応 | 高 | バックエンド追加 |
| flash-targets.json v2 | 高 | ファミリーベース再設計 |
| `--verify` | 高 | 書き込み検証 |
| `--backend` | 高 | ツール選択 |
| `--unlock` | 中 | 読み出し保護解除 |
| bin/hex フォーマット | 中 | ELF以外対応 |
| マルチイメージ | 中 | kernel + app |
| `--dry-run` | 低 | デバッグ用 |

---

## 2. Debugger プラグイン

### 現状の問題点

現在の実装 (218行):
- 5つのプロファイル対応: openocd, jlink, stlink, pyocd, blackmagic
- GDBサーバーは手動起動が必要
- VSCode連携なし
- RTT未対応

#### 2.1 GDBサーバー手動起動の問題

現在のコード:
```lua
if profile == "pyocd" then
    print("Make sure PyOCD is running")
    print("Example: pyocd gdbserver -t " .. mcu_name)
```

**問題**: ユーザーが別ターミナルでサーバーを起動する必要がある

#### 2.2 プロファイル設計の問題

現在は `openocd`, `jlink`, `pyocd` 等のツール名がプロファイル名。
実際には以下の区別が重要:
- **バックエンド**: 使用するツール
- **接続モード**: halt, under-reset, pre-reset

### 理想的な設計

#### コマンドインターフェース

```
xmake debugger [options] [target]

Options:
  -t, --target=TARGET      ターゲット名
  -b, --backend=BACKEND    デバッグバックエンド [pyocd|openocd] (default: auto)
  -p, --port=PORT          GDBサーバーポート (default: 3333)
      --server-only        GDBサーバーのみ起動
      --attach             実行中のサーバーにアタッチ
      --kill               サーバープロセスを終了
  -i, --init=FILE          GDB初期化スクリプト
      --break=SYMBOL       初期ブレークポイント (default: main)
      --rtt                RTT対応を有効化
      --rtt-port=PORT      RTT TCPポート (default: 19021)
      --tui                GDB TUIモードを使用
      --vscode             VSCode launch.json を生成
```

#### GDBサーバープロセスのライフサイクル管理

GDBサーバーの自動起動において、**プロセスの寿命管理**が重要な設計ポイント:

```lua
-- plugins/debugger/server_manager.lua
local M = {}
local _server_state = {
    pid = nil,
    port = nil,
    backend = nil,
    start_time = nil
}

-- PIDファイルのパス
local function pid_file_path()
    return path.join(os.tmpdir(), "xmake_gdb_server.pid")
end

-- サーバー状態の永続化
function M.save_state()
    if _server_state.pid then
        local state = {
            pid = _server_state.pid,
            port = _server_state.port,
            backend = _server_state.backend,
            start_time = os.time()
        }
        io.savefile(pid_file_path(), json.encode(state))
    end
end

-- サーバー状態の復元
function M.load_state()
    local pid_file = pid_file_path()
    if os.isfile(pid_file) then
        local content = io.readfile(pid_file)
        local state = json.decode(content)
        -- プロセスが生存しているか確認
        if state and state.pid and is_process_alive(state.pid) then
            _server_state = state
            return state
        else
            -- 古いPIDファイルを削除
            os.rm(pid_file)
        end
    end
    return nil
end

-- プロセス生存確認
function is_process_alive(pid)
    -- macOS/Linux: kill -0 で確認（シグナル送信せずに存在確認）
    local ok = os.exec("kill -0 " .. pid .. " 2>/dev/null")
    return ok == 0
end

-- サーバー起動
function M.start(config)
    -- 1. 既存サーバーの確認
    local existing = M.load_state()
    if existing and existing.port == config.port then
        -- 同じポートで既に起動中
        if existing.backend == config.backend then
            print("GDB server already running (PID: " .. existing.pid .. ")")
            return existing.pid
        else
            -- 別のバックエンドが起動中 → 停止してから起動
            print("Stopping existing server (different backend)")
            M.stop()
        end
    end
    
    -- 2. ポートが使用中か確認
    if is_port_in_use(config.port) then
        raise("Port " .. config.port .. " is already in use")
    end
    
    -- 3. バックグラウンドでサーバー起動
    local backend = require("plugins.debugger.backends." .. config.backend)
    local pid = backend.start_server(config)
    
    -- 4. サーバーが ready になるまで待機
    local timeout = 5000  -- 5秒
    if not wait_for_port(config.port, timeout) then
        -- 起動失敗時はプロセスを終了
        if pid then os.kill(pid) end
        raise("GDB server failed to start within " .. (timeout/1000) .. " seconds")
    end
    
    -- 5. 状態を保存
    _server_state = {
        pid = pid,
        port = config.port,
        backend = config.backend
    }
    M.save_state()
    
    print("GDB server started on port " .. config.port .. " (PID: " .. pid .. ")")
    return pid
end

-- サーバー停止
function M.stop()
    local state = M.load_state()
    if state and state.pid then
        print("Stopping GDB server (PID: " .. state.pid .. ")")
        os.kill(state.pid, "TERM")
        
        -- 終了を待機（最大2秒）
        for i = 1, 20 do
            if not is_process_alive(state.pid) then break end
            os.sleep(100)
        end
        
        -- まだ生きていれば強制終了
        if is_process_alive(state.pid) then
            os.kill(state.pid, "KILL")
        end
        
        -- PIDファイル削除
        os.rm(pid_file_path())
        _server_state = {}
    end
end

-- ポート使用確認
function is_port_in_use(port)
    -- lsof または netstat を使用
    if is_host("macosx", "linux") then
        local ok = os.exec("lsof -i:" .. port .. " >/dev/null 2>&1")
        return ok == 0
    else
        -- Windows: netstat
        local ok = os.exec('netstat -an | findstr "' .. port .. '" >nul 2>&1')
        return ok == 0
    end
end

-- ポート待機
function wait_for_port(port, timeout_ms)
    local start = os.mclock()
    while (os.mclock() - start) < timeout_ms do
        if is_port_in_use(port) then return true end
        os.sleep(100)
    end
    return false
end

return M
```

**重要な設計ポイント**:

1. **PIDトラッキング**: サーバーPIDをファイルに保存し、セッション間で状態を維持
2. **孤児プロセス対策**: xmake終了時にサーバーが残らないよう管理
3. **ポート競合検出**: 既に使用中のポートへの起動を防止
4. **グレースフル終了**: SIGTERM → 待機 → SIGKILL の順で確実に終了
5. **バックエンド切り替え**: 異なるバックエンドへの切り替え時に自動停止

#### GDBサーバー自動起動（改善版）

#### GDBサーバー自動起動（改善版）

```lua
function debug_embedded_target(target, config)
    local server_manager = require("plugins.debugger.server_manager")
    local port = config.port or 3333
    
    -- 1. サーバーを起動（既存確認含む）
    local server_pid = server_manager.start({
        port = port,
        backend = config.backend,
        device = config.device,
        interface = config.interface
    })
    
    -- 2. GDBクライアントを起動
    local gdb_args = build_gdb_args(target, config)
    local exit_code = os.execv(config.gdb, gdb_args)
    
    -- 3. GDB終了後の処理
    if config.kill_on_exit then
        server_manager.stop()
    else
        print("GDB server still running (PID: " .. server_pid .. ")")
        print("Use 'xmake debugger --kill' to stop")
    end
    
    return exit_code
end
```

#### VSCode launch.json 生成

```lua
function generate_vscode_launch(target, config)
    local launch = {
        version = "0.2.0",
        configurations = {{
            name = "Debug " .. target:name(),
            type = "cortex-debug",
            request = "launch",
            servertype = config.backend,
            cwd = "${workspaceFolder}",
            executable = "${workspaceFolder}/" .. path.relative(target:targetfile(), os.projectdir()),
            device = target:values("embedded.mcu"),
            svdFile = config.svd,
            runToEntryPoint = config.break_symbol or "main",
            rttConfig = config.rtt and {
                enabled = true,
                address = "auto",
                decoders = {{port = 0, type = "console"}}
            } or nil
        }}
    }
    
    os.mkdir(path.join(os.projectdir(), ".vscode"))
    local json = import("core.base.json")
    json.savefile(path.join(os.projectdir(), ".vscode", "launch.json"), launch)
    print("Generated .vscode/launch.json")
end
```

### 不足機能まとめ

| 機能 | 優先度 | 説明 |
|------|--------|------|
| GDBサーバー自動起動 | 高 | ワンコマンドでデバッグ開始 |
| VSCode launch.json生成 | 高 | IDE連携 |
| RTT対応 | 高 | リアルタイムログ |
| `--kill` | 中 | サーバー終了 |
| `--server-only` | 中 | サーバーのみ起動 |
| SVDファイル自動検出 | 中 | レジスタビュー |
| セミホスティング | 低 | stdio over SWD |

---

## 3. Emulator プラグイン

### 現状の問題点

現在の実装 (55行):
```lua
-- 単なるプロジェクトタスクへのラッパー
task("emulator.run")
    on_run(function ()
        os.exec("xmake renode")
    end)
```

**問題点**:
1. プロジェクトの `xmake renode` を呼ぶだけ
2. プロジェクト固有コード（パス等）が `xmake.lua` にハードコード
3. 汎用性がない

#### プロジェクト側のハードコード

```lua
-- xmake.lua (プロジェクト固有)
task("renode")
    on_run(function ()
        local renode = "/Applications/Renode.app/Contents/MacOS/Renode"  -- ハードコード
        if not os.isfile(renode) then renode = "renode" end
        os.execv(renode, {"renode/run.resc"})  -- パスハードコード
    end)
```

### 理想的な設計

#### コマンドインターフェース

```
xmake emulator [options] [target]

Options:
  -t, --target=TARGET      ターゲット名
  -e, --engine=ENGINE      エミュレータ [renode] (default: renode)
  -s, --script=FILE        エミュレータスクリプト (.resc)
  -p, --platform=FILE      プラットフォーム定義 (.repl)
  -i, --interactive        インタラクティブモード (default)
      --headless           ヘッドレス実行（GUI無効）
      --timeout=SEC        タイムアウト秒数
      --uart-log=FILE      UARTログ出力ファイル
      --gdb                GDBサーバーを有効化
      --gdb-port=PORT      GDBポート (default: 3333)
      --generate-script    .rescスクリプトを自動生成

Test Automation:
  xmake emulator.test [options]    # 自動テスト実行
  xmake emulator.robot FILE        # Robot Frameworkテスト
```

#### target values 設計

```lua
target("firmware")
    add_rules("embedded")
    
    -- Emulator設定
    set_values("emulator.engine", "renode")
    set_values("emulator.script", "$(projectdir)/renode/run.resc")
    set_values("emulator.platform", "$(projectdir)/renode/platform.repl")
    set_values("emulator.uart_log", "$(buildir)/uart.log")
    set_values("emulator.timeout", 60)
    set_values("emulator.gdb", true)
```

#### Renode検出とラッパー

```lua
-- plugins/emulator/engines/renode.lua
local M = {}

function M.find()
    local tool_registry = require("utils.tool_registry")
    return tool_registry.find("renode")
end

function M.run(config)
    local renode = M.find()
    if not renode then
        raise("Renode not found. Install with: xmake require renode")
    end
    
    local args = {}
    
    if config.headless then
        table.insert(args, "--disable-xwt")
        table.insert(args, "--console")
    end
    
    if config.script then
        if config.interactive then
            table.insert(args, config.script)
        else
            table.insert(args, "-e")
            table.insert(args, "include @" .. config.script)
        end
    end
    
    return os.execv(renode.program, args)
end

function M.test(config)
    local renode_test = require("utils.tool_registry").find("renode-test")
    if not renode_test then
        raise("renode-test not found")
    end
    
    local args = {config.robot_file}
    if config.output_dir then
        table.insert(args, "-r")
        table.insert(args, config.output_dir)
    end
    
    return os.execv(renode_test.program, args)
end

return M
```

#### .rescスクリプト自動生成

```lua
function generate_renode_script(target, config)
    local firmware = target:targetfile()
    local platform = config.platform
    
    local script = string.format([[
# Auto-generated Renode script for %s
# Generated by xmake emulator --generate-script
mach create "%s"
machine LoadPlatformDescription @%s

sysbus LoadELF @%s

showAnalyzer sysbus.uart1
%s
start
]], 
        target:name(),
        target:name(),
        platform,
        firmware,
        config.gdb and "machine StartGdbServer 3333" or "")
    
    local script_path = path.join(config.output_dir or os.tmpdir(), target:name() .. ".resc")
    io.writefile(script_path, script)
    return script_path
end
```

### 不足機能まとめ

| 機能 | 優先度 | 説明 |
|------|--------|------|
| プロジェクト固有コード排除 | 高 | 汎用化 |
| Renodeパス自動検出 | 高 | パッケージ対応で解決 |
| .rescスクリプト自動生成 | 中 | target valuesから生成 |
| Robot Framework統合 | 中 | テスト自動化 |
| GDB連携 | 中 | emulator + debugger |
| タイムアウト制御 | 低 | CI/CD用 |

---

## 4. Deploy プラグイン

### 判定結果: 削除推奨

現在の実装は：
- パスがハードコード（`examples/headless_webhost/build/...`）
- `xmake install` で代替可能
- HTTPサーバー機能は別プラグインに分離すべき

### 代替案

```lua
-- xmake.lua でのインストール設定
target("headless_webhost")
    add_rules("wasm")
    
    set_installdir("$(projectdir)/web")
    
    on_install(function(target)
        os.cp(target:targetfile(), target:installdir())
        os.cp(target:targetfile() .. ".js", target:installdir())
    end)
```

### serve プラグイン（新規）

HTTPサーバー機能を独立させる：

```lua
-- plugins/serve/xmake.lua
task("serve")
    set_category("action")
    
    on_run(function()
        import("core.base.option")
        
        local dir = option.get("dir") or os.curdir()
        local port = option.get("port") or 8080
        
        print("Serving %s at http://localhost:%d/", dir, port)
        os.execv("python3", {"-m", "http.server", tostring(port), "--directory", dir})
    end)
    
    set_menu {
        usage = "xmake serve [options]",
        description = "Start a simple HTTP server",
        options = {
            {'d', "dir", "kv", nil, "Directory to serve"},
            {'p', "port", "kv", "8080", "Port number"}
        }
    }
task_end()
```

---

## 5. tool_registry 改善

### 現在の実装

```lua
-- utils/tool_registry.lua (約180行)
local _cache = {
    pyocd = nil,
    pyocd_checked = false,
    gdb = {},
    lldb = nil,
    lldb_checked = false
}

function find_pyocd()
    -- パッケージ→システムの順で検索
end

function find_gdb(toolchain)
    -- ツールチェーンに応じたGDBを検索
end
```

### 改善版

```lua
-- utils/tool_registry.lua (改善版)
local M = {}
local _cache = {}

-- ツール定義
local TOOLS = {
    pyocd = {
        names = {"pyocd"},
        package = "pyocd",
        install_hint = "xmake require pyocd"
    },
    openocd = {
        names = {"openocd"},
        package = "openocd",
        install_hint = "xmake require openocd"
    },
    renode = {
        names = {"renode", "Renode"},
        package = "renode",
        paths = {
            "/Applications/Renode.app/Contents/MacOS/Renode",  -- macOS app
        },
        install_hint = "xmake require renode"
    },
    ["renode-test"] = {
        names = {"renode-test"},
        package = "renode",
        paths = {
            "/Applications/Renode.app/Contents/MacOS/renode-test",
        }
    },
    jlink = {
        names = {"JLinkExe", "JLink", "jlink"},
        -- package = nil,  -- ライセンス制約によりパッケージなし
        install_hint = "Download from https://www.segger.com/downloads/jlink/"
    },
    qemu = {
        names = {"qemu-system-arm"},
        -- package = nil,  -- システムインストール前提
        install_hint = "brew install qemu / apt install qemu-system-arm"
    }
}

function M.find(tool_name)
    if _cache[tool_name] ~= nil then
        return _cache[tool_name] or nil
    end
    
    local def = TOOLS[tool_name]
    if not def then
        _cache[tool_name] = false
        return nil
    end
    
    import("lib.detect.find_tool")
    import("core.base.global")
    
    -- 1. xmake パッケージを確認
    if def.package then
        local pkg_tool = find_in_package(def.package, def.names[1])
        if pkg_tool then
            pkg_tool.source = "package"
            _cache[tool_name] = pkg_tool
            return pkg_tool
        end
    end
    
    -- 2. 固定パスを確認
    if def.paths then
        for _, p in ipairs(def.paths) do
            if os.isfile(p) then
                local result = {program = p, source = "fixed_path"}
                _cache[tool_name] = result
                return result
            end
        end
    end
    
    -- 3. PATH から検索
    for _, name in ipairs(def.names) do
        local found = find_tool(name)
        if found then
            found.source = "system"
            _cache[tool_name] = found
            return found
        end
    end
    
    _cache[tool_name] = false
    return nil
end

function M.require(tool_name)
    local tool = M.find(tool_name)
    if not tool then
        local def = TOOLS[tool_name]
        local hint = def and def.install_hint or "Please install " .. tool_name
        raise(tool_name .. " not found.\n\n" .. hint)
    end
    return tool
end

function M.clear_cache()
    _cache = {}
end

return M
```

---

## 6. 実装計画

### Phase 0: パッケージ実装 ✅ 完了

| タスク | 状態 | 成果物 |
|--------|------|--------|
| OpenOCDパッケージ | ✅ 完了 | `packages/o/openocd/xmake.lua` |
| Renodeパッケージ | ✅ 完了 | `packages/r/renode/xmake.lua` |

### Phase 1: 基盤整備

| タスク | 優先度 | 説明 |
|--------|--------|------|
| tool_registry改善 | 高 | OpenOCD/Renode対応追加 |
| emulatorプラグイン再設計 | 高 | プロジェクト固有コード排除 |
| deployプラグイン削除検討 | 中 | xmake installへの統合 |

### Phase 2: Flash改善

| タスク | 優先度 | 説明 |
|--------|--------|------|
| flash-targets.json v2 | 高 | ファミリーベース設計 |
| flashバックエンド抽象化 | 高 | backends/ディレクトリ |
| OpenOCDバックエンド | 高 | backends/openocd.lua |
| flashオプション拡充 | 中 | --verify, --unlock等 |

### Phase 3: Debugger改善

| タスク | 優先度 | 説明 |
|--------|--------|------|
| GDBサーバー自動起動 | 高 | バックグラウンド起動 |
| **プロセス寿命管理** | 高 | PID追跡、孤児プロセス対策、グレースフル終了 |
| VSCode launch.json生成 | 高 | --vscodeオプション |
| RTT対応 | 中 | PyOCD/OpenOCD RTT |

### Phase 4: ESP32対応

| タスク | 優先度 | 説明 |
|--------|--------|------|
| `esp-toolchain` パッケージ | 高 | xtensa-esp-elf, riscv32-esp-elf ツールチェーン |
| xmake toolchain登録 | 高 | `set_toolchains("esp-xtensa")` 対応 |

> **詳細**: [esp32-support-investigation.md](esp32-support-investigation.md) を参照

### Phase 5: 追加機能（継続的）

| タスク | 優先度 | 説明 |
|--------|--------|------|
| serveプラグイン | 低 | HTTPサーバー |
| セミホスティング | 低 | stdio over SWD |

---

## 7. ディレクトリ構造（理想形）

```
packages/a/arm-embedded/
├── xmake.lua
├── rules/
│   ├── embedded.lua
│   └── embedded_test.lua
├── plugins/
│   ├── flash/
│   │   ├── xmake.lua
│   │   ├── backends/
│   │   │   ├── pyocd.lua
│   │   │   └── openocd.lua
│   │   └── database/
│   │       └── flash-targets.json
│   ├── debugger/
│   │   ├── xmake.lua
│   │   ├── backends/
│   │   │   ├── pyocd.lua
│   │   │   └── openocd.lua
│   │   └── templates/
│   │       └── launch.json.lua
│   ├── emulator/
│   │   ├── xmake.lua
│   │   ├── engines/
│   │   │   └── renode.lua
│   │   └── templates/
│   │       └── platform.resc.lua
│   └── serve/
│       └── xmake.lua
└── utils/
    ├── tool_registry.lua
    └── process.lua

packages/o/openocd/
└── xmake.lua            # ✅ 実装済み

packages/r/renode/
└── xmake.lua            # ✅ 実装済み
```

---

## 8. 互換性

### 破壊的変更

| 変更 | 影響 | 移行方法 |
|------|------|----------|
| `deploy` 削除 | 低 | `xmake install` 使用 |
| オプション名変更 | 中 | 旧オプションをdeprecate |

### 移行期間

1. v0.4.0: 新機能追加、旧コマンドは警告付きで維持
2. v0.5.0: 旧コマンドを deprecated に
3. v1.0.0: 旧コマンドを削除

---

*作成日: 2026-02-04*
*最終更新: 2026-02-04*
*調査・パッケージ実装完了、Windows arm64/ESP32調査追記、GDBサーバープロセス管理設計追加*
