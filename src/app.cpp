#include "app.h"
#include <shlwapi.h>
#include <tlhelp32.h>
#include <cwchar>
#include <cmath>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <utility>

const float kBandFreq[kBands] = { 31.f, 62.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };
const wchar_t* kBandLabel[kBands] = { L"31", L"62", L"125", L"250", L"500", L"1k", L"2k", L"4k", L"8k", L"16k" };

// ---------------------------------------------------------------- 图形 EQ 频段表
static const float kFreq10[kBands] = { 31.f, 62.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };
static const float kFreq15[15] = {
    25.f, 40.f, 63.f, 100.f, 160.f, 250.f, 400.f, 630.f,
    1000.f, 1600.f, 2500.f, 4000.f, 6300.f, 10000.f, 16000.f };
static const float kFreq31[31] = {
    20.f, 25.f, 31.5f, 40.f, 50.f, 63.f, 80.f, 100.f,
    125.f, 160.f, 200.f, 250.f, 315.f, 400.f, 500.f, 630.f,
    800.f, 1000.f, 1250.f, 1600.f, 2000.f, 2500.f, 3150.f, 4000.f,
    5000.f, 6300.f, 8000.f, 10000.f, 12500.f, 16000.f, 20000.f };

const float* BandFreqs(int count, int* n) {
    if (count == 15) { if (n) *n = 15; return kFreq15; }
    if (count == 31) { if (n) *n = 31; return kFreq31; }
    if (n) *n = kBands;
    return kFreq10;
}

float BandFreqAt(int count, int i) {
    int n = 0;
    const float* t = BandFreqs(count, &n);
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    return t[i];
}

const wchar_t* BandLabel(int count, int i) {
    static wchar_t buf[32];
    float f = BandFreqAt(count, i);
    if (f >= 1000.f) {
        if (f >= 10000.f)      swprintf(buf, 32, L"%.0fk", f / 1000.f);
        else if (f == (float)(int)f && (int)f % 1000 == 0) swprintf(buf, 32, L"%.0fk", f / 1000.f);
        else                   swprintf(buf, 32, L"%.1fk", f / 1000.f);
    } else if (f >= 100.f) {
        swprintf(buf, 32, L"%.0f", f);
    } else {
        swprintf(buf, 32, L"%.0f", f);
    }
    return buf;
}

float EqGainAt(const float* eq, int count, float f) {
    int n = 0;
    const float* t = BandFreqs(count, &n);
    if (!eq || n <= 0) return 0.f;
    if (f <= t[0]) return eq[0];
    if (f >= t[n - 1]) return eq[n - 1];
    for (int i = 0; i + 1 < n; ++i) {
        if (f >= t[i] && f <= t[i + 1]) {
            float lf = logf(f);
            float l0 = logf(t[i]), l1 = logf(t[i + 1]);
            float k = (l1 > l0) ? (lf - l0) / (l1 - l0) : 0.f;
            return eq[i] + (eq[i + 1] - eq[i]) * k;
        }
    }
    return eq[n - 1];
}

// ---------------------------------------------------------------- 名称表
const wchar_t* kParamTypeName[] = {
    L"峰值 Peak", L"低架 LowShelf", L"高架 HighShelf", L"低通 LowPass",
    L"高通 HighPass", L"陷波 Notch", L"全通 AllPass", L"带通 BandPass",
    L"低架 一阶", L"高架 一阶", L"低通 一阶", L"高通 一阶",
    L"低通 LR4", L"高通 LR4", L"倾斜 Tilt" };
const wchar_t* kCompModeName[] = { L"关闭", L"柔和", L"中等", L"强力" };
const wchar_t* kSpatialModeName[] = { L"关闭", L"8D 环绕", L"10D 影院", L"8 字漂移" };
const wchar_t* kTestToneName[] = { L"关闭", L"正弦", L"粉噪", L"扫频" };


// 耳机校正曲线（AutoEq 风格目标曲线的近似值，供没有测量文件的用户快速对齐听感）
const Correction kCorrections[] = {
    { L"关闭",        L"不叠加任何校正，完全按你的 EQ 走", {0,0,0,0,0,0,0,0,0,0} },
    { L"Harman 近似", L"入耳式目标：低频托底、2-4k 提亮，最通用的讨喜曲线",
      {5.0f,4.0f,2.0f,0.f,-0.5f,0.f,1.5f,2.0f,-1.0f,0.5f} },
    { L"平直监听",    L"只补一点极低频滚降，尽量还原录音本来的样子",
      {3.0f,2.0f,1.0f,0.f,0.f,0.f,0.f,0.f,0.f,0.f} },
    { L"低频增强",    L"氛围与下潜更足，适合电子、R&B 与动作游戏",
      {8.0f,7.0f,4.0f,1.0f,0.f,0.f,0.f,0.f,0.f,0.5f} },
    { L"高频柔化",    L"削掉容易刺耳的 8-16k，适合解析过亮或听久累的耳机",
      {0.f,0.f,0.f,0.f,0.f,0.f,1.0f,-1.0f,-3.0f,-3.0f} },
    { L"人声前置",    L"1-4k 微微抬起，看剧、听说唱、语音都更清楚",
      {0.f,0.5f,0.f,-0.5f,0.f,1.0f,2.0f,1.0f,0.f,0.f} },
};
const int kCorrectionCount = (int)(sizeof(kCorrections) / sizeof(kCorrections[0]));

