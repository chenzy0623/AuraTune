// 缓冲档位对照测试：分别用 0/1/2 三种 bufferMode 打开渲染流，打印 bufferFrames 与延迟
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include "../src/app.h"
#include "../src/audio.h"

int main() {
    SetConsoleOutputCP(65001);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (int m = 0; m <= 2; ++m) {
        AudioEngine e;
        Settings s;
        e.updateSettings(s);
        e.setBufferMode(m);
        e.start();
        Sleep(1500);
        std::wstring dev = e.deviceName();
        char devn[256] = {0};
        WideCharToMultiByte(CP_UTF8, 0, dev.c_str(), -1, devn, 255, nullptr, nullptr);
        printf("mode=%d ok=%d fs=%d frames=%d latency=%.2fms device=%s\n",
               m, (int)e.deviceOk(), e.sampleRate(), e.bufferFrames(),
               (double)e.latencyMs(), devn);
        fflush(stdout);
        e.stop();
        Sleep(300);
    }
    return 0;
}
