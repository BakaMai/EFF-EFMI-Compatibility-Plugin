// ============================================================================
// EFF-EFMI-Compatibility-Plugin (hide_uid.c) v1.1.0
// https://github.com/BakaMai/EFF-EFMI-Compatibility-Plugin
// ============================================================================
// Coexistence bridge & standalone UID/Name hider for Arknights: Endfield
// running DX11 + EFMI (XXMI / 3DMigoto) + EFF simultaneously:
//   1) Fixes EFF's [Ins] popup control panel crash (0xC0000005 in dcomp.dll
//      SetContent caused by 3DMigoto wrapping CreateSwapChainForComposition as
//      HackerSwapChain) by isolating eff.dll's dedicated UI thread via
//      3DMigoto's native per-thread hooking_quirk_protection fast-path.
//   2) Protects 3DMigoto's res_width / res_height from being overwritten by
//      EFF's 604x640 DirectComposition SwapChain, preventing in-game Mod GUI
//      menus from blowing up in scale.
//   3) Provides a zero-UI, zero-SwapChain 3-layer IL2CPP UID / Name hider
//      derived from zonemodAE/ApplepieManager (GNU AGPL-3.0) without conflicting
//      with EFF's d3dcompiler_47.dll proxy loader.
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "applepie_mgr.h"
#include "ini_config.h"

// ============================================================================
// Globals & Logging
// ============================================================================

static HMODULE          g_hSelfModule   = NULL;
static HANDLE           g_hLogFile      = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static int              g_logLockReady  = 0;

static HWND             g_gameHwnd      = NULL;
static DWORD            g_mainThreadId  = 0;
static volatile LONG    g_hookDisabled  = 0;

static wchar_t          g_iniPath[MAX_PATH] = {0};
static wchar_t          g_logPath[MAX_PATH] = {0};

static void Log(const char* fmt, ...) {
    if (g_hLogFile == INVALID_HANDLE_VALUE || !g_logLockReady) return;
    EnterCriticalSection(&g_logLock);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[2048];
    int prefix = snprintf(buf, sizeof(buf) - 4, "[%02d:%02d:%02d.%03d] ",
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (prefix < 0) prefix = 0;

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf + prefix, sizeof(buf) - (size_t)prefix - 4, fmt, args);
    va_end(args);
    if (len < 0) len = 0;

    size_t total = (size_t)prefix + (size_t)len;
    if (total > sizeof(buf) - 3) total = sizeof(buf) - 3;
    buf[total++] = '\r';
    buf[total++] = '\n';
    buf[total]   = '\0';

    DWORD written = 0;
    WriteFile(g_hLogFile, buf, (DWORD)total, &written, NULL);
    FlushFileBuffers(g_hLogFile);
    LeaveCriticalSection(&g_logLock);
}

static void InitModulePaths(HMODULE hMod) {
    wchar_t dllPath[MAX_PATH] = {0};
    if (!GetModuleFileNameW(hMod, dllPath, MAX_PATH)) {
        wcscpy(g_iniPath, L"plugin\\hide_uid.ini");
        wcscpy(g_logPath, L"plugin\\hide_uid_log.txt");
        return;
    }
    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (!slash) slash = wcsrchr(dllPath, L'/');
    if (slash) *(slash + 1) = L'\0';
    else dllPath[0] = L'\0';

    _snwprintf(g_iniPath, MAX_PATH - 1, L"%shide_uid.ini", dllPath);
    _snwprintf(g_logPath, MAX_PATH - 1, L"%shide_uid_log.txt", dllPath);
}

// ============================================================================
// Optional Applepie v1 Metadata Export (harmless, zero overhead)
// ============================================================================

static AP_PluginInfo g_pluginInfo = {
    APPLEPIE_PLUGIN_API_VERSION,
    "Hide UID Lite",
    "Standalone 3-layer UID/Card hider + EFF/3DMigoto Ins panel fix",
    "plugin\\hide_uid.ini",
    0
};

__declspec(dllexport) AP_PluginInfo* AP_GetPluginInfo(void) {
    return &g_pluginInfo;
}

// ============================================================================
// IL2CPP API Exports
// ============================================================================