// 预设曲线：曲风典型频谱 + 常见有线耳机频响缺口的保守修形，全部限制在 ±4dB 内避免失真
const Preset kPresets[] = {
    // ---------------- 音乐（常用预设名） ----------------
    { L"正常", L"不加任何修饰，原汁原味", MODE_MUSIC,
      {0,0,0,0,0,0,0,0,0,0}, 0.f, 0.f, 0.f, 1.00f, 0.15f, 0 },
    { L"流行", L"低频厚一点、人声靠前，听华语流行最顺耳", MODE_MUSIC,
      {0.5f,2.0f,1.2f,0.5f,-0.5f,0.f,0.8f,0.5f,0.f,0.5f}, 1.5f, 0.5f, 0.8f, 1.08f, 0.30f, 1 },
    { L"摇滚", L"鼓点有拳头感，吉他有砂砾质感", MODE_MUSIC,
      {1.0f,2.5f,1.5f,0.f,-1.2f,-0.5f,0.8f,2.0f,1.5f,0.8f}, 2.0f, 1.0f, 1.0f, 1.12f, 0.45f, 2 },
    { L"爵士", L"温暖的中频、松弛的高频，铜管与贝斯不刺", MODE_MUSIC,
      {1.0f,1.8f,1.0f,0.5f,-0.2f,0.f,0.5f,0.5f,0.3f,0.2f}, 1.4f, 0.6f, 0.6f, 1.02f, 0.20f, 1 },
    { L"古典", L"平直通透，保留厅堂与弦乐的空气感", MODE_MUSIC,
      {0.f,0.f,0.f,-0.4f,-0.4f,0.f,0.f,0.5f,0.8f,1.0f}, 0.f, 0.3f, 1.2f, 1.04f, 0.12f, 2 },
    { L"人声", L"咬字清晰、气息贴耳，齿音不刮", MODE_MUSIC,
      {0.5f,1.0f,0.f,0.5f,-0.8f,1.4f,2.4f,1.8f,0.8f,0.6f}, 0.6f, 2.0f, 0.8f, 1.04f, 0.28f, 1 },
    { L"低音增强", L"下潜更深、量感更足，舞曲与 Hip-Hop 专用", MODE_MUSIC,
      {4.0f,4.0f,2.6f,1.0f,0.f,-0.5f,-0.5f,0.f,0.f,0.f}, 3.0f, 0.f, 0.4f, 1.02f, 0.35f, 3 },
    { L"高音增强", L"亮度与细节拉满，老录音也能听出空气感", MODE_MUSIC,
      {-1.0f,-1.0f,-0.5f,0.f,0.f,0.f,0.8f,1.8f,2.4f,2.4f}, 0.f, 1.2f, 1.4f, 1.06f, 0.22f, 2 },
    { L"电子", L"低频扎实、声场拉宽，适合 EDM 与合成器", MODE_MUSIC,
      {3.0f,3.0f,1.8f,0.4f,-0.2f,0.f,0.6f,1.2f,1.2f,0.8f}, 2.4f, 0.6f, 1.2f, 1.30f, 0.40f, 1 },

    // ---------------- 游戏 ----------------
    { L"FPS 脚步", L"压低枪炮轰鸣，抬高 2k-8k 的脚步与换弹细节", MODE_GAME,
      {0.f,1.5f,1.0f,0.f,-1.5f,0.f,2.5f,3.5f,2.0f,0.5f}, 1.0f, 1.5f, 0.f, 1.35f, 0.50f, 0 },
    { L"FPS 竞技", L"不牺牲听感的全能竞技曲线，压缩更克制", MODE_GAME,
      {0.f,1.0f,0.5f,0.f,-1.0f,0.f,1.0f,1.5f,0.8f,0.f}, 0.5f, 0.8f, 0.f, 1.15f, 0.60f, 2 },
    { L"通用", L"轻度修形 + 限幅，长时间游玩不累耳", MODE_GAME,
      {0.5f,1.0f,0.5f,0.f,0.f,0.f,0.5f,0.8f,0.5f,0.f}, 0.5f, 0.5f, 0.4f, 1.05f, 0.45f, 2 },
    { L"二游 剧情", L"原神/星铁/绝区零 的管弦与战斗音效更有包围感", MODE_GAME,
      {1.0f,2.0f,1.0f,0.5f,0.f,0.5f,1.0f,1.2f,0.8f,1.5f}, 2.0f, 0.8f, 1.4f, 1.28f, 0.25f, 1 },
    { L"二游 语音", L"剧情对话与角色语音更清楚，BGM 不抢戏", MODE_GAME,
      {0.f,0.5f,0.f,-0.8f,0.5f,1.5f,2.2f,1.8f,1.0f,0.5f}, 0.f, 2.2f, 0.4f, 1.10f, 0.40f, 5 },
    { L"3A 剧情", L"过场与对话优先：中频贴耳、低频不轰，环境音有层次", MODE_GAME,
      {1.5f,1.2f,0.6f,0.8f,0.4f,0.8f,1.0f,0.6f,1.0f,1.6f}, 1.2f, 1.2f, 1.6f, 1.42f, 0.20f, 1 },
    { L"3A 动作", L"保留大动态的爆点冲击，又压住爆音（战神/GTA/战地）", MODE_GAME,
      {2.0f,2.4f,0.8f,0.f,-0.6f,0.4f,1.2f,1.6f,1.4f,1.2f}, 2.6f, 1.0f, 1.2f, 1.35f, 0.55f, 2 },
    { L"3A 电影", L"爆炸、载具、配乐下潜更深，像影院 LFE 补了一层垫底", MODE_GAME,
      {3.0f,3.2f,1.0f,0.2f,-0.8f,0.f,0.6f,0.8f,1.2f,1.0f}, 3.5f, 0.5f, 1.2f, 1.30f, 0.28f, 3 },
    { L"3A 夜间", L"深夜低音量玩：等响补偿 + 脚步与语音都留住，不吵到人", MODE_GAME,
      {1.8f,2.2f,1.2f,0.6f,0.f,0.6f,1.4f,1.4f,1.0f,1.8f}, 2.4f, 1.4f, 1.8f, 1.15f, 0.25f, 3 },

    // ---------------- 电影（按类型做曲线，选一次就够） ----------------
    { L"动作片", L"爆炸、追车、枪战都有冲击力，低频扎实不糊", MODE_MOVIE,
      {3.0f,3.2f,1.8f,0.4f,-0.8f,0.2f,0.8f,1.2f,1.0f,0.6f}, 3.2f, 0.8f, 1.4f, 1.30f, 0.45f, 3 },
    { L"科幻 / 太空", L"低频下潜深，高频有空气感，包围感最强", MODE_MOVIE,
      {2.4f,2.6f,1.0f,0.f,-0.6f,0.f,0.6f,1.0f,1.6f,2.2f}, 2.4f, 0.6f, 2.2f, 1.60f, 0.30f, 1 },
    { L"恐怖 / 惊悚", L"心跳与低频更压迫，呼吸声、脚步声更清晰", MODE_MOVIE,
      {2.8f,2.2f,0.6f,0.2f,1.0f,1.2f,1.6f,1.2f,0.4f,0.8f}, 2.6f, 1.8f, 0.8f, 1.15f, 0.35f, 5 },
    { L"战争片", L"炮火层次分明，爆点有冲击又不刺耳（诺兰式混音）", MODE_MOVIE,
      {2.6f,2.4f,1.0f,-0.2f,-1.0f,0.4f,1.4f,1.6f,1.2f,0.6f}, 2.8f, 1.4f, 1.2f, 1.42f, 0.55f, 2 },
    { L"剧情 / 文艺", L"对白最清楚，配乐垫在后面，长时间看不累", MODE_MOVIE,
      {0.f,0.4f,0.8f,1.2f,0.8f,0.4f,0.f,0.6f,0.2f,-0.2f}, 0.4f, 2.8f, 0.4f, 1.05f, 0.22f, 5 },
    { L"动画 / 家庭", L"人声甜美、弦乐明亮，小孩和老人都听得舒服", MODE_MOVIE,
      {0.6f,1.0f,0.8f,0.6f,0.f,0.4f,0.8f,1.2f,1.4f,1.0f}, 1.0f, 1.8f, 0.8f, 1.15f, 0.18f, 1 },

    // ---------------- 动漫（番剧 / 剧场版 / OP・ED 按类型分） ----------------
    { L"热血 战斗", L"打斗与呐喊有冲击力，低频扎实不糊，招式音效更带劲", MODE_ANIME,
      {3.0f,3.0f,1.6f,0.4f,-0.8f,0.f,0.6f,1.6f,1.4f,0.8f}, 2.8f, 1.0f, 1.0f, 1.28f, 0.45f, 2 },
    { L"日常 治愈", L"柔和温暖，齿音不刺，看日常番长时间不累", MODE_ANIME,
      {0.4f,0.8f,1.0f,1.2f,0.8f,0.4f,0.2f,0.4f,0.2f,0.f}, 0.8f, 2.0f, 0.4f, 1.02f, 0.18f, 5 },
    { L"恋爱 情感", L"人声贴耳、情感细节多，主题曲和告白场面更动人", MODE_ANIME,
      {0.6f,1.2f,1.4f,1.6f,1.0f,0.6f,0.8f,1.0f,0.4f,0.2f}, 1.0f, 2.8f, 0.6f, 1.06f, 0.20f, 5 },
    { L"奇幻 魔法", L"高频通透、声场宽，魔法与结界场面更有包围感", MODE_ANIME,
      {1.2f,1.6f,0.8f,0.6f,0.2f,0.4f,1.2f,1.8f,2.0f,1.6f}, 1.6f, 1.4f, 2.0f, 1.45f, 0.35f, 1 },
    { L"偶像 音乐", L"低频有弹性、中高频发亮，Live 与插入歌更热闹", MODE_ANIME,
      {2.4f,2.6f,1.4f,0.4f,-0.4f,0.6f,1.8f,2.4f,1.6f,1.0f}, 2.2f, 2.0f, 1.2f, 1.25f, 0.35f, 2 },
    { L"机战 科幻", L"低频有力、细节清晰，机体与爆炸声层次分明", MODE_ANIME,
      {2.8f,2.8f,1.2f,0.f,-1.0f,0.2f,1.0f,1.4f,1.8f,2.0f}, 2.6f, 0.8f, 1.8f, 1.40f, 0.50f, 3 },
};
const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

