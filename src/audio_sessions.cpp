// AuraTune 音频会话枚举：列出「正在出声」的应用（音乐模式选择处理目标用）
// 纯 WASAPI 会话 API（IAudioSessionManager2），无需驱动。
#include "audio.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>        // 必须在 mmdeviceapi.h 之后
#include <audiosessiontypes.h>
#include <tlhelp32.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <cwchar>

// mingw 下 IAudioSessionManager2 / IAudioSessionControl2 的 __uuidof 有时链接不上，
// 这里直接用本地 IID 常量。
// 实测（Windows 11 + mingw-w64 头文件）：IAudioSessionControl2 的 IID 末两位是 9C6D，
// SDK 文档 / 旧代码里常见的 9C6F 在本机运行时会 QueryInterface 失败(E_NOINTERFACE)。
// 本文件先试 __uuidof（编译期静态 GUID，无链接依赖），失败再退化到常量 9C6D。
static const IID IID_IAudioSessionManager2_L  = {0x77AA99A0,0x1BD6,0x484F,{0x8B,0xC7,0x2C,0x65,0x4C,0x9A,0x9B,0x6F}};
static const IID IID_IAudioSessionControl2_L  = {0xBFB7FF88,0x7239,0x4FC9,{0x8F,0xA2,0x07,0xC9,0x50,0xBE,0x9C,0x6D}};

// 拿到 IAudioSessionControl2：__uuidof 优先，失败再试字面 IID
static IAudioSessionControl2* AsSessionControl2(IAudioSessionControl* ctl) {
    IAudioSessionControl2* c2 = nullptr;
    if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2)) && c2)
        return c2;
    c2 = nullptr;
    if (SUCCEEDED(ctl->QueryInterface(IID_IAudioSessionControl2_L, (void**)&c2)) && c2)
        return c2;
    return nullptr;
}

// ------------------------------------------------------------------ 小工具
static std::wstring FileNamePart(const std::wstring& path) {
    const size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

static bool IsUnknownName(const std::wstring& exe) { return exe == L"未知进程"; }

// 同一应用：exe 名相同；连名字都取不到的，退化为 pid 相同才算同一个
static bool SameApp(const AudioApp& a, const AudioApp& b) {
    if (IsUnknownName(a.exe) || IsUnknownName(b.exe)) return a.pid == b.pid;
    return _wcsicmp(a.exe.c_str(), b.exe.c_str()) == 0;
}

static std::wstring ProcessExeName(DWORD pid) {
    // 先按任务要求带 VM_READ；受保护/提权进程会拒绝，退一步只要查询权限
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::wstring();
    wchar_t buf[1024];
    DWORD   n = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    std::wstring name;
    if (QueryFullProcessImageNameW(h, 0, buf, &n) && n > 0)
        name = FileNamePart(std::wstring(buf, n));
    CloseHandle(h);
    return name;
}

struct WindowSearch {
    DWORD        pid = 0;
    std::wstring title;
};

static BOOL CALLBACK FindTitleProc(HWND hwnd, LPARAM lp) {
    WindowSearch* d = (WindowSearch*)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != d->pid) return TRUE;
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;
    std::wstring t((size_t)len + 1, L'\0');
    const int n = GetWindowTextW(hwnd, &t[0], len + 1);
    if (n <= 0) return TRUE;
    t.resize((size_t)n);
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t' ||
                          t.back() == L'\r' || t.back() == L'\n'))
        t.pop_back();
    if (t.empty()) return TRUE;
    d->title = t;
    return FALSE;      // 找到第一个就停
}

static std::wstring TopWindowTitle(DWORD pid) {
    WindowSearch d;
    d.pid = pid;
    EnumWindows(FindTitleProc, (LPARAM)&d);
    return d.title;
}

// ------------------------------------------------------------------ session 枚举
std::vector<AudioApp> ListAudioSessions() {
    std::vector<AudioApp> out;

    // 已初始化过（RPC_E_CHANGED_MODE）也能继续用，只是不要配对 CoUninitialize
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    uninit = SUCCEEDED(hrCo);

    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&en);
    if (FAILED(hr) || !en) {
        if (uninit) CoUninitialize();
        return out;
    }

    IMMDevice* dev = nullptr;
    hr = en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev);
    if (FAILED(hr) || !dev) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    en->Release();
    if (FAILED(hr) || !dev) {
        if (uninit) CoUninitialize();
        return out;
    }

    IAudioSessionManager2* sm = nullptr;
    hr = dev->Activate(IID_IAudioSessionManager2_L, CLSCTX_ALL, nullptr, (void**)&sm);
    dev->Release();
    if (FAILED(hr) || !sm) {
        if (uninit) CoUninitialize();
        return out;
    }

    IAudioSessionEnumerator* list = nullptr;
    hr = sm->GetSessionEnumerator(&list);
    if (FAILED(hr) || !list) {
        sm->Release();
        if (uninit) CoUninitialize();
        return out;
    }

    const DWORD self = GetCurrentProcessId();
    int count = 0;
    if (FAILED(list->GetCount(&count))) count = 0;

    for (int i = 0; i < count; ++i) {
        IAudioSessionControl* ctl = nullptr;
        if (FAILED(list->GetSession(i, &ctl)) || !ctl) continue;

        IAudioSessionControl2* c2 = AsSessionControl2(ctl);
        if (c2) {
            DWORD pid = 0;
            AudioSessionState state = AudioSessionStateExpired;
            if (SUCCEEDED(c2->GetProcessId(&pid)) && pid != 0 && pid != self &&
                SUCCEEDED(c2->GetState(&state)) && state != AudioSessionStateExpired) {

                AudioApp app;
                app.pid    = pid;
                app.active = (state == AudioSessionStateActive);
                app.exe    = ProcessExeName(pid);
                if (app.exe.empty()) app.exe = L"未知进程";

                app.label = TopWindowTitle(pid);
                if (app.label.empty()) {
                    std::wstring base = app.exe;
                    if (base.size() > 4 &&
                        _wcsicmp(base.c_str() + base.size() - 4, L".exe") == 0)
                        base.resize(base.size() - 4);
                    app.label = base;
                } else if (app.label.size() > 40) {
                    app.label.resize(40);
                }

                // 同一个 exe 只留一条：优先 active，其次 pid 更大（更晚启动）
                std::vector<AudioApp>::iterator it =
                    std::find_if(out.begin(), out.end(),
                                 [&](const AudioApp& a) { return SameApp(a, app); });
                if (it == out.end()) {
                    out.push_back(app);
                } else if ((app.active && !it->active) ||
                           (app.active == it->active && app.pid > it->pid)) {
                    *it = app;
                }
            }
            c2->Release();
        }
        ctl->Release();
    }

    list->Release();
    sm->Release();
    if (uninit) CoUninitialize();

    std::sort(out.begin(), out.end(), [](const AudioApp& a, const AudioApp& b) {
        if (a.active != b.active) return a.active;      // 正在出声的排前面
        return _wcsicmp(a.exe.c_str(), b.exe.c_str()) < 0;
    });
    return out;
}