#define IL2CPP_DECL(ret, name, ...) \
    typedef ret (*t_##name)(__VA_ARGS__); \
    static t_##name p_##name = NULL

IL2CPP_DECL(void*,       il2cpp_domain_get, void);
IL2CPP_DECL(void*,       il2cpp_thread_attach, void*);
IL2CPP_DECL(void,        il2cpp_thread_detach, void*);
IL2CPP_DECL(void**,      il2cpp_domain_get_assemblies, void*, size_t*);
IL2CPP_DECL(void*,       il2cpp_assembly_get_image, void*);
IL2CPP_DECL(void*,       il2cpp_class_from_name, void*, const char*, const char*);
IL2CPP_DECL(void*,       il2cpp_class_get_method_from_name, void*, const char*, int);
IL2CPP_DECL(void*,       il2cpp_class_get_field_from_name, void*, const char*);
IL2CPP_DECL(size_t,      il2cpp_field_get_offset, void*);
IL2CPP_DECL(void*,       il2cpp_runtime_invoke, void*, void*, void**, void**);
IL2CPP_DECL(void*,       il2cpp_object_unbox, void*);
IL2CPP_DECL(void*,       il2cpp_string_new, const char*);
IL2CPP_DECL(uint32_t,    il2cpp_gchandle_new, void*, int);
IL2CPP_DECL(void,        il2cpp_gchandle_free, uint32_t);
IL2CPP_DECL(void*,       il2cpp_gchandle_get_target, uint32_t);

#undef IL2CPP_DECL

static HMODULE g_hGameAssembly = NULL;

// Resolved IL2CPP MethodInfo* and offsets
static void*  g_methodGameObjectSetActive       = NULL;
static void*  g_methodGameObjectGetActiveSelf   = NULL;
static void*  g_methodGameObjectFind            = NULL;
static void*  g_methodComponentGetGameObject    = NULL;
static void*  g_methodComponentGetTransform     = NULL;
static void*  g_methodObjectGetName             = NULL;
static void*  g_methodTransformGetParent        = NULL;
static void*  g_methodTMPSetText                = NULL;
static void*  g_methodGenerateTextMesh          = NULL;
static void*  g_methodInternalApplicationQuit   = NULL;

static size_t g_offsetMaxVisibleCharacters      = (size_t)-1;

// Active config flags
static volatile LONG g_uidHidden      = 1;
static volatile LONG g_pingHidden     = 0;
static volatile LONG g_menuUidHidden  = 1;
static volatile LONG g_menuNameHidden = 0;
static volatile LONG g_cardUidHidden  = 1;
static volatile LONG g_cardNameHidden = 1;
static volatile LONG g_fixEffPanel    = 1;

// Native function pointers extracted from MethodInfo->methodPointer (offset 0)
typedef struct MethodInfoPrefix {
    void* methodPointer;
} MethodInfoPrefix;

typedef void* (*tNativeGetObj)(void* self, void* methodInfo);
typedef void  (*tTMPSetText)(void* self, void* value, void* methodInfo);
typedef void  (*tTMPVoidMethod)(void* self, void* methodInfo);
typedef void  (*tIl2CppShutdown)(void);
typedef void  (*tAppQuit)(void* methodInfo);

static tNativeGetObj   g_nativeGetGO             = NULL;
static tNativeGetObj   g_nativeGetName           = NULL;
static tNativeGetObj   g_nativeGetTransform      = NULL;
static tNativeGetObj   g_nativeGetParent         = NULL;

static tTMPSetText     g_origSetText             = NULL;
static tTMPVoidMethod  g_origGenerateTextMesh    = NULL;
static tIl2CppShutdown g_origIl2CppShutdown      = NULL;
static tAppQuit        g_origAppQuit             = NULL;

static void*           g_cachedEmptyStr          = NULL;
static uint32_t        g_emptyStrHandle          = 0;

// Watermark SetActive GCHandles (main thread only)
static uint32_t        g_uidTextHandle           = 0;
static uint32_t        g_uidPingHandle           = 0;
static ULONGLONG       g_nextWatermarkProbeTick  = 0;
static ULONGLONG       g_nextWindowCheckTick     = 0;

// Re-entrancy guard per thread (Win32 TLS — 100% LoadLibrary-safe)
static DWORD g_tlsReentrySlot = TLS_OUT_OF_INDEXES;

static int IsCurrentThreadInUidHook(void) {
    return (g_tlsReentrySlot != TLS_OUT_OF_INDEXES) &&
           (TlsGetValue(g_tlsReentrySlot) != NULL);
}

static void SetCurrentThreadInUidHook(int active) {
    if (g_tlsReentrySlot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(g_tlsReentrySlot, active ? (LPVOID)1 : NULL);
    }
}

// ============================================================================
// Safe Memory & IL2CPP Helpers
// ============================================================================

static int ReadIL2CppString(void* str, char* buf, int bufSz) {
    if (!str || bufSz <= 1) {
        if (buf && bufSz > 0) buf[0] = '\0';
        return -1;
    }
    if (IsBadReadPtr(str, 20)) {
        buf[0] = '\0';
        return -1;
    }
    int32_t len = *(const int32_t*)((const char*)str + 16);
    if (len <= 0 || len > 2048) {
        buf[0] = '\0';
        return -1;
    }
    const wchar_t* chars = (const wchar_t*)((const char*)str + 20);
    if (IsBadReadPtr(chars, (size_t)len * sizeof(wchar_t))) {
        buf[0] = '\0';
        return -1;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, chars, len, buf, bufSz - 1, NULL, NULL);
    if (n <= 0) {
        buf[0] = '\0';
        return -1;
    }
    buf[n] = '\0';
    return n;
}

static void* SafeInvoke(void* method, void* obj, void** params, int* outHadException) {
    if (outHadException) *outHadException = 0;
    if (!method || !p_il2cpp_runtime_invoke || g_hookDisabled) return NULL;
    void* exc = NULL;
    void* res = p_il2cpp_runtime_invoke(method, obj, params, &exc);
    if (exc) {
        if (outHadException) *outHadException = 1;
        return NULL;
    }
    return res;
}

// ============================================================================
// Component Classification & Fast-Path Cache
// ============================================================================

typedef enum HideType {
    HT_MENU_UID = 0,
    HT_MENU_NAME,
    HT_CARD_UID,
    HT_CARD_NAME,
    HT_WATERMARK_UID
} HideType;

typedef struct CachedComp {
    void*    comp;
    HideType type;
    uint32_t gchandle;
} CachedComp;

#define COMP_CACHE_CAP 64
static CachedComp g_compCache[COMP_CACHE_CAP];
static SRWLOCK    g_cacheLock = SRWLOCK_INIT;

static int IsHideActive(HideType t) {
    switch (t) {
        case HT_MENU_UID:      return g_menuUidHidden != 0;
        case HT_MENU_NAME:     return g_menuNameHidden != 0;
        case HT_CARD_UID:      return g_cardUidHidden != 0;
        case HT_CARD_NAME:     return g_cardNameHidden != 0;
        case HT_WATERMARK_UID: return g_uidHidden != 0;
        default:               return 0;
    }
}

static int LookupCachedComp(void* comp, HideType* outType) {
    int found = 0;
    AcquireSRWLockShared(&g_cacheLock);
    for (int i = 0; i < COMP_CACHE_CAP; i++) {
        if (g_compCache[i].comp == comp) {
            *outType = g_compCache[i].type;
            found = 1;
            break;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);
    return found;
}

static void StoreCachedComp(void* comp, HideType type) {
    if (!comp || !p_il2cpp_gchandle_new || g_hookDisabled) return;

    AcquireSRWLockExclusive(&g_cacheLock);
    int freeIdx = -1;
    for (int i = 0; i < COMP_CACHE_CAP; i++) {
        if (g_compCache[i].comp == comp) {
            ReleaseSRWLockExclusive(&g_cacheLock);
            return;
        }
        if (freeIdx < 0 && g_compCache[i].comp == NULL) {
            freeIdx = i;
        }
    }
    // If all 64 slots are occupied, evict any destroyed Unity component slot
    if (freeIdx < 0 && g_nativeGetGO) {
        for (int i = 0; i < COMP_CACHE_CAP; i++) {
            void* aliveGo = g_nativeGetGO(g_compCache[i].comp, g_methodComponentGetGameObject);
            if (!aliveGo) {
                if (g_compCache[i].gchandle && p_il2cpp_gchandle_free) {
                    p_il2cpp_gchandle_free(g_compCache[i].gchandle);
                }
                g_compCache[i].comp     = NULL;
                g_compCache[i].gchandle = 0;
                freeIdx = i;
                break;
            }
        }
    }
    if (freeIdx >= 0) {
        uint32_t h = p_il2cpp_gchandle_new(comp, 1);
        if (h != 0) {
            g_compCache[freeIdx].comp     = comp;
            g_compCache[freeIdx].type     = type;
            g_compCache[freeIdx].gchandle = h;
        }
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
}

enum IdentityAncestorFlag {
    IAF_PERSONAL_INFO = 1 << 0,
    IAF_WATCH_PANEL   = 1 << 1,
    IAF_TEXT_NODE     = 1 << 2,
    IAF_UID_PANEL     = 1 << 3,
    IAF_PING          = 1 << 4
};

static unsigned GetIdentityAncestorFlags(void* component) {
    if (!component || !g_nativeGetTransform || !g_nativeGetParent || !g_nativeGetName)
        return 0;

    unsigned flags = 0;
    void* cur = g_nativeGetTransform(component, g_methodComponentGetTransform);
    for (int level = 0; cur && level < 16; level++) {
        cur = g_nativeGetParent(cur, g_methodTransformGetParent);
        if (!cur) break;

        void* nameStr = g_nativeGetName(cur, g_methodObjectGetName);
        if (!nameStr) continue;
        char name[64];
        if (ReadIL2CppString(nameStr, name, sizeof(name)) <= 0) continue;

        if (strcmp(name, "PersonalInfoNode") == 0)
            flags |= IAF_PERSONAL_INFO;
        else if (strcmp(name, "WatchPanel") == 0)
            flags |= IAF_WATCH_PANEL;
        else if (strcmp(name, "TextNode") == 0)
            flags |= IAF_TEXT_NODE;
        else if (strcmp(name, "UIDPanelPanel") == 0)
            flags |= IAF_UID_PANEL;
        else if (strcmp(name, "PingCon") == 0)
            flags |= IAF_PING;
    }
    return flags;
}

static int ShouldSuppressComponent(void* component) {
    if (!component || g_hookDisabled) return 0;

    // Fast path: O(1) cached component check (handles both hidden AND preserved targets)
    HideType cachedType;
    if (LookupCachedComp(component, &cachedType)) {
        return IsHideActive(cachedType);
    }

    if (!g_nativeGetGO || !g_nativeGetName) return 0;
    if (!g_uidHidden && !g_menuUidHidden && !g_menuNameHidden &&
        !g_cardUidHidden && !g_cardNameHidden) {
        return 0;
    }

    void* go = g_nativeGetGO(component, g_methodComponentGetGameObject);
    if (!go) return 0;

    void* nameStr = g_nativeGetName(go, g_methodObjectGetName);
    if (!nameStr) return 0;

    char name[64];
    if (ReadIL2CppString(nameStr, name, sizeof(name)) <= 0) return 0;

    if (strcmp(name, "ManagerNumber") == 0) {
        StoreCachedComp(component, HT_MENU_UID);
        return g_menuUidHidden != 0;
    }
    if (strcmp(name, "ManagerName") == 0) {
        StoreCachedComp(component, HT_MENU_NAME);
        return g_menuNameHidden != 0;
    }
    if (strcmp(name, "PlayerUidTxt") == 0) {
        StoreCachedComp(component, HT_CARD_UID);
        return g_cardUidHidden != 0;
    }
    if (strcmp(name, "NameText") == 0) {
        unsigned ancestors = GetIdentityAncestorFlags(component);
        if (ancestors & IAF_PERSONAL_INFO) {
            StoreCachedComp(component, HT_CARD_NAME);
            return g_cardNameHidden != 0;
        }
        if (ancestors & IAF_WATCH_PANEL) {
            StoreCachedComp(component, HT_MENU_NAME);
            return g_menuNameHidden != 0;
        }
        return 0;
    }
    if (strcmp(name, "NameTxt") == 0) {
        unsigned ancestors = GetIdentityAncestorFlags(component);
        if (ancestors & IAF_PERSONAL_INFO) {
            StoreCachedComp(component, HT_CARD_NAME);
            return g_cardNameHidden != 0;
        }
        return 0;
    }
    if (strcmp(name, "Text") == 0) {
        unsigned ancestors = GetIdentityAncestorFlags(component);
        if ((ancestors & IAF_TEXT_NODE) && (ancestors & IAF_UID_PANEL) &&
            !(ancestors & IAF_PING)) {
            StoreCachedComp(component, HT_WATERMARK_UID);
            return g_uidHidden != 0;
        }
        return 0;
    }

    return 0;
}

// ============================================================================
// Layer 1: Main-Thread SetActive(false) on Watermark GameObjects
// ============================================================================

static void HideNodeByPathOnMainThread(uint32_t* ioHandle, const char* path, const char* label) {
    if (g_hookDisabled || !g_methodGameObjectFind || !g_methodGameObjectSetActive ||
        !p_il2cpp_string_new) {
        return;
    }

    void* go = (*ioHandle && p_il2cpp_gchandle_get_target)
                   ? p_il2cpp_gchandle_get_target(*ioHandle)
                   : NULL;

    if (go && g_methodGameObjectGetActiveSelf && p_il2cpp_object_unbox) {
        int hadExc = 0;
        void* boxedBool = SafeInvoke(g_methodGameObjectGetActiveSelf, go, NULL, &hadExc);
        if (!hadExc && boxedBool) {
            uint8_t isActive = *(const uint8_t*)p_il2cpp_object_unbox(boxedBool);
            if (!isActive) {
                return; // Still alive and already inactive
            }
            // Game re-enabled the node; turn it back off
            int32_t activeVal = 0;
            void* setActiveArgs[1] = { &activeVal };
            SafeInvoke(g_methodGameObjectSetActive, go, setActiveArgs, NULL);
            return;
        }
        // Exception (MissingReferenceException) -> GameObject was destroyed (e.g. re-login)
        if (*ioHandle && p_il2cpp_gchandle_free) {
            p_il2cpp_gchandle_free(*ioHandle);
            *ioHandle = 0;
        }
        go = NULL;
    } else if (go) {
        return;
    }

    void* s = p_il2cpp_string_new(path);
    if (!s) return;
    void* findArgs[1] = { s };
    int findExc = 0;
    go = SafeInvoke(g_methodGameObjectFind, NULL, findArgs, &findExc);
    if (findExc || !go) return;

    int32_t activeVal = 0; // false
    void* setActiveArgs[1] = { &activeVal };
    int setExc = 0;
    SafeInvoke(g_methodGameObjectSetActive, go, setActiveArgs, &setExc);
    if (setExc) return;

    if (p_il2cpp_gchandle_new) {
        *ioHandle = p_il2cpp_gchandle_new(go, 1);
    }
    Log("[HideUID] SetActive(false) applied to %s (%p)", label, go);
}

static void CheckWindowHealth(ULONGLONG now) {
    if (now >= g_nextWindowCheckTick) {
        g_nextWindowCheckTick = now + 250;
        if (g_gameHwnd && !IsWindow(g_gameHwnd)) {
            InterlockedExchange(&g_hookDisabled, 1);
        }
    }
}

static void PumpWatermarkOutsideRebuild(void) {
    if (g_hookDisabled) return;

    ULONGLONG now = GetTickCount64();
    CheckWindowHealth(now);
    if (g_hookDisabled) return;

    // Only call GameObject.Find / SetActive on Unity's main thread
    if (g_mainThreadId == 0 || GetCurrentThreadId() != g_mainThreadId) return;
    if (!g_uidHidden && !g_pingHidden) return;
    if (now < g_nextWatermarkProbeTick) return;

    g_nextWatermarkProbeTick = now + 1500;

    if (g_uidHidden) {
        HideNodeByPathOnMainThread(
            &g_uidTextHandle,
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode/Text",
            "Watermark UID");
    }
    // When hide_ping=0 (default), PingCon is left 100% untouched!
    if (g_pingHidden) {
        HideNodeByPathOnMainThread(
            &g_uidPingHandle,
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode/PingCon",
            "Watermark Ping");
    }
}

// ============================================================================
// Shutdown Boundary Hooks
// ============================================================================

static void hkIl2CppShutdown(void) {
    InterlockedExchange(&g_hookDisabled, 1);
    if (g_origIl2CppShutdown) {
        g_origIl2CppShutdown();
    }
}

static void hkAppQuit(void* methodInfo) {
    InterlockedExchange(&g_hookDisabled, 1);
    if (g_origAppQuit) {
        g_origAppQuit(methodInfo);
    }
}

// ============================================================================
// Layer 2 & Layer 3: TMP_Text::set_text & TextMeshProUGUI::GenerateTextMesh
// ============================================================================

static void hkTMPSetText(void* self, void* value, void* methodInfo) {
    if (!g_origSetText) return;
    if (g_hookDisabled || IsCurrentThreadInUidHook()) {
        g_origSetText(self, value, methodInfo);
        return;
    }

    SetCurrentThreadInUidHook(1);
    PumpWatermarkOutsideRebuild();
    if (!g_hookDisabled && g_cachedEmptyStr && ShouldSuppressComponent(self)) {
        value = g_cachedEmptyStr;
    }
    SetCurrentThreadInUidHook(0);

    g_origSetText(self, value, methodInfo);
}

static void hkGenerateTextMesh(void* self, void* methodInfo) {
    if (!g_origGenerateTextMesh) return;
    if (g_hookDisabled || IsCurrentThreadInUidHook()) {
        g_origGenerateTextMesh(self, methodInfo);
        return;
    }

    SetCurrentThreadInUidHook(1);
    CheckWindowHealth(GetTickCount64());
    int suppress = (!g_hookDisabled && ShouldSuppressComponent(self));
    SetCurrentThreadInUidHook(0);

    if (!suppress || g_offsetMaxVisibleCharacters == (size_t)-1) {
        g_origGenerateTextMesh(self, methodInfo);
        return;
    }

    char* fieldAddr = (char*)self + g_offsetMaxVisibleCharacters;
    if (IsBadWritePtr(fieldAddr, sizeof(int))) {
        g_origGenerateTextMesh(self, methodInfo);
        return;
    }

    volatile LONG* field = (volatile LONG*)fieldAddr;
    LONG previousValue = *field;
    InterlockedExchange(field, 0);

    g_origGenerateTextMesh(self, methodInfo);

    // Atomically restore m_maxVisibleCharacters unless TMP explicitly wrote another value
    if (!IsBadWritePtr(fieldAddr, sizeof(int))) {
        InterlockedCompareExchange(field, previousValue, 0);
    }
}

// ============================================================================
// Unity Window & IL2CPP Metadata Initialization
// ============================================================================

typedef struct FindWndCtx {
    DWORD pid;
    HWND  hwnd;
} FindWndCtx;

static BOOL CALLBACK EnumUnityWndProc(HWND hwnd, LPARAM lParam) {
    FindWndCtx* ctx = (FindWndCtx*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;

    wchar_t cls[64] = {0};
    if (GetClassNameW(hwnd, cls, 63) && wcscmp(cls, L"UnityWndClass") == 0) {
        ctx->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND FindOwnUnityWindow(void) {
    FindWndCtx ctx;
    ctx.pid  = GetCurrentProcessId();
    ctx.hwnd = NULL;
    EnumWindows(EnumUnityWndProc, (LPARAM)&ctx);
    return ctx.hwnd;
}

static void* FindClassInAssemblies(void** asms, size_t count,
                                   const char* ns, const char* name) {
    for (size_t i = 0; i < count; i++) {
        if (!asms[i]) continue;
        void* img = p_il2cpp_assembly_get_image(asms[i]);
        if (!img) continue;
        void* klass = p_il2cpp_class_from_name(img, ns, name);
        if (klass) return klass;
    }
    return NULL;
}

static int ResolveIL2CppExports(void) {
    g_hGameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!g_hGameAssembly) return 0;

#define R(name) p_##name = (t_##name)GetProcAddress(g_hGameAssembly, #name)
    R(il2cpp_domain_get);
    R(il2cpp_thread_attach);
    R(il2cpp_thread_detach);
    R(il2cpp_domain_get_assemblies);
    R(il2cpp_assembly_get_image);
    R(il2cpp_class_from_name);
    R(il2cpp_class_get_method_from_name);
    R(il2cpp_class_get_field_from_name);
    R(il2cpp_field_get_offset);
    R(il2cpp_runtime_invoke);
    R(il2cpp_object_unbox);
    R(il2cpp_string_new);
    R(il2cpp_gchandle_new);
    R(il2cpp_gchandle_free);
    R(il2cpp_gchandle_get_target);
#undef R

    return p_il2cpp_domain_get && p_il2cpp_thread_attach &&
           p_il2cpp_domain_get_assemblies && p_il2cpp_assembly_get_image &&
           p_il2cpp_class_from_name && p_il2cpp_class_get_method_from_name &&
           p_il2cpp_runtime_invoke && p_il2cpp_string_new &&
           p_il2cpp_gchandle_new;
}

// ============================================================================
// EFF Ins Control Panel + 3DMigoto (EFMI) Coexistence Fix
// ============================================================================
// Root cause in DX11 + EFMI + EFF coexistence mode:
//   When pressing Ins, eff.dll's dedicated UI thread (EiemControlPanel::Window)
//   calls D3D11CreateDevice -> IDXGIFactory2::CreateSwapChainForComposition
//   (604x640, AlphaMode=PREMULTIPLIED) -> IDCompositionVisual::SetContent.
//   3DMigoto intercepts both calls:
//     1) Hooked_CreateSwapChainForComposition overwrites G->mResolutionInfo
//        (res_width=604, res_height=640), breaking in-game Mod GUI menu scale.
//     2) wrap_factory2_swap_chain returns HackerSwapChain*, whose QueryInterface
//        returns HackerSwapChain* for dcomp.dll's internal CDXGISwapChain IID
//        {7652C85A-...}, causing an immediate 0xC0000005 crash inside SetContent.
//
// Fix:
//   Patch eff.dll's IAT entries for D3D11CreateDevice and DCompositionCreateDevice.
//   On eff.dll's dedicated UI thread, dynamically locate 3DMigoto's TLS slot
//   (get_tls()->hooking_quirk_protection at offset +0) via bytecode verification,
//   let 3DMigoto allocate its own per-thread TLS struct cleanly, and arm
//   hooking_quirk_protection = 1 on that UI thread. Both D3D11CreateDevice and
//   Hooked_CreateSwapChainForComposition take 3DMigoto's native passthrough
//   fast-path, keeping EFF's panel 100% unwrapped and leaving 3DMigoto's
//   res_width / res_height completely untouched.
// ============================================================================

typedef HRESULT (WINAPI *tEffD3D11CreateDevice)(
    void*       pAdapter,
    int         DriverType,
    HMODULE     Software,
    UINT        Flags,
    const UINT* pFeatureLevels,
    UINT        FeatureLevels,
    UINT        SDKVersion,
    void**      ppDevice,
    UINT*       pFeatureLevel,
    void**      ppImmediateContext);

typedef HRESULT (WINAPI *tEffDCompositionCreateDevice)(
    void*       dxgiDevice,
    const void* iid,
    void**      dcompositionDevice);

static tEffD3D11CreateDevice        g_origEffD3D11CreateDevice        = NULL;
static tEffDCompositionCreateDevice g_origEffDCompositionCreateDevice = NULL;
static volatile LONG                g_effFixInstalled                 = 0;
static volatile LONG                g_effDCompLogged                  = 0;

static int GetModuleImageBounds(HMODULE hMod, uintptr_t* outStart, uintptr_t* outEnd) {
    if (!hMod || IsBadReadPtr(hMod, sizeof(IMAGE_DOS_HEADER))) return 0;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)hMod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) {
        return 0;
    }
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)((const uint8_t*)hMod + dos->e_lfanew);
    if (IsBadReadPtr(nt, sizeof(IMAGE_NT_HEADERS64))) return 0;
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage < 0x1000) {
        return 0;
    }
    *outStart = (uintptr_t)hMod;
    *outEnd   = (uintptr_t)hMod + (uintptr_t)nt->OptionalHeader.SizeOfImage;
    return 1;
}

static int PatchOrQueryModuleIAT(HMODULE hMod, const char* targetDll, const char* targetFunc,
                                 void* newFunc, void** outOrigFunc, int doPatch) {
    uintptr_t imgStart = 0, imgEnd = 0;
    if (!GetModuleImageBounds(hMod, &imgStart, &imgEnd)) return 0;

    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)hMod;
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)((const uint8_t*)hMod + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impDir.VirtualAddress == 0 || impDir.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) return 0;
    if (imgStart + impDir.VirtualAddress + sizeof(IMAGE_IMPORT_DESCRIPTOR) > imgEnd) return 0;

    const uint8_t* base = (const uint8_t*)hMod;
    const IMAGE_IMPORT_DESCRIPTOR* desc =
        (const IMAGE_IMPORT_DESCRIPTOR*)(base + impDir.VirtualAddress);

    for (; (uintptr_t)(desc + 1) <= imgEnd; desc++) {
        if (desc->Name == 0 || desc->FirstThunk == 0) break;
        if (desc->Name >= nt->OptionalHeader.SizeOfImage) continue;

        const char* dllName = (const char*)(base + desc->Name);
        if (_stricmp(dllName, targetDll) != 0) continue;
        if (desc->OriginalFirstThunk == 0 ||
            desc->OriginalFirstThunk >= nt->OptionalHeader.SizeOfImage ||
            desc->FirstThunk >= nt->OptionalHeader.SizeOfImage) {
            continue;
        }

        const IMAGE_THUNK_DATA64* origThunk =
            (const IMAGE_THUNK_DATA64*)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA64* iatThunk =
            (IMAGE_THUNK_DATA64*)((uint8_t*)hMod + desc->FirstThunk);

        for (; (uintptr_t)(origThunk + 1) <= imgEnd && (uintptr_t)(iatThunk + 1) <= imgEnd;
             origThunk++, iatThunk++) {
            if (origThunk->u1.AddressOfData == 0) break;
            if (IMAGE_SNAP_BY_ORDINAL64(origThunk->u1.Ordinal)) continue;
            if (origThunk->u1.AddressOfData >= nt->OptionalHeader.SizeOfImage) continue;

            const IMAGE_IMPORT_BY_NAME* ibn =
                (const IMAGE_IMPORT_BY_NAME*)(base + origThunk->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, targetFunc) != 0) continue;

            if (!doPatch) return 1;

            void** slot = (void**)&iatThunk->u1.Function;
            if (*slot == newFunc) return 1;

            if (outOrigFunc && *outOrigFunc == NULL) {
                *outOrigFunc = *slot;
            }

            DWORD oldProt = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
                return 0;
            }
            InterlockedExchangePointer((PVOID volatile*)slot, newFunc);
            DWORD dummy = 0;
            VirtualProtect(slot, sizeof(void*), oldProt, &dummy);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
            return 1;
        }
    }
    return 0;
}

