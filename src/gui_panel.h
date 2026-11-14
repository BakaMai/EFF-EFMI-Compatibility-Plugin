#pragma once
// ============================================================================
// Pure Win32 + DWM + 32-bit DIB Analytical SDF Control Panel (gui_panel.h)
// ============================================================================
// Zero DirectX / Zero DXGI / Zero SwapChain / Zero ImGui:
//   - Physically impossible to touch 3DMigoto's D3D11 hooks or res_width/height
//   - Independent top-level tool window; coexists side-by-side with EFF's Ins
//     panel (EFF_ResourceControls) without hooking UnityWndClass WndProc
//   - Subpixel anti-aliased rounded cards & animated toggle capsules rendered
//     on CPU into a double-buffered DIBSection + ClearType DrawTextW
// ============================================================================

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ini_config.h"

#define PANEL_BASE_W       430
#define PANEL_BASE_H       556
#define PANEL_TIMER_ANIM   101
#define PANEL_ROW_COUNT    6

// Hit-test IDs
#define HIT_NONE          -1
#define HIT_ROW_0          0
#define HIT_ROW_1          1
#define HIT_ROW_2          2
#define HIT_ROW_3          3
#define HIT_ROW_4          4
#define HIT_ROW_5          5
#define HIT_HOTKEY_BTN     10
#define HIT_LANG_BTN       11
#define HIT_CLOSE_BTN      12

typedef void (*tOnPanelConfigChanged)(void);

typedef UINT (WINAPI *tGetDpiForWindow)(HWND hwnd);

static HWND                  g_panelHwnd           = NULL;
static volatile LONG         g_panelVisible        = 0;
static int                   g_panelHasCustomPos   = 0;
static int                   g_hoverId             = HIT_NONE;
static int                   g_recordingHotkey     = 0;
static int                   g_waitKeyReleaseAfterBind = 0;
static float                 g_knobAnim[PANEL_ROW_COUNT] = { 1.f, 0.f, 1.f, 0.f, 1.f, 1.f };
static int                   g_animTimerActive     = 0;

static SRWLOCK               g_guiCfgLock          = SRWLOCK_INIT;
static HotkeyChord           g_activeHotkey        = { VK_HOME, 0, 0, 0 };
static int                   g_uiLang              = 0; // 0 = zh, 1 = en
static tOnPanelConfigChanged g_onConfigChanged     = NULL;
static tGetDpiForWindow      g_pGetDpiForWindow    = NULL;

// Shared atomic flags defined in hide_uid.c
extern volatile LONG g_uidHidden;
extern volatile LONG g_pingHidden;
extern volatile LONG g_menuUidHidden;
extern volatile LONG g_menuNameHidden;
extern volatile LONG g_cardUidHidden;
extern volatile LONG g_cardNameHidden;
extern volatile LONG g_fixEffPanel;
extern volatile LONG g_hookDisabled;
extern HWND          g_gameHwnd;

static UINT GetPanelDpi(HWND hwnd) {
    if (g_pGetDpiForWindow && hwnd) {
        UINT dpi = g_pGetDpiForWindow(hwnd);
        if (dpi >= 72 && dpi <= 480) return dpi;
    }
    HDC screen = GetDC(NULL);
    UINT dpi = 96;
    if (screen) {
        int logY = GetDeviceCaps(screen, LOGPIXELSY);
        if (logY >= 72 && logY <= 480) dpi = (UINT)logY;
        ReleaseDC(NULL, screen);
    }
    return dpi;
}

static int ScalePx(int px, UINT dpi) {
    return MulDiv(px, (int)dpi, 96);
}

static int GetRowFlagValue(int idx) {
    switch (idx) {
        case 0: return g_uidHidden != 0;
        case 1: return g_pingHidden != 0;
        case 2: return g_menuUidHidden != 0;
        case 3: return g_menuNameHidden != 0;
        case 4: return g_cardUidHidden != 0;
        case 5: return g_cardNameHidden != 0;
        default: return 0;
    }
}

static void SetRowFlagValue(int idx, int val) {
    LONG v = val ? 1 : 0;
    switch (idx) {
        case 0: InterlockedExchange(&g_uidHidden, v); break;
        case 1: InterlockedExchange(&g_pingHidden, v); break;
        case 2: InterlockedExchange(&g_menuUidHidden, v); break;
        case 3: InterlockedExchange(&g_menuNameHidden, v); break;
        case 4: InterlockedExchange(&g_cardUidHidden, v); break;
        case 5: InterlockedExchange(&g_cardNameHidden, v); break;
        default: break;
    }
}

// ============================================================================
// Analytical SDF Software Rasterizer Helpers (0x00RRGGBB BGRA little-endian)
// ============================================================================

static float Clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static uint32_t PackRgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t LerpColor(uint32_t c0, uint32_t c1, float t) {
    t = Clamp01(t);
    float r0 = (float)((c0 >> 16) & 0xFF), g0 = (float)((c0 >> 8) & 0xFF), b0 = (float)(c0 & 0xFF);
    float r1 = (float)((c1 >> 16) & 0xFF), g1 = (float)((c1 >> 8) & 0xFF), b1 = (float)(c1 & 0xFF);
    uint8_t r = (uint8_t)(r0 + (r1 - r0) * t + 0.5f);
    uint8_t g = (uint8_t)(g0 + (g1 - g0) * t + 0.5f);
    uint8_t b = (uint8_t)(b0 + (b1 - b0) * t + 0.5f);
    return PackRgb(r, g, b);
}