const GameProcess kGames[] = {
    { L"genshinimpact.exe",            L"原神" },
    { L"yuanshen.exe",                 L"原神(国内)" },
    { L"starrail.exe",                 L"崩坏：星穹铁道" },
    { L"zenlesszonezero.exe",          L"绝区零" },
    { L"wutheringwaves.exe",           L"鸣潮" },
    { L"cs2.exe",                      L"CS2" },
    { L"valorant-win64-shipping.exe",  L"VALORANT" },
    { L"r5apex.exe",                   L"Apex 英雄" },
    { L"apex_legends.exe",             L"Apex 英雄" },
    { L"overwatch.exe",                L"守望先锋" },
    { L"deltaforceclient-win64-shipping.exe", L"三角洲行动" },
    { L"narakabladepoint.exe",         L"永劫无间" },
    { L"pubg.exe",                     L"PUBG" },
    { L"tslgame.exe",                  L"PUBG" },
    { L"gta5.exe",                     L"GTA5" },
    { L"eldenring.exe",                L"艾尔登法环" },
    { L"bf2042.exe",                   L"战地2042" },
    { L"rainbowsix.exe",               L"彩虹六号" },
};
const int kGameCount = (int)(sizeof(kGames) / sizeof(kGames[0]));

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    wchar_t* p = wcsrchr(buf, L'\\');
    if (p) *p = 0;
    return std::wstring(buf);
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t last = a[a.size() - 1];
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

