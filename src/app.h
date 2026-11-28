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
constexpr int   kBands      = 10;      // 预设/校正固定用 10 段
constexpr int   kMaxBands   = 31;      // 图形均衡器最多 31 段
constexpr int   kMaxParams  = 6;       // 参量均衡器最多 6 个滤波器
constexpr int   kSpectrumBars = 48;

// 10 段均衡器中心频率
extern const float  kBandFreq[kBands];
extern const wchar_t* kBandLabel[kBands];

// 图形均衡器频段表：count 只能是 10 / 15 / 31
const float* BandFreqs(int count, int* n);
// 频段标签（如 "1k"），返回静态缓冲，仅用于界面绘制
const wchar_t* BandLabel(int count, int i);
// 把 count 段的频段序号换算到 Hz
float BandFreqAt(int count, int i);
// 在「count 段」曲线上取 f 处的增益（对数频率线性插值），用于预设映射/画曲线
float EqGainAt(const float* eq, int count, float f);

// ------------------------------------------------ 参量均衡器滤波器类型
enum ParamType {
    PT_PEAK = 0, PT_LOW_SHELF, PT_HIGH_SHELF, PT_LOW_PASS,
    PT_HIGH_PASS, PT_NOTCH, PT_ALL_PASS, PT_BAND_PASS,
    // v2.2 补齐：一阶（6dB/oct）版本、LR4 版本、倾斜
    PT_LOW_SHELF_1, PT_HIGH_SHELF_1, PT_LOW_PASS_1, PT_HIGH_PASS_1,
    PT_LOW_PASS_LR4, PT_HIGH_PASS_LR4, PT_TILT
};
extern const wchar_t* kParamTypeName[];
constexpr int kParamTypeCount = 15;

struct ParamFilter {
    bool  on   = false;
    int   type = PT_PEAK;
    float freq = 1000.f;
    float gain = 0.f;      // dB，仅 peak/shelf 使用
    float q    = 1.0f;
};

// ------------------------------------------------ 压缩模式 / 空间模式 / 测试音
enum CompMode { COMP_OFF = 0, COMP_SOFT, COMP_MEDIUM, COMP_FIRM };
extern const wchar_t* kCompModeName[];
constexpr int kCompModeCount = 4;

enum SpatialMode { SP_OFF = 0, SP_8D, SP_10D, SP_FIG8 };
extern const wchar_t* kSpatialModeName[];
constexpr int kSpatialModeCount = 4;

enum TestTone { TT_OFF = 0, TT_SINE, TT_PINK, TT_SWEEP };
extern const wchar_t* kTestToneName[];
constexpr int kTestToneCount = 4;

enum AppMode { MODE_MUSIC = 0, MODE_GAME = 1, MODE_MOVIE = 2, MODE_ANIME = 3 };

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

// -------------------------------------------------- 用户自定义曲线（可起名、可删除）
struct CustomPreset {
    std::wstring name;
    int   mode = MODE_MUSIC;
    int   eqCount = kBands;
    float eq[kMaxBands] = {};
    float bass = 0.f, vocal = 0.f, ambience = 0.f, width = 1.f, dynamics = 0.3f;
    float crossfeed = 0.35f;
    int   correction = 0;
};

constexpr int kMaxCustom = 40;

// ------------------------------------------------------------------ 设置
struct Settings {
    int   mode          = MODE_MUSIC;
    int   presetMusic   = 1;
    int   presetGame    = 9;
    int   presetMovie   = 22;   // 电影模式默认预设（剧情 / 文艺）
    int   presetAnime   = 24;   // 动漫模式默认预设（番剧 对白）
    int   eqCount       = kBands;          // 10 / 15 / 31
    float eq[kMaxBands] = {0,2,1.2f,0.5f,-0.5f,0,0.8f,0.5f,0,0.5f};
    float bass          = 1.5f;
    float vocal         = 0.5f;
    float ambience      = 0.6f;
    float width         = 1.08f;
    float dynamics      = 0.30f;
    float crossfeed     = 0.35f;
    int   correction    = 1;      // 耳机校正
    float vol           = 0.0f;   // dB, -30..+12
    float preamp        = 0.0f;   // dB, -12..+12 主增益（Enhanced EQ 的 Master Gain）
    float balance       = 0.0f;   // -1..1
    bool  bypass        = false;
    bool  autoSwitch    = true;

