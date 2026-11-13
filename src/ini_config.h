#pragma once
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct HideUidConfig {
    int hide_uid;
    int hide_ping;
    int hide_menu_uid;
    int hide_menu_name;
    int hide_card_uid;
    int hide_card_name;
    int fix_eff_panel;
} HideUidConfig;

static void InitDefaultConfig(HideUidConfig* cfg) {
    // Defaults matching user preset:
    // Watermark: hide UID = ON, hide ping = OFF (keep ping)
    // Menu:      hide UID = ON, hide name = OFF (keep menu name)
    // Card:      hide UID = ON, hide name = ON
    // EFF Fix:   fix Ins control panel crash & protect 3DMigoto res_width/res_height = ON
    cfg->hide_uid       = 1;
    cfg->hide_ping      = 0;
    cfg->hide_menu_uid  = 1;
    cfg->hide_menu_name = 0;
    cfg->hide_card_uid  = 1;
    cfg->hide_card_name = 1;
    cfg->fix_eff_panel  = 1;
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

static void WriteDefaultConfigFile(const wchar_t* path) {
    static const char kDefaultIni[] =
        "; ============================================================================\r\n"
        "; Hide UID Lite for Arknights: Endfield (EFF Companion Plugin)\r\n"
        "; 1 = 隐藏 (勾选), 0 = 保留显示 (不勾选)\r\n"
        "; 修改后重启游戏生效\r\n"
        "; ============================================================================\r\n"
        "\r\n"
        "[uid]\r\n"
        "; 水印 (右下角常驻)\r\n"
        "hide_uid=1\r\n"
        "hide_ping=0\r\n"
        "\r\n"
        "; 菜单 (ESC 菜单栏)\r\n"
        "hide_menu_uid=1\r\n"
        "hide_menu_name=0\r\n"
        "\r\n"
        "; 名片 (个人名片 / 好友名片页)\r\n"
        "hide_card_uid=1\r\n"
        "hide_card_name=1\r\n"
        "\r\n"
        "[eff]\r\n"
        "; 修复 DX11 + EFMI 共存模式下按 Ins 打开 EFF 面板崩溃，并保护 3DMigoto res_width/res_height 不被 604x640 覆盖\r\n"
        "fix_eff_panel=1\r\n";

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hFile, kDefaultIni, (DWORD)(sizeof(kDefaultIni) - 1), &written, NULL);
        CloseHandle(hFile);
    }
}

static int LoadHideUidConfig(const wchar_t* path, HideUidConfig* cfg) {
    InitDefaultConfig(cfg);

    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        WriteDefaultConfigFile(path);
        return 0; // Using defaults (created fresh file)
    }

    FILE* f = _wfopen(path, L"rb");
    if (!f) return 0;

    char line[512];
    int lineNo = 0;
    while (fgets(line, sizeof(line), f)) {
        lineNo++;
        char* p = line;
        // Strip UTF-8 BOM on line 1
        if (lineNo == 1 && (unsigned char)p[0] == 0xEF &&
            (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
            p += 3;
        }
        // Trim leading whitespace
        while (*p == ' ' || *p == '\t') p++;
        // Trim trailing whitespace/newlines
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

        // Trim key trailing whitespace
        size_t klen = strlen(key);
        while (klen > 0 && (key[klen - 1] == ' ' || key[klen - 1] == '\t')) {
            key[--klen] = '\0';
        }
        // Trim val leading whitespace and inline comments
        while (*val == ' ' || *val == '\t') val++;
        char* semi = strchr(val, ';');
        if (semi) *semi = '\0';
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen - 1] == ' ' || val[vlen - 1] == '\t')) {
            val[--vlen] = '\0';
        }

        if (_stricmp(key, "hide_uid") == 0)
            cfg->hide_uid = ParseBoolValue(val, cfg->hide_uid);
        else if (_stricmp(key, "hide_ping") == 0)
            cfg->hide_ping = ParseBoolValue(val, cfg->hide_ping);
        else if (_stricmp(key, "hide_menu_uid") == 0)
            cfg->hide_menu_uid = ParseBoolValue(val, cfg->hide_menu_uid);
        else if (_stricmp(key, "hide_menu_name") == 0)
            cfg->hide_menu_name = ParseBoolValue(val, cfg->hide_menu_name);
        else if (_stricmp(key, "hide_card_uid") == 0)
            cfg->hide_card_uid = ParseBoolValue(val, cfg->hide_card_uid);
        else if (_stricmp(key, "hide_card_name") == 0)
            cfg->hide_card_name = ParseBoolValue(val, cfg->hide_card_name);
        else if (_stricmp(key, "fix_eff_panel") == 0)
            cfg->fix_eff_panel = ParseBoolValue(val, cfg->fix_eff_panel);
    }

    fclose(f);
    return 1;
}
