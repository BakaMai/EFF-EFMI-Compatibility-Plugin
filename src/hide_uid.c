// ============================================================================
// EFF-EFMI-Compatibility-Plugin (hide_uid.c) v1.2.0
// https://github.com/BakaMai/EFF-EFMI-Compatibility-Plugin
// ============================================================================
// Coexistence bridge & live-toggle UID/Name hider for Arknights: Endfield
// running DX11 + EFMI (XXMI / 3DMigoto) + EFF simultaneously:
//   1) Fixes EFF's [Ins] popup control panel crash (0xC0000005 in dcomp.dll
//      SetContent caused by 3DMigoto wrapping CreateSwapChainForComposition as
//      HackerSwapChain) by isolating eff.dll's dedicated UI thread via
//      3DMigoto's native per-thread hooking_quirk_protection fast-path.
//   2) Protects 3DMigoto's res_width / res_height from being overwritten by
//      EFF's 604x640 DirectComposition SwapChain, preventing in-game Mod GUI
//      menus from blowing up in scale.
//   3) Provides a 3-layer IL2CPP UID / Name hider with a pure Win32 + DWM + GDI
//      [HOME] GUI control panel (zero DX11, zero SwapChain, zero WndProc hook)
//      for instant live toggling without any resolution or EFF menu conflicts.
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "applepie_mgr.h"
#include "ini_config.h"
#include "gui_panel.h"

// ============================================================================
// Globals & Logging
// ============================================================================

static HMODULE          g_hSelfModule    = NULL;
static HANDLE           g_hLogFile       = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static CRITICAL_SECTION g_saveIniLock;
static int              g_logLockReady   = 0;
static int              g_saveLockReady  = 0;

HWND                    g_gameHwnd       = NULL;
static DWORD            g_mainThreadId   = 0;
volatile LONG           g_hookDisabled   = 0;

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
    "EFF-EFMI Compatibility & Hide UID",
    "3-layer UID/Card hider with live GUI panel + EFF/3DMigoto Ins panel fix",
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
IL2CPP_DECL(void*,       il2cpp_class_get_parent, void*);
IL2CPP_DECL(void*,       il2cpp_class_get_method_from_name, void*, const char*, int);
IL2CPP_DECL(uint32_t,    il2cpp_method_get_flags, void*, uint32_t*);
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
static void*  g_methodGameObjectSetActive            = NULL;
static void*  g_methodGameObjectGetActiveSelf        = NULL;
static void*  g_methodGameObjectGetActiveInHierarchy = NULL;
static void*  g_methodGameObjectGetTransform         = NULL;
static void*  g_methodGameObjectFind                 = NULL;
static void*  g_methodComponentGetGameObject         = NULL;
static void*  g_methodComponentGetTransform          = NULL;
static void*  g_methodObjectGetName                  = NULL;
static void*  g_methodTransformGetParent             = NULL;
static void*  g_methodTransformFind                  = NULL;
static void*  g_methodTMPSetText                     = NULL;
static void*  g_methodTMPSetMaxVisibleChars          = NULL;
static void*  g_methodSetAllDirty                    = NULL;
static void*  g_methodForceMeshUpdate2               = NULL;
static void*  g_methodForceMeshUpdate0               = NULL;
static void*  g_methodRebuild                        = NULL;
static void*  g_methodGraphicGetCanvasRenderer       = NULL;
static void*  g_methodCanvasRendererSetCull          = NULL;
static void*  g_methodGenerateTextMesh               = NULL;
static void*  g_methodCanvasWillRender               = NULL;
static void*  g_methodEventSystemUpdate              = NULL;
static void*  g_methodInternalApplicationQuit        = NULL;

static size_t g_offsetCachedPtr                      = 0x10;
static size_t g_offsetMaxVisibleCharacters           = (size_t)-1;
static size_t g_offsetHavePropertiesChanged          = (size_t)-1;

// Active config flags (shared with gui_panel.h)
volatile LONG g_uidHidden      = 1;
volatile LONG g_pingHidden     = 0;
volatile LONG g_menuUidHidden  = 1;
volatile LONG g_menuNameHidden = 0;
volatile LONG g_cardUidHidden  = 1;
volatile LONG g_cardNameHidden = 1;
volatile LONG g_fixEffPanel    = 1;

static volatile LONG g_configRevision     = 1;
static LONG          g_appliedRevision    = 0;
static int           g_watermarkSynced    = 0;
static int           g_generateMeshHooked = 0;

// Native function pointers extracted from MethodInfo->methodPointer (offset 0)
typedef struct MethodInfoPrefix {
    void* methodPointer;
} MethodInfoPrefix;

typedef void  (*tTMPSetText)(void* self, void* value, void* methodInfo);
typedef void  (*tTMPVoidMethod)(void* self, void* methodInfo);
typedef void  (*tCanvasWillRender)(void* arg0, void* arg1);
typedef void  (*tIl2CppShutdown)(void);
typedef void  (*tAppQuit)(void* methodInfo);

static tTMPSetText       g_origSetText             = NULL;
static tTMPVoidMethod    g_origGenerateTextMesh    = NULL;
static tCanvasWillRender g_origCanvasWillRender    = NULL;
static tTMPVoidMethod    g_origEventSystemUpdate   = NULL;
static tIl2CppShutdown   g_origIl2CppShutdown      = NULL;
static tAppQuit          g_origAppQuit             = NULL;

static void*             g_cachedEmptyStr          = NULL;
static uint32_t          g_emptyStrHandle          = 0;

// Watermark SetActive GCHandles (main thread only)
static uint32_t          g_uidTextHandle           = 0;
static uint32_t          g_uidPingHandle           = 0;
static int               g_uidTouchedByUs          = 0;
static int               g_pingTouchedByUs         = 0;
static ULONGLONG         g_nextWatermarkProbeTick  = 0;
static ULONGLONG         g_nextWindowCheckTick     = 0;

// Per-stage TLS bitmask guards (separate bits so PumpMainThreadSync -> ForceMeshUpdate
// NEVER blocks hkGenerateTextMesh from applying m_maxVisibleCharacters!)
#define TLS_BIT_PUMP     0x1u
#define TLS_BIT_GENMESH  0x2u
#define TLS_BIT_SETTEXT  0x4u

static DWORD g_tlsReentrySlot = TLS_OUT_OF_INDEXES;

static uintptr_t GetThreadHookMask(void) {
    if (g_tlsReentrySlot == TLS_OUT_OF_INDEXES) return 0;
    return (uintptr_t)TlsGetValue(g_tlsReentrySlot);
}

static void SetThreadHookBit(uintptr_t bit, int active) {
    if (g_tlsReentrySlot == TLS_OUT_OF_INDEXES) return;
    uintptr_t cur = (uintptr_t)TlsGetValue(g_tlsReentrySlot);
    if (active) cur |= bit;
    else        cur &= ~bit;
    TlsSetValue(g_tlsReentrySlot, (LPVOID)cur);
}

