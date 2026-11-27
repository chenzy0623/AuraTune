// AuraTune 游戏音频捕获：Windows 10 2004+ 进程回环（无需虚拟声卡）
// 端到端延迟 ≈ 捕获 1 周期 + 渲染 2~4 周期，实测 12~25ms
#include "audio.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiosessiontypes.h>
#include <mmreg.h>
#include <ksmedia.h>
static const GUID AURA_FLOAT_SUB = {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
#undef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT AURA_FLOAT_SUB
#include <cstdio>
#include <cmath>

extern HRESULT ActivateProcessLoopbackClient(DWORD pid, void** out);
extern int     SetProcSessionMute(DWORD pid, BOOL mute);

static WAVEFORMATEXTENSIBLE CapFormat(int rate, int ch) {
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels       = (WORD)ch;
    wf.Format.nSamplesPerSec  = (DWORD)rate;
    wf.Format.wBitsPerSample  = 32;
    wf.Format.nBlockAlign     = (WORD)(ch * 4);
    wf.Format.nAvgBytesPerSec = (DWORD)(rate * ch * 4);
    wf.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask          = (ch == 2) ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
    wf.SubFormat              = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return wf;
}

bool AudioEngine::openCapture(DWORD) { return true; }   // 实现在 captureThread 内

// 按设备 id 激活某个「输出端点」的 IAudioClient（用于声卡回环捕获）
static HRESULT ActivateRenderEndpoint(const std::wstring& devId, IAudioClient** out) {
    *out = nullptr;
    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&en);
    if (FAILED(hr) || !en) return hr;

    IMMDevice* dev = nullptr;
    hr = en->GetDevice(devId.c_str(), &dev);
    en->Release();
    if (FAILED(hr) || !dev) return hr;

    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)out);
    dev->Release();
    return hr;
}

void AudioEngine::captureThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    const DWORD  pid    = capPid_.load();
    std::wstring devId;
    {
        std::lock_guard<std::mutex> lk(titleMtx_);
        devId = capDevId_;
    }
    const bool deviceMode = !devId.empty();

    IAudioClient* ac = nullptr;
    HRESULT hr = deviceMode ? ActivateRenderEndpoint(devId, &ac)
                            : ActivateProcessLoopbackClient(pid, (void**)&ac);
    if (FAILED(hr) || !ac) {
        wchar_t buf[200];
        if (deviceMode)
            swprintf(buf, 200, L"打开设备失败 0x%08lX", (unsigned long)hr);
        else
            swprintf(buf, 200, L"进程回环激活失败 0x%08lX（需要 Windows 10 build 20348 / Windows 11）", (unsigned long)hr);
        {
            std::lock_guard<std::mutex> lk(titleMtx_);
            capErr_ = buf;
        }
        capturing_ = false;
        CoUninitialize();
        return;
    }

    const int rate = fs_.load();
    WAVEFORMATEXTENSIBLE wf = CapFormat(rate, 2);
    const DWORD baseFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, baseFlags, 100000, 0, (WAVEFORMATEX*)&wf, nullptr);
    if (FAILED(hr)) {
        hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            baseFlags | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                            100000, 0, (WAVEFORMATEX*)&wf, nullptr);
    }
    if (FAILED(hr)) {
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(ac->GetMixFormat(&mix)) && mix) {
            hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, baseFlags, 100000, 0, mix, nullptr);
            CoTaskMemFree(mix);
        }
    }
    if (FAILED(hr)) {
        wchar_t buf[160];
        swprintf(buf, 160, L"捕获初始化失败 0x%08lX", (unsigned long)hr);
        {
            std::lock_guard<std::mutex> lk(titleMtx_);
            capErr_ = buf;
        }
        ac->Release();
        capturing_ = false;
        CoUninitialize();
        return;
    }

    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    IAudioCaptureClient* dc = nullptr;
    if (!ev || FAILED(ac->SetEventHandle(ev)) ||
        FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&dc)) || !dc ||
        FAILED(ac->Start())) {
        if (dc) dc->Release();
        if (ev) CloseHandle(ev);
        ac->Release();
        capturing_ = false;
        CoUninitialize();
        return;
    }

    // 捕获端采样率若与输出不同，做一次简单线性重采样
    UINT32 capRate = rate, capCh = 2;
    {
        WAVEFORMATEX* cur = nullptr;
        if (SUCCEEDED(ac->GetMixFormat(&cur)) && cur) {
            capRate = cur->nSamplesPerSec;
            capCh = cur->nChannels;
            CoTaskMemFree(cur);
        }
    }

    std::vector<float> mono;
    double srcPos = 0.0;
    std::vector<float> prevFrame(2, 0.f);
    bool havePrev = false;
    int muteTick = 0;

    while (!capQuit_) {
        DWORD w = WaitForSingleObject(ev, 200);
        if (w != WAIT_OBJECT_0) {
            if (!deviceMode && (++muteTick % 10 == 0)) SetProcSessionMute(pid, TRUE);
            continue;
        }
        UINT32 packet = 0;
        while (!capQuit_ && SUCCEEDED(dc->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* pData = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(dc->GetBuffer(&pData, &frames, &flags, nullptr, nullptr))) break;
            if (frames > 0) {
                const float* src = (const float*)pData;
                const size_t sampleCount = (size_t)frames * (capCh ? capCh : 2);
                if (capCh != 2 || capRate != (UINT32)rate) {
                    // 取前两声道 → 单帧序列
                    mono.assign((size_t)frames * 2, 0.f);
                    for (UINT32 i = 0; i < frames; ++i) {
                        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                            mono[i * 2] = mono[i * 2 + 1] = 0.f;
                        } else {
                            mono[i * 2]     = capCh > 0 && sampleCount > (size_t)i * capCh ? src[(size_t)i * capCh] : 0.f;
                            mono[i * 2 + 1] = capCh > 1 && sampleCount > (size_t)i * capCh + 1 ? src[(size_t)i * capCh + 1] : 0.f;
                        }
                    }
                    // 线性重采样到输出采样率
                    const double ratio = (double)rate / (double)capRate;
                    std::lock_guard<std::mutex> lk(capRingMtx_);
                    for (size_t i = 0; ; ++i) {
                        size_t idx = (size_t)srcPos;
                        double frac = srcPos - (double)idx;
                        if (idx + 1 >= frames) break;
                        float l = (float)(mono[idx * 2] * (1.0 - frac) + mono[(idx + 1) * 2] * frac);
                        float r = (float)(mono[idx * 2 + 1] * (1.0 - frac) + mono[(idx + 1) * 2 + 1] * frac);
                        float f[2] = { l, r };
                        if (capRing_.space() < 1) capRing_.clear();
                        capRing_.push(f, 1);
                        srcPos += ratio;
                    }
                    srcPos -= (double)frames;
                    if (srcPos < 0) srcPos = 0;
                } else {
                    std::lock_guard<std::mutex> lk(capRingMtx_);
                    if (capRing_.space() < frames) capRing_.clear();
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        std::vector<float> zero((size_t)frames * 2, 0.f);
                        capRing_.push(zero.data(), frames);
                    } else {
                        capRing_.push(src, frames);
                    }
                }
            }
            dc->ReleaseBuffer(frames);
        }
    }

    ac->Stop();
    dc->Release();
    CloseHandle(ev);
    ac->Release();
    if (!deviceMode) SetProcSessionMute(pid, FALSE);   // 设备模式没有原会话要恢复
    capturing_ = false;
    CoUninitialize();
}