typedef struct MigotoTlsRef {
    uintptr_t varAddr;
    uintptr_t iatAddr;
} MigotoTlsRef;

static DWORD* ScanCodeForMigotoTlsIdx(const uint8_t* code, size_t maxBytes,
                                      uintptr_t imgStart, uintptr_t imgEnd) {
    if (!code || (uintptr_t)code < imgStart || (uintptr_t)code >= imgEnd) return NULL;
    if ((uintptr_t)code + maxBytes > imgEnd) {
        maxBytes = (size_t)(imgEnd - (uintptr_t)code);
    }
    if (maxBytes < 24 || IsBadReadPtr(code, maxBytes)) return NULL;

    MigotoTlsRef refs[12];
    int refCount = 0;

    // Match 3DMigoto's get_tls():
    //   mov ecx, dword ptr [rip + disp32]  ; 8B 0D <disp32> (tls_idx)
    //   call qword ptr [rip + callDisp]    ; FF 15 <disp32> (TlsGetValue / TlsSetValue)
    for (size_t i = 0; i + 12 <= maxBytes && refCount < 12; i++) {
        if (code[i] == 0x8B && code[i + 1] == 0x0D) {
            int32_t disp = 0;
            memcpy(&disp, code + i + 2, sizeof(disp));
            uintptr_t varAddr = (uintptr_t)(code + i + 6) + (intptr_t)disp;
            if (varAddr < imgStart || varAddr + sizeof(DWORD) > imgEnd) continue;

            size_t limit = i + 16;
            if (limit + 6 > maxBytes) limit = maxBytes - 6;
            for (size_t j = i + 6; j <= limit; j++) {
                if (code[j] == 0xFF && code[j + 1] == 0x15) {
                    int32_t callDisp = 0;
                    memcpy(&callDisp, code + j + 2, sizeof(callDisp));
                    uintptr_t iatAddr = (uintptr_t)(code + j + 6) + (intptr_t)callDisp;
                    if (iatAddr >= imgStart && iatAddr + sizeof(void*) <= imgEnd) {
                        refs[refCount].varAddr = varAddr;
                        refs[refCount].iatAddr = iatAddr;
                        refCount++;
                    }
                    break;
                }
                if (code[j] == 0xE8) {
                    int32_t callRel = 0;
                    memcpy(&callRel, code + j + 1, sizeof(callRel));
                    refs[refCount].varAddr = varAddr;
                    refs[refCount].iatAddr = (uintptr_t)(code + j + 5) + (intptr_t)callRel;
                    refCount++;
                    break;
                }
                if ((code[j] == 0xFF && code[j + 1] >= 0xD0 && code[j + 1] <= 0xD7) ||
                    (code[j] == 0x41 && code[j + 1] == 0xFF && code[j + 2] >= 0xD0 && code[j + 2] <= 0xD7)) {
                    refs[refCount].varAddr = varAddr;
                    refs[refCount].iatAddr = (uintptr_t)(code + j);
                    refCount++;
                    break;
                }
            }
        }
    }

    for (int a = 0; a < refCount; a++) {
        for (int b = a + 1; b < refCount; b++) {
            if (refs[a].varAddr == refs[b].varAddr && refs[a].iatAddr != refs[b].iatAddr) {
                DWORD* pSlot = (DWORD*)refs[a].varAddr;
                if (!IsBadReadPtr(pSlot, sizeof(DWORD))) {
                    DWORD slotVal = *pSlot;
                    if (slotVal != TLS_OUT_OF_INDEXES && slotVal < 1088) {
                        return pSlot;
                    }
                }
            }
        }
    }
    return NULL;
}

