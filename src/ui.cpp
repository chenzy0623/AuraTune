// AuraTune 界面：Win32 + GDI+ 全自绘深色 UI（无标题栏，支持拖拽/缩放/托盘）
#include "app.h"
#include "audio.h"
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Gdiplus;

// ------------------------------------------------------------------ 主题色
static const Color COL_BG    (255, 11, 14, 19);
static const Color COL_PANEL (255, 21, 26, 35);
static const Color COL_PANEL2(255, 28, 35, 46);
static const Color COL_LINE  (255, 42, 52, 68);
static const Color COL_TEXT  (255, 231, 236, 243);
static const Color COL_DIM   (255, 133, 144, 163);
static const Color COL_ACCENT(255, 78, 141, 255);
static const Color COL_ACC2  (255, 54, 211, 191);
static const Color COL_WARN  (255, 255, 176, 32);
static const Color COL_GOOD  (255, 61, 220, 151);
static const Color COL_RED   (255, 255, 96, 96);

// ------------------------------------------------------------------ 控件 id
enum : int {
    ID_NONE = 0, ID_MIN = 1, ID_CLOSE = 2, ID_MAX = 3, ID_FULL = 4,
    ID_SAVE = 5, ID_DEL = 6, ID_NAME = 7,
    ID_TAB_MUSIC = 10, ID_TAB_GAME = 11,
    ID_PRESET0 = 100,
    ID_CORR = 200, ID_CORRITEM0 = 210,
    ID_EQ0 = 300,
    ID_KNOB0 = 400,
    ID_XF = 500, ID_VOL = 501,
    ID_BUF0 = 700, ID_CAP = 710, ID_APO = 720
};

struct Layout {
    RECT topbar;
    RECT tabMusic, tabGame, btnMin, btnClose, btnMax;
    RECT presetPanel, corrPanel, gamePanel, corrBox;
    RECT nameBox, saveBtn, delBtn;
    int  presetRef[64] = {}; int presetKind[64] = {}; int presetN = 0;
    int  presetRowH = 36, presetListTop = 134, presetVisible = 0, presetScroll = 0;
    bool presetTwoLine = true;
    RECT capBtn, bufBtn[3], apoBtn;
    RECT eqPanel;
    int  eqColW = 40; int eqTop = 0, eqBot = 0;
    RECT knobPanel, specPanel;
    RECT knob[5];
    RECT xf, volSlider;
    int  leftW = 316;
};

struct App {
    HWND hwnd = nullptr;
    HDC  memdc = nullptr;
    HBITMAP membmp = nullptr;
    int  memW = 0, memH = 0;
    int  W = 1200, H = 675;
    Layout lay;

    Settings    settings;
    AudioEngine engine;

    int  hot = 0, active = 0;
    int  dragY = 0;
    float dragVal = 0.f;

    bool corrOpen = false;
    int  bufMode = 1;
    bool bufUserSet = false;
    int  presetScroll = 0;
    std::wstring detectedGame;
    DWORD detectedPid = 0;
    bool capturing = false;
    bool autoSwitched = false;

    std::wstring customName = L"我的曲线";
    bool nameEdit = false;
    bool saveDirty = false;
    DWORD saveAt = 0;

    std::wstring toast;
    DWORD toastUntil = 0;

    bool  trayAdded = false;
    HICON hIconBig = nullptr, hIconSmall = nullptr;
    bool  quitting = false;

    bool  fullscreen = false;
    WINDOWPLACEMENT prevPl{};
    LONG_PTR prevStyle = 0;

    Font* fTiny = nullptr; Font* fSmall = nullptr; Font* fBody = nullptr;
    Font* fBold = nullptr; Font* fTitle = nullptr; Font* fSmallBold = nullptr;

    float meterL = 0, meterR = 0;
    float bars[kSpectrumBars] = {};
    bool  barsInit = false;
};

static App A;

static const wchar_t* kWndClass = L"AuraTuneMainWnd";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT_PTR TIMER_UI = 1;

// ------------------------------------------------------------------ 基础工具
static inline RECT Rc(int x, int y, int w, int h) { RECT r{ x, y, x + w, y + h }; return r; }
static inline int  W_(const RECT& r) { return r.right - r.left; }
static inline int  H_(const RECT& r) { return r.bottom - r.top; }
static inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

static void Text(Graphics& gr, const std::wstring& s, const Font* f, const RECT& rc, Color c,
                 StringAlignment ha = StringAlignmentNear, StringAlignment va = StringAlignmentCenter,
                 bool ellipsis = true) {
    if (s.empty() || !f) return;
    SolidBrush br(c);
    StringFormat sf;
    sf.SetAlignment(ha);
    sf.SetLineAlignment(va);
    if (ellipsis) sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF r((REAL)rc.left, (REAL)rc.top, (REAL)W_(rc), (REAL)H_(rc));
    gr.DrawString(s.c_str(), (INT)s.size(), f, r, &sf, &br);
}

static void FillRound(Graphics& gr, const RECT& rc, int radius, Color fill) {
    GraphicsPath p;
    const REAL x = (REAL)rc.left, y = (REAL)rc.top, w = (REAL)W_(rc), h = (REAL)H_(rc);
    const REAL d = (REAL)(radius * 2);
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(fill);
    gr.FillPath(&b, &p);
}

static void StrokeRound(Graphics& gr, const RECT& rc, int radius, Color line, REAL width = 1.f) {
    GraphicsPath p;
    const REAL x = (REAL)rc.left, y = (REAL)rc.top, w = (REAL)W_(rc), h = (REAL)H_(rc);
    const REAL d = (REAL)(radius * 2);
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    Pen pen(line, width);
    gr.DrawPath(&pen, &p);
}

static void FillRectC(Graphics& gr, Color c, REAL x, REAL y, REAL w, REAL h) {
    SolidBrush b(c);
    gr.FillRectangle(&b, x, y, w, h);
}

static void FillPolyC(Graphics& gr, Color c, Point* pts, int n) {
    SolidBrush b(c);
    gr.FillPolygon(&b, pts, n);
}

static void Panel(Graphics& gr, const RECT& rc, const std::wstring& title) {
    FillRound(gr, rc, 12, COL_PANEL);
    StrokeRound(gr, rc, 12, COL_LINE, 1.f);
    if (!title.empty()) Text(gr, title, A.fBold, Rc(rc.left + 18, rc.top + 8, W_(rc) - 36, 26), COL_TEXT);
}

static void RegisterHit_DEPRECATED(int, const RECT&) {}

// 全局命中表
static std::vector<std::pair<int, RECT>> gHits;

static void Hit(int id, const RECT& r) { gHits.push_back({ id, r }); }

static void VSlider(Graphics& gr, const RECT& track, float v01, bool hot) {
    const int cx = (track.left + track.right) / 2;
    FillRound(gr, Rc(cx - 2, track.top, 4, H_(track)), 2, COL_PANEL2);
    const int midY = (track.top + track.bottom) / 2;
    const int hy = (int)(track.bottom - Clamp01(v01) * H_(track));
    {
        SolidBrush lb(Color(70, 120, 140, 170));
        gr.FillRectangle(&lb, (REAL)(cx - 14), (REAL)midY, 28.f, 1.f);
    }
    int a = std::min(hy, midY), b = std::max(hy, midY);
    if (b - a > 0) FillRound(gr, Rc(cx - 2, a, 4, b - a), 2, hot ? COL_ACCENT : Color(255, 62, 78, 110));
    const int hw = hot ? 16 : 14;
    RECT hr = Rc(cx - hw / 2, hy - 6, hw, 12);
    FillRound(gr, hr, 6, hot ? Color(255, 255, 255, 255) : Color(255, 206, 217, 233));
    StrokeRound(gr, hr, 6, Color(120, 0, 0, 0), 1.f);
}

static void HSlider(Graphics& gr, const RECT& track, float v01, bool hot) {
    const int cy = (track.top + track.bottom) / 2;
    FillRound(gr, Rc(track.left, cy - 2, W_(track), 4), 2, COL_PANEL2);
    const int w = (int)(Clamp01(v01) * W_(track));
    if (w > 2) FillRound(gr, Rc(track.left, cy - 2, w, 4), 2, hot ? COL_ACCENT : Color(255, 62, 78, 110));
    RECT hr = Rc(track.left + w - 6, cy - 7, 12, 14);
    FillRound(gr, hr, 6, hot ? Color(255, 255, 255, 255) : Color(255, 206, 217, 233));
    StrokeRound(gr, hr, 6, Color(120, 0, 0, 0), 1.f);
}

