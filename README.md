# EFF-EFMI-Compatibility-Plugin v1.2.0

🌐 **[English Documentation (README.EN.md)](./README.EN.md)**

**《明日方舟：终末地》(Arknights: Endfield) DX11 模式下 EFF 与 EFMI (XXMI / 3DMigoto) 完美共存桥接插件 & 零冲突图形面板版 UID 隐藏工具**  
**修复 EFF `Ins` 控制面板不弹出问题 · 彻底保护 Mod GUI 菜单分辨率 (`res_width` / `res_height`) · 按 `HOME` 随时呼出实时生效的 UID 隐藏控制面板**

---

## ✨ v1.2.0 更新亮点

- **🎛️ 全新内置 `HOME` 图形控制面板（零 DX11 / 零交换链 / 零冲突架构）**：
  - 采用纯 **Win32 + DWM 圆角深色窗口 + 32-bit DIB 亚像素抗锯齿自绘**，**不调用任何 DirectX 11 / DXGI 接口、不创建任何 SwapChain、不挂载游戏窗口 `WndProc`**。
  - 从底层物理机制上**绝对不可能污染 3DMigoto 的 `res_width` / `res_height`**，也可与 EFF 的 **`Ins` 面板同屏并排同时打开**，互不抢占输入或崩溃。
- **⚡ 6 项独立开关 · 1 帧无损双向实时生效**：
  - 支持对 **右下角 UID 水印**、**右下角网络延迟 (Ping)**、**ESC 菜单 UID**、**ESC 菜单昵称**、**名片页 UID**、**名片页昵称** 独立实时开关。
  - 采用非破坏性网格控制（保留原始文本不篡改），即使**正打开着 ESC 菜单或个人/好友名片页**，拨动开关也能在下一帧立即隐藏或恢复显示，并自动保存至 `plugin\hide_uid.ini`。
- **⌨️ 支持自定义呼出热键 & 中英双语切换**：
  - 点击面板底部的热键按钮即可直接在游戏内录制新按键（支持单键或 `Ctrl` / `Alt` / `Shift` 组合键），点击右上角 `EN` / `中文` 一键切换界面语言。

---

## 🎯 解决的核心痛点