static void BlendPixel(uint32_t* dst, uint32_t src, float alpha) {
    if (alpha <= 0.002f) return;
    if (alpha >= 0.998f) {
        *dst = src;
        return;
    }
    uint32_t d = *dst;
    float dr = (float)((d >> 16) & 0xFF), dg = (float)((d >> 8) & 0xFF), db = (float)(d & 0xFF);
    float sr = (float)((src >> 16) & 0xFF), sg = (float)((src >> 8) & 0xFF), sb = (float)(src & 0xFF);
    uint8_t r = (uint8_t)(dr + (sr - dr) * alpha + 0.5f);
    uint8_t g = (uint8_t)(dg + (sg - dg) * alpha + 0.5f);
    uint8_t b = (uint8_t)(db + (sb - db) * alpha + 0.5f);
    *dst = PackRgb(r, g, b);
}

static float SdfRoundedBox(float px, float py, float cx, float cy, float hw, float hh, float rad) {
    float qx = fabsf(px - cx) - (hw - rad);
    float qy = fabsf(py - cy) - (hh - rad);
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    float outside = sqrtf(ox * ox + oy * oy);
    float inside = (qx > qy ? qx : qy);
    if (inside > 0.0f) inside = 0.0f;
    return outside + inside - rad;
}

static void DrawSdfRoundedRect(uint32_t* pixels, int bufW, int bufH,
                               RECT rc, float radius,
                               uint32_t fillCol, float fillAlpha,
                               uint32_t borderCol, float borderAlpha, float borderWidth) {
    int x0 = rc.left - 2;   if (x0 < 0) x0 = 0;
    int y0 = rc.top - 2;    if (y0 < 0) y0 = 0;
    int x1 = rc.right + 2;  if (x1 > bufW) x1 = bufW;
    int y1 = rc.bottom + 2; if (y1 > bufH) y1 = bufH;

    float cx = 0.5f * (float)(rc.left + rc.right);
    float cy = 0.5f * (float)(rc.top + rc.bottom);
    float hw = 0.5f * (float)(rc.right - rc.left);
    float hh = 0.5f * (float)(rc.bottom - rc.top);
    if (radius > hw) radius = hw;
    if (radius > hh) radius = hh;

    for (int y = y0; y < y1; y++) {
        uint32_t* row = pixels + (size_t)y * (size_t)bufW;
        float py = (float)y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float d = SdfRoundedBox((float)x + 0.5f, py, cx, cy, hw, hh, radius);
            float fMask = Clamp01(0.5f - d);
            if (fMask > 0.0f && fillAlpha > 0.0f) {
                BlendPixel(&row[x], fillCol, fMask * fillAlpha);
            }
            if (borderWidth > 0.0f && borderAlpha > 0.0f) {
                float bMask = Clamp01(0.5f - fabsf(d + 0.5f * borderWidth) + 0.5f * borderWidth);
                if (bMask > 0.0f) {
                    BlendPixel(&row[x], borderCol, bMask * borderAlpha);
                }
            }
        }
    }
}

static void DrawHLineAlpha(uint32_t* pixels, int bufW, int bufH,
                           int x0, int x1, int y, uint32_t col, float alpha) {
    if (y < 0 || y >= bufH) return;
    if (x0 < 0) x0 = 0;
    if (x1 > bufW) x1 = bufW;
    uint32_t* row = pixels + (size_t)y * (size_t)bufW;
    for (int x = x0; x < x1; x++) {
        BlendPixel(&row[x], col, alpha);
    }
}

static void DrawTogglePill(uint32_t* pixels, int bufW, int bufH,
                           RECT rc, float tAnim, int hovered, UINT dpi) {
    float cx = 0.5f * (float)(rc.left + rc.right);
    float cy = 0.5f * (float)(rc.top + rc.bottom);
    float hw = 0.5f * (float)(rc.right - rc.left);
    float hh = 0.5f * (float)(rc.bottom - rc.top);

    uint32_t offBg     = hovered ? PackRgb(52, 63, 82)  : PackRgb(40, 49, 65);
    uint32_t onBg      = hovered ? PackRgb(58, 178, 250): PackRgb(42, 156, 232);
    uint32_t offBorder = PackRgb(72, 86, 110);
    uint32_t onBorder  = PackRgb(118, 212, 255);

    uint32_t pillFill   = LerpColor(offBg, onBg, tAnim);
    uint32_t pillBorder = LerpColor(offBorder, onBorder, tAnim);
    float bw = (float)ScalePx(1, dpi);
    if (bw < 1.0f) bw = 1.0f;

    DrawSdfRoundedRect(pixels, bufW, bufH, rc, hh, pillFill, 1.0f, pillBorder, 0.9f, bw);

    // Knob circle
    float pad = (float)ScalePx(3, dpi);
    float knobR = hh - pad;
    if (knobR < 4.0f) knobR = 4.0f;
    float leftX  = (cx - hw) + hh;
    float rightX = (cx + hw) - hh;
    float knobX  = leftX + (rightX - leftX) * Clamp01(tAnim);
    uint32_t knobCol = LerpColor(PackRgb(176, 188, 206), PackRgb(250, 253, 255), tAnim);

    int kx0 = (int)(knobX - knobR - 4.0f); if (kx0 < 0) kx0 = 0;
    int ky0 = (int)(cy - knobR - 4.0f);    if (ky0 < 0) ky0 = 0;
    int kx1 = (int)(knobX + knobR + 5.0f); if (kx1 > bufW) kx1 = bufW;
    int ky1 = (int)(cy + knobR + 5.0f);    if (ky1 > bufH) ky1 = bufH;

    for (int y = ky0; y < ky1; y++) {
        uint32_t* row = pixels + (size_t)y * (size_t)bufW;
        float dy = ((float)y + 0.5f) - cy;
        for (int x = kx0; x < kx1; x++) {
            float dx = ((float)x + 0.5f) - knobX;
            float dist = sqrtf(dx * dx + dy * dy) - knobR;
            // Subtle drop shadow / glow
            if (dist > -0.5f && dist < 2.5f) {
                float glow = (1.0f - (dist + 0.5f) / 3.0f);
                if (glow > 0.0f) {
                    uint32_t gCol = tAnim > 0.5f ? PackRgb(180, 235, 255) : PackRgb(10, 14, 20);
                    BlendPixel(&row[x], gCol, glow * (tAnim > 0.5f ? 0.28f : 0.22f));
                }
            }
            float mask = Clamp01(0.5f - dist);
            if (mask > 0.0f) {
                BlendPixel(&row[x], knobCol, mask);
            }
        }
    }
}