static std::wstring SettingsPath() { return PathJoin(ExeDir(), L"AuraTune.ini"); }

static float IniFloat(const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(sec, key, L"", buf, 64, SettingsPath().c_str());
    if (!buf[0]) return def;
    return (float)_wtof(buf);
}
static int IniInt(const wchar_t* sec, const wchar_t* key, int def) {
    return (int)IniFloat(sec, key, (float)def);
}
static void WriteFloat(const wchar_t* sec, const wchar_t* key, float v) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%.3f", v);
    WritePrivateProfileStringW(sec, key, buf, SettingsPath().c_str());
}
static std::wstring IniStr(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512] = {};
    GetPrivateProfileStringW(sec, key, def, buf, 511, SettingsPath().c_str());
    return buf;
}
static void WriteStr(const wchar_t* sec, const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(sec, key, v.c_str(), SettingsPath().c_str());
}

void Settings::applyPreset(int idx) {
    if (idx < 0 || idx >= kPresetCount) return;
    const Preset& p = kPresets[idx];
    // 预设固定是 10 段曲线；当前段数不同时按对数频率插值映射过去
    for (int b = 0; b < eqCount && b < kMaxBands; ++b)
        eq[b] = EqGainAt(p.eq, kBands, BandFreqAt(eqCount, b));
    for (int b = eqCount; b < kMaxBands; ++b) eq[b] = 0.f;
    bass = p.bass; vocal = p.vocal; ambience = p.ambience;
    width = p.width; dynamics = p.dynamics; correction = p.correction;
    if (p.mode == MODE_MUSIC) presetMusic = idx;
    else if (p.mode == MODE_MOVIE) presetMovie = idx;
    else if (p.mode == MODE_ANIME) presetAnime = idx;
    else presetGame = idx;
    mode = p.mode;
    customSel = -1;
}

// 切换图形 EQ 段数，并把当前曲线按对数频率重采样到新段数
void Settings::setEqCount(int count) {
    if (count != 10 && count != 15 && count != 31) count = kBands;
    if (count == eqCount) return;
    static float old[kMaxBands];
    int oldCount = eqCount;
    for (int b = 0; b < kMaxBands; ++b) old[b] = eq[b];
    eqCount = count;
    for (int b = 0; b < eqCount; ++b)
        eq[b] = EqGainAt(old, oldCount, BandFreqAt(eqCount, b));
    for (int b = eqCount; b < kMaxBands; ++b) eq[b] = 0.f;
}

// ------------------------------------------------------------ AutoEQ 档案导入
// 支持两种 AutoEQ 纯文本格式：
//   ParametricEQ.txt :  Preamp: -6.9 dB / Filter 1: ON PK Fc 105 Hz Gain 2.4 dB Q 0.70
//   GraphicEQ.txt    :  GraphicEQ: 20 -3.5; 21 -3.4; 25 -3.0; ... 20k -1.0
// Freq 后面可能是 "Hz" 或 "kHz"，数值也可能带小数。解析失败就返回错误码，不破坏当前设置。
static inline const char* SkipSpace(const char* p) { while (*p == ' ' || *p == '\t') ++p; return p; }

static inline double NumAt(const char** pp) {
    const char* p = SkipSpace(*pp);
    char* end = nullptr;
    double v = strtod(p, &end);
    if (end == p) return 0.0;
    *pp = end;
    return v;
}

// 读 "105 Hz" / "1.05 kHz" 这类频率写法，单位缺省按 Hz
static double FreqAt(const char** pp, bool* ok) {
    const char* p = SkipSpace(*pp);
    char* end = nullptr;
    double v = strtod(p, &end);
    if (end == p) { *ok = false; return 0.0; }
    p = SkipSpace(end);
    if ((p[0] == 'k' || p[0] == 'K') && (p[1] == 'H' || p[1] == 'h')) { v *= 1000.0; p += 2; }
    else if (p[0] == 'H' || p[0] == 'h') { p += 1; }
    if (*p == 'z' || *p == 'Z') ++p;
    *ok = true;
    *pp = p;
    return v;
}

static bool StartsWithI(const char* s, const char* head) {
    size_t n = strlen(head);
    for (size_t i = 0; i < n; ++i) {
        char a = (char)tolower((unsigned char)s[i]);
        if (!a) return false;
        if (a != (char)tolower((unsigned char)head[i])) return false;
    }
    return true;
}

