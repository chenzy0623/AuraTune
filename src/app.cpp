#include "app.h"
#include <shlwapi.h>
#include <tlhelp32.h>
#include <cwchar>
#include <cmath>

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
    L"高通 HighPass", L"陷波 Notch", L"全通 AllPass", L"带通 BandPass" };
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
    // ---------------- 音乐 ----------------
    { L"平直还原", L"不染色，保留录音原貌", MODE_MUSIC,
      {0,0,0,0,0,0,0,0,0,0}, 0.f, 0.f, 0.f, 1.00f, 0.15f, 0 },
    { L"R&B 暖声", L"低频厚实、人声靠前，节奏蓝调与 Urban 都对味", MODE_MUSIC,
      {0.5f,2.0f,1.2f,0.5f,-0.5f,0.f,0.8f,0.5f,0.f,0.5f}, 1.5f, 0.5f, 0.8f, 1.08f, 0.30f, 1 },
    { L"摇滚 冲击", L"鼓点有拳头感，吉他有砂砾质感", MODE_MUSIC,
      {1.0f,2.5f,1.5f,0.f,-1.2f,-0.5f,0.8f,2.0f,1.5f,0.8f}, 2.0f, 1.0f, 1.0f, 1.12f, 0.45f, 2 },
    { L"迈克尔·杰克逊", L"Beat It 的鼓、Billie Jean 的贝斯都更紧实，高频有亮度", MODE_MUSIC,
      {2.0f,2.2f,0.5f,-1.0f,0.f,0.5f,1.6f,2.2f,1.2f,0.5f}, 1.8f, 1.5f, 0.6f, 1.05f, 0.35f, 1 },
    { L"邓紫棋 女声透亮", L"气息与齿音通透，胸腔共鸣不糊", MODE_MUSIC,
      {0.5f,1.0f,0.f,0.5f,-0.8f,1.2f,2.4f,2.2f,1.4f,1.2f}, 0.6f, 2.0f, 1.2f, 1.06f, 0.30f, 1 },
    { L"周杰伦 华语人声", L"咬字清晰、人声贴耳，适配《青花瓷》《搁浅》类编曲", MODE_MUSIC,
      {0.5f,1.5f,0.8f,0.f,-1.0f,0.8f,2.0f,1.4f,0.6f,0.2f}, 1.2f, 1.6f, 0.5f, 1.04f, 0.35f, 1 },
    { L"陶喆 复古R&B", L"模拟味的暖色中频，贝斯线更圆", MODE_MUSIC,
      {1.0f,2.0f,-0.5f,0.6f,-0.6f,0.6f,1.2f,0.8f,0.2f,0.3f}, 2.0f, 0.8f, 0.2f, 1.02f, 0.40f, 1 },
    { L"女声 流行向", L"通用女声流行，柔和不刺耳", MODE_MUSIC,
      {0.5f,1.2f,0.4f,0.2f,-0.6f,1.0f,1.8f,1.2f,0.6f,0.8f}, 0.8f, 1.4f, 0.9f, 1.05f, 0.28f, 1 },
    { L"深夜低音量", L"等响补偿：小音量下照样有低频与空气感", MODE_MUSIC,
      {1.5f,2.5f,1.0f,0.5f,0.f,0.f,0.5f,0.8f,1.2f,1.8f}, 2.5f, 0.5f, 1.5f, 1.00f, 0.20f, 3 },

    // ---------------- 游戏 ----------------
    { L"FPS 脚步定向", L"压低枪炮轰鸣，抬高 2k-8k 的脚步与换弹细节", MODE_GAME,
      {0.f,1.5f,1.0f,0.f,-1.5f,0.f,2.5f,3.5f,2.0f,0.5f}, 1.0f, 1.5f, 0.f, 1.35f, 0.50f, 0 },
    { L"FPS 竞技均衡", L"不牺牲听感的全能竞技曲线，压缩更克制", MODE_GAME,
      {0.f,1.0f,0.5f,0.f,-1.0f,0.f,1.0f,1.5f,0.8f,0.f}, 0.5f, 0.8f, 0.f, 1.15f, 0.60f, 2 },
    { L"二游 沉浸", L"原神/星铁/绝区零 的管弦与战斗音效更有包围感", MODE_GAME,
      {1.0f,2.0f,1.0f,0.5f,0.f,0.5f,1.0f,1.2f,0.8f,1.5f}, 2.0f, 0.8f, 1.4f, 1.28f, 0.25f, 1 },
    { L"二游 角色语音", L"剧情对话与角色语音更清楚，BGM 不抢戏", MODE_GAME,
      {0.f,0.5f,0.f,-0.8f,0.5f,1.5f,2.2f,1.8f,1.0f,0.5f}, 0.f, 2.2f, 0.4f, 1.10f, 0.40f, 5 },
    { L"通用游戏", L"轻度修形 + 限幅，长时间游玩不累耳", MODE_GAME,
      {0.5f,1.0f,0.5f,0.f,0.f,0.f,0.5f,0.8f,0.5f,0.f}, 0.5f, 0.5f, 0.4f, 1.05f, 0.45f, 2 },

    // ---------------- 3A 单机 ----------------
    { L"3A 剧情沉浸", L"过场与对话优先：中频贴耳、低频不轰，环境音有层次", MODE_GAME,
      {1.5f,1.2f,0.6f,0.8f,0.4f,0.8f,1.0f,0.6f,1.0f,1.6f}, 1.2f, 1.2f, 1.6f, 1.42f, 0.20f, 1 },
    { L"3A 电影低频", L"爆炸、载具、配乐下潜更深，像影院 LFE 补了一层垫底", MODE_GAME,
      {3.0f,3.2f,1.0f,0.2f,-0.8f,0.f,0.6f,0.8f,1.2f,1.0f}, 3.5f, 0.5f, 1.2f, 1.30f, 0.28f, 3 },
    { L"3A 大场面动态", L"保留大动态的爆点冲击，又压住爆音，适合《战神》《GTA》《战地》", MODE_GAME,
      {2.0f,2.4f,0.8f,0.f,-0.6f,0.4f,1.2f,1.6f,1.4f,1.2f}, 2.6f, 1.0f, 1.2f, 1.35f, 0.55f, 2 },
    { L"3A 深夜通宵", L"深夜低音量玩：等响补偿 + 脚步与语音都留住，不吵到人", MODE_GAME,
      {1.8f,2.2f,1.2f,0.6f,0.f,0.6f,1.4f,1.4f,1.0f,1.8f}, 2.4f, 1.4f, 1.8f, 1.15f, 0.25f, 3 },
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
    if (p.mode == MODE_MUSIC) presetMusic = idx; else presetGame = idx;
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
    presetGame    = IniInt(L"ui", L"presetGame", 9);
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