// ------------------------------------------------------------------ 按 exe 找 pid
DWORD FindProcessByExe(const std::wstring& exeName) {
    if (exeName.empty()) return 0;
    const std::wstring want = FileNamePart(exeName);   // 允许传全路径

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    DWORD found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, want.c_str()) == 0) {
                found = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// ---------------------------------------------------------------- 输出设备枚举
// 列出所有「已启用」的播放端点（含虚拟声卡），标出系统默认项。
// 排序：默认设备最前，其后按名称字母序。
std::vector<AudioDevice> ListRenderDevices() {
    std::vector<AudioDevice> out;

    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    uninit = SUCCEEDED(hrCo);

    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&en)) || !en) {
        if (uninit) CoUninitialize();
        return out;
    }

    // 默认输出设备的 id，用于标记
    std::wstring defId;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &def)) && def) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id)) && id) { defId = id; CoTaskMemFree(id); }
        def->Release();
    }

    IMMDeviceCollection* col = nullptr;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) || !col) {
        en->Release();
        if (uninit) CoUninitialize();
        return out;
    }

    UINT count = 0;
    col->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* d = nullptr;
        if (FAILED(col->Item(i, &d)) || !d) continue;

        AudioDevice ad;
        LPWSTR id = nullptr;
        if (SUCCEEDED(d->GetId(&id)) && id) { ad.id = id; CoTaskMemFree(id); }

        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
                ad.name = pv.pwszVal;
            PropVariantClear(&pv);   // 这里是真正的 VT_LPWSTR，必须清
            ps->Release();
        }
        d->Release();
        if (ad.id.empty()) continue;
        if (ad.name.empty()) ad.name = L"未命名设备";
        ad.isDefault = (!defId.empty() && ad.id == defId);
        out.push_back(ad);
    }
    col->Release();
    en->Release();
    if (uninit) CoUninitialize();

    std::stable_sort(out.begin(), out.end(), [](const AudioDevice& a, const AudioDevice& b) {
        if (a.isDefault != b.isDefault) return a.isDefault;
        return a.name < b.name;
    });
    return out;
}

// ---------------------------------------------------------------- 系统默认播放设备
// Windows 没有公开 API 切换默认播放设备，这里用音频策略服务的未公开接口
// IPolicyConfig（SoundSwitch / nircmd 等都是同样做法）。Win10 19045 / Win11 实测可用。
static const CLSID CLSID_CPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const IID   IID_IPolicyConfig_L       = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x08, 0x1e, 0x50, 0x8a, 0x0a}};

typedef HRESULT(STDMETHODCALLTYPE* SetDefaultEndpoint_t)(void* self, LPCWSTR deviceId, int role);

static void* GetPolicyConfig() {
    void* pc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CPolicyConfigClient, nullptr, CLSCTX_ALL,
                                IID_IPolicyConfig_L, &pc)) || !pc)
        return nullptr;
    return pc;
}

bool SetSystemDefaultRenderDevice(const std::wstring& deviceId) {
    if (deviceId.empty()) return false;
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    uninit = SUCCEEDED(hrCo);

    void* pc = GetPolicyConfig();
    if (!pc) { if (uninit) CoUninitialize(); return false; }

    void** vt = *(void***)pc;
    SetDefaultEndpoint_t fn = (SetDefaultEndpoint_t)vt[13];   // SetDefaultEndpoint
    // 三个角色都切（控制台 / 多媒体 / 通信），否则部分播放器仍走旧设备
    bool ok = SUCCEEDED(fn(pc, deviceId.c_str(), 0)) && SUCCEEDED(fn(pc, deviceId.c_str(), 1));
    fn(pc, deviceId.c_str(), 2);
    ((IUnknown*)pc)->Release();
    if (uninit) CoUninitialize();
    return ok;
}

std::wstring GetSystemDefaultRenderId() {
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    uninit = SUCCEEDED(hrCo);
    std::wstring out;
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), (void**)&en)) && en) {
        IMMDevice* dev = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(dev->GetId(&id)) && id) { out = id; CoTaskMemFree(id); }
            dev->Release();
        }
        en->Release();
    }
    if (uninit) CoUninitialize();
    return out;
}