// ============================================================================
// Layout & Hit-Testing
// ============================================================================

typedef struct PanelLayout {
    UINT dpi;
    int  w;
    int  h;
    RECT header;
    RECT btnLang;
    RECT btnClose;
    RECT secLabel[3];
    RECT card[3];
    RECT row[PANEL_ROW_COUNT];
    RECT pill[PANEL_ROW_COUNT];
    RECT hotkeyCard;
    RECT btnHotkey;
    RECT footer;
} PanelLayout;

static void ComputePanelLayout(HWND hwnd, PanelLayout* L) {
    UINT dpi = GetPanelDpi(hwnd);
    L->dpi = dpi;
    L->w   = ScalePx(PANEL_BASE_W, dpi);
    L->h   = ScalePx(PANEL_BASE_H, dpi);

    RECT client;
    if (hwnd && GetClientRect(hwnd, &client) && client.right > 100 && client.bottom > 100) {
        L->w = client.right;
        L->h = client.bottom;
    }

    L->header.left   = 0;
    L->header.top    = 0;
    L->header.right  = L->w;
    L->header.bottom = ScalePx(62, dpi);

    L->btnClose.right  = L->w - ScalePx(16, dpi);
    L->btnClose.left   = L->btnClose.right - ScalePx(28, dpi);
    L->btnClose.top    = ScalePx(17, dpi);
    L->btnClose.bottom = ScalePx(45, dpi);

    L->btnLang.right  = L->btnClose.left - ScalePx(8, dpi);
    L->btnLang.left   = L->btnLang.right - ScalePx(52, dpi);
    L->btnLang.top    = ScalePx(17, dpi);
    L->btnLang.bottom = ScalePx(45, dpi);

    const int cardLeft  = ScalePx(16, dpi);
    const int cardRight = L->w - ScalePx(16, dpi);
    const int rowH      = ScalePx(50, dpi);
    const int pillW     = ScalePx(44, dpi);
    const int pillH     = ScalePx(24, dpi);

    const int secTopY[3]  = { 72, 204, 336 };
    const int cardTopY[3] = { 94, 226, 358 };

    for (int s = 0; s < 3; s++) {
        L->secLabel[s].left   = ScalePx(22, dpi);
        L->secLabel[s].top    = ScalePx(secTopY[s], dpi);
        L->secLabel[s].right  = cardRight;
        L->secLabel[s].bottom = ScalePx(secTopY[s] + 18, dpi);

        L->card[s].left   = cardLeft;
        L->card[s].top    = ScalePx(cardTopY[s], dpi);
        L->card[s].right  = cardRight;
        L->card[s].bottom = L->card[s].top + rowH * 2;

        for (int r = 0; r < 2; r++) {
            int idx = s * 2 + r;
            L->row[idx].left   = cardLeft;
            L->row[idx].right  = cardRight;
            L->row[idx].top    = L->card[s].top + r * rowH;
            L->row[idx].bottom = L->row[idx].top + rowH;

            L->pill[idx].right  = cardRight - ScalePx(16, dpi);
            L->pill[idx].left   = L->pill[idx].right - pillW;
            L->pill[idx].top    = L->row[idx].top + (rowH - pillH) / 2;
            L->pill[idx].bottom = L->pill[idx].top + pillH;
        }
    }

    L->hotkeyCard.left   = cardLeft;
    L->hotkeyCard.right  = cardRight;
    L->hotkeyCard.top    = ScalePx(472, dpi);
    L->hotkeyCard.bottom = ScalePx(518, dpi);

    L->btnHotkey.right  = cardRight - ScalePx(14, dpi);
    L->btnHotkey.left   = L->btnHotkey.right - ScalePx(150, dpi);
    L->btnHotkey.top    = ScalePx(481, dpi);
    L->btnHotkey.bottom = ScalePx(509, dpi);

    L->footer.left   = cardLeft;
    L->footer.right  = cardRight;
    L->footer.top    = ScalePx(525, dpi);
    L->footer.bottom = ScalePx(549, dpi);
}

static int HitTestControls(const PanelLayout* L, POINT pt) {
    if (PtInRect(&L->btnClose, pt))  return HIT_CLOSE_BTN;
    if (PtInRect(&L->btnLang, pt))   return HIT_LANG_BTN;
    if (PtInRect(&L->btnHotkey, pt)) return HIT_HOTKEY_BTN;
    for (int i = 0; i < PANEL_ROW_COUNT; i++) {
        if (PtInRect(&L->row[i], pt)) return HIT_ROW_0 + i;
    }
    return HIT_NONE;
}

