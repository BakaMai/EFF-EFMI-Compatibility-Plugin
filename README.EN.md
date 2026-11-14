# EFF-EFMI-Compatibility-Plugin v1.2.0

🌐 **[中文说明文档 (README.md)](./README.md)**

**Seamless DX11 Coexistence Bridge for EFF + EFMI (XXMI / 3DMigoto) & Conflict-Free Live GUI UID Hider for *Arknights: Endfield***  
**Fixes EFF `Ins` Control Panel Crash · Protects 3DMigoto `res_width` / `res_height` (Mod GUI Menus) · Press `HOME` In-Game for Instant Live UID/Card Toggling**

---

## ✨ What's New in v1.2.0

- **🎛️ Built-in `HOME` GUI Control Panel (Zero DX11 / Zero SwapChain / Zero Conflict)**:
  - Rendered purely with **Win32 + DWM Dark Rounded Window + 32-bit DIB subpixel SDF rasterization** — **zero DirectX 11, zero DXGI SwapChain, and zero `UnityWndClass` `WndProc` hooks**.
  - Physically **impossible to pollute 3DMigoto's `res_width` / `res_height`** (keeping in-game Mod GUI Menus at 100% native scale), and coexists side-by-side with EFF's **`Ins` Control Panel** without input or overlay conflicts.
- **⚡ 6 Independent Switches with 1-Frame Non-Destructive Live Sync**:
  - Toggle **Bottom-Right UID**, **Network Ping (ms)**, **ESC Menu UID**, **ESC Menu Name**, **Business Card UID**, and **Business Card Name** independently in real time.
  - Preserves original text buffers intact — changes take effect on the very next frame even while the ESC Pause Menu or Profile/Friend Business Card is currently open on screen, and auto-save immediately to `plugin\hide_uid.ini`.
- **⌨️ Custom Hotkey Recorder & Bilingual UI (`中文 / EN`)**:
  - Click the hotkey button inside the panel to bind any single key or `Ctrl` / `Alt` / `Shift` chord, and switch between Chinese and English UI at any time with one click.

---

## 🎯 What Problems Does This Solve?