static DWORD* ResolveMigotoTlsIdxPtr(HMODULE hMigoto) {
    uintptr_t imgStart = 0, imgEnd = 0;
    if (!GetModuleImageBounds(hMigoto, &imgStart, &imgEnd)) return NULL;

    const uint8_t* pCreateDev =
        (const uint8_t*)(uintptr_t)GetProcAddress(hMigoto, "D3D11CreateDevice");
    if (!pCreateDev) return NULL;

    // 1. Check inlined get_tls() directly inside D3D11CreateDevice (standard Release build)
    DWORD* found = ScanCodeForMigotoTlsIdx(pCreateDev, 160, imgStart, imgEnd);
    if (found) return found;

    // 2. Fallback: follow direct E8 rel32 call in first 64 bytes (non-inlined get_tls())
    size_t scanLen = 64;
    if ((uintptr_t)pCreateDev + scanLen <= imgEnd && !IsBadReadPtr(pCreateDev, scanLen)) {
        for (size_t i = 0; i + 5 <= scanLen; i++) {
            if (pCreateDev[i] == 0xE8) {
                int32_t rel = 0;
                memcpy(&rel, pCreateDev + i + 1, sizeof(rel));
                uintptr_t target = (uintptr_t)(pCreateDev + i + 5) + (intptr_t)rel;
                if (target >= imgStart && target + 96 <= imgEnd) {
                    found = ScanCodeForMigotoTlsIdx((const uint8_t*)target, 128, imgStart, imgEnd);
                    if (found) return found;
                }
            }
        }
    }
    return NULL;
}

