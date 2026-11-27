// defdev_test —— 验证未公开 IPolicyConfig 切换系统默认播放设备
#include "audio.h"
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <string>

static const CLSID CLSID_CPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const IID   IID_IPolicyConfig_L       = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x08, 0x1e, 0x50, 0x8a, 0x0a}};

static std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    printf("默认 id = %s\n", U8(GetSystemDefaultRenderId()).c_str());

    void* pc = nullptr;
    static const CLSID CLSID_Vista = {0x294935CE,0xF637,0x4E7C,{0xA4,0x1B,0xAB,0x25,0x54,0x60,0xB8,0x62}};
    HRESULT hr = CoCreateInstance(CLSID_CPolicyConfigClient, nullptr, CLSCTX_ALL, IID_IPolicyConfig_L, &pc);
    if (!pc) hr = CoCreateInstance(CLSID_Vista, nullptr, CLSCTX_ALL, IID_IPolicyConfig_L, &pc);
    printf("CoCreateInstance hr=0x%08lX pc=%p\n", (unsigned long)hr, pc);
    if (!pc) return 1;

    for (const auto& d : ListRenderDevices())
        printf("  dev: %s  [%s]\n", U8(d.name).c_str(), U8(d.id).c_str());

    // 选 CABLE Input
    std::wstring cable;
    for (const auto& d : ListRenderDevices())
        if (d.name.find(L"CABLE Input") != std::wstring::npos) cable = d.id;
    if (cable.empty()) { printf("no CABLE Input\n"); return 2; }

    void** vt = *(void***)pc;
    typedef HRESULT(STDMETHODCALLTYPE* F)(void*, LPCWSTR, int);
    F fn = (F)vt[13];
    for (int role = 0; role < 3; ++role) {
        HRESULT r = fn(pc, cable.c_str(), role);
        printf("SetDefaultEndpoint role=%d hr=0x%08lX\n", role, (unsigned long)r);
    }
    printf("改后默认 id = %s\n", U8(GetSystemDefaultRenderId()).c_str());
    ((IUnknown*)pc)->Release();
    CoUninitialize();
    return 0;
}