// ============================================================================
// Localized UI Copy (UTF-16 escapes for compiler-locale independence)
// ============================================================================

typedef struct RowCopy {
    const wchar_t* titleZh;
    const wchar_t* descZh;
    const wchar_t* titleEn;
    const wchar_t* descEn;
} RowCopy;

static const RowCopy kRowCopy[PANEL_ROW_COUNT] = {
    {
        L"\u9690\u85CF\u53F3\u4E0B\u89D2 UID",
        L"\u753B\u9762\u53F3\u4E0B\u89D2\u5E38\u9A7B\u8D26\u53F7\u6C34\u5370",
        L"Hide Bottom-Right UID",
        L"Persistent account UID watermark on screen"
    },
    {
        L"\u9690\u85CF\u53F3\u4E0B\u89D2\u7F51\u7EDC\u5EF6\u8FDF (Ping)",
        L"\u9ED8\u8BA4\u5173\u95ED\uFF0C\u4FDD\u7559\u53F3\u4E0B\u89D2 ms \u5EF6\u8FDF\u663E\u793A",
        L"Hide Network Ping (ms)",
        L"Keep off to preserve latency indicator"
    },
    {
        L"\u9690\u85CF ESC \u83DC\u5355 UID",
        L"ESC \u6682\u505C\u83DC\u5355\u7BA1\u7406\u5458\u7F16\u53F7 (ManagerNumber)",
        L"Hide ESC Menu UID",
        L"Manager ID number inside pause menu"
    },
    {
        L"\u9690\u85CF ESC \u83DC\u5355\u6635\u79F0",
        L"ESC \u83DC\u5355\u754C\u9762\u7BA1\u7406\u5458\u6635\u79F0",
        L"Hide ESC Menu Name",
        L"Manager nickname inside pause menu"
    },
    {
        L"\u9690\u85CF\u540D\u7247\u9875 UID",
        L"\u4E2A\u4EBA\u540D\u7247\u4E0E\u597D\u53CB\u8BE6\u60C5\u9875 UID",
        L"Hide Business Card UID",
        L"UID on personal & friend profile cards"
    },
    {
        L"\u9690\u85CF\u540D\u7247\u9875\u6635\u79F0",
        L"\u4E2A\u4EBA\u540D\u7247\u4E0E\u597D\u53CB\u8BE6\u60C5\u9875\u6635\u79F0",
        L"Hide Business Card Name",
        L"Nickname on personal & friend profile cards"
    }
};

static const wchar_t* const kSecTitlesZh[3] = {
    L"\u753B\u9762\u5E38\u9A7B\u6C34\u5370",
    L"ESC \u6682\u505C\u83DC\u5355",
    L"\u4E2A\u4EBA\u4E0E\u597D\u53CB\u540D\u7247"
};

static const wchar_t* const kSecTitlesEn[3] = {
    L"IN-GAME WATERMARK",
    L"ESC PAUSE MENU",
    L"PROFILE & FRIEND CARD"
};

// ============================================================================
// Rendering
// ============================================================================

