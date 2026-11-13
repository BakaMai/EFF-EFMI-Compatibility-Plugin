# EFF-EFMI-Compatibility-Plugin v1.1.0

🌐 **[中文说明文档 (README.md)](./README.md)**

**Seamless DX11 Coexistence Bridge for EFF + EFMI (XXMI / 3DMigoto) & Headless UID Hider for *Arknights: Endfield***  
**Fixes EFF `Ins` Control Panel Crash · Protects 3DMigoto `res_width` / `res_height` (Mod GUI Menus) · Conflict-Free 3-Layer UID Hider**

---

## 🎯 What Problems Does This Solve?

When running ***Arknights: Endfield*** in **DX11 mode (`-force-d3d11`)** via XXMI Launcher to use **EFMI (3DMigoto)** mods alongside **[EFF](https://github.com/ssice-a/EFF)** mods, three major compatibility issues arise. This lightweight companion plugin fixes all of them at once:

1. **🛠️ Fixes EFF's `Ins` Control Panel Not Opening (Silent `dcomp.dll` Crash)**
   - **The Problem**: Pressing `Ins` (`Insert`) causes `eff.dll`'s UI thread to create a `604×640` DirectComposition swap chain that gets wrapped by 3DMigoto, triggering an immediate `0xC0000005` access violation inside `dcomp.dll` that silently terminates EFF's panel thread.
   - **The Fix**: Isolates `eff.dll`'s dedicated UI thread so EFF's popup window renders on an unwrapped native swap chain, **opening the `EFF Mod 按键控制` window reliably on `Ins` with working cursor unlock**.

2. **🖥️ Prevents Mod GUI Menus (and `res_width` / `res_height` Shaders) from Blowing Up in Size**
   - **The Problem**: Whenever an overlay plugin (such as legacy `ApplepieManager`'s `480×560` panel or EFF's `604×640` panel) creates a composition swap chain through 3DMigoto, 3DMigoto's global `res_width` and `res_height` get overwritten with the small popup dimensions. Any in-game **Mod GUI Menu** relying on `res_width` / `res_height` becomes massively oversized and unusable.
   - **The Fix**: EFF's panel creation and window resizing bypass 3DMigoto's swap-chain hook completely, and this plugin itself creates zero windows or swap chains—keeping **`res_width` and `res_height` locked to your true game resolution (e.g., `3840×2160` / `2560×1440`) so Mod GUI Menus stay at 100% normal scale**.

3. **🛡️ Built-in Headless 3-Layer UID / Name Hider (Zero `d3dcompiler_47.dll` Conflict)**
   - **The Problem**: Newer `ApplepieManager` builds and EFF both ship their own `d3dcompiler_47.dll` proxy in the game root and cannot overwrite each other, while legacy `applepie_manager.dll` creates a `480×560` ImGui swap chain that corrupts 3DMigoto resolution.
   - **The Fix**: Integrates a clean, pure-C port of `zonemodAE/ApplepieManager`'s 3-layer IL2CPP text suppression (`GameObject.SetActive` + `TMP_Text.set_text` + `GenerateTextMesh` 1-frame flash guard) with **zero UI, zero swap chains, and zero background polling threads**, loaded automatically by EFF's existing `d3dcompiler_47.dll` (`plugin\*.dll`) and untouched by `EFF-Mod-Manager` updates.

---

## 📦 Installation (2 Steps)

> 💡 **Coexistence Notes**:
> 1. Launch the game in **DX11 mode** via **XXMI Launcher** (for EFMI) while keeping EFF's `d3dcompiler_47.dll` and `plugin\eff.dll` installed in the game directory.
> 2. **Do NOT enable an EFMI mod and an EFF mod for the exact same character at the same time** (using EFMI mods on Character A and EFF mods on Character B works seamlessly).
> 3. If you still have an old **`applepie_manager.dll`** inside `<GameRoot>\plugin\`, remove it first so it does not create a `480×560` swap chain.

1. Download the latest release zip from [Releases](https://github.com/BakaMai/EFF-EFMI-Compatibility-Plugin/releases) (or grab `plugin/hide_uid.dll` and `plugin/hide_uid.ini` from this repository).
2. Place `hide_uid.dll` and `hide_uid.ini` into **`<GameRoot>\plugin\`** (right next to `eff.dll`):

```text
Arknights Endfield/                 (Game root folder containing Endfield.exe)
├── d3dcompiler_47.dll              <-- EFF's proxy loader (auto-loads plugin\*.dll)
└── plugin/
    ├── eff.dll                     <-- EFF core runtime
    ├── eff.ini
    ├── hide_uid.dll                <-- ✨ This plugin
    ├── hide_uid.ini                <-- ✨ Configuration file
    └── mods/                       <-- EFF mod packages
```

Once in-game:
- Press **`Ins` (`Insert`)** to open/close the **EFF Mod Control Panel** (`Esc` also closes it).
- Open in-game **Mod GUI Menus** (e.g., `Ctrl + K`) — menu scale and click hitboxes remain completely normal.
- Check `<GameRoot>\plugin\hide_uid_log.txt` for `[EFF-FIX]` and `[HideUID]` status logs.

---

## ⚙️ Configuration (`plugin/hide_uid.ini`)

Edit `plugin\hide_uid.ini` with any text editor (`1` = enabled, `0` = disabled; restart the game to apply changes):

| Section | Key | Default | Description |
| :--- | :--- | :---: | :--- |
| **`[uid]`** | `hide_uid` | **`1`** | **Hide** bottom-right watermark UID |
| **`[uid]`** | `hide_ping` | **`0`** | **Keep visible** bottom-right network latency (Ping ms) |
| **`[uid]`** | `hide_menu_uid` | **`1`** | **Hide** ESC pause menu UID (`ManagerNumber`) |
| **`[uid]`** | `hide_menu_name` | **`0`** | **Keep visible** ESC pause menu player nickname (`ManagerName` / `WatchPanel`) |
| **`[uid]`** | `hide_card_uid` | **`1`** | **Hide** profile / business card UID (`PlayerUidTxt`) |
| **`[uid]`** | `hide_card_name` | **`1`** | **Hide** profile / business card nickname (`PersonalInfoNode`) |
| **`[eff]`** | `fix_eff_panel` | **`1`** | **Enable** EFF `Ins` panel crash fix & 3DMigoto `res_width` / `res_height` resolution protection |

> Tip: If you only want the EFF `Ins` panel crash fix + Mod GUI Menu resolution protection without hiding any UIDs, set all keys under `[uid]` to `0` and keep `fix_eff_panel=1`.

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
- **[zonemodAE/ApplepieManager](https://github.com/zonemodAE/ApplepieManager)** — Original 3-layer IL2CPP UID text suppression research (`il2cpp_text_scanner.h`).
- **[bo3b/3Dmigoto](https://github.com/bo3b/3Dmigoto) & [SpectrumQT/EFMI](https://github.com/SpectrumQT/EFMI-Tools)** — 3DMigoto / XXMI / EFMI framework.
- **[TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)** — Minimalistic x86/x64 API Hooking Library (BSD-2-Clause License).

Licensed under the **[GNU Affero General Public License v3.0 (AGPL-3.0)](./LICENSE)**.