When running ***Arknights: Endfield*** in **DX11 mode (`-force-d3d11`)** via XXMI Launcher to use **EFMI (3DMigoto)** mods alongside **[EFF](https://github.com/ssice-a/EFF)** mods, three major compatibility issues arise. This lightweight companion plugin fixes all of them at once:

1. **🛠️ Fixes EFF's `Ins` Control Panel Not Opening (Silent `dcomp.dll` Crash)**
   - **The Problem**: Pressing `Ins` (`Insert`) causes `eff.dll`'s UI thread to create a `604×640` DirectComposition swap chain that gets wrapped by 3DMigoto, triggering an immediate `0xC0000005` access violation inside `dcomp.dll` that silently terminates EFF's panel thread.
   - **The Fix**: Isolates `eff.dll`'s dedicated UI thread so EFF's popup window renders on an unwrapped native swap chain, **opening the `EFF Mod 按键控制` window reliably on `Ins` with working cursor unlock**.

2. **🖥️ Prevents Mod GUI Menus (and `res_width` / `res_height` Shaders) from Blowing Up in Size**
   - **The Problem**: Whenever an overlay plugin (such as legacy `ApplepieManager`'s `480×560` DX11/ImGui panel or EFF's `604×640` panel) creates a composition swap chain through 3DMigoto, 3DMigoto's global `res_width` and `res_height` get overwritten with the small popup dimensions. Any in-game **Mod GUI Menu** relying on `res_width` / `res_height` becomes massively oversized and unusable.
   - **The Fix**: EFF's panel creation and window resizing bypass 3DMigoto's swap-chain hook completely, and this plugin's own `HOME` control panel uses pure Win32/GDI rendering (zero swap chains) — keeping **`res_width` and `res_height` locked to your true game resolution (e.g., `3840×2160` / `2560×1440`) so Mod GUI Menus stay at 100% normal scale**.

3. **🛡️ Built-in Conflict-Free 3-Layer UID / Name Hider with Live GUI (Zero `d3dcompiler_47.dll` Conflict)**
   - **The Problem**: Newer `ApplepieManager` builds and EFF both ship their own `d3dcompiler_47.dll` proxy in the game root and cannot overwrite each other, while legacy `applepie_manager.dll` creates a `480×560` ImGui swap chain that corrupts 3DMigoto resolution.
   - **The Fix**: Combines a 3-layer IL2CPP suppression engine (`GameObject.SetActive` + `TMP_Text` tracking + `GenerateTextMesh` / `CanvasRenderer` 0-flash live sync) with a pure-Win32 `HOME` GUI control panel, loaded automatically by EFF's existing `d3dcompiler_47.dll` (`plugin\*.dll`) and untouched by `EFF-Mod-Manager` updates.

---

## 📦 Installation & Usage (2 Steps)

> [!WARNING]
> When using this plugin, **immediately remove / uninstall** the **`Hide UI＆UID`** mod inside your `EFMI\Mods` folder (originating from GameBanana: [https://gamebanana.com/mods/657915](https://gamebanana.com/mods/657915)).  
> That legacy shader-based hider suffers from severe drawbacks and is completely superseded by this plugin:
> - **Leaks your UID during high-Ping spikes**;
> - **Cannot display network latency (Ping ms)**;
> - **Causes system & rendering performance degradation**.

**Coexistence Checklist:**
1. Launch the game in **DX11 mode** via **XXMI Launcher** (for EFMI) while keeping EFF's `d3dcompiler_47.dll` and `plugin\eff.dll` installed in the game directory.
2. **Do NOT enable an EFMI mod and an EFF mod for the exact same character at the same time** (using EFMI mods on Character A and EFF mods on Character B works seamlessly).
3. If you still have an old **`applepie_manager.dll`** inside `<GameRoot>\plugin\`, remove it first so it does not create a `480×560` swap chain.

**Installation Steps:**

1. Download the latest release zip from [Releases](https://github.com/BakaMai/EFF-EFMI-Compatibility-Plugin/releases) (or grab `plugin/hide_uid.dll` and `plugin/hide_uid.ini` from this repository).
2. Place `hide_uid.dll` and `hide_uid.ini` into **`<GameRoot>\plugin\`** (right next to `eff.dll`):

```text
Arknights Endfield/                 (Game root folder containing Endfield.exe)
├── d3dcompiler_47.dll              <-- EFF's proxy loader (auto-loads plugin\*.dll)
└── plugin/
    ├── eff.dll                     <-- EFF core runtime
    ├── eff.ini
    ├── hide_uid.dll                <-- ✨ This plugin
    ├── hide_uid.ini                <-- ✨ Configuration file (auto-saved by GUI)
    └── mods/                       <-- EFF mod packages
```

**In-Game Hotkeys:**
- Press **`HOME`** (default hotkey, customizable inside the panel) to open/close the **Hide UID Control Panel** (`Esc` also closes it; drag the header to reposition).
- Press **`Ins` (`Insert`)** to open/close the **EFF Mod Control Panel** (both panels can remain open side-by-side).
- Open in-game **EFMI Mod GUI Menus** (e.g., `Ctrl + K`) — menu scale and click hitboxes remain 100% normal.

---

## ⚙️ Configuration (`plugin/hide_uid.ini`)

All switches can be toggled live in-game via the `HOME` GUI panel (which auto-saves to `plugin\hide_uid.ini`), or edited manually with any text editor:

| Section | Key | Default | Description |
| :--- | :--- | :---: | :--- |
| **`[gui]`** | `toggle_key` | **`HOME`** | Hotkey to open/close the GUI panel in-game (e.g. `HOME`, `END`, `F9`, `Ctrl+HOME`; set `NONE` to disable hotkey) |
| **`[gui]`** | `language` | **`zh`** | Panel UI language: `zh` (Chinese) / `en` (English) |
| **`[uid]`** | `hide_uid` | **`1`** | **Hide** bottom-right watermark UID |
| **`[uid]`** | `hide_ping` | **`0`** | **Keep visible** bottom-right network latency (Ping ms) |
| **`[uid]`** | `hide_menu_uid` | **`1`** | **Hide** ESC pause menu UID (`ManagerNumber`) |
| **`[uid]`** | `hide_menu_name` | **`0`** | **Keep visible** ESC pause menu player nickname (`ManagerName` / `WatchPanel`) |
| **`[uid]`** | `hide_card_uid` | **`1`** | **Hide** profile / business card UID (`PlayerUidTxt`) |
| **`[uid]`** | `hide_card_name` | **`1`** | **Hide** profile / business card nickname (`PersonalInfoNode`) |
| **`[eff]`** | `fix_eff_panel` | **`1`** | **Enable** EFF `Ins` panel crash fix & 3DMigoto `res_width` / `res_height` resolution protection |

> Tip: If you only want the EFF `Ins` panel crash fix + Mod GUI Menu resolution protection without hiding any UIDs or using the hotkey panel, set `toggle_key=NONE`, set all keys under `[uid]` to `0`, and keep `fix_eff_panel=1`.

---

## 🛠️ Building from Source

MinHook C sources are vendored under `deps/minhook` with zero external dependencies. Build with either **Visual Studio (MSVC `cl.exe`)** or **[Zig](https://ziglang.org/) (`zig cc`)**:

```bat
build.bat
```

Output binary: `plugin\hide_uid.dll`

---

## 🙏 Credits & License

- **[ssice-a/EFF](https://github.com/ssice-a/EFF)** — Endfield resource modding framework & plugin ecosystem.
- **[zonemodAE/ApplepieManager](https://github.com/zonemodAE/ApplepieManager)** — Original 3-layer IL2CPP UID text suppression & control panel design inspiration (`il2cpp_text_scanner.h`).
- **[bo3b/3Dmigoto](https://github.com/bo3b/3Dmigoto) & [SpectrumQT/EFMI](https://github.com/SpectrumQT/EFMI-Tools)** — 3DMigoto / XXMI / EFMI framework.
- **[TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)** — Minimalistic x86/x64 API Hooking Library (BSD-2-Clause License).

Licensed under the **[GNU Affero General Public License v3.0 (AGPL-3.0)](./LICENSE)**.
