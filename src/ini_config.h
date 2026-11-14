#pragma once
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct HotkeyChord {
    UINT vk;
    int  ctrl;
    int  alt;
    int  shift;
} HotkeyChord;

typedef struct HideUidConfig {
    int         hide_uid;
    int         hide_ping;
    int         hide_menu_uid;
    int         hide_menu_name;
    int         hide_card_uid;
    int         hide_card_name;
    int         fix_eff_panel;
    HotkeyChord toggle_key;
    int         language; // 0 = zh, 1 = en
} HideUidConfig;

typedef struct KeyNameEntry {
    const char* name;
    UINT        vk;
} KeyNameEntry;

static const KeyNameEntry kKeyTable[] = {
    { "HOME",       VK_HOME },
    { "END",        VK_END },
    { "INSERT",     VK_INSERT },
    { "INS",        VK_INSERT },
    { "DELETE",     VK_DELETE },
    { "DEL",        VK_DELETE },
    { "PGUP",       VK_PRIOR },
    { "PAGEUP",     VK_PRIOR },
    { "PRIOR",      VK_PRIOR },
    { "PGDN",       VK_NEXT },
    { "PAGEDOWN",   VK_NEXT },
    { "NEXT",       VK_NEXT },
    { "PAUSE",      VK_PAUSE },
    { "SCROLL",     VK_SCROLL },
    { "TAB",        VK_TAB },
    { "SPACE",      VK_SPACE },
    { "BACKSPACE",  VK_BACK },
    { "BACK",       VK_BACK },
    { "UP",         VK_UP },
    { "DOWN",       VK_DOWN },
    { "LEFT",       VK_LEFT },
    { "RIGHT",      VK_RIGHT },
    { "NUM0",       VK_NUMPAD0 },
    { "NUM1",       VK_NUMPAD1 },
    { "NUM2",       VK_NUMPAD2 },
    { "NUM3",       VK_NUMPAD3 },
    { "NUM4",       VK_NUMPAD4 },
    { "NUM5",       VK_NUMPAD5 },
    { "NUM6",       VK_NUMPAD6 },
    { "NUM7",       VK_NUMPAD7 },
    { "NUM8",       VK_NUMPAD8 },
    { "NUM9",       VK_NUMPAD9 },
    { "MULTIPLY",   VK_MULTIPLY },
    { "ADD",        VK_ADD },
    { "SUBTRACT",   VK_SUBTRACT },
    { "DECIMAL",    VK_DECIMAL },
    { "DIVIDE",     VK_DIVIDE },
    { "TILDE",      VK_OEM_3 },
    { "`",          VK_OEM_3 },
    { "MINUS",      VK_OEM_MINUS },
    { "-",          VK_OEM_MINUS },
    { "PLUS",       VK_OEM_PLUS },
    { "=",          VK_OEM_PLUS },
    { "[",          VK_OEM_4 },
    { "]",          VK_OEM_6 },
    { "\\",         VK_OEM_5 },
    { ";",          VK_OEM_1 },
    { "'",          VK_OEM_7 },
    { ",",          VK_OEM_COMMA },
    { ".",          VK_OEM_PERIOD },
    { "/",          VK_OEM_2 }
};

static UINT ParseSingleKeyToken(const char* tok) {
    if (!tok || !*tok) return 0;

    // F1 .. F24
    if ((tok[0] == 'F' || tok[0] == 'f') && isdigit((unsigned char)tok[1])) {
        int fn = atoi(tok + 1);
        if (fn >= 1 && fn <= 24) {
            return (UINT)(VK_F1 + (fn - 1));
        }
    }

    // Single ASCII letter or digit
    if (tok[1] == '\0') {
        unsigned char c = (unsigned char)tok[0];
        if (c >= 'a' && c <= 'z') return (UINT)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') return (UINT)c;
        if (c >= '0' && c <= '9') return (UINT)c;
    }

    // Hex fallback VK_0xNN or 0xNN
    if (_strnicmp(tok, "VK_0X", 5) == 0) {
        unsigned int v = 0;
        if (sscanf(tok + 5, "%x", &v) == 1 && v > 0 && v < 256) return (UINT)v;
    } else if (_strnicmp(tok, "0X", 2) == 0) {
        unsigned int v = 0;
        if (sscanf(tok + 2, "%x", &v) == 1 && v > 0 && v < 256) return (UINT)v;
    }

    for (size_t i = 0; i < sizeof(kKeyTable) / sizeof(kKeyTable[0]); i++) {
        if (_stricmp(tok, kKeyTable[i].name) == 0) {
            return kKeyTable[i].vk;
        }
    }
    return 0;
}