    // 参量均衡器（默认关闭，但预先给好常用频点，点开就能用）
    ParamFilter params[kMaxParams] = {
        { false, PT_LOW_SHELF,  100.f,   0.f, 0.70f },
        { false, PT_PEAK,        60.f,   0.f, 1.20f },
        { false, PT_PEAK,       250.f,   0.f, 1.00f },
        { false, PT_PEAK,      1200.f,   0.f, 1.00f },
        { false, PT_PEAK,      3200.f,   0.f, 1.00f },
        { false, PT_HIGH_SHELF, 9000.f,  0.f, 0.70f }
    };

    // 低频分频 / 独立低音声道
    float xoFreq        = 0.f;    // 0 = 关；20..300 Hz（LR4 分频，低频并成单声道）
    float subGain       = 0.f;    // -6..+12 dB

    // 压缩
    int   compMode      = COMP_OFF;
    // 空间音频（8D/10D/8字）
    int   spatialMode   = SP_OFF;
    float spatialRate   = 0.12f;  // 0.02..0.6 Hz
    float spatialDepth  = 0.5f;   // 0..1
    // 卷积（IR 导入）
    bool  irOn          = false;
    std::wstring irPath;
    float irMix         = 1.0f;   // 0..1
    // 测试音
    int   testTone      = TT_OFF;
    float testFreq      = 1000.f;
    float testLevel     = -20.f;  // dBFS
    // 音乐模式捕获目标
    int   musicPid      = 0;
    std::wstring musicApp;        // 目标 exe 名（重启后重新按名字找 pid）
    std::wstring musicLabel;      // 界面上显示的名字
    bool  autoCapture   = true;   // 进入音乐模式自动开始处理
    // 虚拟声卡路由（Win10 也能实时处理别的声音：音乐软件 → 虚拟声卡 → 本程序 → 耳机）
    std::wstring inDevId, inDevName;    // 捕获端（如 CABLE Input）
    std::wstring outDevId, outDevName;  // 输送到真实耳机/音箱；空 = 系统默认
    bool  defaultMoved = false;         // 是否由本程序改过「系统默认播放设备」
    std::wstring prevDefaultId;         // 改之前的默认设备 id，退出时还原

    int   volumePercent = 80;
    bool  startMinimized= false;
    bool  closeToTray   = true;
    std::wstring lastFolder;
    std::wstring lastTrack;
    float lastSeek      = 0.0f;

    std::vector<CustomPreset> customs;
    int   customSel = -1;         // 当前选中的自定义曲线下标（-1 表示选的是内置预设）

    void applyPreset(int idx);
    void applyCustom(int idx);
    void setEqCount(int count);   // 切换段数并按对数频率重采样现有曲线

    // 载入 AutoEQ 档案（ParametricEQ.txt / GraphicEQ.txt），返回 0 成功
    // 返回值：0 成功；1 打不开；2 认不出格式；3 没有可用滤波器
    int  loadAutoEq(const std::wstring& path, std::wstring* msg);

    // 按应用记预设（Enhanced EQ 的 per-app profiles）：同一个播放器下次一出声就用回它的曲线
    bool perAppOn = true;
    std::vector<std::wstring> appExe;
    std::vector<int>          appPreset;
    int  findAppPreset(const std::wstring& exe) const;      // 没有返回 -1
    void setAppPreset(const std::wstring& exe, int idx);
    int  addCustom(const std::wstring& name);     // 返回新下标
    void removeCustom(int idx);
    int  customCountOf(int mode) const;
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