int Settings::loadAutoEq(const std::wstring& path, std::wstring* msg) {
    auto fail = [&](int code, const wchar_t* text) {
        if (msg) *msg = text;
        return code;
    };
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail(1, L"打不开这个文件");
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        return fail(1, L"文件是空的或者太大（上限 4MB）");
    }
    std::string text((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    const BOOL okRead = ReadFile(h, &text[0], (DWORD)text.size(), &got, nullptr);
    CloseHandle(h);
    if (!okRead) return fail(1, L"读取失败");
    text.resize(got);
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF) text.erase(0, 3);   // UTF-8 BOM

    ParamFilter next[kMaxParams];
    int nFilter = 0;
    float newPreamp = 0.f;
    bool sawPreamp = false;

    // GraphicEQ 会先收集点，再插值到当前段数
    std::vector<std::pair<double, double>> pts;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const char* p = SkipSpace(line.c_str());

        if (StartsWithI(p, "Preamp")) {
            p += 6;
            if (*p == ':') ++p;
            newPreamp = (float)NumAt(&p);
            sawPreamp = true;
            continue;
        }
        if (StartsWithI(p, "GraphicEQ")) {
            p += 9;
            if (*p == ':') ++p;
            while (*p) {
                bool ok1 = false, ok2 = false;
                double f = FreqAt(&p, &ok1);
                double g = NumAt(&p);
                ok2 = true;
                if (ok1 && f > 0.0) pts.push_back({ f, g });
                (void)ok2;
                while (*p && *p != ';') ++p;
                if (*p == ';') ++p;
            }
            continue;
        }
        if (StartsWithI(p, "Filter")) {
            p += 6;
            // 真实写法是 "Filter 1: ON PK Fc 105 Hz Gain 2.4 dB Q 0.70"，
            // 也就是 Filter 后面先跟序号和冒号，再是 ON/OFF
            while (*p == ' ' || *p == '\t') ++p;
            while (*p >= '0' && *p <= '9') ++p;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == ':') ++p;
            const char* q = SkipSpace(p);
            bool on = false;
            if (StartsWithI(q, "ON")) { on = true; q += 2; }
            else if (StartsWithI(q, "OFF")) { q += 3; }
            q = SkipSpace(q);
            char type[8] = {0};
            int ti = 0;
            while (*q && *q != ' ' && ti < 7) type[ti++] = *q++;
            int pt = -1;
            if (!strcmp(type, "PK")) pt = PT_PEAK;
            else if (!strcmp(type, "LS")) pt = PT_LOW_SHELF;
            else if (!strcmp(type, "HS")) pt = PT_HIGH_SHELF;
            else if (!strcmp(type, "LP")) pt = PT_LOW_PASS;
            else if (!strcmp(type, "HP")) pt = PT_HIGH_PASS;
            else if (!strcmp(type, "BP")) pt = PT_BAND_PASS;
            else if (!strcmp(type, "NO")) pt = PT_NOTCH;
            else if (!strcmp(type, "LSC")) pt = PT_LOW_SHELF;
            else if (!strcmp(type, "HSC")) pt = PT_HIGH_SHELF;

            double f = 0.0, g = 0.0, qv = 1.0;
            bool haveF = false;
            q = SkipSpace(q);
            if (StartsWithI(q, "Fc")) {
                q += 2;
                bool okf = false;
                f = FreqAt(&q, &okf);
                haveF = okf && f > 0.0;
            }
            const char* gpos = strstr(q, "Gain");
            if (gpos) {
                const char* gp = gpos + 4;
                g = NumAt(&gp);
            }
            const char* qpos = strstr(q, "Q ");
            if (!qpos) qpos = strstr(q, "Q:");
            if (qpos) {
                const char* qp = qpos + 1;
                if (*qp == ':') ++qp;
                double v = NumAt(&qp);
                if (v > 0.0) qv = v;
            }
            if (pt >= 0 && haveF && nFilter < kMaxParams) {
                ParamFilter& pf = next[nFilter++];
                pf.on = on;
                pf.type = pt;
                pf.freq = (float)f;
                pf.gain = (float)g;
                pf.q = (float)qv;
            }
            continue;
        }
    }

    if (!pts.empty()) {
        // GraphicEQ：把点集按对数频率插值到当前段数
        std::sort(pts.begin(), pts.end());
        for (int b = 0; b < eqCount; ++b) {
            float fc = BandFreqAt(eqCount, b);
            double g = pts.front().second;
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                if (fc >= pts[i].first && fc <= pts[i + 1].first) {
                    double t = (log10(fc) - log10(pts[i].first)) /
                               (log10(pts[i + 1].first) - log10(pts[i].first) + 1e-9);
                    g = pts[i].second + (pts[i + 1].second - pts[i].second) * t;
                    break;
                }
            }
            if (fc < pts.front().first) g = pts.front().second;
            if (fc > pts.back().first) g = pts.back().second;
            eq[b] = (float)std::max(-18.0, std::min(18.0, g));
        }
        for (int b = eqCount; b < kMaxBands; ++b) eq[b] = 0.f;
        if (sawPreamp) preamp = std::max(-24.f, std::min(24.f, newPreamp));
        correction = 0;   // 导入的曲线本身就是校正结果，别再叠加内建校正
        if (msg) {
            wchar_t buf[160];
            swprintf(buf, 160, L"已按 AutoEQ 图形曲线填入 %d 段（前置 %.1f dB）", eqCount, preamp);
            *msg = buf;
        }
        return 0;
    }

    if (nFilter == 0) return fail(2, L"没认出格式：需要 AutoEQ 的 ParametricEQ.txt 或 GraphicEQ.txt");

    for (int i = 0; i < kMaxParams; ++i) {
        if (i < nFilter) params[i] = next[i];
        else { params[i].on = false; }
    }
    if (sawPreamp) preamp = std::max(-24.f, std::min(24.f, newPreamp));
    correction = 0;
    if (msg) {
        wchar_t buf[160];
        swprintf(buf, 160, L"已载入 AutoEQ：%d 个滤波器，前置 %.1f dB", nFilter, preamp);
        *msg = buf;
    }
    return 0;
}