static void OnPanelConfigChanged(void) {
    InterlockedIncrement(&g_configRevision);
    if (g_saveLockReady) {
        EnterCriticalSection(&g_saveIniLock);
        HideUidConfig snap;
        SnapshotGuiPanelConfig(&snap);
        SaveHideUidConfig(g_iniPath, &snap);
        LeaveCriticalSection(&g_saveIniLock);

        char hkBuf[64];
        FormatHotkeyChord(&snap.toggle_key, hkBuf, sizeof(hkBuf));
        Log("[GUI] Config updated -> uid=%d ping=%d menu_uid=%d menu_name=%d card_uid=%d card_name=%d key=%s lang=%s",
            snap.hide_uid, snap.hide_ping, snap.hide_menu_uid, snap.hide_menu_name,
            snap.hide_card_uid, snap.hide_card_name, hkBuf, snap.language ? "en" : "zh");
    }
}

// ============================================================================
// Safe Memory & IL2CPP Helpers (Zero-Exception Pointer Validation + SafeInvoke)
// ============================================================================

static int IsValidUserPtr(const void* p, size_t bytes) {
    uintptr_t addr = (uintptr_t)p;
    if (addr < 0x10000u) return 0;
    if (addr > 0x00007FFFFFFFFFFFull) return 0;
    if (bytes > 0x00007FFFFFFFFFFFull - addr) return 0;
    return 1;
}

static uintptr_t GetUnityCachedPtr(const void* unityObj) {
    if (!IsValidUserPtr(unityObj, g_offsetCachedPtr + sizeof(uintptr_t))) return 0;
    uintptr_t klass = *(const uintptr_t*)unityObj;
    if (!IsValidUserPtr((const void*)klass, sizeof(void*))) return 0;
    uintptr_t cachedPtr = *(const uintptr_t*)((const char*)unityObj + g_offsetCachedPtr);
    if (!IsValidUserPtr((const void*)cachedPtr, sizeof(void*))) return 0;
    return cachedPtr;
}

static int IsUnityObjectAlive(const void* unityObj) {
    return GetUnityCachedPtr(unityObj) != 0;
}