static void KnobDraw(Graphics& gr, const RECT& rc, float v01, const std::wstring& label,
                     const std::wstring& val, bool hot) {
    const int cx = (rc.left + rc.right) / 2;
    const int h = H_(rc);
    float rad = (float)(h - 58) * 0.46f;
    if (rad > 29.f) rad = 29.f;
    if (rad < 13.f) rad = 13.f;
    const int cy = rc.top + (int)rad + 4;
    Pen track(Color(255, 38, 47, 62), 5.f);
    track.SetStartCap(LineCapRound); track.SetEndCap(LineCapRound);
    gr.DrawArc(&track, (REAL)(cx - rad), (REAL)(cy - rad), rad * 2, rad * 2, 135.f, 270.f);
    Pen pv(hot ? COL_ACCENT : COL_ACC2, 5.f);
    pv.SetStartCap(LineCapRound); pv.SetEndCap(LineCapRound);
    gr.DrawArc(&pv, (REAL)(cx - rad), (REAL)(cy - rad), rad * 2, rad * 2, 135.f, 270.f * Clamp01(v01));
    const float ir = rad * 0.72f;
    SolidBrush bfill(COL_PANEL2);
    gr.FillEllipse(&bfill, (REAL)(cx - ir), (REAL)(cy - ir), ir * 2, ir * 2);
    Pen ring(Color(255, 50, 61, 80), 1.3f);
    gr.DrawEllipse(&ring, (REAL)(cx - ir), (REAL)(cy - ir), ir * 2, ir * 2);
    SolidBrush bt(hot ? COL_ACC2 : COL_TEXT);
    if (rad >= 20.f) {
        // 大尺寸时在圆内画一个中心点，小尺寸时留空
        gr.FillEllipse(&bt, (REAL)(cx - 2.f), (REAL)(cy - 2.f), 4.f, 4.f);
    }
    if (rad >= 15.f) {
        const double ang = (135.0 + 270.0 * Clamp01(v01)) * 3.14159265358979 / 180.0;
        const float px = (float)(cx + cos(ang) * (rad - 2.0));
        const float py = (float)(cy + sin(ang) * (rad - 2.0));
        SolidBrush bd(hot ? Color(255, 255, 255, 255) : COL_ACC2);
        gr.FillEllipse(&bd, px - 3.f, py - 3.f, 6.f, 6.f);
    }
    Text(gr, label, A.fTiny, Rc(rc.left, cy + (int)rad + 7, W_(rc), 17),
         hot ? COL_TEXT : COL_DIM, StringAlignmentCenter);
    Text(gr, val, A.fTiny, Rc(rc.left, cy + (int)rad + 23, W_(rc), 16),
         hot ? COL_ACCENT : COL_ACC2, StringAlignmentCenter);
}

// ------------------------------------------------------------------ 布局
static void RebuildLayout(App& a) {
    Layout& L = a.lay;
    const int W = a.W, H = a.H;
    L.topbar = Rc(0, 0, W, 76);
    L.tabMusic = Rc(232, 19, 92, 36);
    L.tabGame  = Rc(330, 19, 92, 36);
    // Win10 同款顺序：最小化 → 最大化 → 关闭
    L.btnMin   = Rc(W - 138, 20, 34, 34);
    L.btnMax   = Rc(W - 96, 20, 34, 34);
    L.btnClose = Rc(W - 54, 20, 34, 34);

    const int lx = 24, leftW = 316;
    L.leftW = leftW;

    const int midTop = 92;
    const int bottomLimit = H - 24;
    const int availH = std::max(360, bottomLimit - midTop);

    // 预设列表：内置预设 + 自定义曲线（当前模式）
    int n = 0;
    for (int i = 0; i < kPresetCount && n < 64; ++i)
        if (kPresets[i].mode == a.settings.mode) { L.presetRef[n] = i; L.presetKind[n] = 0; ++n; }
    for (int i = 0; i < (int)a.settings.customs.size() && n < 64; ++i)
        if (a.settings.customs[i].mode == a.settings.mode) { L.presetRef[n] = i; L.presetKind[n] = 1; ++n; }
    L.presetN = n;

    // 左列：预设（自适应）+ 校正 + 游戏卡
    const int corrH = 88;
    const int cardH = (availH >= 470) ? 150 : 132;
    const int customRowH = 40;                 // 自定义曲线的名字框 + 保存/删除
    int presetH = availH - corrH - 12 - cardH - 12;
    if (presetH < 120) presetH = 120;
    L.presetPanel = Rc(lx, midTop, leftW, presetH);
    L.presetListTop = midTop + 40;
    {
        const int listH = presetH - 48 - customRowH;
        int rowH = (n > 0) ? (listH / n) : 34;
        if (rowH > 34) rowH = 34;
        if (rowH < 20) rowH = 20;
        L.presetRowH = rowH;
        L.presetTwoLine = (rowH >= 30);
        L.presetVisible = (rowH > 0) ? (listH / rowH) : 0;
        if (L.presetVisible > n) L.presetVisible = n;
        const int maxScroll = std::max(0, n - L.presetVisible);
        if (a.presetScroll > maxScroll) a.presetScroll = maxScroll;
        if (a.presetScroll < 0) a.presetScroll = 0;
        L.presetScroll = a.presetScroll;
    }
    {
        const int ry = L.presetPanel.bottom - 34;
        L.nameBox = Rc(lx + 10, ry, leftW - 126, 26);
        L.saveBtn = Rc(lx + leftW - 112, ry, 54, 26);
        L.delBtn  = Rc(lx + leftW - 52, ry, 44, 26);
    }

    L.corrPanel = Rc(lx, L.presetPanel.bottom + 12, leftW, corrH);
    L.corrBox   = Rc(lx + 12, L.corrPanel.top + 42, leftW - 24, 34);

    const int gy = L.corrPanel.bottom + 12;
    const int gh = std::max(96, bottomLimit - gy);
    L.gamePanel = Rc(lx, gy, leftW, gh);
    L.capBtn = Rc(lx + 16, gy + 64, leftW - 32, 34);
    const int bufY = gy + 106;
    for (int i = 0; i < 3; ++i) L.bufBtn[i] = Rc(lx + 96 + i * 62, bufY, 56, 26);
    L.apoBtn = Rc(lx + 16, bufY + 34, leftW - 32, 26);

    const int rx = lx + leftW + 16;
    const int rw = W - rx - 24;

    int knobH = (availH >= 560) ? 152 : 132;
    int specH = (availH >= 560) ? 168 : 124;   // 电平框加高到正常尺寸
    int eqH = availH - knobH - specH - 24;
    if (eqH < 210) { eqH = 210; specH = std::max(96, availH - eqH - knobH - 24); }

    L.eqPanel = Rc(rx, midTop, rw, eqH);
    L.eqColW  = std::max(18, (rw - 44) / kBands);
    L.eqTop   = L.eqPanel.top + 50;
    L.eqBot   = L.eqPanel.bottom - 38;

    L.knobPanel = Rc(rx, L.eqPanel.bottom + 12, rw, knobH);
    {
        const int kw = rw / 5;
        for (int i = 0; i < 5; ++i)
            L.knob[i] = Rc(rx + i * kw, L.knobPanel.top + 22, kw, knobH - 46);
        const int by = L.knobPanel.bottom - 24;
        L.xf        = Rc(rx + 58, by, 128, 16);
        L.volSlider = Rc(rx + rw - 176, by, 160, 16);
    }

    const int sy = L.knobPanel.bottom + 12;
    L.specPanel = Rc(rx, sy, rw, std::max(56, bottomLimit - sy));
}

// ------------------------------------------------------------------ 应用动作
static void Toast(const std::wstring& s, DWORD ms = 2600) {
    A.toast = s;
    A.toastUntil = GetTickCount() + ms;
}

static void ApplyToEngine() {
    A.engine.updateSettings(A.settings);
    A.engine.setVolumePercent(A.settings.volumePercent);
}

// 写 ini 很慢（每个键都是一次文件重写），改成延迟合并写，避免调参/切模式时卡顿
static void SaveSettings() {
    A.saveDirty = true;
    A.saveAt = GetTickCount() + 700;
}