// ------------------------------------------------------------ 按应用记预设
int Settings::findAppPreset(const std::wstring& exe) const {
    if (exe.empty()) return -1;
    for (size_t i = 0; i < appExe.size() && i < appPreset.size(); ++i)
        if (_wcsicmp(appExe[i].c_str(), exe.c_str()) == 0) return appPreset[i];
    return -1;
}

void Settings::setAppPreset(const std::wstring& exe, int idx) {
    if (exe.empty() || idx < 0 || idx >= kPresetCount) return;
    for (size_t i = 0; i < appExe.size() && i < appPreset.size(); ++i) {
        if (_wcsicmp(appExe[i].c_str(), exe.c_str()) == 0) { appPreset[i] = idx; return; }
    }
    if (appExe.size() >= 24) return;
    appExe.push_back(exe);
    appPreset.push_back(idx);
}

// ------------------------------------------------------------ 自定义曲线
void Settings::applyCustom(int idx) {
    if (idx < 0 || idx >= (int)customs.size()) return;
    const CustomPreset& c = customs[idx];
    eqCount = (c.eqCount == 15 || c.eqCount == 31) ? c.eqCount : kBands;
    for (int b = 0; b < kMaxBands; ++b) eq[b] = c.eq[b];
    for (int b = eqCount; b < kMaxBands; ++b) eq[b] = 0.f;
    bass = c.bass; vocal = c.vocal; ambience = c.ambience;
    width = c.width; dynamics = c.dynamics;
    crossfeed = c.crossfeed; correction = c.correction;
    mode = c.mode;
    customSel = idx;
}

int Settings::addCustom(const std::wstring& name) {
    if ((int)customs.size() >= kMaxCustom) return -1;
    CustomPreset c;
    c.name = name.empty() ? L"我的曲线" : name;
    c.mode = mode;
    c.eqCount = eqCount;
    for (int b = 0; b < kMaxBands; ++b) c.eq[b] = eq[b];
    c.bass = bass; c.vocal = vocal; c.ambience = ambience;
    c.width = width; c.dynamics = dynamics;
    c.crossfeed = crossfeed; c.correction = correction;
    customs.push_back(c);
    customSel = (int)customs.size() - 1;
    return customSel;
}

void Settings::removeCustom(int idx) {
    if (idx < 0 || idx >= (int)customs.size()) return;
    customs.erase(customs.begin() + idx);
    if (customSel == idx) customSel = -1;
    else if (customSel > idx) --customSel;
}

int Settings::customCountOf(int m) const {
    int n = 0;
    for (size_t i = 0; i < customs.size(); ++i) if (customs[i].mode == m) ++n;
    return n;
}

