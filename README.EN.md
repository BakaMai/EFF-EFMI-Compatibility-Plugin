# EFF-EFMI-Compatibility-Plugin v1.1.0

🌐 **[中文说明文档 (README.md)](./README.md)**

**Seamless DX11 Coexistence Bridge for EFF + EFMI (XXMI / 3DMigoto) & Headless UID Hider for *Arknights: Endfield***  
**Fixes EFF `Ins` Control Panel Crash · Protects 3DMigoto `res_width` / `res_height` (73layol Menus) · Conflict-Free 3-Layer UID Hider**

---

## 🎯 What Problems Does This Solve?

When running ***Arknights: Endfield*** in **DX11 mode (`-force-d3d11`)** via XXMI Launcher to use **EFMI (3DMigoto)** mods alongside **[EFF](https://github.com/ssice-a/EFF)** mods, three major compatibility issues arise. This lightweight companion plugin fixes all of them at once:

1. **🛠️ Fixes EFF's `Ins` Control Panel Not Opening (Silent `dcomp.dll` Crash)**
   - **The Problem**: Pressing `Ins` (`Insert`) causes `eff.dll`'s dedicated UI thread to create a `604×640` `DXGI_ALPHA_MODE_PREMULTIPLIED` composition swap chain (`CreateSwapChainForComposition`). 3DMigoto intercepts this call and wraps the swap chain in a `HackerSwapChain*`. When `dcomp.dll` (`IDCompositionVisual::SetContent`) queries Microsoft's private `CDXGISwapChain` interface (`{7652C85A-...}`), `HackerSwapChain::QueryInterface` returns the wrapper pointer instead of the native DXGI object, triggering an immediate `0xC0000005` access violation that silently kills EFF's UI thread.
   - **The Fix**: Patches only `eff.dll`'s Import Address Table (IAT) and enables 3DMigoto's native per-thread `hooking_quirk_protection` passthrough on EFF's dedicated UI thread. EFF's popup window receives a pure unwrapped native swap chain, **opening the `EFF Mod 按键控制` window reliably on `Ins` with working cursor unlock (`ClipCursor`)**.

2. **🖥️ Prevents 73layol Menus (and `res_width` / `res_height` Shaders) from Blowing Up in Size**
   - **The Problem**: Whenever an overlay plugin (such as legacy `ApplepieManager`'s `480×560` panel or EFF's `604×640` panel) calls `CreateSwapChainForComposition` through 3DMigoto, `Hooked_CreateSwapChainForComposition` overwrites 3DMigoto's global `res_width` and `res_height` with the small popup dimensions. Any 3DMigoto UI shader relying on `res_width` / `res_height` (such as 73layol's interactive `Ctrl+K` mod menus) becomes massively oversized and unusable.
   - **The Fix**: Because EFF's UI thread takes 3DMigoto's passthrough fast-path and this plugin itself creates zero windows or swap chains, **`res_width` and `res_height` remain locked to your true game resolution (e.g., `3840×2160` / `2560×1440`) even when opening or resizing EFF's `Ins` panel**.

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
- Press **`Ctrl + K`** to use **73layol EFMI menus** at normal resolution.
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

> Tip: If you only want the EFF `Ins` panel crash fix + 73layol resolution protection without hiding any UIDs, set all keys under `[uid]` to `0` and keep `fix_eff_panel=1`.

---

## 🔬 How It Works Under the Hood

3DMigoto (`EFMI\d3d11.dll`) hooks `IDXGIFactory2::CreateSwapChainForComposition` via Deviare (`cHookMgr`) and enforces in `sort_out_swap_chain_device_mess` that any incoming `ID3D11Device` must be wrapped as a `HackerDevice` (calling `DoubleBeepExit()` otherwise). However, every hooked entry point in 3DMigoto starts with a built-in re-entrancy fast-path: `if (get_tls()->hooking_quirk_protection) return fnOrig(...);`.

Instead of installing fragile global hooks on `dxgi.dll`, this plugin patches only `eff.dll`'s own IAT entries (`D3D11CreateDevice` and `DCompositionCreateDevice`), leaving Unity's main rendering thread completely untouched:
- When `eff.dll`'s dedicated `EiemControlPanel::Window::Run` thread calls `D3D11CreateDevice`, the plugin locates 3DMigoto's `tls_idx` slot via instruction-level verification.
- It performs a harmless warmup probe so 3DMigoto's own CRT allocator initializes the thread's `TLS` struct (ensuring safe `operator delete` cleanup during `DLL_THREAD_DETACH`), then sets `hooking_quirk_protection = 1` on that UI thread.
- `D3D11CreateDevice`, `CreateSwapChainForComposition`, `ClipCursor(nullptr)`, and Optimus `Present` on EFF's UI thread all take 3DMigoto's native passthrough fast-path—preventing `HackerSwapChain` wrapping (`0xC0000005` crash) and skipping `override_factory2_swap_chain` (`res_width` / `res_height` pollution).

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
