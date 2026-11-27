// 捕获路径独立测试：cap_test <pid>
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <objbase.h>
#include <cstring>

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc < 2) { printf("usage: cap_test <pid>\n"); return 1; }
    const DWORD pid = (DWORD)atoi(argv[1]);

    AudioEngine e;
    Settings s;
    s.load();
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--bypass") == 0) s.bypass = true;
        if (strcmp(argv[i], "--nosave") == 0) { /* noop, load only */ }
    }
    printf("start engine...\n"); fflush(stdout);
    e.start();
    e.updateSettings(s);
    printf("startCapture pid=%lu ...\n", (unsigned long)pid); fflush(stdout);
    const bool ok = e.startCapture(pid, L"test");
    printf("started=%d capturing=%d err=\n", (int)ok, (int)e.isCapturing()); fflush(stdout);
    for (int i = 0; i < 25; ++i) {
        Sleep(200);
        MeterData m = e.meter();
        printf("t=%4dms rmsL=%.4f peak=%.3f cap=%d lat=%.1f\n",
               (i + 1) * 200, m.rmsL, m.peakL, (int)e.isCapturing(),
               (double)e.latencyMs());
        fflush(stdout);
        if (i == 2) {
            std::wstring err = e.captureError();
            char u8[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, err.c_str(), -1, u8, 511, nullptr, nullptr);
            printf("   capErr='%s' target=?\n", u8);
            fflush(stdout);
        }
    }
    e.stopCapture();
    e.stop();
    printf("done\n");
    return 0;
}