static int ParseHotkeyChord(const char* raw, HotkeyChord* out) {
    if (!out) return 0;
    if (!raw) return 0;

    char buf[128];
    strncpy(buf, raw, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* s = buf;
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    if (*s == '\0') return 0;

    if (_stricmp(s, "NONE") == 0 || _stricmp(s, "OFF") == 0 ||
        _stricmp(s, "DISABLED") == 0) {
        out->vk = 0;
        out->ctrl = 0;
        out->alt = 0;
        out->shift = 0;
        return 1;
    }

    HotkeyChord parsed = { 0, 0, 0, 0 };
    char* ctx = NULL;
    char* tok = strtok_s(s, "+", &ctx);
    while (tok) {
        while (*tok == ' ' || *tok == '\t') tok++;
        size_t tlen = strlen(tok);
        while (tlen > 0 && (tok[tlen - 1] == ' ' || tok[tlen - 1] == '\t')) {
            tok[--tlen] = '\0';
        }
        if (_stricmp(tok, "CTRL") == 0 || _stricmp(tok, "CONTROL") == 0) {
            parsed.ctrl = 1;
        } else if (_stricmp(tok, "ALT") == 0) {
            parsed.alt = 1;
        } else if (_stricmp(tok, "SHIFT") == 0) {
            parsed.shift = 1;
        } else if (*tok != '\0') {
            UINT vk = ParseSingleKeyToken(tok);
            if (vk == 0) return 0;
            parsed.vk = vk;
        }
        tok = strtok_s(NULL, "+", &ctx);
    }

    if (parsed.vk == 0) return 0;
    *out = parsed;
    return 1;
}

static void FormatSingleKeyName(UINT vk, char* out, size_t outSz) {
    if (!out || outSz == 0) return;
    if (vk == 0) {
        snprintf(out, outSz, "NONE");
        return;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        snprintf(out, outSz, "F%u", (unsigned)(vk - VK_F1 + 1));
        return;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        out[0] = (char)vk;
        out[1] = '\0';
        return;
    }
    for (size_t i = 0; i < sizeof(kKeyTable) / sizeof(kKeyTable[0]); i++) {
        if (kKeyTable[i].vk == vk) {
            snprintf(out, outSz, "%s", kKeyTable[i].name);
            return;
        }
    }
    snprintf(out, outSz, "VK_0x%02X", (unsigned)(vk & 0xFF));
}

static void FormatHotkeyChord(const HotkeyChord* chord, char* buf, size_t bufSz) {
    if (!buf || bufSz == 0) return;
    if (!chord || chord->vk == 0) {
        snprintf(buf, bufSz, "NONE");
        return;
    }
    char keyName[32];
    FormatSingleKeyName(chord->vk, keyName, sizeof(keyName));
    snprintf(buf, bufSz, "%s%s%s%s",
             chord->ctrl  ? "Ctrl+"  : "",
             chord->alt   ? "Alt+"   : "",
             chord->shift ? "Shift+" : "",
             keyName);
}

static void InitDefaultConfig(HideUidConfig* cfg) {
    cfg->hide_uid         = 1;
    cfg->hide_ping        = 0;
    cfg->hide_menu_uid    = 1;
    cfg->hide_menu_name   = 0;
    cfg->hide_card_uid    = 1;
    cfg->hide_card_name   = 1;
    cfg->fix_eff_panel    = 1;
    cfg->toggle_key.vk    = VK_HOME;
    cfg->toggle_key.ctrl  = 0;
    cfg->toggle_key.alt   = 0;
    cfg->toggle_key.shift = 0;
    cfg->language         = 0; // zh
}

static int ParseBoolValue(const char* val, int fallback) {
    if (!val || !*val) return fallback;
    if (strcmp(val, "1") == 0 || _stricmp(val, "true") == 0 ||
        _stricmp(val, "yes") == 0 || _stricmp(val, "on") == 0) {
        return 1;
    }
    if (strcmp(val, "0") == 0 || _stricmp(val, "false") == 0 ||
        _stricmp(val, "no") == 0 || _stricmp(val, "off") == 0) {
        return 0;
    }
    return fallback;
}

static int SaveHideUidConfig(const wchar_t* path, const HideUidConfig* cfg) {
    if (!path || !cfg) return 0;

    char keyText[64];
    FormatHotkeyChord(&cfg->toggle_key, keyText, sizeof(keyText));

    char buf[2048];
    int n = snprintf(
        buf, sizeof(buf),
        "; ============================================================================\r\n"
        "; EFF-EFMI-Compatibility-Plugin (Hide UID + GUI Control Panel)\r\n"
        "; 1 = 开启隐藏, 0 = 保留显示 | 游戏内按 %s 可随时呼出图形开关面板实时调整\r\n"
        "; ============================================================================\r\n"
        "\r\n"
        "[gui]\r\n"
        "; 游戏内呼出/收起控制面板热键 (如 HOME, END, F9, Ctrl+HOME 等；填 NONE 禁用面板热键)\r\n"
        "toggle_key=%s\r\n"
        "; 面板界面语言: zh (中文) / en (English)\r\n"
        "language=%s\r\n"
        "\r\n"
        "[uid]\r\n"
        "; 水印 (右下角常驻)\r\n"
        "hide_uid=%d\r\n"
        "hide_ping=%d\r\n"
        "\r\n"
        "; 菜单 (ESC 菜单栏)\r\n"
        "hide_menu_uid=%d\r\n"
        "hide_menu_name=%d\r\n"
        "\r\n"
        "; 名片 (个人名片 / 好友名片页)\r\n"
        "hide_card_uid=%d\r\n"
        "hide_card_name=%d\r\n"
        "\r\n"
        "[eff]\r\n"
        "; 修复 DX11 + EFMI 共存模式下按 Ins 打开 EFF 面板崩溃，并保护 3DMigoto res_width/res_height\r\n"
        "fix_eff_panel=%d\r\n",
        keyText,
        keyText,
        cfg->language == 1 ? "en" : "zh",
        cfg->hide_uid ? 1 : 0,
        cfg->hide_ping ? 1 : 0,
        cfg->hide_menu_uid ? 1 : 0,
        cfg->hide_menu_name ? 1 : 0,
        cfg->hide_card_uid ? 1 : 0,
        cfg->hide_card_name ? 1 : 0,
        cfg->fix_eff_panel ? 1 : 0);

    if (n <= 0 || (size_t)n >= sizeof(buf)) return 0;

    wchar_t tmpPath[MAX_PATH];
    _snwprintf(tmpPath, MAX_PATH - 1, L"%s.tmp", path);
    tmpPath[MAX_PATH - 1] = L'\0';

    HANDLE hFile = CreateFileW(tmpPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        hFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return 0;
        DWORD written = 0;
        BOOL ok = WriteFile(hFile, buf, (DWORD)n, &written, NULL);
        CloseHandle(hFile);
        return ok && written == (DWORD)n;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, buf, (DWORD)n, &written, NULL);
    CloseHandle(hFile);
    if (!ok || written != (DWORD)n) {
        DeleteFileW(tmpPath);
        return 0;
    }
    if (!MoveFileExW(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath);
        return 0;
    }
    return 1;
}

static int LoadHideUidConfig(const wchar_t* path, HideUidConfig* cfg) {
    InitDefaultConfig(cfg);

    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        SaveHideUidConfig(path, cfg);
        return 0;
    }

    FILE* f = _wfopen(path, L"rb");
    if (!f) return 0;

    int sawToggleKey = 0;
    char line[512];
    int lineNo = 0;
    while (fgets(line, sizeof(line), f)) {
        lineNo++;
        char* p = line;
        if (lineNo == 1 && (unsigned char)p[0] == 0xEF &&
            (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
            p += 3;
        }
        while (*p == ' ' || *p == '\t') p++;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n' ||
                           p[len - 1] == ' ' || p[len - 1] == '\t')) {
            p[--len] = '\0';
        }
        if (*p == '\0' || *p == ';' || *p == '#' || *p == '[') continue;

        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = p;
        char* val = eq + 1;

        size_t klen = strlen(key);
        while (klen > 0 && (key[klen - 1] == ' ' || key[klen - 1] == '\t')) {
            key[--klen] = '\0';
        }
        while (*val == ' ' || *val == '\t') val++;

        if (_stricmp(key, "hide_uid") == 0) {
            cfg->hide_uid = ParseBoolValue(val, cfg->hide_uid);
        } else if (_stricmp(key, "hide_ping") == 0) {
            cfg->hide_ping = ParseBoolValue(val, cfg->hide_ping);
        } else if (_stricmp(key, "hide_menu_uid") == 0) {
            cfg->hide_menu_uid = ParseBoolValue(val, cfg->hide_menu_uid);
        } else if (_stricmp(key, "hide_menu_name") == 0) {
            cfg->hide_menu_name = ParseBoolValue(val, cfg->hide_menu_name);
        } else if (_stricmp(key, "hide_card_uid") == 0) {
            cfg->hide_card_uid = ParseBoolValue(val, cfg->hide_card_uid);
        } else if (_stricmp(key, "hide_card_name") == 0) {
            cfg->hide_card_name = ParseBoolValue(val, cfg->hide_card_name);
        } else if (_stricmp(key, "fix_eff_panel") == 0) {
            cfg->fix_eff_panel = ParseBoolValue(val, cfg->fix_eff_panel);
        } else if (_stricmp(key, "toggle_key") == 0 || _stricmp(key, "gui_key") == 0) {
            ParseHotkeyChord(val, &cfg->toggle_key);
            sawToggleKey = 1;
        } else if (_stricmp(key, "language") == 0 || _stricmp(key, "lang") == 0) {
            cfg->language = (_stricmp(val, "en") == 0 || _stricmp(val, "english") == 0 ||
                             strcmp(val, "1") == 0) ? 1 : 0;
        }
    }

    fclose(f);

    // Seamless upgrade: if upgrading from v1.1.0 INI without [gui], persist new keys cleanly
    if (!sawToggleKey) {
        SaveHideUidConfig(path, cfg);
    }
    return 1;
}