static HFONT CreateUiFont(int pxHeight, int weight) {
    return CreateFontW(
        -pxHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI");
}

static void RenderPanel(HWND hwnd, HDC outDc) {
    PanelLayout L;
    ComputePanelLayout(hwnd, &L);
    if (L.w <= 0 || L.h <= 0) return;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = L.w;
    bmi.bmiHeader.biHeight      = -L.h; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* rawBits = NULL;
    HDC memDc = CreateCompatibleDC(outDc);
    if (!memDc) return;

    HBITMAP dib = CreateDIBSection(outDc, &bmi, DIB_RGB_COLORS, &rawBits, NULL, 0);
    if (!dib || !rawBits) {
        if (dib) DeleteObject(dib);
        DeleteDC(memDc);
        return;
    }

    HGDIOBJ oldBmp = SelectObject(memDc, dib);
    uint32_t* pixels = (uint32_t*)rawBits;

    // 1. Background vertical gradient + subtle outer frame
    for (int y = 0; y < L.h; y++) {
        float ty = (float)y / (float)(L.h > 1 ? L.h - 1 : 1);
        uint32_t bg = LerpColor(PackRgb(16, 20, 28), PackRgb(21, 27, 38), ty);
        uint32_t* row = pixels + (size_t)y * (size_t)L.w;
        for (int x = 0; x < L.w; x++) row[x] = bg;
    }

    // Endfield top accent bar (cyan -> gold-white subtle highlight)
    int accentH = ScalePx(3, L.dpi);
    if (accentH < 2) accentH = 2;
    for (int y = 0; y < accentH && y < L.h; y++) {
        uint32_t* row = pixels + (size_t)y * (size_t)L.w;
        for (int x = 0; x < L.w; x++) {
            float tx = (float)x / (float)(L.w > 1 ? L.w - 1 : 1);
            row[x] = LerpColor(PackRgb(46, 172, 248), PackRgb(242, 210, 92), tx * 0.65f);
        }
    }

    // Header separator
    DrawHLineAlpha(pixels, L.w, L.h, 0, L.w, L.header.bottom, PackRgb(45, 56, 76), 0.85f);

    // Language button & Close button
    float radBtn = (float)ScalePx(6, L.dpi);
    DrawSdfRoundedRect(pixels, L.w, L.h, L.btnLang, radBtn,
                       g_hoverId == HIT_LANG_BTN ? PackRgb(42, 54, 74) : PackRgb(28, 36, 50), 1.0f,
                       PackRgb(64, 80, 106), 0.9f, 1.0f);

    DrawSdfRoundedRect(pixels, L.w, L.h, L.btnClose, radBtn,
                       g_hoverId == HIT_CLOSE_BTN ? PackRgb(196, 58, 66) : PackRgb(28, 36, 50), 1.0f,
                       g_hoverId == HIT_CLOSE_BTN ? PackRgb(235, 100, 108) : PackRgb(64, 80, 106), 0.9f, 1.0f);

    // 2. Three section cards + hover highlights + toggle pills
    float cardRad = (float)ScalePx(10, L.dpi);
    for (int s = 0; s < 3; s++) {
        DrawSdfRoundedRect(pixels, L.w, L.h, L.card[s], cardRad,
                           PackRgb(25, 32, 45), 1.0f,
                           PackRgb(44, 56, 76), 0.95f, 1.0f);

        for (int r = 0; r < 2; r++) {
            int idx = s * 2 + r;
            if (g_hoverId == HIT_ROW_0 + idx) {
                RECT hrc = L.row[idx];
                hrc.left   += ScalePx(3, L.dpi);
                hrc.right  -= ScalePx(3, L.dpi);
                hrc.top    += ScalePx(2, L.dpi);
                hrc.bottom -= ScalePx(2, L.dpi);
                DrawSdfRoundedRect(pixels, L.w, L.h, hrc, (float)ScalePx(7, L.dpi),
                                   PackRgb(42, 55, 76), 0.55f, 0, 0.0f, 0.0f);
            }
            DrawTogglePill(pixels, L.w, L.h, L.pill[idx], g_knobAnim[idx],
                           g_hoverId == HIT_ROW_0 + idx, L.dpi);
        }

        // Row divider inside card
        DrawHLineAlpha(pixels, L.w, L.h,
                       L.card[s].left + ScalePx(14, L.dpi),
                       L.card[s].right - ScalePx(14, L.dpi),
                       L.row[s * 2].bottom,
                       PackRgb(42, 53, 72), 0.85f);
    }

    // 3. Hotkey card & recorder button
    DrawSdfRoundedRect(pixels, L.w, L.h, L.hotkeyCard, cardRad,
                       PackRgb(25, 32, 45), 1.0f,
                       PackRgb(44, 56, 76), 0.95f, 1.0f);

    uint32_t hkFill   = g_recordingHotkey ? PackRgb(78, 56, 22)
                      : (g_hoverId == HIT_HOTKEY_BTN ? PackRgb(40, 56, 80) : PackRgb(32, 43, 60));
    uint32_t hkBorder = g_recordingHotkey ? PackRgb(242, 186, 72)
                      : (g_hoverId == HIT_HOTKEY_BTN ? PackRgb(88, 176, 246) : PackRgb(64, 82, 110));
    DrawSdfRoundedRect(pixels, L.w, L.h, L.btnHotkey, (float)ScalePx(7, L.dpi),
                       hkFill, 1.0f, hkBorder, 0.95f, 1.0f);

    // Subtle outer border for Win10 compatibility
    RECT fullRc = { 0, 0, L.w, L.h };
    DrawSdfRoundedRect(pixels, L.w, L.h, fullRc, (float)ScalePx(8, L.dpi),
                       0, 0.0f, PackRgb(54, 68, 92), 0.7f, 1.0f);

    GdiFlush();

    // 4. ClearType Typography Pass
    SetBkMode(memDc, TRANSPARENT);
    int en = (g_uiLang == 1);

    HFONT fTitle   = CreateUiFont(ScalePx(16, L.dpi), FW_BOLD);
    HFONT fSub     = CreateUiFont(ScalePx(11, L.dpi), FW_NORMAL);
    HFONT fSec     = CreateUiFont(ScalePx(12, L.dpi), FW_BOLD);
    HFONT fRowMain = CreateUiFont(ScalePx(14, L.dpi), FW_SEMIBOLD);
    HFONT fRowDesc = CreateUiFont(ScalePx(11, L.dpi), FW_NORMAL);
    HFONT fBtn     = CreateUiFont(ScalePx(12, L.dpi), FW_BOLD);

    HGDIOBJ oldFont = SelectObject(memDc, fTitle);

    // Header title & subtitle
    RECT rcTitle = { ScalePx(20, L.dpi), ScalePx(12, L.dpi), L.btnLang.left - ScalePx(8, L.dpi), ScalePx(35, L.dpi) };
    SetTextColor(memDc, RGB(240, 245, 252));
    DrawTextW(memDc,
              en ? L"Hide UID Control Panel" : L"UID \u9690\u85CF\u63A7\u5236\u9762\u677F",
              -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(memDc, fSub);
    RECT rcSub = { ScalePx(20, L.dpi), ScalePx(36, L.dpi), L.btnLang.left - ScalePx(8, L.dpi), ScalePx(54, L.dpi) };
    SetTextColor(memDc, RGB(114, 168, 214));
    DrawTextW(memDc,
              en ? L"EFF \u00B7 EFMI Coexistence Bridge v1.2" : L"EFF \u00B7 EFMI \u517C\u5BB9\u6865\u63A5\u63D2\u4EF6 v1.2",
              -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Lang & Close button labels
    SelectObject(memDc, fBtn);
    SetTextColor(memDc, RGB(214, 226, 242));
    DrawTextW(memDc, en ? L"\u4E2D\u6587" : L"EN", -1, &L.btnLang,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(memDc, g_hoverId == HIT_CLOSE_BTN ? RGB(255, 255, 255) : RGB(210, 220, 236));
    DrawTextW(memDc, L"\u00D7", -1, &L.btnClose,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Section headers & rows
    for (int s = 0; s < 3; s++) {
        SelectObject(memDc, fSec);
        SetTextColor(memDc, RGB(122, 146, 178));
        DrawTextW(memDc, en ? kSecTitlesEn[s] : kSecTitlesZh[s],
                  -1, &L.secLabel[s], DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        for (int r = 0; r < 2; r++) {
            int idx = s * 2 + r;
            RECT rMain = {
                L.row[idx].left + ScalePx(16, L.dpi),
                L.row[idx].top + ScalePx(7, L.dpi),
                L.pill[idx].left - ScalePx(12, L.dpi),
                L.row[idx].top + ScalePx(28, L.dpi)
            };
            RECT rDesc = {
                L.row[idx].left + ScalePx(16, L.dpi),
                L.row[idx].top + ScalePx(28, L.dpi),
                L.pill[idx].left - ScalePx(12, L.dpi),
                L.row[idx].bottom - ScalePx(5, L.dpi)
            };

            SelectObject(memDc, fRowMain);
            SetTextColor(memDc, RGB(232, 238, 248));
            DrawTextW(memDc, en ? kRowCopy[idx].titleEn : kRowCopy[idx].titleZh,
                      -1, &rMain, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDc, fRowDesc);
            SetTextColor(memDc, RGB(132, 148, 172));
            DrawTextW(memDc, en ? kRowCopy[idx].descEn : kRowCopy[idx].descZh,
                      -1, &rDesc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    // Hotkey row
    RECT rcHkLabel = {
        L.hotkeyCard.left + ScalePx(16, L.dpi),
        L.hotkeyCard.top,
        L.btnHotkey.left - ScalePx(10, L.dpi),
        L.hotkeyCard.bottom
    };
    SelectObject(memDc, fRowMain);
    SetTextColor(memDc, RGB(232, 238, 248));
    DrawTextW(memDc, en ? L"Panel Hotkey" : L"\u9762\u677F\u547C\u51FA\u70ED\u952E",
              -1, &rcHkLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(memDc, fBtn);
    if (g_recordingHotkey) {
        SetTextColor(memDc, RGB(255, 214, 112));
        DrawTextW(memDc, en ? L"Press key... (Esc)" : L"\u8BF7\u6309\u952E... (Esc\u53D6\u6D88)",
                  -1, &L.btnHotkey, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        char hkAscii[64];
        HotkeyChord snapHk;
        AcquireSRWLockShared(&g_guiCfgLock);
        snapHk = g_activeHotkey;
        ReleaseSRWLockShared(&g_guiCfgLock);
        FormatHotkeyChord(&snapHk, hkAscii, sizeof(hkAscii));

        wchar_t hkWide[64] = {0};
        MultiByteToWideChar(CP_UTF8, 0, hkAscii, -1, hkWide, 63);
        SetTextColor(memDc, RGB(126, 206, 255));
        DrawTextW(memDc, hkWide, -1, &L.btnHotkey,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Footer status
    SelectObject(memDc, fRowDesc);
    SetTextColor(memDc, RGB(112, 128, 152));
    DrawTextW(memDc,
              en ? L"Changes apply live & auto-save to hide_uid.ini \u00B7 Esc to close"
                 : L"\u5F00\u5173\u5B9E\u65F6\u751F\u6548\u5E76\u81EA\u52A8\u4FDD\u5B58\u81F3 hide_uid.ini \u00B7 \u6309 Esc \u6536\u8D77",
              -1, &L.footer, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    BitBlt(outDc, 0, 0, L.w, L.h, memDc, 0, 0, SRCCOPY);

    SelectObject(memDc, oldFont);
    DeleteObject(fTitle);
    DeleteObject(fSub);
    DeleteObject(fSec);
    DeleteObject(fRowMain);
    DeleteObject(fRowDesc);
    DeleteObject(fBtn);

    SelectObject(memDc, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDc);
}

// ============================================================================
// Window Visibility, DWM Styling & Event Handling
// ============================================================================

static void ApplyDwmWindowStyle(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DWORD cornerPref = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));
    COLORREF borderCol = RGB(52, 66, 90);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &borderCol, sizeof(borderCol));
}

static void SyncKnobTargetsImmediate(void) {
    for (int i = 0; i < PANEL_ROW_COUNT; i++) {
        g_knobAnim[i] = GetRowFlagValue(i) ? 1.0f : 0.0f;
    }
}

static void StartKnobAnimTimer(HWND hwnd) {
    if (!g_animTimerActive && hwnd) {
        SetTimer(hwnd, PANEL_TIMER_ANIM, 15, NULL);
        g_animTimerActive = 1;
    }
}

static void NotifyAndPersistGuiChanges(void) {
    if (g_onConfigChanged) {
        g_onConfigChanged();
    }
}

static void PositionPanelOnFirstShow(HWND hwnd) {
    if (g_panelHasCustomPos) return;

    UINT dpi = GetPanelDpi(hwnd);
    int w = ScalePx(PANEL_BASE_W, dpi);
    int h = ScalePx(PANEL_BASE_H, dpi);

    RECT refRc;
    if (!g_gameHwnd || !IsWindow(g_gameHwnd) || !GetWindowRect(g_gameHwnd, &refRc)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &refRc, 0);
    }

    int x = refRc.left + ((refRc.right - refRc.left) - w) / 2;
    int y = refRc.top  + ((refRc.bottom - refRc.top) - h) / 2;

    // If EFF's Ins panel is currently visible, place neatly beside it so neither overlaps!
    HWND effWnd = FindWindowW(L"EFF_ResourceControls", NULL);
    if (effWnd && IsWindowVisible(effWnd)) {
        DWORD effPid = 0;
        GetWindowThreadProcessId(effWnd, &effPid);
        if (effPid == GetCurrentProcessId()) {
            RECT effRc;
            if (GetWindowRect(effWnd, &effRc)) {
                int tryRight = effRc.right + ScalePx(16, dpi);
                if (tryRight + w <= refRc.right) {
                    x = tryRight;
                    y = effRc.top;
                } else if (effRc.left - ScalePx(16, dpi) - w >= refRc.left) {
                    x = effRc.left - ScalePx(16, dpi) - w;
                    y = effRc.top;
                }
            }
        }
    }

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    g_panelHasCustomPos = 1;
}

static void SetPanelVisible(HWND hwnd, int visible) {
    if (!hwnd) return;
    int cur = (g_panelVisible != 0);
    if (cur == (visible != 0)) return;

    if (visible) {
        g_recordingHotkey = 0;
        SyncKnobTargetsImmediate();
        PositionPanelOnFirstShow(hwnd);
        InterlockedExchange(&g_panelVisible, 1);
        ClipCursor(NULL);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    } else {
        g_recordingHotkey = 0;
        g_hoverId = HIT_NONE;
        if (g_animTimerActive) {
            KillTimer(hwnd, PANEL_TIMER_ANIM);
            g_animTimerActive = 0;
        }
        InterlockedExchange(&g_panelVisible, 0);
        ShowWindow(hwnd, SW_HIDE);

        // Return focus smoothly to EFF's Ins panel if open, otherwise to Endfield
        HWND effWnd = FindWindowW(L"EFF_ResourceControls", NULL);
        if (effWnd && IsWindowVisible(effWnd)) {
            DWORD effPid = 0;
            GetWindowThreadProcessId(effWnd, &effPid);
            if (effPid == GetCurrentProcessId()) {
                SetForegroundWindow(effWnd);
                return;
            }
        }
        if (g_gameHwnd && IsWindow(g_gameHwnd) && !IsIconic(g_gameHwnd)) {
            SetForegroundWindow(g_gameHwnd);
        }
    }
}

static int IsModifierOnlyVk(UINT vk) {
    return vk == VK_SHIFT   || vk == VK_LSHIFT   || vk == VK_RSHIFT   ||
           vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU    || vk == VK_LMENU    || vk == VK_RMENU    ||
           vk == VK_LWIN    || vk == VK_RWIN;
}

static LRESULT CALLBACK HideUidPanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderPanel(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            PanelLayout L;
            ComputePanelLayout(hwnd, &L);
            if (PtInRect(&L.header, pt) &&
                !PtInRect(&L.btnClose, pt) &&
                !PtInRect(&L.btnLang, pt)) {
                if (g_hoverId != HIT_NONE) {
                    g_hoverId = HIT_NONE;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorA(NULL, g_hoverId != HIT_NONE ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme;
            tme.cbSize    = sizeof(tme);
            tme.dwFlags   = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);

            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            PanelLayout L;
            ComputePanelLayout(hwnd, &L);
            int hit = HitTestControls(&L, pt);
            SetCursor(LoadCursorA(NULL, hit != HIT_NONE ? IDC_HAND : IDC_ARROW));
            if (hit != g_hoverId) {
                g_hoverId = hit;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_hoverId != HIT_NONE) {
                g_hoverId = HIT_NONE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            PanelLayout L;
            ComputePanelLayout(hwnd, &L);
            int hit = HitTestControls(&L, pt);

            if (hit == HIT_CLOSE_BTN) {
                SetPanelVisible(hwnd, 0);
                return 0;
            }
            if (hit == HIT_LANG_BTN) {
                g_recordingHotkey = 0;
                g_uiLang = (g_uiLang == 0) ? 1 : 0;
                NotifyAndPersistGuiChanges();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (hit == HIT_HOTKEY_BTN) {
                g_recordingHotkey = !g_recordingHotkey;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (hit >= HIT_ROW_0 && hit < HIT_ROW_0 + PANEL_ROW_COUNT) {
                g_recordingHotkey = 0;
                int rowIdx = hit - HIT_ROW_0;
                int nextVal = !GetRowFlagValue(rowIdx);
                SetRowFlagValue(rowIdx, nextVal);
                NotifyAndPersistGuiChanges();
                StartKnobAnimTimer(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (g_recordingHotkey) {
                g_recordingHotkey = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            UINT vk = (UINT)wp;
            if (g_recordingHotkey) {
                if (vk == VK_ESCAPE) {
                    g_recordingHotkey = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (IsModifierOnlyVk(vk)) {
                    return 0;
                }
                HotkeyChord nextHk;
                nextHk.vk    = vk;
                nextHk.ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                nextHk.alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
                nextHk.shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

                AcquireSRWLockExclusive(&g_guiCfgLock);
                g_activeHotkey = nextHk;
                ReleaseSRWLockExclusive(&g_guiCfgLock);

                g_recordingHotkey = 0;
                g_waitKeyReleaseAfterBind = 1;
                NotifyAndPersistGuiChanges();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (vk == VK_ESCAPE) {
                SetPanelVisible(hwnd, 0);
                return 0;
            }
            break;
        }

        case WM_TIMER:
            if (wp == PANEL_TIMER_ANIM) {
                int moving = 0;
                for (int i = 0; i < PANEL_ROW_COUNT; i++) {
                    float target = GetRowFlagValue(i) ? 1.0f : 0.0f;
                    float diff = target - g_knobAnim[i];
                    if (fabsf(diff) > 0.015f) {
                        g_knobAnim[i] += diff * 0.34f;
                        moving = 1;
                    } else {
                        g_knobAnim[i] = target;
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                if (!moving) {
                    KillTimer(hwnd, PANEL_TIMER_ANIM);
                    g_animTimerActive = 0;
                }
                return 0;
            }
            break;

        case WM_DPICHANGED: {
            const RECT* suggested = (const RECT*)lp;
            if (suggested) {
                SetWindowPos(hwnd, NULL,
                             suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_CLOSE:
            SetPanelVisible(hwnd, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
// Dedicated UI Thread (Event-Driven + Edge-Triggered Chord Polling)
// ============================================================================

static int IsOurProcessForeground(void) {
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static int IsChordPhysicallyDown(const HotkeyChord* hk) {
    if (!hk || hk->vk == 0) return 0;
    if ((GetAsyncKeyState((int)hk->vk) & 0x8000) == 0) return 0;
    int ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    int altDown   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
    int shiftDown = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
    return (ctrlDown == (hk->ctrl != 0)) &&
           (altDown  == (hk->alt  != 0)) &&
           (shiftDown == (hk->shift != 0));
}

static DWORD WINAPI GuiPanelThread(LPVOID unused) {
    (void)unused;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        g_pGetDpiForWindow = (tGetDpiForWindow)(void*)(uintptr_t)
            GetProcAddress(hUser32, "GetDpiForWindow");
    }

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = HideUidPanelWndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = L"Endfield_HideUID_ControlPanel";
    RegisterClassExW(&wc);

    UINT initDpi = GetPanelDpi(NULL);
    int initW = ScalePx(PANEL_BASE_W, initDpi);
    int initH = ScalePx(PANEL_BASE_H, initDpi);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Hide UID Control Panel",
        WS_POPUP,
        100, 100, initW, initH,
        NULL, NULL, wc.hInstance, NULL);

    if (!hwnd) return 1;

    g_panelHwnd = hwnd;
    ApplyDwmWindowStyle(hwnd);
    SyncKnobTargetsImmediate();

    int prevDown = 0;
    MSG msg;

    while (!g_hookDisabled) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_panelHwnd = NULL;
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Auto-hide if game window is minimized or destroyed
        if (g_panelVisible && g_gameHwnd) {
            if (!IsWindow(g_gameHwnd)) {
                SetPanelVisible(hwnd, 0);
                break;
            }
            if (IsIconic(g_gameHwnd)) {
                SetPanelVisible(hwnd, 0);
            } else if (GetForegroundWindow() == hwnd) {
                // Keep mouse cursor free while user interacts with our panel
                ClipCursor(NULL);
            }
        }

        // Edge-triggered hotkey detection (only when game or our panel is foreground)
        HotkeyChord hk;
        AcquireSRWLockShared(&g_guiCfgLock);
        hk = g_activeHotkey;
        ReleaseSRWLockShared(&g_guiCfgLock);

        if (!g_recordingHotkey && hk.vk != 0) {
            int down = IsChordPhysicallyDown(&hk);
            if (g_waitKeyReleaseAfterBind) {
                if (!down && (GetAsyncKeyState((int)hk.vk) & 0x8000) == 0) {
                    g_waitKeyReleaseAfterBind = 0;
                }
                prevDown = down;
            } else {
                if (down && !prevDown && g_gameHwnd && IsWindow(g_gameHwnd) &&
                    !IsIconic(g_gameHwnd) && IsOurProcessForeground()) {
                    SetPanelVisible(hwnd, !g_panelVisible);
                }
                prevDown = down;
            }
        } else {
            prevDown = 0;
        }

        Sleep(g_panelVisible ? 12 : 25);
    }

    if (IsWindow(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
        DestroyWindow(hwnd);
    }
    g_panelHwnd = NULL;
    return 0;
}

static void InitGuiPanelState(const HideUidConfig* cfg, tOnPanelConfigChanged onChanged) {
    AcquireSRWLockExclusive(&g_guiCfgLock);
    g_activeHotkey = cfg->toggle_key;
    ReleaseSRWLockExclusive(&g_guiCfgLock);
    g_uiLang = cfg->language;
    g_onConfigChanged = onChanged;
}

static void SnapshotGuiPanelConfig(HideUidConfig* outCfg) {
    outCfg->hide_uid       = g_uidHidden ? 1 : 0;
    outCfg->hide_ping      = g_pingHidden ? 1 : 0;
    outCfg->hide_menu_uid  = g_menuUidHidden ? 1 : 0;
    outCfg->hide_menu_name = g_menuNameHidden ? 1 : 0;
    outCfg->hide_card_uid  = g_cardUidHidden ? 1 : 0;
    outCfg->hide_card_name = g_cardNameHidden ? 1 : 0;
    outCfg->fix_eff_panel  = g_fixEffPanel ? 1 : 0;
    AcquireSRWLockShared(&g_guiCfgLock);
    outCfg->toggle_key     = g_activeHotkey;
    ReleaseSRWLockShared(&g_guiCfgLock);
    outCfg->language       = g_uiLang;
}