static int IsPathUnderDir(const wchar_t* path, const wchar_t* dir) {
    if (!path || !dir || !*dir) return 0;
    size_t dlen = wcslen(dir);
    while (dlen > 0 && (dir[dlen - 1] == L'\\' || dir[dlen - 1] == L'/')) {
        dlen--;
    }
    if (dlen == 0) return 0;
    if (_wcsnicmp(path, dir, dlen) != 0) return 0;
    return (path[dlen] == L'\\' || path[dlen] == L'/' || path[dlen] == L'\0');
}

static int Find3DMigotoModuleAndTlsSlot(HMODULE* outMigoto, DWORD* outTlsSlot) {
    *outMigoto  = NULL;
    *outTlsSlot = TLS_OUT_OF_INDEXES;

    wchar_t sysDir[MAX_PATH] = {0};
    wchar_t winDir[MAX_PATH] = {0};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    GetWindowsDirectoryW(winDir, MAX_PATH);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W me;
    memset(&me, 0, sizeof(me));
    me.dwSize = sizeof(me);

    if (Module32FirstW(snap, &me)) {
        do {
            if (!me.hModule || me.hModule == g_hSelfModule) continue;
            if (IsPathUnderDir(me.szExePath, sysDir) || IsPathUnderDir(me.szExePath, winDir)) {
                continue;
            }
            if (!GetProcAddress(me.hModule, "D3D11CreateDevice") ||
                !GetProcAddress(me.hModule, "D3D11CreateDeviceAndSwapChain")) {
                continue;
            }
            DWORD* pTlsSlot = ResolveMigotoTlsIdxPtr(me.hModule);
            if (pTlsSlot) {
                *outMigoto  = me.hModule;
                *outTlsSlot = *pTlsSlot;
                CloseHandle(snap);
                return 1;
            }
        } while (Module32NextW(snap, &me));
    }

    CloseHandle(snap);
    return 0;
}