static void FlushSave() {
    if (!A.saveDirty) return;
    if (GetTickCount() < A.saveAt) return;
    A.saveDirty = false;
    A.settings.save();
}

static void ToggleMaximize() {
    if (!A.hwnd) return;
    ShowWindow(A.hwnd, IsZoomed(A.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

static void ToggleFullscreen() {
    if (!A.hwnd) return;
    if (!A.fullscreen) {
        if (IsIconic(A.hwnd)) ShowWindow(A.hwnd, SW_RESTORE);   // 从最小化直接进全屏也要先恢复
        A.prevPl.length = sizeof(A.prevPl);
        GetWindowPlacement(A.hwnd, &A.prevPl);
        A.prevStyle = GetWindowLongPtrW(A.hwnd, GWL_STYLE);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(A.hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        // 用 rcWork 而不是 rcMonitor：任务栏要露出来
        RECT wa = mi.rcWork;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowLongPtrW(A.hwnd, GWL_STYLE, (A.prevStyle | WS_POPUP) & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
        SetWindowPos(A.hwnd, HWND_TOP, wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        A.fullscreen = true;
        Toast(L"全屏：按 Esc 或 F11 退出");
    } else {
        SetWindowLongPtrW(A.hwnd, GWL_STYLE, A.prevStyle);
        SetWindowPlacement(A.hwnd, &A.prevPl);
        SetWindowPos(A.hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        A.fullscreen = false;
    }
    RebuildLayout(A);
    InvalidateRect(A.hwnd, nullptr, FALSE);
}

static int FirstPresetOfMode(int mode) {
    for (int i = 0; i < kPresetCount; ++i)
        if (kPresets[i].mode == mode) return i;
    return 0;
}

static void SwitchMode(int mode) {
    if (A.settings.mode == mode) return;
    A.settings.mode = mode;
    int idx = (mode == MODE_MUSIC) ? A.settings.presetMusic : A.settings.presetGame;
    // 防止 ini 里的预设下标属于另一个模式（会导致切模式后又被拉回去）
    if (idx < 0 || idx >= kPresetCount || kPresets[idx].mode != mode)
        idx = FirstPresetOfMode(mode);
    A.settings.applyPreset(idx);
    A.settings.mode = mode;
    if (!A.bufUserSet) {
        // 游戏模式默认切到超低缓冲，满足 FPS 低延迟需求；缓冲没变就不要重开音频流（重开会卡一下）
        const int want = (mode == MODE_GAME) ? 0 : 1;
        if (want != A.bufMode) {
            A.bufMode = want;
            A.engine.setBufferMode(want);
            A.engine.requestReconnect();
        }
    }
    A.corrOpen = false;
    A.presetScroll = 0;
    ApplyToEngine();
    SaveSettings();
    RebuildLayout(A);
    InvalidateRect(A.hwnd, nullptr, FALSE);
}

static void ApplyPresetIndex(int idx) {
    A.settings.applyPreset(idx);
    ApplyToEngine();
    SaveSettings();
    InvalidateRect(A.hwnd, nullptr, FALSE);
}

// 把当前 EQ + 旋钮 + 校正存成一条自定义曲线
static void SaveAsCustom() {
    std::wstring nm = A.customName;
    while (!nm.empty() && (nm.back() == L' ' || nm.back() == L'\t')) nm.pop_back();
    if (nm.empty()) {
        wchar_t d[64];
        swprintf(d, 64, L"我的曲线 %d", (int)A.settings.customs.size() + 1);
        nm = d;
    }
    const int idx = A.settings.addCustom(nm);
    if (idx < 0) { Toast(L"自定义曲线已达上限 40 条"); return; }
    A.settings.applyCustom(idx);
    A.settings.mode = (A.settings.mode == MODE_GAME) ? MODE_GAME : MODE_MUSIC;
    ApplyToEngine();
    SaveSettings();
    RebuildLayout(A);
    InvalidateRect(A.hwnd, nullptr, FALSE);
    std::wstring msg = L"已保存曲线：";
    msg += nm;
    Toast(msg);
}

static void DeleteSelectedCustom() {
    const int idx = A.settings.customSel;
    if (idx < 0 || idx >= (int)A.settings.customs.size()) {
        Toast(L"请先在列表里选中一条自定义曲线（★ 开头）");
        return;
    }
    std::wstring nm = A.settings.customs[idx].name;
    const int mode = A.settings.customs[idx].mode;
    A.settings.removeCustom(idx);
    if (A.settings.mode == mode) {
        const int fb = FirstPresetOfMode(mode);
        A.settings.applyPreset(fb);
        A.settings.mode = mode;
        ApplyToEngine();
    }
    SaveSettings();
    RebuildLayout(A);
    InvalidateRect(A.hwnd, nullptr, FALSE);
    std::wstring msg = L"已删除曲线：";
    msg += nm;
    Toast(msg);
}

static bool EquaApoInstalled(std::wstring* cfgPath) {
    wchar_t pf[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, pf))) return false;
    std::wstring p = PathJoin(pf, L"EqualizerAPO\\config\\config.txt");
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (cfgPath) *cfgPath = p;
    return true;
}

static void ExportApo() {
    std::wstring dir = PathJoin(ExeDir(), L"apo");
    CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t name[128];
    const int pi = (A.settings.mode == MODE_GAME) ? A.settings.presetGame : A.settings.presetMusic;
    swprintf(name, 128, L"AuraTune_%s.txt", kPresets[pi].name);
    std::wstring path = PathJoin(dir, name);

    FILE* f = _wfopen(path.c_str(), L"w, ccs=UTF-8");
    if (!f) { Toast(L"导出失败：无法写入 apo 目录"); return; }

    fwprintf(f, L"# AuraTune 导出 - %s\n", kPresets[pi].name);
    fwprintf(f, L"# 用法：Equalizer APO 配置编辑器 -> 打开本文件 -> 选择耳机设备 -> 保存\n");
    fwprintf(f, L"# 优势：完全在 Windows 音频引擎内运行，额外延迟 0 ms（FPS 首选）\n\n");
    fwprintf(f, L"Preamp: %d dB\n", (int)-lround(A.settings.bass * 0.6f));
    for (int b = 0; b < kBands; ++b) {
        float g = A.settings.eq[b] + kCorrections[A.settings.correction].eq[b];
        if (fabsf(g) < 0.05f) continue;
        fwprintf(f, L"Filter %d: ON PK Fc %d Hz Gain %.1f dB Q 1.1\n",
                 b + 1, (int)kBandFreq[b], g);
    }
    if (fabsf(A.settings.bass) > 0.05f)
        fwprintf(f, L"Filter 11: ON LS Fc 105 Hz Gain %.1f dB Q 0.7\n", A.settings.bass);
    if (fabsf(A.settings.vocal) > 0.05f)
        fwprintf(f, L"Filter 12: ON PK Fc 2600 Hz Gain %.1f dB Q 1.0\n", A.settings.vocal);
    if (fabsf(A.settings.ambience) > 0.05f)
        fwprintf(f, L"Filter 13: ON HS Fc 11000 Hz Gain %.1f dB Q 0.7\n", A.settings.ambience * 0.8f);
    fclose(f);

    std::wstring cfg;
    if (EquaApoInstalled(&cfg)) {
        Toast(L"已导出到 apo 目录（检测到 Equalizer APO，可直接导入）");
    } else {
        Toast(L"已导出到 apo 目录（未检测到 Equalizer APO）");
    }
    ShellExecuteW(A.hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void ToggleCapture() {
    if (A.capturing) {
        A.engine.stopCapture();
        A.capturing = false;
        Toast(L"已停止游戏音频处理");
        return;
    }
    if (A.detectedPid == 0) { Toast(L"没有检测到游戏进程"); return; }
    A.engine.startCapture(A.detectedPid, A.detectedGame);
    A.capturing = true;
    Toast(A.detectedGame + L" 的音频已接管");
}

// ------------------------------------------------------------------ 绘制
static void Paint(App& a) {
    HDC dc = GetDC(a.hwnd);
    // 后台缓冲只在尺寸变化时重建（每帧重建会明显掉帧）
    if (!a.memdc || a.memW != a.W || a.memH != a.H) {
        if (a.memdc) { DeleteObject(a.membmp); DeleteDC(a.memdc); a.memdc = nullptr; a.membmp = nullptr; }
        a.memdc = CreateCompatibleDC(dc);
        a.membmp = CreateCompatibleBitmap(dc, a.W, a.H);
        a.memW = a.W;
        a.memH = a.H;
    }
    SelectObject(a.memdc, a.membmp);
    gHits.clear();

    {
        Graphics gr(a.memdc);
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        gr.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        gr.SetPixelOffsetMode(PixelOffsetModeHalf);

        {
            LinearGradientBrush bg(Point(0, 0), Point(a.W, a.H),
                                   Color(255, 16, 21, 29), Color(255, 8, 10, 14));
            gr.FillRectangle(&bg, 0, 0, a.W, a.H);
            GraphicsPath glow;
            glow.AddEllipse((REAL)(a.W / 2 - 520), -360.f, 1040.f, 460.f);
            PathGradientBrush pg(&glow);
            pg.SetCenterColor(Color(40, 76, 141, 255));
            Color surround[] = { Color(0, 76, 141, 255) };
            int cnt = 1;
            pg.SetSurroundColors(surround, &cnt);
            gr.FillPath(&pg, &glow);
        }

        RebuildLayout(a);
        const Layout& L = a.lay;
        const Settings& s = a.settings;
        const bool game = (s.mode == MODE_GAME);

        // ---------------- 顶栏
        {
            FillRectC(gr, Color(120, 12, 15, 21), 0.f, 0.f, (REAL)a.W, 76.f);
            FillRectC(gr, Color(255, 28, 35, 48), 0.f, 75.f, (REAL)a.W, 1.f);
        }
        Text(gr, L"AuraTune", a.fTitle, Rc(24, 10, 220, 30), COL_TEXT);
        Text(gr, game ? L"有线耳机调音台 · 游戏引擎" : L"有线耳机调音台 · 音乐引擎",
             a.fTiny, Rc(24, 40, 300, 20), COL_DIM);

        if (!game) { FillRound(gr, L.tabMusic, 9, COL_ACCENT); Text(gr, L"音乐", a.fBold, L.tabMusic, Color(255, 255, 255, 255), StringAlignmentCenter); }
        else Text(gr, L"音乐", a.fBody, L.tabMusic, COL_DIM, StringAlignmentCenter);
        Hit(ID_TAB_MUSIC, L.tabMusic);

        if (game) { FillRound(gr, L.tabGame, 9, COL_ACCENT); Text(gr, L"游戏", a.fBold, L.tabGame, Color(255, 255, 255, 255), StringAlignmentCenter); }
        else Text(gr, L"游戏", a.fBody, L.tabGame, COL_DIM, StringAlignmentCenter);
        Hit(ID_TAB_GAME, L.tabGame);

        {
            std::wstring dev = a.engine.deviceName();
            if (dev.empty()) dev = L"正在打开输出设备…";
            Text(gr, dev, a.fSmall, Rc(a.W - 640, 12, 380, 22), COL_TEXT, StringAlignmentFar);

            wchar_t lat[128];
            const float lms = a.engine.latencyMs();
            {
                static int once = 0;
            }
            if (a.capturing && lms > 0) swprintf(lat, 128, L"输出 %.1f ms · 端到端约 %.1f ms", lms, lms + 11.f);
            else if (lms > 0)          swprintf(lat, 128, L"输出延迟 %.1f ms", lms);
            else                       swprintf(lat, 128, L"输出延迟 --");
            Text(gr, lat, a.fTiny, Rc(a.W - 640, 36, 380, 20),
                 (lms > 0.f && lms < 12.f) ? COL_GOOD : (lms > 30.f ? COL_WARN : COL_DIM), StringAlignmentFar);
        }

        {
            FillRound(gr, L.btnMin, 8, a.hot == ID_MIN ? COL_PANEL2 : Color(0, 0, 0, 0));
            Pen p(COL_DIM, 1.6f);
            gr.DrawLine(&p, (REAL)(L.btnMin.left + 11), (REAL)(L.btnMin.top + 17), (REAL)(L.btnMin.left + 23), (REAL)(L.btnMin.top + 17));
            Hit(ID_MIN, L.btnMin);

            FillRound(gr, L.btnMax, 8, a.hot == ID_MAX ? COL_PANEL2 : Color(0, 0, 0, 0));
            {
                const bool maximized = IsZoomed(a.hwnd) != 0;
                Pen pm(a.hot == ID_MAX ? COL_TEXT : COL_DIM, 1.6f);
                if (!maximized) {
                    gr.DrawRectangle(&pm, (REAL)(L.btnMax.left + 11), (REAL)(L.btnMax.top + 11), 12.f, 12.f);
                } else {
                    gr.DrawRectangle(&pm, (REAL)(L.btnMax.left + 10), (REAL)(L.btnMax.top + 13), 10.f, 10.f);
                    gr.DrawRectangle(&pm, (REAL)(L.btnMax.left + 14), (REAL)(L.btnMax.top + 9), 10.f, 10.f);
                }
            }
            Hit(ID_MAX, L.btnMax);

            FillRound(gr, L.btnClose, 8, a.hot == ID_CLOSE ? Color(255, 130, 44, 44) : Color(0, 0, 0, 0));
            Pen pc(a.hot == ID_CLOSE ? Color(255, 255, 220, 220) : COL_DIM, 1.6f);
            gr.DrawLine(&pc, (REAL)(L.btnClose.left + 12), (REAL)(L.btnClose.top + 12), (REAL)(L.btnClose.left + 22), (REAL)(L.btnClose.top + 22));
            gr.DrawLine(&pc, (REAL)(L.btnClose.left + 22), (REAL)(L.btnClose.top + 12), (REAL)(L.btnClose.left + 12), (REAL)(L.btnClose.top + 22));
            Hit(ID_CLOSE, L.btnClose);
        }

        // ---------------- 预设（内置 + 自定义，自适应行高 + 溢出滚动）
        Panel(gr, L.presetPanel, game ? L"游戏预设 / 曲线" : L"音乐预设 / 曲线");
        for (int k = 0; k < L.presetVisible; ++k) {
            const int slot = k + L.presetScroll;
            const int kind = L.presetKind[slot];
            const int ref  = L.presetRef[slot];
            bool on = false;
            std::wstring name, desc;
            if (kind == 0) {
                if (ref < 0 || ref >= kPresetCount) continue;
                on = (ref == (game ? s.presetGame : s.presetMusic)) && s.customSel < 0;
                name = kPresets[ref].name;
                desc = kPresets[ref].desc;
            } else {
                if (ref < 0 || ref >= (int)s.customs.size()) continue;
                on = (s.customSel == ref);
                name = L"★ ";
                name += s.customs[ref].name;
                desc = L"我的自定义曲线 · 可继续微调";
            }
            RECT rr = Rc(L.presetPanel.left + 10, L.presetListTop + k * L.presetRowH, L.leftW - 20, L.presetRowH - 4);
            if (on) {
                FillRound(gr, rr, 8, Color(255, 33, 45, 68));
                SolidBrush acc(COL_ACCENT);
                gr.FillRectangle(&acc, (REAL)rr.left, (REAL)(rr.top + 4), 3.f, (REAL)(H_(rr) - 8));
            } else if (a.hot == (kind == 0 ? ID_PRESET0 + ref : ID_PRESET0 + 1000 + ref)) {
                FillRound(gr, rr, 8, Color(90, 42, 50, 68));
            }
            const Color nameCol = on ? COL_TEXT : (kind == 1 ? Color(255, 168, 214, 205) : Color(255, 202, 210, 222));
            if (L.presetTwoLine) {
                Text(gr, name, on ? a.fBold : a.fBody,
                     Rc(rr.left + 12, rr.top + 1, W_(rr) - 24, H_(rr) / 2), nameCol);
                Text(gr, desc, a.fTiny,
                     Rc(rr.left + 12, rr.top + H_(rr) / 2 - 2, W_(rr) - 24, H_(rr) / 2), COL_DIM);
            } else {
                Text(gr, name, on ? a.fSmallBold : a.fSmall, rr, nameCol);
            }
            Hit(kind == 0 ? ID_PRESET0 + ref : ID_PRESET0 + 1000 + ref, rr);
        }

        // ---------------- 自定义曲线：名字框 + 保存 + 删除
        {
            FillRound(gr, L.nameBox, 7, a.nameEdit ? Color(255, 35, 43, 58) : COL_PANEL2);
            StrokeRound(gr, L.nameBox, 7, a.nameEdit ? COL_ACCENT : (a.hot == ID_NAME ? COL_LINE : Color(255, 34, 42, 54)));
            const std::wstring shown = a.customName.empty() ? std::wstring(L"给新曲线起个名字…") : a.customName;
            Text(gr, shown, a.fSmall, Rc(L.nameBox.left + 8, L.nameBox.top, W_(L.nameBox) - 12, H_(L.nameBox)),
                 a.customName.empty() ? Color(255, 96, 106, 122) : COL_TEXT);
            if (a.nameEdit && (GetTickCount() / 500) % 2 == 0) {
                const int wpx = (int)(a.customName.size() * 7.2f);
                const int cx = std::min(L.nameBox.right - 6, L.nameBox.left + 8 + wpx);
                FillRectC(gr, COL_ACCENT, (REAL)cx, (REAL)(L.nameBox.top + 6), 1.6f, (REAL)(H_(L.nameBox) - 12));
            }
            Hit(ID_NAME, L.nameBox);

            FillRound(gr, L.saveBtn, 7, a.hot == ID_SAVE ? Color(255, 44, 96, 168) : Color(255, 36, 66, 118));
            Text(gr, L"保存", a.fTiny, L.saveBtn, COL_TEXT, StringAlignmentCenter, StringAlignmentCenter, false);
            Hit(ID_SAVE, L.saveBtn);

            FillRound(gr, L.delBtn, 7, a.hot == ID_DEL ? Color(255, 140, 52, 52) : Color(255, 46, 38, 42));
            Text(gr, L"删除", a.fTiny, L.delBtn, a.hot == ID_DEL ? COL_RED : Color(255, 214, 150, 150),
                 StringAlignmentCenter, StringAlignmentCenter, false);
            Hit(ID_DEL, L.delBtn);
        }

        // ---------------- 校正
        Panel(gr, L.corrPanel, L"耳机校正曲线");
        FillRound(gr, L.corrBox, 8, a.corrOpen ? Color(255, 35, 43, 58) : COL_PANEL2);
        StrokeRound(gr, L.corrBox, 8, (a.hot == ID_CORR || a.corrOpen) ? COL_ACCENT : COL_LINE);
        Text(gr, kCorrections[s.correction].name, a.fBody,
             Rc(L.corrBox.left + 12, L.corrBox.top, W_(L.corrBox) - 46, H_(L.corrBox)), COL_TEXT);
        {
            Pen p(COL_DIM, 1.6f);
            const int ax = L.corrBox.right - 22, ay = L.corrBox.top + 14;
            gr.DrawLine(&p, (REAL)(ax - 5), (REAL)ay, (REAL)ax, (REAL)(ay + 5));
            gr.DrawLine(&p, (REAL)ax, (REAL)(ay + 5), (REAL)(ax + 5), (REAL)ay);
        }
        Hit(ID_CORR, L.corrBox);

        // ---------------- 游戏卡
        Panel(gr, L.gamePanel, L"游戏音频");
        {
            const std::wstring gname = a.detectedGame.empty() ? L"未检测到游戏进程" : a.detectedGame;
            Text(gr, gname, a.fSmall, Rc(L.gamePanel.left + 16, L.gamePanel.top + 38, W_(L.gamePanel) - 32, 20),
                 a.detectedGame.empty() ? COL_DIM : COL_GOOD);

            const bool canCap = (a.detectedPid != 0) || a.capturing;
            FillRound(gr, L.capBtn, 9,
                      a.capturing ? Color(255, 122, 46, 46)
                                  : (canCap ? (a.hot == ID_CAP ? Color(255, 96, 156, 255) : COL_ACCENT)
                                            : Color(255, 34, 41, 53)));
            Text(gr, a.capturing ? L"停止处理游戏音频" : L"开始处理游戏音频", a.fBold, L.capBtn,
                 canCap ? Color(255, 255, 255, 255) : Color(255, 110, 120, 135), StringAlignmentCenter, StringAlignmentCenter, false);
            Hit(ID_CAP, L.capBtn);

            Text(gr, L"缓冲策略", a.fTiny, Rc(L.gamePanel.left + 16, L.bufBtn[0].top + 4, 76, 18), COL_DIM);
            const wchar_t* bn[3] = { L"超低", L"低", L"稳" };
            for (int i = 0; i < 3; ++i) {
                const bool on = (a.bufMode == i);
                FillRound(gr, L.bufBtn[i], 7, on ? Color(255, 39, 68, 112) : COL_PANEL2);
                if (on) StrokeRound(gr, L.bufBtn[i], 7, COL_ACCENT);
                Text(gr, bn[i], a.fTiny, L.bufBtn[i], on ? COL_TEXT : COL_DIM,
                     StringAlignmentCenter, StringAlignmentCenter, false);
                Hit(ID_BUF0 + i, L.bufBtn[i]);
            }

            FillRound(gr, L.apoBtn, 8, a.hot == ID_APO ? Color(255, 33, 50, 46) : Color(255, 25, 32, 42));
            StrokeRound(gr, L.apoBtn, 8, Color(255, 44, 70, 62));
            Text(gr, L"导出零延迟 APO 配置", a.fTiny, L.apoBtn,
                 a.hot == ID_APO ? COL_ACC2 : Color(255, 118, 176, 164), StringAlignmentCenter, StringAlignmentCenter, false);
            Hit(ID_APO, L.apoBtn);

            if (H_(L.gamePanel) > 220) {
                Text(gr, L"APO 在系统音频引擎内处理，", a.fTiny,
                     Rc(L.gamePanel.left + 16, L.gamePanel.top + 194, W_(L.gamePanel) - 32, 18), COL_DIM, StringAlignmentNear, StringAlignmentNear, false);
                Text(gr, L"网卡延迟 0ms，FPS 首选。", a.fTiny,
                     Rc(L.gamePanel.left + 16, L.gamePanel.top + 212, W_(L.gamePanel) - 32, 18), COL_DIM, StringAlignmentNear, StringAlignmentNear, false);
            }
        }

        // ---------------- EQ
        Panel(gr, L.eqPanel, L"10 段图形均衡器");
        {
            wchar_t buf[160];
            swprintf(buf, 160, L"%s", kCorrections[s.correction].desc);
            Text(gr, buf, a.fTiny, Rc(L.eqPanel.right - 320, L.eqPanel.top + 10, 300, 22), COL_DIM, StringAlignmentFar);

            const int pad = 22;
            const int top = L.eqTop, bot = L.eqBot;
            const int midY = (top + bot) / 2;

            for (int i = 0; i <= 4; ++i) {
                const int y = top + (bot - top) * i / 4;
                SolidBrush lb(Color(22, 90, 110, 150));
                gr.FillRectangle(&lb, (REAL)(L.eqPanel.left + pad - 6), (REAL)y, (REAL)(W_(L.eqPanel) - pad * 2 + 12), 1.f);
                wchar_t db[16];
                const int val = 12 - i * 6;
                if (i == 0) wcscpy(db, L"+12");
                else if (i == 4) wcscpy(db, L"-12");
                else swprintf(db, 16, L"%+d", val);
                Text(gr, db, a.fTiny, Rc(L.eqPanel.left + 2, y - 8, 18, 16),
                     Color(90, 150, 165, 190), StringAlignmentCenter, StringAlignmentCenter, false);
            }

            if (bot > top) {
                GraphicsPath curve;
                for (int b = 0; b < kBands; ++b) {
                    const float x0 = (float)(L.eqPanel.left + pad + L.eqColW * b + L.eqColW / 2);
                    const float y0 = (float)(midY - s.eq[b] / 12.0 * ((bot - top) / 2.0));
                    if (b == 0) { curve.StartFigure(); curve.AddLine(x0, y0, x0, y0); }
                    else {
                        const float xp = (float)(L.eqPanel.left + pad + L.eqColW * (b - 1) + L.eqColW / 2);
                        const float yp = (float)(midY - s.eq[b - 1] / 12.0 * ((bot - top) / 2.0));
                        for (int k = 1; k <= 14; ++k) {
                            const float t = k / 14.f;
                            const float st = t * t * (3.f - 2.f * t);   // smoothstep
                            curve.AddLine(xp + (x0 - xp) * (k - 1) / 14.f, yp + (y0 - yp) * (k - 1) / 14.f,
                                          xp + (x0 - xp) * st,     yp + (y0 - yp) * st);
                        }
                    }
                }
                Pen glow(Color(52, 78, 141, 255), 8.f);
                gr.DrawPath(&glow, &curve);
                Pen pen(COL_ACCENT, 2.2f);
                gr.DrawPath(&pen, &curve);
            }

            for (int b = 0; b < kBands; ++b) {
                RECT tr = Rc(L.eqPanel.left + pad + L.eqColW * b + L.eqColW / 2 - 12, top, 24, bot - top);
                VSlider(gr, tr, (s.eq[b] + 12.f) / 24.f, a.active == ID_EQ0 + b || a.hot == ID_EQ0 + b);
                wchar_t vb[24];
                swprintf(vb, 24, L"%+.1f", s.eq[b]);
                Text(gr, vb, a.fTiny, Rc(L.eqPanel.left + pad + L.eqColW * b - 8, bot + 2, L.eqColW + 16, 16),
                     fabsf(s.eq[b]) > 0.05f ? COL_TEXT : COL_DIM, StringAlignmentCenter, StringAlignmentCenter, false);
                Text(gr, kBandLabel[b], a.fTiny, Rc(L.eqPanel.left + pad + L.eqColW * b - 10, bot + 18, L.eqColW + 20, 16),
                     COL_DIM, StringAlignmentCenter, StringAlignmentCenter, false);
                Hit(ID_EQ0 + b, tr);
            }
        }

        // ---------------- 旋钮
        Panel(gr, L.knobPanel, L"音色塑造");
        {
            const float vals[5] = { s.bass / 10.f, (s.vocal + 6.f) / 12.f, (s.ambience + 6.f) / 12.f,
                                    s.width / 2.f, s.dynamics };
            const wchar_t* labels[5] = { L"低音增强", L"清晰度", L"氛围感", L"声场宽度", L"动态增强" };
            wchar_t vb[5][24];
            swprintf(vb[0], 24, L"%+.1f", s.bass);
            swprintf(vb[1], 24, L"%+.1f", s.vocal);
            swprintf(vb[2], 24, L"%+.1f", s.ambience);
            swprintf(vb[3], 24, L"%.2f", s.width);
            swprintf(vb[4], 24, L"%.0f%%", s.dynamics * 100.f);
            for (int i = 0; i < 5; ++i) {
                KnobDraw(gr, L.knob[i], vals[i], labels[i], vb[i],
                         a.active == ID_KNOB0 + i || a.hot == ID_KNOB0 + i);
                Hit(ID_KNOB0 + i, L.knob[i]);
            }
            Text(gr, L"串扰", a.fTiny, Rc(L.xf.left - 46, L.xf.top - 2, 44, 18), COL_DIM, StringAlignmentFar);
            HSlider(gr, L.xf, s.crossfeed, a.hot == ID_XF || a.active == ID_XF);
            Hit(ID_XF, L.xf);

            {
                wchar_t vv[24];
                swprintf(vv, 24, L"音量 %d%%", s.volumePercent);
                Text(gr, vv, a.fTiny, Rc(L.volSlider.left - 82, L.volSlider.top - 2, 76, 18), COL_DIM, StringAlignmentFar);
            }
            HSlider(gr, L.volSlider, s.volumePercent / 100.f, a.hot == ID_VOL || a.active == ID_VOL);
            Hit(ID_VOL, Rc(L.volSlider.left, L.volSlider.top - 6, W_(L.volSlider), H_(L.volSlider) + 12));
        }

        // ---------------- 频谱
        Panel(gr, L.specPanel, L"实时频谱 / 电平");
        {
            const int pad = 18;
            const int top = L.specPanel.top + 36;
            const int bot = L.specPanel.bottom - 14;
            if (bot > top) {
                const int bw = (W_(L.specPanel) - pad * 2 - 24) / kSpectrumBars;
                for (int i = 0; i < kSpectrumBars; ++i) {
                    const float v = a.bars[i];
                    const int hgt = (int)(v * (bot - top));
                    if (hgt <= 0) continue;
                    RECT br = Rc(L.specPanel.left + pad + i * bw + 1, bot - hgt, std::max(2, bw - 2), hgt);
                    const double tt = (double)hgt / (double)(bot - top);
                    Color c1 = Color(255, (BYTE)(54 + 100 * tt), (BYTE)(211 - 60 * tt), (BYTE)(191 + 60 * tt));
                    SolidBrush sb(c1);
                    gr.FillRectangle(&sb, (REAL)br.left, (REAL)br.top, (REAL)W_(br), (REAL)H_(br));
                }
                const int mx = L.specPanel.left + W_(L.specPanel) - pad;
                const int h1 = (int)(a.meterL * (bot - top));
                const int h2 = (int)(a.meterR * (bot - top));
                if (h1 > 0) FillRectC(gr, Color(255, 78, 141, 255), (REAL)(mx - 12), (REAL)(bot - h1), 5.f, (REAL)h1);
                if (h2 > 0) FillRectC(gr, Color(255, 54, 211, 191), (REAL)(mx - 5), (REAL)(bot - h2), 5.f, (REAL)h2);
            }
        }

        // ---------------- 校正下拉
        if (a.corrOpen) {
            const int itemH = 30;
            RECT list = Rc(L.corrBox.left, L.corrBox.bottom + 4, W_(L.corrBox), itemH * kCorrectionCount + 8);
            FillRound(gr, list, 10, Color(255, 26, 33, 44));
            StrokeRound(gr, list, 10, COL_ACCENT);
            for (int i = 0; i < kCorrectionCount; ++i) {
                RECT ir = Rc(list.left + 6, list.top + 4 + i * itemH, W_(list) - 12, itemH);
                if (i == a.settings.correction) FillRound(gr, ir, 6, Color(255, 36, 54, 86));
                else if (a.hot == ID_CORRITEM0 + i) FillRound(gr, ir, 6, Color(90, 40, 48, 64));
                Text(gr, kCorrections[i].name, a.fSmall, Rc(ir.left + 10, ir.top, W_(ir) - 20, H_(ir)), COL_TEXT);
                Hit(ID_CORRITEM0 + i, ir);
            }
        }

        // ---------------- 提示
        if (!a.toast.empty() && GetTickCount() < a.toastUntil) {
            RECT tr = Rc(a.W / 2 - 240, a.H - 74, 480, 34);
            FillRound(gr, tr, 10, Color(242, 30, 38, 52));
            StrokeRound(gr, tr, 10, COL_ACCENT);
            Text(gr, a.toast, a.fSmall, tr, COL_TEXT, StringAlignmentCenter, StringAlignmentCenter, false);
        }
    }

    BitBlt(dc, 0, 0, a.W, a.H, a.memdc, 0, 0, SRCCOPY);
    ReleaseDC(a.hwnd, dc);
}

// ------------------------------------------------------------------ 输入
static int FindHit(int x, int y) {
    for (int i = (int)gHits.size() - 1; i >= 0; --i) {
        const RECT& r = gHits[i].second;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return gHits[i].first;
    }
    return ID_NONE;
}

static void SetEqFromY(int band, int y) {
    const Layout& L = A.lay;
    float v = (float)(L.eqBot - y) / (float)(L.eqBot - L.eqTop);
    v = Clamp01(v);
    A.settings.eq[band] = v * 24.f - 12.f;
    ApplyToEngine();
}

static void OnDown(int x, int y) {
    const int id = FindHit(x, y);
    A.active = id;
    const Layout& L = A.lay;

    if (id != ID_NAME) {
        A.nameEdit = false;
        if (A.customName.empty()) A.customName = L"我的曲线";
    }
    if (id == ID_NAME) {
        if (!A.nameEdit) { A.customName.clear(); A.nameEdit = true; }  // 首次点进来先清空，直接打新名字
        SetFocus(A.hwnd);
        InvalidateRect(A.hwnd, nullptr, FALSE);
    } else if (id >= ID_EQ0 && id < ID_EQ0 + kBands) {
        SetEqFromY(id - ID_EQ0, y);
    } else if (id == ID_VOL) {
        A.settings.volumePercent = (int)(Clamp01((float)(x - L.volSlider.left) / (float)W_(L.volSlider)) * 100.f);
        A.engine.setVolumePercent(A.settings.volumePercent);
    } else if (id == ID_XF) {
        A.settings.crossfeed = Clamp01((float)(x - L.xf.left) / (float)W_(L.xf));
        ApplyToEngine();
    } else if (id >= ID_KNOB0 && id < ID_KNOB0 + 5) {
        A.dragY = y;
        const int i = id - ID_KNOB0;
        const float v[5] = { A.settings.bass / 10.f, (A.settings.vocal + 6.f) / 12.f,
                             (A.settings.ambience + 6.f) / 12.f, A.settings.width / 2.f, A.settings.dynamics };
        A.dragVal = v[i];
    }
    InvalidateRect(A.hwnd, nullptr, FALSE);
}

static void OnMove(int x, int y, bool down) {
    if (!down) {
        const int id = FindHit(x, y);
        if (id != A.hot) { A.hot = id; InvalidateRect(A.hwnd, nullptr, FALSE); }
        return;
    }
    const int id = A.active;
    const Layout& L = A.lay;
    if (id >= ID_EQ0 && id < ID_EQ0 + kBands) {
        SetEqFromY(id - ID_EQ0, y);
        InvalidateRect(A.hwnd, nullptr, FALSE);
    } else if (id == ID_VOL) {
        A.settings.volumePercent = (int)(Clamp01((float)(x - L.volSlider.left) / (float)W_(L.volSlider)) * 100.f);
        A.engine.setVolumePercent(A.settings.volumePercent);
        InvalidateRect(A.hwnd, nullptr, FALSE);
    } else if (id == ID_XF) {
        A.settings.crossfeed = Clamp01((float)(x - L.xf.left) / (float)W_(L.xf));
        ApplyToEngine();
        InvalidateRect(A.hwnd, nullptr, FALSE);
    } else if (id >= ID_KNOB0 && id < ID_KNOB0 + 5) {
        const float dv = (float)(A.dragY - y) / 150.f;
        float v = Clamp01(A.dragVal + dv);
        const int i = id - ID_KNOB0;
        switch (i) {
        case 0: A.settings.bass = v * 10.f; break;
        case 1: A.settings.vocal = v * 12.f - 6.f; break;
        case 2: A.settings.ambience = v * 12.f - 6.f; break;
        case 3: A.settings.width = v * 2.f; break;
        case 4: A.settings.dynamics = v; break;
        }
        ApplyToEngine();
        InvalidateRect(A.hwnd, nullptr, FALSE);
    }
}

static void OnUp(int x, int y) {
    const int id = FindHit(x, y);
    if (id == A.active) {
        if (id == ID_MIN) {
            ShowWindow(A.hwnd, SW_MINIMIZE);
        } else if (id == ID_CLOSE) {
            if (A.settings.closeToTray) {
                ShowWindow(A.hwnd, SW_HIDE);
                Toast(L"AuraTune 仍在托盘中运行", 1800);
            } else {
                PostMessageW(A.hwnd, WM_CLOSE, 0, 0);
            }
        } else if (id == ID_MAX) {
            if (A.fullscreen) ToggleFullscreen();
            ToggleMaximize();
        } else if (id == ID_TAB_MUSIC) {
            SwitchMode(MODE_MUSIC);
        } else if (id == ID_TAB_GAME) {
            SwitchMode(MODE_GAME);
        } else if (id == ID_NAME) {
            A.nameEdit = true;
        } else if (id == ID_SAVE) {
            SaveAsCustom();
        } else if (id == ID_DEL) {
            DeleteSelectedCustom();
        } else if (id >= ID_PRESET0 + 1000 && id < ID_PRESET0 + 1000 + 64) {
            const int ci = id - (ID_PRESET0 + 1000);
            if (ci >= 0 && (int)ci < (int)A.settings.customs.size()) {
                A.settings.applyCustom(ci);
                ApplyToEngine();
                SaveSettings();
                InvalidateRect(A.hwnd, nullptr, FALSE);
            }
        } else if (id >= ID_PRESET0 && id < ID_PRESET0 + 64) {
            const int k = id - ID_PRESET0;
            if (k >= 0 && k < kPresetCount) {
                ApplyPresetIndex(k);
            }
        } else if (id == ID_CORR) {
            A.corrOpen = !A.corrOpen;
        } else if (id >= ID_CORRITEM0 && id < ID_CORRITEM0 + 64) {
            const int i = id - ID_CORRITEM0;
            if (i < kCorrectionCount) {
                A.settings.correction = i;
                ApplyToEngine();
                SaveSettings();
            }
            A.corrOpen = false;
        } else if (id == ID_CAP) {
            ToggleCapture();
        } else if (id == ID_APO) {
            ExportApo();
        } else if (id >= ID_BUF0 && id < ID_BUF0 + 3) {
            A.bufMode = id - ID_BUF0;
            A.bufUserSet = true;
            A.engine.setBufferMode(A.bufMode);
            A.engine.requestReconnect();
            Toast(A.bufMode == 0 ? L"超低缓冲：延迟最低，若爆音请改「低」"
                                 : (A.bufMode == 1 ? L"低缓冲：延迟与稳定平衡" : L"稳：最大抗爆音能力"));
        } else if (A.corrOpen) {
            A.corrOpen = false;
        }
    }
    A.active = ID_NONE;
    InvalidateRect(A.hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------------ 图标 / 托盘
static HICON MakeIcon(int size) {
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    {
        Graphics gr(&bmp);
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        gr.Clear(Color(0, 0, 0, 0));
        SolidBrush bg(Color(255, 22, 28, 40));
        gr.FillEllipse(&bg, 1.f, 1.f, (REAL)(size - 2), (REAL)(size - 2));
        Pen ring(COL_ACCENT, (REAL)std::max(1.0f, size / 16.0f));
        gr.DrawEllipse(&ring, 2.f, 2.f, (REAL)(size - 4), (REAL)(size - 4));
        Pen wave(COL_ACC2, (REAL)std::max(1.5f, size / 11.0f));
        wave.SetStartCap(LineCapRound); wave.SetEndCap(LineCapRound);
        const float cy = size / 2.f;
        const float xs[7] = { 0.22f, 0.35f, 0.48f, 0.62f, 0.76f };
        const float hs[5] = { 0.16f, 0.30f, 0.40f, 0.26f, 0.14f };
        for (int i = 0; i < 5; ++i) {
            const float x = xs[i] * size;
            gr.DrawLine(&wave, x, cy - hs[i] * size, x, cy + hs[i] * size);
        }
    }
    HICON ic = nullptr;
    bmp.GetHICON(&ic);
    return ic;
}

static void AddTray(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = A.hIconSmall ? A.hIconSmall : LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wcscpy(nid.szTip, L"AuraTune 耳机调音台");
    Shell_NotifyIconW(NIM_ADD, &nid);
    A.trayAdded = true;
}

static void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"显示主界面");
    AppendMenuW(m, MF_STRING, 2, L"音乐 / 游戏 切换");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 3, L"退出");
    SetForegroundWindow(hwnd);
    const UINT cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1) {
        ShowWindow(hwnd, SW_SHOW);
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    } else if (cmd == 2) {
        SwitchMode(A.settings.mode == MODE_MUSIC ? MODE_GAME : MODE_MUSIC);
    } else if (cmd == 3) {
        A.quitting = true;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

// ------------------------------------------------------------------ 字体
static Font* MakeFont(float px, int style, const wchar_t* a1, const wchar_t* a2) {
    Font* f = new Font(a1, px, style, UnitPixel);
    if (f->GetLastStatus() != Ok) {
        delete f;
        f = new Font(a2, px, style, UnitPixel);
    }
    return f;
}

static void InitFonts() {
    A.fTiny      = MakeFont(11.5f, FontStyleRegular,    L"Microsoft YaHei UI", L"Segoe UI");
    A.fSmall     = MakeFont(13.0f, FontStyleRegular,    L"Microsoft YaHei UI", L"Segoe UI");
    A.fBody      = MakeFont(14.0f, FontStyleRegular,    L"Microsoft YaHei UI", L"Segoe UI");
    A.fBold      = MakeFont(14.0f, FontStyleBold,       L"Microsoft YaHei UI", L"Segoe UI");
    A.fSmallBold = MakeFont(13.0f, FontStyleBold,       L"Microsoft YaHei UI", L"Segoe UI");
    A.fTitle     = MakeFont(23.0f, FontStyleBold,       L"Microsoft YaHei UI", L"Segoe UI");
}

// ------------------------------------------------------------------ 窗口过程
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        A.hwnd = hwnd;
        SetTimer(hwnd, TIMER_UI, 16, nullptr);
        return 0;
    }
    case WM_SIZE: {
        if (wParam != SIZE_MINIMIZED) {
            A.W = LOWORD(lParam);
            A.H = HIWORD(lParam);
            RebuildLayout(A);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const bool zoomed = IsZoomed(hwnd) != 0;
        const int border = (A.fullscreen || zoomed) ? 0 : 6;
        const bool left = pt.x < border, right = pt.x >= rc.right - border;
        const bool top = pt.y < border, bottom = pt.y >= rc.bottom - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        if (pt.y < 74) {
            if (FindHit(pt.x, pt.y) == ID_NONE) return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = 960;
        mm->ptMinTrackSize.y = 620;
        return 0;
    }
    case WM_CHAR: {
        if (!A.nameEdit) break;
        const wchar_t c = (wchar_t)wParam;
        if (c == 8 || c == 127) {
            if (!A.customName.empty()) A.customName.pop_back();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (c == 27) { A.nameEdit = false; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (c == L'\r') { A.nameEdit = false; SaveAsCustom(); return 0; }
        if (c >= 32 && A.customName.size() < 20) {
            A.customName.push_back(c);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (A.nameEdit) {
            if (wParam == VK_RETURN) { A.nameEdit = false; SaveAsCustom(); return 0; }
            if (wParam == VK_ESCAPE) { A.nameEdit = false; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wParam == VK_BACK) {
                if (!A.customName.empty()) A.customName.pop_back();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }
        if (wParam == VK_F11) { if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE); ToggleFullscreen(); return 0; }
        if (wParam == VK_ESCAPE && A.fullscreen) { ToggleFullscreen(); return 0; }
        if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000)) { ToggleFullscreen(); return 0; }
        break;
    }
    case WM_NCLBUTTONDBLCLK: {
        if (wParam == HTCAPTION) {
            if (A.fullscreen) ToggleFullscreen();
            else ToggleMaximize();
            return 0;
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (y < 74 && FindHit(x, y) == ID_NONE) {
            if (A.fullscreen) ToggleFullscreen();
            else ToggleMaximize();
            return 0;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        Paint(A);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!A.hwnd) break;
        const bool down = (wParam & MK_LBUTTON) != 0;
        OnMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), down && A.active != ID_NONE);
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        A.hot = ID_NONE;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        OnDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        OnUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        ReleaseCapture();
        return 0;
    case WM_MOUSEWHEEL: {
        const short dz = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        // 预设面板内滚动（预设放不下时）
        if (A.lay.presetN > A.lay.presetVisible && PtInRect(&A.lay.presetPanel, pt)) {
            A.presetScroll += (dz > 0 ? -1 : 1);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&A.lay.eqPanel, pt) && A.lay.eqBot > A.lay.eqTop) {
            // 均衡器区域滚轮微调：命中哪一段就调哪一段
            const int pad = 22;
            for (int b = 0; b < kBands; ++b) {
                RECT tr = Rc(A.lay.eqPanel.left + pad + A.lay.eqColW * b - 4, A.lay.eqTop,
                             A.lay.eqColW + 8, A.lay.eqBot - A.lay.eqTop);
                if (PtInRect(&tr, pt)) {
                    A.settings.eq[b] = std::max(-12.f, std::min(12.f, A.settings.eq[b] + (dz > 0 ? 0.5f : -0.5f)));
                    ApplyToEngine();
                    SaveSettings();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
        }
        const int nv = std::min(100, std::max(0, A.settings.volumePercent + (dz > 0 ? 2 : -2)));
        if (nv != A.settings.volumePercent) {
            A.settings.volumePercent = nv;
            A.engine.setVolumePercent(nv);
            if (A.toast.empty() || true) { wchar_t tb[32]; swprintf(tb, 32, L"音量 %d%%", nv); Toast(tb, 1200); }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_DROPFILES: {
        DragFinish((HDROP)wParam);
        Toast(L"这个版本不再内置播放器，请用你的音乐软件放歌");
        return 0;
    }
    case WM_TIMER: {
        if (wParam != TIMER_UI) break;
        static int tick = 0;
        ++tick;

        FlushSave();

        MeterData m = A.engine.meter();
        A.meterL = A.meterL * 0.6f + std::min(1.f, m.rmsL * 3.2f) * 0.4f;
        A.meterR = A.meterR * 0.6f + std::min(1.f, m.rmsR * 3.2f) * 0.4f;
        if (!A.barsInit) { for (int i = 0; i < kSpectrumBars; ++i) A.bars[i] = 0.f; A.barsInit = true; }
        for (int i = 0; i < kSpectrumBars; ++i) {
            const float t = m.bars[i];
            A.bars[i] += (t - A.bars[i]) * 0.35f;
            if (A.bars[i] < 0.002f) A.bars[i] = 0.f;
        }

        A.capturing = A.engine.isCapturing();

        if (tick % 120 == 0) {
            DWORD pid = 0;
            std::wstring gname = DetectRunningGame(&pid);
            if (gname != A.detectedGame || pid != A.detectedPid) {
                A.detectedGame = gname;
                A.detectedPid = pid;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (A.settings.autoSwitch) {
                if (!gname.empty() && A.settings.mode == MODE_MUSIC) {
                    SwitchMode(MODE_GAME);
                    A.autoSwitched = true;
                } else if (gname.empty() && A.settings.mode == MODE_GAME && A.autoSwitched) {
                    SwitchMode(MODE_MUSIC);
                    A.autoSwitched = false;
                }
            }
        }
        // 只重画频谱和提示条，别整窗重画（切模式/拖旋钮才整体重画）
        {
            RECT sp = A.lay.specPanel;
            InflateRect(&sp, 2, 2);
            InvalidateRect(hwnd, &sp, FALSE);
            RECT tb = Rc(A.W / 2 - 244, A.H - 78, 488, 42);
            InvalidateRect(hwnd, &tb, FALSE);
        }
        return 0;
    }
    case WM_TRAY:
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE: {
        if (!A.quitting) {
            A.settings.save();
            A.engine.stop();
            if (A.trayAdded) {
                NOTIFYICONDATAW nid{};
                nid.cbSize = sizeof(nid);
                nid.hWnd = hwnd;
                nid.uID = 1;
                Shell_NotifyIconW(NIM_DELETE, &nid);
            }
            KillTimer(hwnd, TIMER_UI);
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int RunAuraTune(HINSTANCE hInst) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return 1;

    A.settings.load();
    A.bufMode = (A.settings.mode == MODE_GAME) ? 0 : 1;

    InitFonts();
    A.hIconBig = MakeIcon(64);
    A.hIconSmall = MakeIcon(16);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWndClass;
    wc.hIcon = A.hIconBig;
    wc.hIconSm = A.hIconSmall;
    RegisterClassExW(&wc);

    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int maxW = (wa.right - wa.left) - 40;
    const int maxH = (wa.bottom - wa.top) - 40;
    // 默认按 16:9 给一个正常比例，再按工作区夹取（不贴满、不超屏）
    int dw = 1200, dh = 675;
    if (dw > maxW) { dw = maxW; dh = dw * 9 / 16; }
    if (dh > maxH) { dh = maxH; dw = dh * 16 / 9; }
    if (dw < 960) dw = 960;
    if (dh < 620) dh = 620;
    A.W = dw; A.H = dh;
    RECT wr{ 0, 0, A.W, A.H };
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, kWndClass, L"AuraTune 耳机调音台",
                                WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    // 圆角 + 阴影
    {
        DWORD corner = 2;   // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
        MARGINS mg{ 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &mg);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /*IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    }

    AddTray(hwnd);
    A.engine.start();
    ApplyToEngine();
    A.engine.setBufferMode(A.bufMode);

    // 自动曲线名从已有序号往后排
    if (!A.settings.customs.empty()) {
        wchar_t d[64];
        swprintf(d, 64, L"我的曲线 %d", (int)A.settings.customs.size() + 1);
        A.customName = d;
    }

    // 居中
    {
        RECT rc;
        GetWindowRect(hwnd, &rc);
        const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hwnd, nullptr, (sw - (rc.right - rc.left)) / 2, (sh - (rc.bottom - rc.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    if (A.settings.startMinimized) ShowWindow(hwnd, SW_HIDE);
    else { ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    A.engine.stop();
    if (A.hIconBig) DestroyIcon(A.hIconBig);
    if (A.hIconSmall) DestroyIcon(A.hIconSmall);
    GdiplusShutdown(token);
    CoUninitialize();
    return 0;
}