void Settings::load() {
    mode          = IniInt(L"ui", L"mode", MODE_MUSIC);    presetMusic   = IniInt(L"ui", L"presetMusic", 1);
    presetMovie   = IniInt(L"ui", L"presetMovie", 22);
    presetAnime   = IniInt(L"ui", L"presetAnime", 24);
    presetMovie   = IniInt(L"ui", L"presetMovie", 22);
    eqCount       = IniInt(L"dsp", L"eqCount", kBands);
    if (eqCount != 10 && eqCount != 15 && eqCount != 31) eqCount = kBands;
    for (int b = 0; b < eqCount && b < kMaxBands; ++b) {
        wchar_t key[16];
        swprintf(key, 16, L"eq%d", b);
        eq[b] = IniFloat(L"eq", key, b < kBands ? kPresets[1].eq[b] : 0.f);
    }
    for (int b = eqCount; b < kMaxBands; ++b) eq[b] = 0.f;
    bass          = IniFloat(L"dsp", L"bass", 1.5f);
    vocal         = IniFloat(L"dsp", L"vocal", 0.5f);
    ambience      = IniFloat(L"dsp", L"ambience", 0.8f);
    width         = IniFloat(L"dsp", L"width", 1.08f);
    dynamics      = IniFloat(L"dsp", L"dynamics", 0.30f);
    crossfeed     = IniFloat(L"dsp", L"crossfeed", 0.35f);
    correction    = IniInt(L"dsp", L"correction", 1);
    preamp        = IniFloat(L"dsp", L"preamp", 0.f);
    vol           = IniFloat(L"dsp", L"vol", 0.f);
    balance       = IniFloat(L"dsp", L"balance", 0.f);
    bypass        = IniInt(L"dsp", L"bypass", 0) != 0;
    xoFreq        = IniFloat(L"dsp", L"xoFreq", 0.f);
    subGain       = IniFloat(L"dsp", L"subGain", 0.f);
    compMode      = IniInt(L"dsp", L"compMode", COMP_OFF);
    spatialMode   = IniInt(L"dsp", L"spatialMode", SP_OFF);
    spatialRate   = IniFloat(L"dsp", L"spatialRate", 0.12f);
    spatialDepth  = IniFloat(L"dsp", L"spatialDepth", 0.5f);
    irOn          = IniInt(L"dsp", L"irOn", 0) != 0;
    irMix         = IniFloat(L"dsp", L"irMix", 1.f);
    testTone      = IniInt(L"dsp", L"testTone", TT_OFF);
    testFreq      = IniFloat(L"dsp", L"testFreq", 1000.f);
    testLevel     = IniFloat(L"dsp", L"testLevel", -20.f);
    musicPid      = IniInt(L"music", L"pid", 0);
    autoCapture   = IniInt(L"music", L"autoCapture", 1) != 0;
    volumePercent = IniInt(L"play", L"volume", 80);
    autoSwitch    = IniInt(L"ui", L"autoSwitch", 1) != 0;
    startMinimized= IniInt(L"ui", L"startMinimized", 0) != 0;
    closeToTray   = IniInt(L"ui", L"closeToTray", 1) != 0;
    lastSeek      = IniFloat(L"play", L"seek", 0.f);

    // 参量均衡器
    for (int i = 0; i < kMaxParams; ++i) {
        wchar_t sec[16];
        swprintf(sec, 16, L"p%d", i);
        params[i].on   = IniInt(sec, L"on", 0) != 0;
        params[i].type = IniInt(sec, L"type", PT_PEAK);
        if (params[i].type < 0 || params[i].type >= kParamTypeCount) params[i].type = PT_PEAK;
        params[i].freq = IniFloat(sec, L"freq", 1000.f);
        params[i].gain = IniFloat(sec, L"gain", 0.f);
        params[i].q    = IniFloat(sec, L"q", 1.0f);
    }

    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(L"play", L"folder", L"", buf, MAX_PATH * 2, SettingsPath().c_str());
    lastFolder = buf;
    GetPrivateProfileStringW(L"play", L"track", L"", buf, MAX_PATH * 2, SettingsPath().c_str());
    lastTrack = buf;
    irPath = IniStr(L"dsp", L"irPath", L"");
    musicApp = IniStr(L"music", L"app", L"");
    musicLabel = IniStr(L"music", L"label", L"");
    inDevId    = IniStr(L"music", L"inId", L"");
    inDevName  = IniStr(L"music", L"inName", L"");
    outDevId   = IniStr(L"music", L"outId", L"");
    outDevName = IniStr(L"music", L"outName", L"");
    defaultMoved = IniInt(L"music", L"defaultMoved", 0) != 0;
    prevDefaultId = IniStr(L"music", L"prevDefault", L"");

    // 自定义曲线
    customs.clear();
    customSel = -1;
    int nCustom = IniInt(L"custom", L"count", 0);
    if (nCustom < 0) nCustom = 0;
    if (nCustom > kMaxCustom) nCustom = kMaxCustom;
    for (int i = 0; i < nCustom; ++i) {
        wchar_t sec[16];
        swprintf(sec, 16, L"c%d", i);
        CustomPreset c;
        c.name = IniStr(sec, L"name", L"我的曲线");
        c.mode = IniInt(sec, L"mode", MODE_MUSIC);
        c.eqCount = IniInt(sec, L"eqCount", kBands);
        if (c.eqCount != 10 && c.eqCount != 15 && c.eqCount != 31) c.eqCount = kBands;
        for (int b = 0; b < c.eqCount && b < kMaxBands; ++b) {
            wchar_t key[16];
            swprintf(key, 16, L"eq%d", b);
            c.eq[b] = IniFloat(sec, key, 0.f);
        }
        c.bass      = IniFloat(sec, L"bass", 1.f);
        c.vocal     = IniFloat(sec, L"vocal", 0.5f);
        c.ambience  = IniFloat(sec, L"ambience", 0.6f);
        c.width     = IniFloat(sec, L"width", 1.05f);
        c.dynamics  = IniFloat(sec, L"dynamics", 0.3f);
        c.crossfeed = IniFloat(sec, L"crossfeed", 0.35f);
        c.correction= IniInt(sec, L"correction", 0);
        customs.push_back(c);
    }

    // 按应用记预设
    perAppOn = IniInt(L"apps", L"on", 1) != 0;
    appExe.clear();
    appPreset.clear();
    int nApp = IniInt(L"apps", L"count", 0);
    if (nApp < 0) nApp = 0;
    if (nApp > 24) nApp = 24;
    for (int i = 0; i < nApp; ++i) {
        wchar_t sec[16];
        swprintf(sec, 16, L"a%d", i);
        std::wstring e = IniStr(sec, L"exe", L"");
        int idx = IniInt(sec, L"preset", -1);
        if (e.empty() || idx < 0 || idx >= kPresetCount) continue;
        appExe.push_back(e);
        appPreset.push_back(idx);
    }
}