static int ReadIL2CppString(const void* str, char* buf, int bufSz) {
    if (!str || bufSz <= 1) {
        if (buf && bufSz > 0) buf[0] = '\0';
        return -1;
    }
    if (!IsValidUserPtr(str, 20)) {
        buf[0] = '\0';
        return -1;
    }
    int32_t len = *(const int32_t*)((const char*)str + 16);
    if (len <= 0 || len > 2048) {
        buf[0] = '\0';
        return -1;
    }
    const wchar_t* chars = (const wchar_t*)((const char*)str + 20);
    if (!IsValidUserPtr(chars, (size_t)len * sizeof(wchar_t))) {
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

// SafeInvoke executes inside GameAssembly.dll's MSVC try/catch(Il2CppExceptionWrapper),
// guaranteeing managed exceptions NEVER unwind our C stack or wedge TLS flags!
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

static int SafeUnboxBool(void* boxedBool, int fallback) {
    if (!boxedBool || !p_il2cpp_object_unbox) return fallback;
    void* ptr = p_il2cpp_object_unbox(boxedBool);
    if (!IsValidUserPtr(ptr, 1)) return fallback;
    return (*(const uint8_t*)ptr) != 0;
}

// ============================================================================
// Component Classification & Fast-Path Seen-Table + Live Target Cache
// ============================================================================

typedef enum HideType {
    HT_NONE           = 0,
    HT_MENU_UID       = 1,
    HT_MENU_NAME      = 2,
    HT_CARD_UID       = 3,
    HT_CARD_NAME      = 4,
    HT_WATERMARK_UID  = 5,
    HT_WATERMARK_PING = 6
} HideType;

#define SEEN_EMPTY        0
#define SEEN_NON_TARGET  -1

typedef struct SeenEntry {
    const void* comp;
    uintptr_t   cachedPtr;
    int         tag; // SEEN_EMPTY (0), SEEN_NON_TARGET (-1), or (int)HideType (1..6)
} SeenEntry;

#define SEEN_TABLE_CAP 1024
static SeenEntry g_seenTable[SEEN_TABLE_CAP];

typedef struct CachedComp {
    void*     comp;
    uintptr_t cachedPtr;
    HideType  type;
    uint32_t  gchandle;
    uint32_t  origTextHandle;
    int       origMaxVisible;
} CachedComp;

#define COMP_CACHE_CAP 64
static CachedComp g_compCache[COMP_CACHE_CAP];
static SRWLOCK    g_cacheLock = SRWLOCK_INIT;

static int IsHideActive(HideType t) {
    switch (t) {
        case HT_MENU_UID:       return g_menuUidHidden != 0;
        case HT_MENU_NAME:      return g_menuNameHidden != 0;
        case HT_CARD_UID:       return g_cardUidHidden != 0;
        case HT_CARD_NAME:      return g_cardNameHidden != 0;
        case HT_WATERMARK_UID:  return g_uidHidden != 0;
        case HT_WATERMARK_PING: return g_pingHidden != 0;
        default:                return 0;
    }
}

static uint32_t HashCompPair(const void* comp, uintptr_t cachedPtr) {
    uint64_t x = ((uint64_t)(uintptr_t)comp >> 4) ^ ((uint64_t)cachedPtr * 11400714819323198485ull);
    x ^= x >> 33;
    return (uint32_t)x;
}

static int LookupSeenComp(const void* comp, uintptr_t cachedPtr, HideType* outType) {
    uint32_t base = HashCompPair(comp, cachedPtr) & (SEEN_TABLE_CAP - 1);
    int result = SEEN_EMPTY;
    AcquireSRWLockShared(&g_cacheLock);
    for (uint32_t probe = 0; probe < 4; probe++) {
        const SeenEntry* e = &g_seenTable[(base + probe) & (SEEN_TABLE_CAP - 1)];
        if (e->comp == comp && e->cachedPtr == cachedPtr && e->tag != SEEN_EMPTY) {
            result = e->tag;
            if (result > 0 && outType) {
                *outType = (HideType)result;
            }
            break;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);
    return result;
}

static void RecordSeenComp(const void* comp, uintptr_t cachedPtr, int tag) {
    uint32_t base = HashCompPair(comp, cachedPtr) & (SEEN_TABLE_CAP - 1);
    AcquireSRWLockExclusive(&g_cacheLock);
    uint32_t targetSlot = base;
    int foundSlot = 0;
    for (uint32_t probe = 0; probe < 4; probe++) {
        uint32_t idx = (base + probe) & (SEEN_TABLE_CAP - 1);
        if ((g_seenTable[idx].comp == comp && g_seenTable[idx].cachedPtr == cachedPtr) ||
            g_seenTable[idx].tag == SEEN_EMPTY) {
            targetSlot = idx;
            foundSlot = 1;
            break;
        }
    }
    if (!foundSlot) {
        // Prefer overwriting a non-target or dead slot rather than a live tracked target
        for (uint32_t probe = 0; probe < 4; probe++) {
            uint32_t idx = (base + probe) & (SEEN_TABLE_CAP - 1);
            if (g_seenTable[idx].tag <= 0 || !IsUnityObjectAlive(g_seenTable[idx].comp)) {
                targetSlot = idx;
                break;
            }
        }
    }
    g_seenTable[targetSlot].comp      = comp;
    g_seenTable[targetSlot].cachedPtr = cachedPtr;
    g_seenTable[targetSlot].tag       = tag;
    ReleaseSRWLockExclusive(&g_cacheLock);
}

static void ClearCachedCompEntryLocked(int idx) {
    if (g_compCache[idx].gchandle && p_il2cpp_gchandle_free) {
        p_il2cpp_gchandle_free(g_compCache[idx].gchandle);
    }
    if (g_compCache[idx].origTextHandle && p_il2cpp_gchandle_free) {
        p_il2cpp_gchandle_free(g_compCache[idx].origTextHandle);
    }
    memset(&g_compCache[idx], 0, sizeof(CachedComp));
}

static int ReadCompMaxVisibleChars(const void* comp, int fallback) {
    if (!comp || g_offsetMaxVisibleCharacters == (size_t)-1) return fallback;
    const char* addr = (const char*)comp + g_offsetMaxVisibleCharacters;
    if (!IsValidUserPtr(addr, sizeof(int))) return fallback;
    int v = *(const int*)addr;
    return (v > 0) ? v : fallback;
}

static void StoreCachedComp(void* comp, uintptr_t cachedPtr, HideType type, const char* debugName) {
    if (!comp || !cachedPtr || !p_il2cpp_gchandle_new || g_hookDisabled) return;

    int initialMaxVis = ReadCompMaxVisibleChars(comp, 99999);
    int inserted = 0;

    AcquireSRWLockExclusive(&g_cacheLock);
    int freeIdx = -1;
    for (int i = 0; i < COMP_CACHE_CAP; i++) {
        if (g_compCache[i].comp != NULL && !IsUnityObjectAlive(g_compCache[i].comp)) {
            ClearCachedCompEntryLocked(i);
        }
        if (g_compCache[i].comp == comp && g_compCache[i].cachedPtr == cachedPtr) {
            g_compCache[i].type = type;
            ReleaseSRWLockExclusive(&g_cacheLock);
            return;
        }
        if (freeIdx < 0 && g_compCache[i].comp == NULL) {
            freeIdx = i;
        }
    }
    if (freeIdx >= 0) {
        uint32_t h = p_il2cpp_gchandle_new(comp, 0);
        if (h != 0) {
            g_compCache[freeIdx].comp           = comp;
            g_compCache[freeIdx].cachedPtr      = cachedPtr;
            g_compCache[freeIdx].type           = type;
            g_compCache[freeIdx].gchandle       = h;
            g_compCache[freeIdx].origTextHandle = 0;
            g_compCache[freeIdx].origMaxVisible = initialMaxVis;
            inserted = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_cacheLock);

    if (inserted) {
        Log("[HideUID] Tracked target TMP_Text: name='%s' type=%d comp=%p",
            debugName ? debugName : "?", (int)type, comp);
    }
}

static void UpdateCachedOrigMaxVisible(const void* comp, int maxVis) {
    if (maxVis <= 0) return;
    AcquireSRWLockExclusive(&g_cacheLock);
    for (int i = 0; i < COMP_CACHE_CAP; i++) {
        if (g_compCache[i].comp == comp) {
            g_compCache[i].origMaxVisible = maxVis;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
}

static int GetCachedOrigMaxVisible(const void* comp) {
    int res = 99999;
    AcquireSRWLockShared(&g_cacheLock);
    for (int i = 0; i < COMP_CACHE_CAP; i++) {
        if (g_compCache[i].comp == comp && g_compCache[i].origMaxVisible > 0) {
            res = g_compCache[i].origMaxVisible;
            break;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);
    return res;
}

static void RecordOriginalCompText(void* comp, void* strObj) {
    if (!comp || !strObj || strObj == g_cachedEmptyStr ||
        !p_il2cpp_gchandle_new || g_hookDisabled) {
        return;
    }
    AcquireSRWLockExclusive(&g_cacheLock);
    for (int i = 0; i < COMP_CACHE_CAP; i++) {
        if (g_compCache[i].comp == comp) {
            void* prev = (g_compCache[i].origTextHandle && p_il2cpp_gchandle_get_target)
                             ? p_il2cpp_gchandle_get_target(g_compCache[i].origTextHandle)
                             : NULL;
            if (prev != strObj) {
                if (g_compCache[i].origTextHandle && p_il2cpp_gchandle_free) {
                    p_il2cpp_gchandle_free(g_compCache[i].origTextHandle);
                }
                g_compCache[i].origTextHandle = p_il2cpp_gchandle_new(strObj, 0);
            }
            break;
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

static unsigned GetIdentityAncestorFlagsSafe(void* component) {
    if (!IsUnityObjectAlive(component) ||
        !g_methodComponentGetTransform || !g_methodTransformGetParent || !g_methodObjectGetName) {
        return 0;
    }

    unsigned flags = 0;
    void* cur = SafeInvoke(g_methodComponentGetTransform, component, NULL, NULL);
    for (int level = 0; IsUnityObjectAlive(cur) && level < 24; level++) {
        cur = SafeInvoke(g_methodTransformGetParent, cur, NULL, NULL);
        if (!IsUnityObjectAlive(cur)) break;

        void* nameStr = SafeInvoke(g_methodObjectGetName, cur, NULL, NULL);
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

static int IdentifyTargetComponent(void* component, HideType* outType) {
    if (outType) *outType = HT_NONE;
    if (!component || g_hookDisabled) return 0;

    uintptr_t cachedPtr = GetUnityCachedPtr(component);
    if (cachedPtr == 0) return 0;

    int seen = LookupSeenComp(component, cachedPtr, outType);
    if (seen > 0) return 1;
    if (seen == SEEN_NON_TARGET) return 0;

    // Slow-path inspection runs ONCE per unique (comp, cachedPtr) on the Unity main thread
    // via SafeInvoke so managed exceptions are 100% caught inside GameAssembly.dll.
    if (g_mainThreadId == 0 || GetCurrentThreadId() != g_mainThreadId ||
        !g_methodComponentGetGameObject || !g_methodObjectGetName) {
        return 0;
    }

    void* go = SafeInvoke(g_methodComponentGetGameObject, component, NULL, NULL);
    if (!IsUnityObjectAlive(go)) {
        RecordSeenComp(component, cachedPtr, SEEN_NON_TARGET);
        return 0;
    }

    void* nameStr = SafeInvoke(g_methodObjectGetName, go, NULL, NULL);
    char name[64];
    if (!nameStr || ReadIL2CppString(nameStr, name, sizeof(name)) <= 0) {
        RecordSeenComp(component, cachedPtr, SEEN_NON_TARGET);
        return 0;
    }

    HideType matchedType = HT_MENU_UID;
    int matched = 0;

    if (strcmp(name, "ManagerNumber") == 0) {
        matchedType = HT_MENU_UID;
        matched = 1;
    } else if (strcmp(name, "ManagerName") == 0) {
        matchedType = HT_MENU_NAME;
        matched = 1;
    } else if (strcmp(name, "PlayerUidTxt") == 0) {
        matchedType = HT_CARD_UID;
        matched = 1;
    } else if (strcmp(name, "NameText") == 0 || strcmp(name, "NameTxt") == 0) {
        unsigned ancestors = GetIdentityAncestorFlagsSafe(component);
        if (ancestors & IAF_PERSONAL_INFO) {
            matchedType = HT_CARD_NAME;
            matched = 1;
        } else if (ancestors & IAF_WATCH_PANEL) {
            matchedType = HT_MENU_NAME;
            matched = 1;
        }
    } else if (strcmp(name, "Text") == 0 || strncmp(name, "Ping", 4) == 0 ||
               strcmp(name, "DelayText") == 0 || strcmp(name, "MsText") == 0) {
        unsigned ancestors = GetIdentityAncestorFlagsSafe(component);
        if ((ancestors & IAF_UID_PANEL) && (ancestors & IAF_PING)) {
            matchedType = HT_WATERMARK_PING;
            matched = 1;
        } else if (strcmp(name, "Text") == 0 &&
                   (ancestors & IAF_TEXT_NODE) && (ancestors & IAF_UID_PANEL) &&
                   !(ancestors & IAF_PING)) {
            matchedType = HT_WATERMARK_UID;
            matched = 1;
        }
    }

    if (matched) {
        RecordSeenComp(component, cachedPtr, (int)matchedType);
        StoreCachedComp(component, cachedPtr, matchedType, name);
        *outType = matchedType;
        return 1;
    }

    RecordSeenComp(component, cachedPtr, SEEN_NON_TARGET);
    return 0;
}

// ============================================================================
// Layer 1 & Live State Sync: Main-Thread SetActive + Non-Destructive Mesh Sync
// ============================================================================

static int IsParentActiveInHierarchy(void* go) {
    if (!IsUnityObjectAlive(go)) return 0;
    if (!g_methodGameObjectGetTransform || !g_methodTransformGetParent ||
        !g_methodComponentGetGameObject || !g_methodGameObjectGetActiveInHierarchy) {
        return 1;
    }
    void* tr = SafeInvoke(g_methodGameObjectGetTransform, go, NULL, NULL);
    if (!IsUnityObjectAlive(tr)) return 0;
    void* parentTr = SafeInvoke(g_methodTransformGetParent, tr, NULL, NULL);
    if (!IsUnityObjectAlive(parentTr)) return 1;
    void* parentGo = SafeInvoke(g_methodComponentGetGameObject, parentTr, NULL, NULL);
    if (!IsUnityObjectAlive(parentGo)) return 0;
    int hadExc = 0;
    void* boxed = SafeInvoke(g_methodGameObjectGetActiveInHierarchy, parentGo, NULL, &hadExc);
    if (hadExc || !boxed) return 0;
    return SafeUnboxBool(boxed, 1);
}

static void* FindWatermarkTargetGo(const char* fullPath,
                                   const char* parentPath,
                                   const char* childName,
                                   HideType fallbackType) {
    if (!p_il2cpp_string_new) return NULL;

    // 1. Direct GameObject.Find(fullPath) when target is active
    if (g_methodGameObjectFind) {
        void* sFull = p_il2cpp_string_new(fullPath);
        if (sFull) {
            void* args[1] = { sFull };
            void* go = SafeInvoke(g_methodGameObjectFind, NULL, args, NULL);
            if (IsUnityObjectAlive(go)) return go;
        }
    }

    // 2. Parent GameObject.Find(parentPath) + Transform.Find(childName)
    //    Finds the child EVEN WHEN SetActive(false) made it inactive!
    if (g_methodGameObjectFind && g_methodGameObjectGetTransform &&
        g_methodTransformFind && g_methodComponentGetGameObject) {
        void* sParent = p_il2cpp_string_new(parentPath);
        void* sChild  = p_il2cpp_string_new(childName);
        if (sParent && sChild) {
            void* findArgs[1] = { sParent };
            void* parentGo = SafeInvoke(g_methodGameObjectFind, NULL, findArgs, NULL);
            if (IsUnityObjectAlive(parentGo)) {
                void* parentTr = SafeInvoke(g_methodGameObjectGetTransform, parentGo, NULL, NULL);
                if (IsUnityObjectAlive(parentTr)) {
                    void* trArgs[1] = { sChild };
                    void* childTr = SafeInvoke(g_methodTransformFind, parentTr, trArgs, NULL);
                    if (IsUnityObjectAlive(childTr)) {
                        void* childGo = SafeInvoke(g_methodComponentGetGameObject, childTr, NULL, NULL);
                        if (IsUnityObjectAlive(childGo)) return childGo;
                    }
                }
            }
        }
    }

    // 3. Fallback via tracked TMP_Text component in g_compCache (prefer active parent panel)
    if (g_methodComponentGetGameObject) {
        void* candidates[COMP_CACHE_CAP];
        int candCount = 0;
        AcquireSRWLockShared(&g_cacheLock);
        for (int i = 0; i < COMP_CACHE_CAP; i++) {
            if (g_compCache[i].comp != NULL &&
                g_compCache[i].type == fallbackType &&
                IsUnityObjectAlive(g_compCache[i].comp)) {
                candidates[candCount++] = g_compCache[i].comp;
            }
        }
        ReleaseSRWLockShared(&g_cacheLock);

        void* firstAliveGo = NULL;
        for (int c = 0; c < candCount; c++) {
            void* matchedComp = candidates[c];
            if (!IsUnityObjectAlive(matchedComp)) continue;

            if (fallbackType == HT_WATERMARK_UID) {
                void* go = SafeInvoke(g_methodComponentGetGameObject, matchedComp, NULL, NULL);
                if (IsUnityObjectAlive(go)) {
                    if (IsParentActiveInHierarchy(go)) return go;
                    if (!firstAliveGo) firstAliveGo = go;
                }
            } else if (fallbackType == HT_WATERMARK_PING &&
                       g_methodComponentGetTransform && g_methodTransformGetParent && g_methodObjectGetName) {
                void* cur = SafeInvoke(g_methodComponentGetTransform, matchedComp, NULL, NULL);
                for (int lvl = 0; IsUnityObjectAlive(cur) && lvl < 8; lvl++) {
                    void* nStr = SafeInvoke(g_methodObjectGetName, cur, NULL, NULL);
                    char nBuf[48];
                    if (nStr && ReadIL2CppString(nStr, nBuf, sizeof(nBuf)) > 0 &&
                        strcmp(nBuf, "PingCon") == 0) {
                        void* go = SafeInvoke(g_methodComponentGetGameObject, cur, NULL, NULL);
                        if (IsUnityObjectAlive(go)) {
                            if (IsParentActiveInHierarchy(go)) return go;
                            if (!firstAliveGo) firstAliveGo = go;
                        }
                        break;
                    }
                    cur = SafeInvoke(g_methodTransformGetParent, cur, NULL, NULL);
                }
            }
        }
        if (firstAliveGo) return firstAliveGo;
    }

    return NULL;
}

static int ApplyGoActiveIfNeeded(void* go, int wantVisible, const char* label) {
    if (!IsUnityObjectAlive(go) || !g_methodGameObjectSetActive) return 0;

    if (g_methodGameObjectGetActiveSelf) {
        int hadExc = 0;
        void* boxed = SafeInvoke(g_methodGameObjectGetActiveSelf, go, NULL, &hadExc);
        if (!hadExc && boxed) {
            int curActive = SafeUnboxBool(boxed, -1);
            if (curActive == (wantVisible ? 1 : 0)) {
                return 1;
            }
        }
    }

    uint8_t activeByte = wantVisible ? 1 : 0;
    void* args[1] = { &activeByte };
    int setExc = 0;
    SafeInvoke(g_methodGameObjectSetActive, go, args, &setExc);
    if (setExc) return 0;

    Log("[HideUID] SetActive(%s) applied to %s (%p)",
        wantVisible ? "true" : "false", label, go);
    return 1;
}

static int SetNodeActiveOnMainThread(uint32_t* ioHandle,
                                     const char* fullPath,
                                     const char* parentPath,
                                     const char* childName,
                                     HideType fallbackType,
                                     int wantVisible,
                                     const char* label) {
    if (g_hookDisabled || !g_methodGameObjectSetActive) return 0;

    void* go = (*ioHandle && p_il2cpp_gchandle_get_target)
                   ? p_il2cpp_gchandle_get_target(*ioHandle)
                   : NULL;

    // Discard stale handle if destroyed OR if its parent UIDPanelPanel was deactivated
    // (e.g. Login screen panel deactivated after entering the 3D game world)
    if (go && (!IsUnityObjectAlive(go) || !IsParentActiveInHierarchy(go))) {
        if (*ioHandle && p_il2cpp_gchandle_free) {
            p_il2cpp_gchandle_free(*ioHandle);
        }
        *ioHandle = 0;
        go = NULL;
    }

    if (!go) {
        go = FindWatermarkTargetGo(fullPath, parentPath, childName, fallbackType);
        if (!go) return 0;
        if (p_il2cpp_gchandle_new) {
            *ioHandle = p_il2cpp_gchandle_new(go, 0);
        }
    }

    return ApplyGoActiveIfNeeded(go, wantVisible, label);
}

static void RefreshSingleTMPComponent(const CachedComp* entry) {
    void* comp = entry->comp;
    if (!IsUnityObjectAlive(comp)) return;

    int hide = IsHideActive(entry->type);
    int targetMaxVis = hide ? 0 : (entry->origMaxVisible > 0 ? entry->origMaxVisible : 99999);

    // 1. Hardware CanvasRenderer batch cull toggle (0-frame GPU visibility guarantee
    //    even if UIText skips SetMesh on 0-vertex mesh Info, without altering layout!)
    if (g_methodGraphicGetCanvasRenderer && g_methodCanvasRendererSetCull) {
        void* cr = SafeInvoke(g_methodGraphicGetCanvasRenderer, comp, NULL, NULL);
        if (IsUnityObjectAlive(cr)) {
            uint8_t cullVal = hide ? 1 : 0;
            void* cullArgs[1] = { &cullVal };
            SafeInvoke(g_methodCanvasRendererSetCull, cr, cullArgs, NULL);
        }
    }

    if (g_generateMeshHooked && g_offsetMaxVisibleCharacters != (size_t)-1) {
        // 2. Non-destructive live mesh update: m_text stays 100% intact!
        if (g_methodTMPSetMaxVisibleChars) {
            int32_t mvArg = (int32_t)targetMaxVis;
            void* args[1] = { &mvArg };
            SafeInvoke(g_methodTMPSetMaxVisibleChars, comp, args, NULL);
        } else {
            char* fieldAddr = (char*)comp + g_offsetMaxVisibleCharacters;
            if (IsValidUserPtr(fieldAddr, sizeof(int))) {
                *(int*)fieldAddr = targetMaxVis;
            }
        }

        if (g_offsetHavePropertiesChanged != (size_t)-1) {
            char* dirtyAddr = (char*)comp + g_offsetHavePropertiesChanged;
            if (IsValidUserPtr(dirtyAddr, 1)) {
                *(uint8_t*)dirtyAddr = 1;
            }
        }

        if (g_methodSetAllDirty) {
            SafeInvoke(g_methodSetAllDirty, comp, NULL, NULL);
        }

        if (g_methodForceMeshUpdate2) {
            uint8_t ignoreActive = 0;
            uint8_t forceReparse = 0;
            void* fmArgs[2] = { &ignoreActive, &forceReparse };
            SafeInvoke(g_methodForceMeshUpdate2, comp, fmArgs, NULL);
        } else if (g_methodForceMeshUpdate0) {
            SafeInvoke(g_methodForceMeshUpdate0, comp, NULL, NULL);
        } else if (g_methodRebuild) {
            int32_t preRenderStage = 3; // CanvasUpdate.PreRender
            void* rbArgs[1] = { &preRenderStage };
            SafeInvoke(g_methodRebuild, comp, rbArgs, NULL);
        }
    } else if (g_methodTMPSetText && g_cachedEmptyStr) {
        // Fallback only if GenerateTextMesh hook was unavailable
        if (hide) {
            void* args[1] = { g_cachedEmptyStr };
            SafeInvoke(g_methodTMPSetText, comp, args, NULL);
        } else if (entry->origTextHandle && p_il2cpp_gchandle_get_target) {
            void* origStr = p_il2cpp_gchandle_get_target(entry->origTextHandle);
            if (origStr) {
                void* args[1] = { origStr };
                SafeInvoke(g_methodTMPSetText, comp, args, NULL);
            }
        }
    }
}

static void RefreshCachedTextComponentsOnMainThread(void) {
    if (g_hookDisabled) return;

    CachedComp snap[COMP_CACHE_CAP];
    int count = 0;

    AcquireSRWLockExclusive(&g_cacheLock);
    for (int i = 0; i < COMP_CACHE_CAP; i++) {
        if (g_compCache[i].comp != NULL) {
            if (!IsUnityObjectAlive(g_compCache[i].comp)) {
                ClearCachedCompEntryLocked(i);
                continue;
            }
            snap[count++] = g_compCache[i];
        }
    }
    ReleaseSRWLockExclusive(&g_cacheLock);

    for (int i = 0; i < count; i++) {
        RefreshSingleTMPComponent(&snap[i]);
    }
    Log("[HideUID] Live-synced %d active TMP_Text components", count);
}

static void CheckWindowHealth(ULONGLONG now) {
    if (now >= g_nextWindowCheckTick) {
        g_nextWindowCheckTick = now + 250;
        if (g_gameHwnd && !IsWindow(g_gameHwnd)) {
            InterlockedExchange(&g_hookDisabled, 1);
        }
    }
}

static void PumpMainThreadSync(void) {
    if (g_hookDisabled) return;
    if (GetThreadHookMask() & TLS_BIT_PUMP) return;

    ULONGLONG now = GetTickCount64();
    CheckWindowHealth(now);
    if (g_hookDisabled) return;

    if (g_mainThreadId == 0 || GetCurrentThreadId() != g_mainThreadId) return;

    LONG curRev = g_configRevision;
    int configJustChanged = (curRev != g_appliedRevision);

    if (!configJustChanged && g_watermarkSynced && now < g_nextWatermarkProbeTick) {
        return;
    }
    if (!configJustChanged && !g_watermarkSynced && now < g_nextWatermarkProbeTick) {
        return;
    }

    SetThreadHookBit(TLS_BIT_PUMP, 1);

    g_appliedRevision = curRev;
    g_nextWatermarkProbeTick = now + 1000;

    // 1. Watermark UID node
    int uidOk = 1;
    if (g_uidHidden) {
        g_uidTouchedByUs = 1;
        uidOk = SetNodeActiveOnMainThread(
            &g_uidTextHandle,
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode/Text",
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode",
            "Text",
            HT_WATERMARK_UID,
            0, "Watermark UID");
    } else if (configJustChanged || g_uidTouchedByUs || g_uidTextHandle != 0) {
        uidOk = SetNodeActiveOnMainThread(
            &g_uidTextHandle,
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode/Text",
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode",
            "Text",
            HT_WATERMARK_UID,
            1, "Watermark UID");
        if (uidOk) {
            g_uidTouchedByUs = 0;
        }
    }

    // 2. Watermark Ping node (untouched unless user hides/unhides ping)
    int pingOk = 1;
    if (g_pingHidden) {
        g_pingTouchedByUs = 1;
        pingOk = SetNodeActiveOnMainThread(
            &g_uidPingHandle,
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode/PingCon",
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode",
            "PingCon",
            HT_WATERMARK_PING,
            0, "Watermark Ping");
    } else if (g_pingTouchedByUs) {
        pingOk = SetNodeActiveOnMainThread(
            &g_uidPingHandle,
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode/PingCon",
            "UINode/UIRoot/UIDPanelPanel/Main/TextNode",
            "PingCon",
            HT_WATERMARK_PING,
            1, "Watermark Ping");
        if (pingOk) {
            g_pingTouchedByUs = 0;
        }
    }

    g_watermarkSynced = (uidOk && pingOk) ? 1 : 0;

    // 3. Immediately update any open Menu / Card / Watermark TMP_Text components
    if (configJustChanged) {
        RefreshCachedTextComponentsOnMainThread();
    }

    SetThreadHookBit(TLS_BIT_PUMP, 0);
}

// ============================================================================
// Shutdown & Frame Hooks
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

static void hkEventSystemUpdate(void* self, void* methodInfo) {
    if (!g_hookDisabled &&
        (g_configRevision != g_appliedRevision ||
         !g_watermarkSynced ||
         GetTickCount64() >= g_nextWatermarkProbeTick)) {
        PumpMainThreadSync();
    }
    if (g_origEventSystemUpdate) {
        g_origEventSystemUpdate(self, methodInfo);
    }
}

static void hkCanvasWillRender(void* arg0, void* arg1) {
    if (!g_hookDisabled &&
        (g_configRevision != g_appliedRevision ||
         !g_watermarkSynced ||
         GetTickCount64() >= g_nextWatermarkProbeTick)) {
        PumpMainThreadSync();
    }
    if (g_origCanvasWillRender) {
        g_origCanvasWillRender(arg0, arg1);
    }
}

// ============================================================================
// Layer 2 & Layer 3: TMP_Text::set_text & TextMeshProUGUI::GenerateTextMesh
// ============================================================================

static void hkTMPSetText(void* self, void* value, void* methodInfo) {
    if (!g_origSetText) return;
    if (g_hookDisabled || (GetThreadHookMask() & TLS_BIT_SETTEXT)) {
        g_origSetText(self, value, methodInfo);
        return;
    }

    PumpMainThreadSync();

    SetThreadHookBit(TLS_BIT_SETTEXT, 1);
    if (!g_hookDisabled) {
        HideType t = HT_NONE;
        if (IdentifyTargetComponent(self, &t) && t != HT_NONE) {
            RecordOriginalCompText(self, value);
            // Only clobber string if GenerateTextMesh hook is absent; otherwise keep
            // m_text 100% intact so toggling OFF in GUI restores live text immediately!
            if (!g_generateMeshHooked && g_cachedEmptyStr && IsHideActive(t)) {
                value = g_cachedEmptyStr;
            }
        }
    }
    SetThreadHookBit(TLS_BIT_SETTEXT, 0);

    g_origSetText(self, value, methodInfo);
}

static void hkGenerateTextMesh(void* self, void* methodInfo) {
    if (!g_origGenerateTextMesh) return;
    if (g_hookDisabled || (GetThreadHookMask() & TLS_BIT_GENMESH)) {
        g_origGenerateTextMesh(self, methodInfo);
        return;
    }

    PumpMainThreadSync();

    HideType t = HT_NONE;
    int isTarget = (!g_hookDisabled && IdentifyTargetComponent(self, &t) && t != HT_NONE);
    if (!isTarget || g_offsetMaxVisibleCharacters == (size_t)-1) {
        SetThreadHookBit(TLS_BIT_GENMESH, 1);
        g_origGenerateTextMesh(self, methodInfo);
        SetThreadHookBit(TLS_BIT_GENMESH, 0);
        return;
    }

    char* fieldAddr = (char*)self + g_offsetMaxVisibleCharacters;
    if (!IsValidUserPtr(fieldAddr, sizeof(int))) {
        SetThreadHookBit(TLS_BIT_GENMESH, 1);
        g_origGenerateTextMesh(self, methodInfo);
        SetThreadHookBit(TLS_BIT_GENMESH, 0);
        return;
    }

    volatile LONG* field = (volatile LONG*)fieldAddr;
    LONG curVal = *field;
    if (curVal > 0) {
        UpdateCachedOrigMaxVisible(self, (int)curVal);
    }

    if (IsHideActive(t)) {
        InterlockedExchange(field, 0);
        SetThreadHookBit(TLS_BIT_GENMESH, 1);
        g_origGenerateTextMesh(self, methodInfo);
        SetThreadHookBit(TLS_BIT_GENMESH, 0);
    } else {
        if (curVal <= 0) {
            InterlockedExchange(field, (LONG)GetCachedOrigMaxVisible(self));
        }
        SetThreadHookBit(TLS_BIT_GENMESH, 1);
        g_origGenerateTextMesh(self, methodInfo);
        SetThreadHookBit(TLS_BIT_GENMESH, 0);
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

static void* FindMethodInHierarchy(void* klass, const char* name, int argsCount) {
    for (int depth = 0; klass && depth < 16; depth++) {
        void* m = p_il2cpp_class_get_method_from_name(klass, name, argsCount);
        if (m) return m;
        if (!p_il2cpp_class_get_parent) break;
        klass = p_il2cpp_class_get_parent(klass);
    }
    return NULL;
}

static size_t FindFieldOffsetInHierarchy(void* klass, const char* fieldName) {
    if (!p_il2cpp_class_get_field_from_name || !p_il2cpp_field_get_offset) {
        return (size_t)-1;
    }
    for (int depth = 0; klass && depth < 16; depth++) {
        void* f = p_il2cpp_class_get_field_from_name(klass, fieldName);
        if (f) return p_il2cpp_field_get_offset(f);
        if (!p_il2cpp_class_get_parent) break;
        klass = p_il2cpp_class_get_parent(klass);
    }
    return (size_t)-1;
}

static int ResolveIL2CppExports(void) {
    g_hGameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!g_hGameAssembly) return 0;

#define R(name) p_##name = (t_##name)(void*)(uintptr_t)GetProcAddress(g_hGameAssembly, #name)
    R(il2cpp_domain_get);
    R(il2cpp_thread_attach);
    R(il2cpp_thread_detach);
    R(il2cpp_domain_get_assemblies);
    R(il2cpp_assembly_get_image);
    R(il2cpp_class_from_name);
    R(il2cpp_class_get_parent);
    R(il2cpp_class_get_method_from_name);
    R(il2cpp_method_get_flags);
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

    DWORD* found = ScanCodeForMigotoTlsIdx(pCreateDev, 160, imgStart, imgEnd);
    if (found) return found;

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

    uint8_t* migotoTls = (uint8_t*)TlsGetValue(migotoTlsSlot);
    if (!migotoTls && pMigotoCreateDev) {
        UINT invalidFL = 1;
        (void)pMigotoCreateDev(NULL, 0, NULL, 0, &invalidFL, 1, 7, NULL, NULL, NULL);
        migotoTls = (uint8_t*)TlsGetValue(migotoTlsSlot);
    }

    if (!migotoTls || IsBadWritePtr(migotoTls, 1)) {
        Log("[EFF-FIX] [WARN] Could not obtain writable 3DMigoto TLS struct (slot=%lu, ptr=%p)",
            (unsigned long)migotoTlsSlot, (void*)migotoTls);
        return g_origEffD3D11CreateDevice(
            pAdapter, DriverType, Software, Flags, pFeatureLevels,
            FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
    }

    migotoTls[0] = 1;

    HRESULT hr = g_origEffD3D11CreateDevice(
        pAdapter, DriverType, Software, Flags, pFeatureLevels,
        FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

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
    InitializeCriticalSection(&g_saveIniLock);
    g_logLockReady  = 1;
    g_saveLockReady = 1;
    g_hLogFile = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    Log("=== EFF-EFMI-Compatibility-Plugin v1.2.0 (Live GUI + 3DMigoto Panel Fix) ===");

    HideUidConfig cfg;
    int hadIni = LoadHideUidConfig(g_iniPath, &cfg);
    g_uidHidden      = cfg.hide_uid;
    g_pingHidden     = cfg.hide_ping;
    g_menuUidHidden  = cfg.hide_menu_uid;
    g_menuNameHidden = cfg.hide_menu_name;
    g_cardUidHidden  = cfg.hide_card_uid;
    g_cardNameHidden = cfg.hide_card_name;
    g_fixEffPanel    = cfg.fix_eff_panel;

    InitGuiPanelState(&cfg, OnPanelConfigChanged);

    char hkStr[64];
    FormatHotkeyChord(&cfg.toggle_key, hkStr, sizeof(hkStr));
    Log("[Config] %s | uid=%d ping=%d menu_uid=%d menu_name=%d card_uid=%d card_name=%d fix_eff=%d key=%s lang=%s",
        hadIni ? "Loaded hide_uid.ini" : "Generated default hide_uid.ini",
        (int)g_uidHidden, (int)g_pingHidden,
        (int)g_menuUidHidden, (int)g_menuNameHidden,
        (int)g_cardUidHidden, (int)g_cardNameHidden,
        (int)g_fixEffPanel, hkStr, cfg.language ? "en" : "zh");

    // Launch independent pure-Win32/GDI control panel UI thread (zero D3D11/SwapChain)
    HANDLE hGuiThread = CreateThread(NULL, 0, GuiPanelThread, NULL, 0, NULL);
    if (hGuiThread) CloseHandle(hGuiThread);

    TryInstallEffPanelFix();

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

    void* goClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "GameObject");
    if (goClass) {
        g_methodGameObjectSetActive          = FindMethodInHierarchy(goClass, "SetActive", 1);
        g_methodGameObjectGetActiveSelf      = FindMethodInHierarchy(goClass, "get_activeSelf", 0);
        g_methodGameObjectGetActiveInHierarchy = FindMethodInHierarchy(goClass, "get_activeInHierarchy", 0);
        g_methodGameObjectGetTransform       = FindMethodInHierarchy(goClass, "get_transform", 0);
        g_methodGameObjectFind               = FindMethodInHierarchy(goClass, "Find", 1);
    }

    void* compClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Component");
    if (compClass) {
        g_methodComponentGetGameObject = FindMethodInHierarchy(compClass, "get_gameObject", 0);
        g_methodComponentGetTransform  = FindMethodInHierarchy(compClass, "get_transform", 0);
    }

    void* objClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Object");
    if (objClass) {
        g_methodObjectGetName = FindMethodInHierarchy(objClass, "get_name", 0);
        size_t cachedOff = FindFieldOffsetInHierarchy(objClass, "m_CachedPtr");
        if (cachedOff != (size_t)-1 && cachedOff >= sizeof(void*) && cachedOff <= 0x40) {
            g_offsetCachedPtr = cachedOff;
        }
    }

    void* trClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Transform");
    if (trClass) {
        g_methodTransformGetParent = FindMethodInHierarchy(trClass, "get_parent", 0);
        g_methodTransformFind      = FindMethodInHierarchy(trClass, "Find", 1);
    }

    void* tmpClass     = FindClassInAssemblies(asms, asmCount, "TMPro", "TMP_Text");
    void* tmpUguiClass = FindClassInAssemblies(asms, asmCount, "TMPro", "TextMeshProUGUI");
    void* graphicClass = FindClassInAssemblies(asms, asmCount, "UnityEngine.UI", "Graphic");
    void* crClass      = FindClassInAssemblies(asms, asmCount, "UnityEngine", "CanvasRenderer");

    if (tmpClass) {
        g_methodTMPSetText            = FindMethodInHierarchy(tmpClass, "set_text", 1);
        g_methodTMPSetMaxVisibleChars = FindMethodInHierarchy(tmpClass, "set_maxVisibleCharacters", 1);
        g_offsetMaxVisibleCharacters  = FindFieldOffsetInHierarchy(tmpClass, "m_maxVisibleCharacters");
        g_offsetHavePropertiesChanged = FindFieldOffsetInHierarchy(tmpClass, "m_havePropertiesChanged");
    }

    void* meshSearchClass = tmpUguiClass ? tmpUguiClass : tmpClass;
    if (meshSearchClass) {
        g_methodSetAllDirty              = FindMethodInHierarchy(meshSearchClass, "SetAllDirty", 0);
        g_methodForceMeshUpdate2         = FindMethodInHierarchy(meshSearchClass, "ForceMeshUpdate", 2);
        g_methodForceMeshUpdate0         = FindMethodInHierarchy(meshSearchClass, "ForceMeshUpdate", 0);
        g_methodRebuild                  = FindMethodInHierarchy(meshSearchClass, "Rebuild", 1);
        g_methodGraphicGetCanvasRenderer = FindMethodInHierarchy(meshSearchClass, "get_canvasRenderer", 0);
    }
    if (!g_methodSetAllDirty && graphicClass) {
        g_methodSetAllDirty = FindMethodInHierarchy(graphicClass, "SetAllDirty", 0);
    }
    if (!g_methodGraphicGetCanvasRenderer && graphicClass) {
        g_methodGraphicGetCanvasRenderer = FindMethodInHierarchy(graphicClass, "get_canvasRenderer", 0);
    }
    if (crClass) {
        g_methodCanvasRendererSetCull = FindMethodInHierarchy(crClass, "set_cull", 1);
    }

    if (tmpUguiClass) {
        g_methodGenerateTextMesh = FindMethodInHierarchy(tmpUguiClass, "GenerateTextMesh", 0);
    }

    void* canvasClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Canvas");
    if (canvasClass) {
        void* m = p_il2cpp_class_get_method_from_name(canvasClass, "SendWillRenderCanvases", 0);
        if (m) {
            uint32_t iflags = 0;
            if (!p_il2cpp_method_get_flags || (p_il2cpp_method_get_flags(m, &iflags) & 0x0010)) {
                g_methodCanvasWillRender = m;
            }
        }
    }

    void* esClass = FindClassInAssemblies(asms, asmCount, "UnityEngine.EventSystems", "EventSystem");
    if (esClass) {
        void* m = p_il2cpp_class_get_method_from_name(esClass, "Update", 0);
        if (m) {
            uint32_t iflags = 0;
            if (!p_il2cpp_method_get_flags || !(p_il2cpp_method_get_flags(m, &iflags) & 0x0010)) {
                g_methodEventSystemUpdate = m;
            }
        }
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

    Log("[IL2CPP] Resolved helpers: m_CachedPtr=+0x%zx m_maxVis=+0x%zx m_propChanged=+0x%zx "
        "setMaxVis=%p SetAllDirty=%p ForceMesh(2/0)=%p/%p Rebuild=%p CR.set_cull=%p Tr.Find=%p",
        g_offsetCachedPtr, g_offsetMaxVisibleCharacters, g_offsetHavePropertiesChanged,
        g_methodTMPSetMaxVisibleChars, g_methodSetAllDirty,
        g_methodForceMeshUpdate2, g_methodForceMeshUpdate0,
        g_methodRebuild, g_methodCanvasRendererSetCull, g_methodTransformFind);

    g_cachedEmptyStr = p_il2cpp_string_new("");
    if (g_cachedEmptyStr) {
        g_emptyStrHandle = p_il2cpp_gchandle_new(g_cachedEmptyStr, 1);
    }

    if (attachedThread && p_il2cpp_thread_detach) {
        p_il2cpp_thread_detach(attachedThread);
        attachedThread = NULL;
    }

    MH_STATUS mhInit = MH_Initialize();
    if (mhInit != MH_OK && mhInit != MH_ERROR_ALREADY_INITIALIZED) {
        Log("[ERROR] MH_Initialize failed: %d", (int)mhInit);
        return 1;
    }

    void* pShutdown = (void*)(uintptr_t)GetProcAddress(g_hGameAssembly, "il2cpp_shutdown");
    if (pShutdown && MH_CreateHook(pShutdown, (void*)(uintptr_t)hkIl2CppShutdown, (void**)&g_origIl2CppShutdown) == MH_OK) {
        MH_EnableHook(pShutdown);
    }
    if (g_methodInternalApplicationQuit) {
        void* pQuit = ((MethodInfoPrefix*)g_methodInternalApplicationQuit)->methodPointer;
        if (pQuit && MH_CreateHook(pQuit, (void*)(uintptr_t)hkAppQuit, (void**)&g_origAppQuit) == MH_OK) {
            MH_EnableHook(pQuit);
        }
    }

    void* setTextTarget = ((MethodInfoPrefix*)g_methodTMPSetText)->methodPointer;
    if (!setTextTarget ||
        MH_CreateHook(setTextTarget, (void*)(uintptr_t)hkTMPSetText, (void**)&g_origSetText) != MH_OK ||
        MH_EnableHook(setTextTarget) != MH_OK) {
        Log("[ERROR] Failed to hook TMP_Text.set_text (%p)", setTextTarget);
        return 1;
    }
    Log("[Hook] TMP_Text.set_text hooked at %p", setTextTarget);

    void* genTarget = NULL;
    if (g_methodGenerateTextMesh && g_offsetMaxVisibleCharacters != (size_t)-1) {
        genTarget = ((MethodInfoPrefix*)g_methodGenerateTextMesh)->methodPointer;
        if (genTarget && genTarget != setTextTarget) {
            if (MH_CreateHook(genTarget, (void*)(uintptr_t)hkGenerateTextMesh, (void**)&g_origGenerateTextMesh) == MH_OK &&
                MH_EnableHook(genTarget) == MH_OK) {
                g_generateMeshHooked = 1;
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

    void* canvasTarget = NULL;
    if (g_methodCanvasWillRender) {
        canvasTarget = ((MethodInfoPrefix*)g_methodCanvasWillRender)->methodPointer;
        if (canvasTarget && canvasTarget != setTextTarget && canvasTarget != genTarget) {
            if (MH_CreateHook(canvasTarget, (void*)(uintptr_t)hkCanvasWillRender, (void**)&g_origCanvasWillRender) == MH_OK &&
                MH_EnableHook(canvasTarget) == MH_OK) {
                Log("[Hook] Canvas.SendWillRenderCanvases hooked at %p (instant 1-frame GUI sync)",
                    canvasTarget);
            }
        }
    }

    if (g_methodEventSystemUpdate) {
        void* esTarget = ((MethodInfoPrefix*)g_methodEventSystemUpdate)->methodPointer;
        if (esTarget && esTarget != setTextTarget && esTarget != genTarget && esTarget != canvasTarget) {
            if (MH_CreateHook(esTarget, (void*)(uintptr_t)hkEventSystemUpdate, (void**)&g_origEventSystemUpdate) == MH_OK &&
                MH_EnableHook(esTarget) == MH_OK) {
                Log("[Hook] EventSystem.Update hooked at %p (main-thread frame pump)",
                    esTarget);
            }
        }
    }

    Log("[Ready] All active hooks & pure-Win32 GUI panel armed.");
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
