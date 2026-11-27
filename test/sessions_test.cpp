// 临时控制台测试：枚举默认输出设备上正在出声的音频会话
// 编译（项目根目录）：
//   /d/llvm-mingw/bin/g++ -std=c++17 -O2 test/sessions_test.cpp src/audio_sessions.cpp \
//       -o test/sessions_test.exe -lole32 -lmmdevapi -luuid
#include "../src/audio.h"
#include <cstdio>
#include <string>
#include <windows.h>

static std::string U8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("===== ListAudioSessions() =====\n");
    const std::vector<AudioApp> v = ListAudioSessions();
    printf("count = %u\n", (unsigned)v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const AudioApp& a = v[i];
        printf("[%u] pid=%-6lu active=%-5s exe=%-28s label=%s\n",
               (unsigned)(i + 1), (unsigned long)a.pid, a.active ? "true" : "false",
               U8(a.exe).c_str(), U8(a.label).c_str());
    }
    if (v.empty()) printf("(列表为空：当前没有第三方应用在默认输出设备上持有音频会话)\n");

    printf("\n===== FindProcessByExe() =====\n");
    const DWORD pidExp = FindProcessByExe(L"explorer.exe");
    printf("explorer.exe      -> pid=%lu %s\n", (unsigned long)pidExp, pidExp ? "(found)" : "(not found)");
    const DWORD pidCase = FindProcessByExe(L"EXPLORER.EXE");       // 大小写不敏感
    printf("EXPLORER.EXE      -> pid=%lu %s\n", (unsigned long)pidCase, pidCase ? "(found)" : "(not found)");
    const DWORD pidNone = FindProcessByExe(L"no_such_proc_xyz.exe");
    printf("no_such_proc.exe  -> pid=%lu %s\n", (unsigned long)pidNone, pidNone ? "(found)" : "(not found)");

    // 常见会出声的进程，顺便看看哪个在跑（不依赖窗口）
    const wchar_t* probe[] = { L"chrome.exe", L"msedge.exe", L"QQMusic.exe", L"cloudmusic.exe",
                               L"Spotify.exe", L"foobar2000.exe", L"potplayermini64.exe",
                               L"svchost.exe", L"bash.exe" };
    printf("\n----- 其它进程探测 -----\n");
    for (int i = 0; i < (int)(sizeof(probe) / sizeof(probe[0])); ++i) {
        const DWORD p = FindProcessByExe(probe[i]);
        printf("%-24s -> pid=%lu\n", U8(probe[i]).c_str(), (unsigned long)p);
    }

    // 二次调用：确认重复调用不崩、COM 初始化可重入
    const std::vector<AudioApp> v2 = ListAudioSessions();
    printf("\nsecond call count = %u (should match %u)\n", (unsigned)v2.size(), (unsigned)v.size());
    return 0;
}