void Settings::save() const {
    // 首次写 ini 时先落 UTF-16LE BOM：这样 WritePrivateProfileStringW 会按 Unicode 写，
    // 中文预设名/设备名不会因为系统代码页而变成乱码。
    {
        const std::wstring path = SettingsPath();
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                const wchar_t bom = 0xFEFF;
                DWORD wrote = 0;
                WriteFile(f, &bom, sizeof(bom), &wrote, nullptr);
            }
        }
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    }
    WriteFloat(L"ui", L"mode",        (float)mode);
    WriteFloat(L"ui", L"presetMusic", (float)presetMusic);
    WriteFloat(L"ui", L"presetGame",  (float)presetGame);
    WriteFloat(L"ui", L"presetMovie", (float)presetMovie);
    WriteFloat(L"ui", L"presetAnime", (float)presetAnime);
    WriteFloat(L"dsp", L"eqCount", (float)eqCount);
    for (int b = 0; b < eqCount; ++b) {
        wchar_t key[16];
        swprintf(key, 16, L"eq%d", b);
        WriteFloat(L"eq", key, eq[b]);
    }
    WriteFloat(L"dsp", L"bass", bass);
    WriteFloat(L"dsp", L"vocal", vocal);
    WriteFloat(L"dsp", L"ambience", ambience);
    WriteFloat(L"dsp", L"width", width);
    WriteFloat(L"dsp", L"dynamics", dynamics);
    WriteFloat(L"dsp", L"crossfeed", crossfeed);
    WriteFloat(L"dsp", L"correction", (float)correction);
    WriteFloat(L"dsp", L"preamp", preamp);
    WriteFloat(L"dsp", L"vol", vol);
    WriteFloat(L"dsp", L"balance", balance);
    WriteFloat(L"dsp", L"bypass", bypass ? 1.f : 0.f);
    WriteFloat(L"dsp", L"xoFreq", xoFreq);
    WriteFloat(L"dsp", L"subGain", subGain);
    WriteFloat(L"dsp", L"compMode", (float)compMode);
    WriteFloat(L"dsp", L"spatialMode", (float)spatialMode);
    WriteFloat(L"dsp", L"spatialRate", spatialRate);
    WriteFloat(L"dsp", L"spatialDepth", spatialDepth);
    WriteFloat(L"dsp", L"irOn", irOn ? 1.f : 0.f);
    WriteFloat(L"dsp", L"irMix", irMix);
    WriteFloat(L"dsp", L"testTone", (float)testTone);
    WriteFloat(L"dsp", L"testFreq", testFreq);
    WriteFloat(L"dsp", L"testLevel", testLevel);
    WriteFloat(L"music", L"pid", (float)musicPid);
    WriteFloat(L"music", L"autoCapture", autoCapture ? 1.f : 0.f);
    WriteStr(L"dsp", L"irPath", irPath);
    WriteStr(L"music", L"app", musicApp);
    WriteStr(L"music", L"label", musicLabel);
    WriteStr(L"music", L"inId", inDevId);
    WriteStr(L"music", L"inName", inDevName);
    WriteStr(L"music", L"outId", outDevId);
    WriteStr(L"music", L"outName", outDevName);
    WriteFloat(L"music", L"defaultMoved", defaultMoved ? 1.f : 0.f);
    WriteStr(L"music", L"prevDefault", prevDefaultId);
    for (int i = 0; i < kMaxParams; ++i) {
        wchar_t sec[16];
        swprintf(sec, 16, L"p%d", i);
        WriteFloat(sec, L"on", params[i].on ? 1.f : 0.f);
        WriteFloat(sec, L"type", (float)params[i].type);
        WriteFloat(sec, L"freq", params[i].freq);
        WriteFloat(sec, L"gain", params[i].gain);
        WriteFloat(sec, L"q", params[i].q);
    }
    WriteFloat(L"play", L"volume", (float)volumePercent);
    WriteFloat(L"ui", L"autoSwitch", autoSwitch ? 1.f : 0.f);
    WriteFloat(L"ui", L"startMinimized", startMinimized ? 1.f : 0.f);
    WriteFloat(L"ui", L"closeToTray", closeToTray ? 1.f : 0.f);
    WriteFloat(L"play", L"seek", lastSeek);
    WritePrivateProfileStringW(L"play", L"folder", lastFolder.c_str(), SettingsPath().c_str());
    WritePrivateProfileStringW(L"play", L"track", lastTrack.c_str(), SettingsPath().c_str());

    // 自定义曲线
    WriteFloat(L"custom", L"count", (float)customs.size());
    for (size_t i = 0; i < customs.size(); ++i) {
        wchar_t sec[16];
        swprintf(sec, 16, L"c%d", (int)i);
        const CustomPreset& c = customs[i];
        WriteStr(sec, L"name", c.name);
        WriteFloat(sec, L"mode", (float)c.mode);
        WriteFloat(sec, L"eqCount", (float)c.eqCount);
        for (int b = 0; b < c.eqCount && b < kMaxBands; ++b) {
            wchar_t key[16];
            swprintf(key, 16, L"eq%d", b);
            WriteFloat(sec, key, c.eq[b]);
        }
        WriteFloat(sec, L"bass", c.bass);
        WriteFloat(sec, L"vocal", c.vocal);
        WriteFloat(sec, L"ambience", c.ambience);
        WriteFloat(sec, L"width", c.width);
        WriteFloat(sec, L"dynamics", c.dynamics);
        WriteFloat(sec, L"crossfeed", c.crossfeed);
        WriteFloat(sec, L"correction", (float)c.correction);
    }

    // 按应用记预设
    WriteFloat(L"apps", L"on", perAppOn ? 1.f : 0.f);
    WriteFloat(L"apps", L"count", (float)appExe.size());
    for (size_t i = 0; i < appExe.size() && i < appPreset.size(); ++i) {
        wchar_t sec[16];
        swprintf(sec, 16, L"a%d", (int)i);
        WriteStr(sec, L"exe", appExe[i]);
        WriteFloat(sec, L"preset", (float)appPreset[i]);
    }
}

std::wstring FormatTime(double sec) {
    if (sec < 0 || sec > 1e9) sec = 0;
    int s = (int)(sec + 0.5);
    wchar_t buf[32];
    swprintf(buf, 32, L"%d:%02d", s / 60, s % 60);
    return buf;
}

std::wstring DetectRunningGame(DWORD* pidOut) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return L"";
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    std::wstring found;
    if (Process32FirstW(snap, &pe)) {
        do {
            for (int i = 0; i < kGameCount; ++i) {
                if (_wcsicmp(pe.szExeFile, kGames[i].exe) == 0) {
                    found = kGames[i].label;
                    if (pidOut) *pidOut = pe.th32ProcessID;
                    CloseHandle(snap);
                    return found;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return L"";
}
