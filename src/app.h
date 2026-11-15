// AuraTune 耳机调音台 - 公共定义
// 纯 Win32 + WASAPI + Media Foundation + GDI+，无第三方依赖
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

constexpr int   kSampleRate = 48000;
constexpr int   kBands      = 10;
constexpr int   kSpectrumBars = 48;

// 10 段均衡器中心频率
extern const float  kBandFreq[kBands];
extern const wchar_t* kBandLabel[kBands];

enum AppMode { MODE_MUSIC = 0, MODE_GAME = 1 };

// ------------------------------------------------------- 耳机校正（AutoEq 风格目标曲线，近似值）
struct Correction {
    const wchar_t* name;
    const wchar_t* desc;
    float eq[kBands];
};
extern const Correction kCorrections[];
extern const int        kCorrectionCount;

// ------------------------------------------------------------------ 预设
struct Preset {
    const wchar_t* name;
    const wchar_t* desc;
    int   mode;
    float eq[kBands];
    float bass;             // 低音增强 dB
    float vocal;            // 清晰度/人声 dB
    float ambience;         // 氛围/空气感 dB (12k 架 + 扩散)
    float width;            // 0..2 立体声宽度 (环绕感)
    float dynamics;         // 0..1 动态增强
    int   correction;       // 默认耳机校正索引
};

extern const Preset kPresets[];
extern const int    kPresetCount;

// ------------------------------------------------------------------ 设置
struct Settings {
    int   mode          = MODE_MUSIC;
    int   presetMusic   = 1;
    int   presetGame    = 9;
    float eq[kBands]    = {0,2,1.2f,0.5f,-0.5f,0,0.8f,0.5f,0,0.5f};
    float bass          = 1.5f;
    float vocal         = 0.5f;
    float ambience      = 0.6f;
    float width         = 1.08f;
    float dynamics      = 0.30f;
    float crossfeed     = 0.35f;
    int   correction    = 1;      // 耳机校正
    float vol           = 0.0f;   // dB, -30..+12
    float balance       = 0.0f;   // -1..1
    bool  bypass        = false;
    bool  autoSwitch    = true;
    int   volumePercent = 80;
    bool  startMinimized= false;
    bool  closeToTray   = true;
    std::wstring lastFolder;
    std::wstring lastTrack;
    float lastSeek      = 0.0f;

    void applyPreset(int idx);
    void load();
    void save() const;
};

std::wstring ExeDir();
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring FormatTime(double sec);

// 游戏进程检测表
struct GameProcess { const wchar_t* exe; const wchar_t* label; };
extern const GameProcess kGames[];
extern const int kGameCount;
std::wstring DetectRunningGame(DWORD* pidOut);
