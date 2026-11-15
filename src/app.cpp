#include "app.h"
#include <shlwapi.h>
#include <tlhelp32.h>
#include <cwchar>

const float kBandFreq[kBands] = { 31.f, 62.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };
const wchar_t* kBandLabel[kBands] = { L"31", L"62", L"125", L"250", L"500", L"1k", L"2k", L"4k", L"8k", L"16k" };

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

void Settings::applyPreset(int idx) {
    if (idx < 0 || idx >= kPresetCount) return;
    const Preset& p = kPresets[idx];
    for (int b = 0; b < kBands; ++b) eq[b] = p.eq[b];
    bass = p.bass; vocal = p.vocal; ambience = p.ambience;
    width = p.width; dynamics = p.dynamics; correction = p.correction;
    if (p.mode == MODE_MUSIC) presetMusic = idx; else presetGame = idx;
    mode = p.mode;
}

void Settings::load() {
    mode          = IniInt(L"ui", L"mode", MODE_MUSIC);
    presetMusic   = IniInt(L"ui", L"presetMusic", 1);
    presetGame    = IniInt(L"ui", L"presetGame", 9);
    for (int b = 0; b < kBands; ++b) {
        wchar_t key[16];
        swprintf(key, 16, L"eq%d", b);
        eq[b] = IniFloat(L"eq", key, kPresets[1].eq[b]);
    }
    bass          = IniFloat(L"dsp", L"bass", 1.5f);
    vocal         = IniFloat(L"dsp", L"vocal", 0.5f);
    ambience      = IniFloat(L"dsp", L"ambience", 0.8f);
    width         = IniFloat(L"dsp", L"width", 1.08f);
    dynamics      = IniFloat(L"dsp", L"dynamics", 0.30f);
    crossfeed     = IniFloat(L"dsp", L"crossfeed", 0.35f);
    correction    = IniInt(L"dsp", L"correction", 1);
    vol           = IniFloat(L"dsp", L"vol", 0.f);
    balance       = IniFloat(L"dsp", L"balance", 0.f);
    bypass        = IniInt(L"dsp", L"bypass", 0) != 0;
    volumePercent = IniInt(L"play", L"volume", 80);
    autoSwitch    = IniInt(L"ui", L"autoSwitch", 1) != 0;
    startMinimized= IniInt(L"ui", L"startMinimized", 0) != 0;
    closeToTray   = IniInt(L"ui", L"closeToTray", 1) != 0;
    lastSeek      = IniFloat(L"play", L"seek", 0.f);

    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(L"play", L"folder", L"", buf, MAX_PATH * 2, SettingsPath().c_str());
    lastFolder = buf;
    GetPrivateProfileStringW(L"play", L"track", L"", buf, MAX_PATH * 2, SettingsPath().c_str());
    lastTrack = buf;
}

void Settings::save() const {
    WriteFloat(L"ui", L"mode",        (float)mode);
    WriteFloat(L"ui", L"presetMusic", (float)presetMusic);
    WriteFloat(L"ui", L"presetGame",  (float)presetGame);
    for (int b = 0; b < kBands; ++b) {
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
    WriteFloat(L"dsp", L"vol", vol);
    WriteFloat(L"dsp", L"balance", balance);
    WriteFloat(L"dsp", L"bypass", bypass ? 1.f : 0.f);
    WriteFloat(L"play", L"volume", (float)volumePercent);
    WriteFloat(L"ui", L"autoSwitch", autoSwitch ? 1.f : 0.f);
    WriteFloat(L"ui", L"startMinimized", startMinimized ? 1.f : 0.f);
    WriteFloat(L"ui", L"closeToTray", closeToTray ? 1.f : 0.f);
    WriteFloat(L"play", L"seek", lastSeek);
    WritePrivateProfileStringW(L"play", L"folder", lastFolder.c_str(), SettingsPath().c_str());
    WritePrivateProfileStringW(L"play", L"track", lastTrack.c_str(), SettingsPath().c_str());
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