当你在 **DX11 模式 (`-force-d3d11`)** 下通过 XXMI Launcher 启动游戏，同时使用 **EFMI (3DMigoto)** 角色 Mod 与 **[EFF](https://github.com/ssice-a/EFF)** 角色 Mod 时，原本会遇到三大冲突痛点，本插件一次性全部解决：

1. **🛠️ 修复按 `Ins` 无法呼出 EFF 控制面板（底层线程静默崩溃）**
   - **原版问题**：在挂载了 EFMI (`d3d11.dll`) 的 DX11 环境下按下 `Ins` 键时，`eff.dll` 的 UI 线程创建 `604×640` 透明合成交换链时会被 3DMigoto 包装拦截，导致 `dcomp.dll` 合成阶段触发 `0xC0000005` 异常使 EFF 面板线程直接静默退出，表现为**按 `Ins` 毫无反应、没有任何弹窗**。
   - **修复效果**：本插件对 `eff.dll` 面板专属线程进行隔离直通，**游戏内按 `Ins` 秒弹 `EFF Mod 按键控制` 悬浮窗，且鼠标解锁正常生效**。

2. **🖥️ 彻底根治 Mod GUI 菜单（及使用 `res_width` / `res_height` 的 Mod UI）巨大化变形**
   - **原版问题**：无论是旧版 `ApplepieManager` 的 `480×560` DX11/ImGui 悬浮窗，还是 EFF 的 `604×640` 面板，只要经过 3DMigoto 的交换链钩子，都会把 3DMigoto 全局分辨率变量 `res_width` / `res_height` 覆写成小窗口尺寸，导致游戏内各类 Mod GUI 菜单按错误分辨率缩放而撑爆整个屏幕。
   - **修复效果**：EFF 面板创建及任意拖拽缩放窗口大小时完全绕过 3DMigoto 交换链拦截，且本插件自身的 `HOME` 控制面板采用纯 Win32/GDI 渲染（零 SwapChain），**`res_width` / `res_height` 永远保持游戏真实渲染分辨率（如 `3840×2160` / `2560×1440`），Mod GUI 菜单比例 100% 正常**。

3. **🛡️ 内置零冲突三层 UID / 名片实时隐藏面板（完美替代 `ApplepieManager`，告别 `d3dcompiler_47.dll` 冲突）**
   - **原版问题**：新版 `ApplepieManager` 与 EFF 均依赖游戏根目录同名的 `d3dcompiler_47.dll` 代理加载器，无法直接覆盖共存；旧版 `applepie_manager.dll` 则自带 `480×560` ImGui 交换链污染分辨率。
   - **修复效果**：内置三层 IL2CPP 隐藏核心（`GameObject.SetActive` + `TMP_Text` 追踪 + `GenerateTextMesh` / `CanvasRenderer` 零闪烁实时刷新）与纯 Win32 `HOME` 图形控制面板，直接由 EFF 自带的 `d3dcompiler_47.dll` 自动挂载加载，零帧率损耗，且不会被 `EFF-Mod-Manager` 更新或卸载时误删。

---

## 📦 安装与使用方法（两步即用）

> [!WARNING]
> 在使用本插件的同时，请**立刻删除 / 卸载** `EFMI\Mods` 文件夹下的 **`Hide UI＆UID`** 插件（源于 GameBanana：[https://gamebanana.com/mods/657915](https://gamebanana.com/mods/657915)）。  
> 该旧版着色器类隐藏插件存在以下严重缺陷，已由本插件完全替代：
> - **高延迟（高 Ping）时会漏出 UID**（无法彻底隐藏）；
> - **一刀切抹除了右下角网络延迟（Ping）显示**；
> - **持续拦截 UI 渲染通道，会造成系统与游戏帧率性能下降**。

**共存前提检查：**
1. 保持通过 **XXMI Launcher** 以 **DX11 模式**启动游戏（加载 EFMI），同时游戏根目录保留 EFF 安装好的 `d3dcompiler_47.dll` 与 `plugin\eff.dll`。
2. **切勿对同一个角色同时启用 EFMI Mod 和 EFF Mod**（不同角色分别使用 EFMI 和 EFF 完全互不干扰）。
3. 如果你的 `<游戏根目录>\plugin\` 下还留有旧版 **`applepie_manager.dll`**，请先将其移走或删除，避免旧版创建 `480×560` 交换链或重复 Hook。

**安装步骤：**

1. 前往 [Releases](https://github.com/BakaMai/EFF-EFMI-Compatibility-Plugin/releases) 下载最新压缩包（或直接取仓库内的 `plugin/hide_uid.dll` 与 `plugin/hide_uid.ini`）。
2. 将 `hide_uid.dll` 和 `hide_uid.ini` 放入 **`<游戏根目录>\plugin\`** 文件夹（与 `eff.dll` 同级）即可：

```text
Arknights Endfield/                 (包含 Endfield.exe 的游戏根目录)
├── d3dcompiler_47.dll              <-- EFF 自带的代理加载器（自动扫描加载 plugin\*.dll）
└── plugin/
    ├── eff.dll                     <-- EFF 核心插件
    ├── eff.ini
    ├── hide_uid.dll                <-- ✨ 本插件主程序
    ├── hide_uid.ini                <-- ✨ 本插件配置文件（面板修改后自动保存）
    └── mods/                       <-- EFF 格式 Mod 存放目录
```

**游戏内快捷键操作：**
- 按 **`HOME`**（默认热键，可在面板内自由改键）：呼出 / 收起 **`UID 隐藏控制面板`**（支持鼠标拖拽标题栏移动、按 `Esc` 收起，所有开关点击即刻生效并自动写入 `hide_uid.ini`）。
- 按 **`Ins` (`Insert`)**：正常弹出 **`EFF Mod 按键控制`** 窗口（两个面板可同时开启、互不干扰）。
- 呼出 **EFMI Mod GUI 菜单**（如 `Ctrl + K`）：菜单尺寸与点击热区 100% 正常。

---

## ⚙️ 配置文件说明 (`plugin/hide_uid.ini`)

除了在游戏内按 `HOME` 通过图形面板直接点击调整外，也可以使用任意文本编辑器手动修改 `plugin\hide_uid.ini`：

| 分类 | 配置项 | 默认值 | 功能说明 |
| :--- | :--- | :---: | :--- |
| **面板 (`[gui]`)** | `toggle_key` | **`HOME`** | 游戏内呼出/收起图形面板热键（如 `HOME`、`END`、`F9`、`Ctrl+HOME`；填 `NONE` 可禁用面板热键） |
| **面板 (`[gui]`)** | `language` | **`zh`** | 面板界面语言：`zh`（中文） / `en`（English） |
| **水印 (`[uid]`)** | `hide_uid` | **`1`** | **隐藏** 画面右下角常驻 UID 水印 |
| **水印 (`[uid]`)** | `hide_ping` | **`0`** | **保留显示** 右下角网络延迟 (Ping ms) |
| **菜单 (`[uid]`)** | `hide_menu_uid` | **`1`** | **隐藏** ESC 菜单界面 UID (`ManagerNumber`) |
| **菜单 (`[uid]`)** | `hide_menu_name` | **`0`** | **保留显示** ESC 菜单界面管理员昵称 (`ManagerName` / `WatchPanel`) |
| **名片 (`[uid]`)** | `hide_card_uid` | **`1`** | **隐藏** 个人名片 / 好友名片页 UID (`PlayerUidTxt`) |
| **名片 (`[uid]`)** | `hide_card_name` | **`1`** | **隐藏** 个人名片 / 好友名片页昵称 (`PersonalInfoNode`) |
| **共存修复 (`[eff]`)** | `fix_eff_panel` | **`1`** | **开启** EFF `Ins` 面板防崩溃隔离与 3DMigoto `res_width` / `res_height` 分辨率保护 |

> 注：如果只想单独使用「EFF `Ins` 面板修复 + Mod GUI 菜单分辨率保护」而不需要隐藏任何 UID 或热键面板，可将 `toggle_key=NONE` 并将 `[uid]` 下的项全部设为 `0`、保留 `fix_eff_panel=1`。

---

## 🛠️ 从源码编译

仓库已内置完整的 `MinHook` C 源码，无任何外部第三方库依赖，支持 **Visual Studio (MSVC `cl.exe`)** 或 **[Zig](https://ziglang.org/) (`zig cc`)** 一键编译：

```bat
:: 在 x64 Native Tools Command Prompt for VS 或已配置 zig 的终端中运行：
build.bat
```

编译产物输出至：`plugin\hide_uid.dll`

---

## 🙏 致谢与开源协议

- **[ssice-a/EFF](https://github.com/ssice-a/EFF)**：Endfield 资源级 Mod 框架与 `d3dcompiler_47.dll` 插件加载生态。
- **[zonemodAE/ApplepieManager](https://github.com/zonemodAE/ApplepieManager)**：三层 IL2CPP 文本节点隐藏思路与控制面板交互参考（`il2cpp_text_scanner.h`）。
- **[bo3b/3Dmigoto](https://github.com/bo3b/3Dmigoto) & [SpectrumQT/EFMI](https://github.com/SpectrumQT/EFMI-Tools)**：3DMigoto / XXMI / EFMI 渲染框架。
- **[TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)**：轻量级 x86/x64 API Hook 库（BSD-2-Clause License）。

本项目遵循上游衍生要求，采用 **[GNU Affero General Public License v3.0 (AGPL-3.0)](./LICENSE)** 开源发布。