static HRESULT WINAPI hkEffD3D11CreateDevice(
    void*       pAdapter,
    int         DriverType,
    HMODULE     Software,
    UINT        Flags,
    const UINT* pFeatureLevels,
    UINT        FeatureLevels,
    UINT        SDKVersion,
    void**      ppDevice,
    UINT*       pFeatureLevel,
    void**      ppImmediateContext) {

    if (!g_origEffD3D11CreateDevice) return E_FAIL;

    HMODULE hMigoto = NULL;
    DWORD   migotoTlsSlot = TLS_OUT_OF_INDEXES;
    if (!Find3DMigotoModuleAndTlsSlot(&hMigoto, &migotoTlsSlot)) {
        Log("[EFF-FIX] D3D11CreateDevice passthrough on tid=%lu (no active 3DMigoto detected)",
            (unsigned long)GetCurrentThreadId());
        return g_origEffD3D11CreateDevice(
            pAdapter, DriverType, Software, Flags, pFeatureLevels,
            FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
    }

    tEffD3D11CreateDevice pMigotoCreateDev =
        (tEffD3D11CreateDevice)(void*)(uintptr_t)GetProcAddress(hMigoto, "D3D11CreateDevice");

    // Ensure 3DMigoto allocates its per-thread TLS struct using its own CRT allocator
    // and initializes _D3D11CreateDevice before we enable hooking_quirk_protection.
    uint8_t* migotoTls = (uint8_t*)TlsGetValue(migotoTlsSlot);
    if (!migotoTls && pMigotoCreateDev) {
        UINT invalidFL = 1; // Triggers immediate E_INVALIDARG after get_tls() + InitD311()
        (void)pMigotoCreateDev(NULL, 0 /* D3D_DRIVER_TYPE_UNKNOWN */, NULL, 0,
                               &invalidFL, 1, 7 /* D3D11_SDK_VERSION */, NULL, NULL, NULL);
        migotoTls = (uint8_t*)TlsGetValue(migotoTlsSlot);
    }

    if (!migotoTls || IsBadWritePtr(migotoTls, 1)) {
        Log("[EFF-FIX] [WARN] Could not obtain writable 3DMigoto TLS struct (slot=%lu, ptr=%p)",
            (unsigned long)migotoTlsSlot, (void*)migotoTls);
        return g_origEffD3D11CreateDevice(
            pAdapter, DriverType, Software, Flags, pFeatureLevels,
            FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
    }

    // Arm 3DMigoto's native hooking_quirk_protection on EFF's dedicated UI thread.
    migotoTls[0] = 1;

    HRESULT hr = g_origEffD3D11CreateDevice(
        pAdapter, DriverType, Software, Flags, pFeatureLevels,
        FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

    // Re-assert hooking_quirk_protection = 1 on EFF's dedicated UI thread so the
    // subsequent CreateSwapChainForComposition, ClipCursor(NULL), and Optimus Present
    // all bypass 3DMigoto without wrapping SwapChain or touching res_width/res_height.
    migotoTls = (uint8_t*)TlsGetValue(migotoTlsSlot);
    if (migotoTls && !IsBadWritePtr(migotoTls, 1)) {
        migotoTls[0] = SUCCEEDED(hr) ? 1 : 0;
    }

    Log("[EFF-FIX] Isolated EFF panel D3D11CreateDevice from 3DMigoto "
        "(tid=%lu, hr=0x%08lX, dev=%p, migoto=%p, tlsSlot=%lu)",
        (unsigned long)GetCurrentThreadId(),
        (unsigned long)hr,
        (ppDevice && *ppDevice) ? *ppDevice : NULL,
        (void*)hMigoto,
        (unsigned long)migotoTlsSlot);
    return hr;
}

static HRESULT WINAPI hkEffDCompositionCreateDevice(
    void*       dxgiDevice,
    const void* iid,
    void**      dcompositionDevice) {

    if (!g_origEffDCompositionCreateDevice) return E_FAIL;
    HRESULT hr = g_origEffDCompositionCreateDevice(dxgiDevice, iid, dcompositionDevice);

    if (InterlockedCompareExchange(&g_effDCompLogged, 1, 0) == 0) {
        Log("[EFF-FIX] EFF Ins panel DCompositionCreateDevice hr=0x%08lX (comp=%p) -- "
            "SwapChain unwrapped & 3DMigoto res_width/res_height intact!",
            (unsigned long)hr,
            (dcompositionDevice && *dcompositionDevice) ? *dcompositionDevice : NULL);
    }
    return hr;
}

static HMODULE FindEffPluginModule(void) {
    HMODULE hEff = GetModuleHandleW(L"eff.dll");
    if (hEff && hEff != g_hSelfModule) return hEff;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return NULL;

    MODULEENTRY32W me;
    memset(&me, 0, sizeof(me));
    me.dwSize = sizeof(me);

    HMODULE found = NULL;
    if (Module32FirstW(snap, &me)) {
        do {
            if (!me.hModule || me.hModule == g_hSelfModule) continue;
            if (!GetProcAddress(me.hModule, "AP_GetPluginInfo") ||
                !GetProcAddress(me.hModule, "AP_GetHotkeys")) {
                continue;
            }
            if (PatchOrQueryModuleIAT(me.hModule, "d3d11.dll", "D3D11CreateDevice", NULL, NULL, 0) &&
                PatchOrQueryModuleIAT(me.hModule, "dcomp.dll", "DCompositionCreateDevice", NULL, NULL, 0)) {
                found = me.hModule;
                break;
            }
        } while (Module32NextW(snap, &me));
    }

    CloseHandle(snap);
    return found;
}

static void TryInstallEffPanelFix(void) {
    if (!g_fixEffPanel || g_effFixInstalled) return;

    HMODULE hEff = FindEffPluginModule();
    if (!hEff) return;

    int okD3D = PatchOrQueryModuleIAT(
        hEff, "d3d11.dll", "D3D11CreateDevice",
        (void*)(uintptr_t)hkEffD3D11CreateDevice,
        (void**)&g_origEffD3D11CreateDevice, 1);

    int okDComp = PatchOrQueryModuleIAT(
        hEff, "dcomp.dll", "DCompositionCreateDevice",
        (void*)(uintptr_t)hkEffDCompositionCreateDevice,
        (void**)&g_origEffDCompositionCreateDevice, 1);

    if (okD3D) {
        InterlockedExchange(&g_effFixInstalled, 1);
        Log("[EFF-FIX] Installed IAT isolation on eff.dll (%p) | origD3D11=%p origDComp=%p (dcompPatched=%d)",
            (void*)hEff,
            (void*)(uintptr_t)g_origEffD3D11CreateDevice,
            (void*)(uintptr_t)g_origEffDCompositionCreateDevice,
            okDComp);
    }
}

static DWORD WINAPI InitThread(LPVOID unused) {
    (void)unused;

    InitializeCriticalSection(&g_logLock);
    g_logLockReady = 1;
    g_hLogFile = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    Log("=== Hide UID Lite v1.1 (EFF Companion + 3DMigoto Panel Fix) ===");

    HideUidConfig cfg;
    int hadIni = LoadHideUidConfig(g_iniPath, &cfg);
    g_uidHidden      = cfg.hide_uid;
    g_pingHidden     = cfg.hide_ping;
    g_menuUidHidden  = cfg.hide_menu_uid;
    g_menuNameHidden = cfg.hide_menu_name;
    g_cardUidHidden  = cfg.hide_card_uid;
    g_cardNameHidden = cfg.hide_card_name;
    g_fixEffPanel    = cfg.fix_eff_panel;

    Log("[Config] %s | uid=%d ping=%d menu_uid=%d menu_name=%d card_uid=%d card_name=%d fix_eff_panel=%d",
        hadIni ? "Loaded hide_uid.ini" : "Generated default hide_uid.ini",
        (int)g_uidHidden, (int)g_pingHidden,
        (int)g_menuUidHidden, (int)g_menuNameHidden,
        (int)g_cardUidHidden, (int)g_cardNameHidden,
        (int)g_fixEffPanel);

    // Immediately arm EFF Ins panel fix (eff.dll loads before hide_uid.dll in plugin\*.dll)
    TryInstallEffPanelFix();

    // Wait up to 90s for our own process's UnityWndClass + GameAssembly.dll
    for (int attempt = 1; attempt <= 180 && !g_hookDisabled; attempt++) {
        TryInstallEffPanelFix();
        HWND wnd = FindOwnUnityWindow();
        if (wnd && ResolveIL2CppExports()) {
            g_gameHwnd = wnd;
            g_mainThreadId = GetWindowThreadProcessId(wnd, NULL);
            Log("[IL2CPP] Ready after %d attempts (hwnd=%p, mainTid=%lu)",
                attempt, (void*)g_gameHwnd, (unsigned long)g_mainThreadId);
            break;
        }
        Sleep(500);
    }
    TryInstallEffPanelFix();

    if (!g_gameHwnd || !g_hGameAssembly || !p_il2cpp_domain_get) {
        Log("[ERROR] Timed out waiting for UnityWndClass / GameAssembly.dll");
        return 1;
    }

    void* domain = NULL;
    void* attachedThread = NULL;
    void** asms = NULL;
    size_t asmCount = 0;

    for (int i = 0; i < 100 && !g_hookDisabled; i++) {
        domain = p_il2cpp_domain_get();
        if (domain) {
            if (!attachedThread) {
                attachedThread = p_il2cpp_thread_attach(domain);
            }
            asms = p_il2cpp_domain_get_assemblies(domain, &asmCount);
            if (asms && asmCount > 0) break;
        }
        Sleep(100);
    }

    if (!asms || asmCount == 0) {
        Log("[ERROR] Failed to enumerate IL2CPP assemblies");
        if (attachedThread && p_il2cpp_thread_detach) p_il2cpp_thread_detach(attachedThread);
        return 1;
    }
    Log("[IL2CPP] Enumerated %u assemblies", (unsigned)asmCount);

    // Resolve classes & methods
    void* goClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "GameObject");
    if (goClass) {
        g_methodGameObjectSetActive     = p_il2cpp_class_get_method_from_name(goClass, "SetActive", 1);
        g_methodGameObjectGetActiveSelf = p_il2cpp_class_get_method_from_name(goClass, "get_activeSelf", 0);
        g_methodGameObjectFind          = p_il2cpp_class_get_method_from_name(goClass, "Find", 1);
    }

    void* compClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Component");
    if (compClass) {
        g_methodComponentGetGameObject = p_il2cpp_class_get_method_from_name(compClass, "get_gameObject", 0);
        g_methodComponentGetTransform  = p_il2cpp_class_get_method_from_name(compClass, "get_transform", 0);
    }

    void* objClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Object");
    if (objClass) {
        g_methodObjectGetName = p_il2cpp_class_get_method_from_name(objClass, "get_name", 0);
    }

    void* trClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Transform");
    if (trClass) {
        g_methodTransformGetParent = p_il2cpp_class_get_method_from_name(trClass, "get_parent", 0);
    }

    void* tmpClass = FindClassInAssemblies(asms, asmCount, "TMPro", "TMP_Text");
    if (tmpClass) {
        g_methodTMPSetText = p_il2cpp_class_get_method_from_name(tmpClass, "set_text", 1);
        if (p_il2cpp_class_get_field_from_name && p_il2cpp_field_get_offset) {
            void* maxVisField = p_il2cpp_class_get_field_from_name(tmpClass, "m_maxVisibleCharacters");
            if (maxVisField) {
                g_offsetMaxVisibleCharacters = p_il2cpp_field_get_offset(maxVisField);
            }
        }
    }

    void* tmpUguiClass = FindClassInAssemblies(asms, asmCount, "TMPro", "TextMeshProUGUI");
    if (tmpUguiClass) {
        g_methodGenerateTextMesh = p_il2cpp_class_get_method_from_name(tmpUguiClass, "GenerateTextMesh", 0);
    }

    void* appClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Application");
    if (appClass) {
        g_methodInternalApplicationQuit = p_il2cpp_class_get_method_from_name(appClass, "Internal_ApplicationQuit", 0);
    }

    if (!g_methodTMPSetText || !g_methodComponentGetGameObject || !g_methodObjectGetName) {
        Log("[ERROR] Missing required methods (set_text=%p, get_gameObject=%p, get_name=%p)",
            g_methodTMPSetText, g_methodComponentGetGameObject, g_methodObjectGetName);
        if (attachedThread && p_il2cpp_thread_detach) p_il2cpp_thread_detach(attachedThread);
        return 1;
    }

    g_nativeGetGO   = (tNativeGetObj)((MethodInfoPrefix*)g_methodComponentGetGameObject)->methodPointer;
    g_nativeGetName = (tNativeGetObj)((MethodInfoPrefix*)g_methodObjectGetName)->methodPointer;
    if (g_methodComponentGetTransform)
        g_nativeGetTransform = (tNativeGetObj)((MethodInfoPrefix*)g_methodComponentGetTransform)->methodPointer;
    if (g_methodTransformGetParent)
        g_nativeGetParent = (tNativeGetObj)((MethodInfoPrefix*)g_methodTransformGetParent)->methodPointer;

    g_cachedEmptyStr = p_il2cpp_string_new("");
    if (g_cachedEmptyStr) {
        g_emptyStrHandle = p_il2cpp_gchandle_new(g_cachedEmptyStr, 1);
    }

    // Detach startup worker thread from IL2CPP GC before installing runtime hooks
    if (attachedThread && p_il2cpp_thread_detach) {
        p_il2cpp_thread_detach(attachedThread);
        attachedThread = NULL;
    }

    MH_STATUS mhInit = MH_Initialize();
    if (mhInit != MH_OK && mhInit != MH_ERROR_ALREADY_INITIALIZED) {
        Log("[ERROR] MH_Initialize failed: %d", (int)mhInit);
        return 1;
    }

    // Install shutdown boundary guards
    void* pShutdown = (void*)GetProcAddress(g_hGameAssembly, "il2cpp_shutdown");
    if (pShutdown && MH_CreateHook(pShutdown, (void*)hkIl2CppShutdown, (void**)&g_origIl2CppShutdown) == MH_OK) {
        MH_EnableHook(pShutdown);
    }
    if (g_methodInternalApplicationQuit) {
        void* pQuit = ((MethodInfoPrefix*)g_methodInternalApplicationQuit)->methodPointer;
        if (pQuit && MH_CreateHook(pQuit, (void*)hkAppQuit, (void**)&g_origAppQuit) == MH_OK) {
            MH_EnableHook(pQuit);
        }
    }

    // Install TMP_Text.set_text hook
    void* setTextTarget = ((MethodInfoPrefix*)g_methodTMPSetText)->methodPointer;
    if (!setTextTarget ||
        MH_CreateHook(setTextTarget, (void*)hkTMPSetText, (void**)&g_origSetText) != MH_OK ||
        MH_EnableHook(setTextTarget) != MH_OK) {
        Log("[ERROR] Failed to hook TMP_Text.set_text (%p)", setTextTarget);
        return 1;
    }
    Log("[Hook] TMP_Text.set_text hooked at %p", setTextTarget);

    // Install TextMeshProUGUI.GenerateTextMesh fallback hook
    if (g_methodGenerateTextMesh && g_offsetMaxVisibleCharacters != (size_t)-1) {
        void* genTarget = ((MethodInfoPrefix*)g_methodGenerateTextMesh)->methodPointer;
        if (genTarget && genTarget != setTextTarget) {
            if (MH_CreateHook(genTarget, (void*)hkGenerateTextMesh, (void**)&g_origGenerateTextMesh) == MH_OK &&
                MH_EnableHook(genTarget) == MH_OK) {
                Log("[Hook] TextMeshProUGUI.GenerateTextMesh hooked at %p (m_maxVisibleCharacters=+0x%zx)",
                    genTarget, g_offsetMaxVisibleCharacters);
            } else {
                Log("[WARN] Failed to hook GenerateTextMesh (%p)", genTarget);
            }
        }
    } else {
        Log("[WARN] GenerateTextMesh fallback skipped (method=%p, offset=0x%zx)",
            g_methodGenerateTextMesh, g_offsetMaxVisibleCharacters);
    }

    Log("[Ready] All active hooks armed; startup worker exiting cleanly (0 background threads).");
    return 0;
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hSelfModule = hModule;
        DisableThreadLibraryCalls(hModule);

        HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\Endfield_HideUID_Lite_InstanceGuard");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (hMutex) CloseHandle(hMutex);
            return TRUE;
        }

        InitModulePaths(hModule);
        g_tlsReentrySlot = TlsAlloc();
        HANDLE hThread = CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_hookDisabled, 1);
    }
    return TRUE;
}
