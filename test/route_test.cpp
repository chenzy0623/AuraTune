// route_test —— 验证「虚拟声卡回环」整条链路：
//   渲染线程把测试音（1kHz 正弦）送到 CABLE Input
//   捕获线程从同一个 CABLE Input 回环抓回来
//   只要 meter.rms > 0 就说明：设备回环捕获 + DSP + 输出 全部通了。
#include "audio.h"
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <string>

static std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // 找 CABLE Input（VB-Audio Virtual Cable 的播放端）
    std::wstring cableId, cableName;
    for (const auto& d : ListRenderDevices()) {
        printf("  dev: [%s] %s%s\n", U8(d.name).c_str(), d.isDefault ? " (默认)" : "", "");
        if (cableId.empty() && d.name.find(L"CABLE Input") != std::wstring::npos) {
            cableId = d.id;
            cableName = d.name;
        }
    }
    if (cableId.empty()) { printf("没找到 CABLE Input，退出\n"); return 1; }
    printf("用虚拟声卡: %s\n", U8(cableName).c_str());

    Settings s;
    s.load();
    s.testTone = TT_SINE;
    s.testFreq = 1000.f;
    s.testLevel = -12.f;
    s.bypass = false;

    AudioEngine e;
    e.setRenderDevice(cableId, cableName);     // 声音送到 CABLE Input
    if (!e.start()) { printf("引擎启动失败\n"); return 2; }
    e.updateSettings(s);
    Sleep(600);
    printf("渲染设备: %s  ok=%d\n", U8(e.deviceName()).c_str(), (int)e.deviceOk());

    if (!e.startCaptureDevice(cableId, cableName)) { printf("捕获启动失败\n"); return 3; }
    for (int i = 0; i < 12; ++i) {
        Sleep(300);
        MeterData m = e.meter();
        printf("t=%.1fs cap=%d rms=%.4f peak=%.4f\n", (i + 1) * 0.3,
               (int)e.isCapturing(), m.rmsL, m.peakL);
    }
    printf("captureError: %s\n", U8(e.captureError()).c_str());
    e.stopCapture();
    e.stop();
    CoUninitialize();
    return 0;
}
