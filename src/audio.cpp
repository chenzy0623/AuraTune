// AuraTune 音频引擎实现
// 输出：WASAPI 共享模式（优先 IAudioClient3 最小周期 → 超低延迟）
// 解码：Media Foundation Source Reader（MP3 / FLAC / WAV / AAC / WMA）
// 捕获：Windows 10 2004+ 进程回环（无需虚拟声卡驱动，直接抓游戏进程音频）
#include "audio.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <audiosessiontypes.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------- 进程回环定义
typedef enum _AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;
typedef enum _PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;
typedef struct _AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;
typedef struct _AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
} AUDIOCLIENT_ACTIVATION_PARAMS;

static const GUID IID_IAudioClient3_L = {0x7ED4EE07,0x8E67,0x4CD4,{0x8C,0x1A,0x2B,0x7A,0x59,0x87,0xAD,0x42}};

// 让异步激活同步化的小助手
class ActivateHandler : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE      ev  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HRESULT     hr  = E_FAIL;
    IUnknown*   res = nullptr;
    ULONG       ref = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) ||
            IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler))) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement((LONG*)&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement((LONG*)&ref);
        if (r == 0) { CloseHandle(ev); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hrA = E_FAIL;
        IUnknown* unk = nullptr;
        if (op) op->GetActivateResult(&hrA, &unk);
        hr = hrA;
        if (SUCCEEDED(hrA)) res = unk;
        else if (unk) unk->Release();
        SetEvent(ev);
        return S_OK;
    }
};

extern "C++" HRESULT ActivateProcessLoopbackClient(DWORD pid, void** out) {
    *out = nullptr;
    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = (BYTE*)&ap;

    ActivateHandler* h = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    // 用 IID_IAudioClient 激活：进程回环只保证返回 IAudioClient，
    // 传 IID_IAudioClient3 在部分系统上会返回 E_ILLEGAL_METHOD_CALL(0x8000000E)。
    HRESULT hr = ActivateAudioInterfaceAsync(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &pv, h, &op);
    if (SUCCEEDED(hr)) {
        WaitForSingleObject(h->ev, 4000);
        hr = h->hr;
        if (SUCCEEDED(hr) && h->res) { *out = h->res; h->res = nullptr; }
    }
    if (op) op->Release();
    h->Release();
    // 注意：绝不能对指向栈上数据的 VT_BLOB 调 PropVariantClear()
    // —— 它会 CoTaskMemFree 这个栈地址，造成堆损坏（0xC0000374）。
    pv.vt = VT_EMPTY;
    pv.blob.pBlobData = nullptr;
    pv.blob.cbSize = 0;
    return hr;
}

// 静音 / 恢复指定进程在默认输出设备上的所有音频会话
extern "C++" int SetProcSessionMute(DWORD pid, BOOL mute) {
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&en))) return 0;
    IMMDevice* dev = nullptr;
    int n = 0;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
        IAudioSessionManager2* sm = nullptr;
        if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&sm)) && sm) {
            IAudioSessionEnumerator* list = nullptr;
            if (SUCCEEDED(sm->GetSessionEnumerator(&list)) && list) {
                int cnt = 0;
                list->GetCount(&cnt);
                for (int i = 0; i < cnt; ++i) {
                    IAudioSessionControl* c = nullptr;
                    if (FAILED(list->GetSession(i, &c)) || !c) continue;
                    IAudioSessionControl2* c2 = nullptr;
                    if (SUCCEEDED(c->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2)) && c2) {
                        DWORD spid = 0;
                        c2->GetProcessId(&spid);
                        if (spid == pid) {
                            ISimpleAudioVolume* v = nullptr;
                            if (SUCCEEDED(c2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&v)) && v) {
                                v->SetMute(mute, nullptr);
                                v->Release();
                                ++n;
                            }
                        }
                        c2->Release();
                    }
                    c->Release();
                }
                list->Release();
            }
            sm->Release();
        }
        dev->Release();
    }
    en->Release();
    return n;
}

// ---------------------------------------------------------------- 工具
static WAVEFORMATEXTENSIBLE MakeFloatFormat(int rate, int ch) {
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels       = (WORD)ch;
    wf.Format.nSamplesPerSec  = (DWORD)rate;
    wf.Format.wBitsPerSample  = 32;
    wf.Format.nBlockAlign     = (WORD)(ch * 4);
    wf.Format.nAvgBytesPerSec = (DWORD)(rate * ch * 4);
    wf.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask          = ch == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
    wf.SubFormat              = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return wf;
}

static double QpcMs() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

// ---------------------------------------------------------------- 构造 / 析构
AudioEngine::AudioEngine() {
    playerRing_.init(48000 * 4);
    capRing_.init(48000 * 2);
}
AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start() {
    MFStartup(MF_VERSION);
    if (thRender_.joinable() || thDecoder_.joinable()) return true;
    quit_ = false;
    thDecoder_ = std::thread(&AudioEngine::decoderThread, this);
    thRender_  = std::thread(&AudioEngine::renderThread, this);
    return true;
}

void AudioEngine::stop() {
    quit_ = true;
    stopCapture();
    if (thRender_.joinable())  thRender_.join();
    if (thDecoder_.joinable()) thDecoder_.join();
    MFShutdown();
}

// ---------------------------------------------------------------- 播放控制
bool AudioEngine::openFile(const std::wstring& path) {
    if (path.empty()) return false;
    {
        std::lock_guard<std::mutex> lk(playMtx_);
        pendingPath_ = path;
        pendingClose_ = false;
    }
    trackPath_ = path;
    eof_ = false;
    return true;
}

void AudioEngine::closeFile() {
    {
        std::lock_guard<std::mutex> lk(playMtx_);
        pendingClose_ = true;
        pendingPath_.clear();
    }
    paused_ = true; playReq_ = false; eof_ = true;
    trackOpen_ = false;
    trackDuration_ = 0;
}

void AudioEngine::play() {
    if (eof_) { seekReq_ = 0.0; eof_ = false; }
    paused_ = false; playReq_ = true;
}
void AudioEngine::pause() { paused_ = true; playReq_ = false; }
void AudioEngine::togglePlay() { if (isPlaying()) pause(); else play(); }
bool AudioEngine::isPlaying() const { return playReq_ && !paused_ && trackOpen_ && !eof_; }
bool AudioEngine::hasTrack() const { return trackOpen_; }

void AudioEngine::seekTo(double sec) {
    if (sec < 0) sec = 0;
    seekReq_ = sec;
    startOffset_ = sec;
    framesRendered_ = 0;
    eof_ = false;
    std::lock_guard<std::mutex> lk(playMtx_);
    playerRing_.clear();
}

double AudioEngine::position() const {
    return startOffset_ + (double)framesRendered_ / (double)fs_;
}
double AudioEngine::duration() const { return trackDuration_; }

std::wstring AudioEngine::title() const {
    std::lock_guard<std::mutex> lk(titleMtx_);
    return trackTitle_;
}

void AudioEngine::updateSettings(const Settings& s) {
    std::lock_guard<std::mutex> lk(mtx_);
    params_ = s;
    paramsDirty_ = true;
}
void AudioEngine::setVolumePercent(int pct, bool) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    volPct_ = pct;
    masterGain_ = (float)pct / 100.0f;
}

std::wstring AudioEngine::deviceName() const {
    std::lock_guard<std::mutex> lk(titleMtx_);
    return devName_;
}
void AudioEngine::requestReconnect() { reconnect_ = true; }

MeterData AudioEngine::meter() const {
    std::lock_guard<std::mutex> lk(meterMtx_);
    return meter_;
}
float AudioEngine::dspGr() const { return dsp_.grDb(); }

bool AudioEngine::startCapture(DWORD pid, const std::wstring& label) {
    stopCapture();
    if (pid == 0) return false;
    capPid_ = pid;
    capDevId_.clear();
    {
        std::lock_guard<std::mutex> lk(titleMtx_);
        capLabel_ = label;
        capErr_.clear();
    }
    capturing_ = true;
    capQuit_ = false;
    {
        std::lock_guard<std::mutex> lk(capRingMtx_);
        capRing_.clear();
    }
    SetProcSessionMute(pid, TRUE);
    thCapture_ = std::thread(&AudioEngine::captureThread, this);
    return true;
}

// 声卡回环捕获：抓某个输出设备上正在播放的声音（Win10 也支持，无需 20348）
bool AudioEngine::startCaptureDevice(const std::wstring& deviceId, const std::wstring& label) {
    stopCapture();
    if (deviceId.empty()) return false;
    capPid_ = 0;
    capDevId_ = deviceId;
    {
        std::lock_guard<std::mutex> lk(titleMtx_);
        capLabel_ = label;
        capErr_.clear();
    }
    capturing_ = true;
    capQuit_ = false;
    {
        std::lock_guard<std::mutex> lk(capRingMtx_);
        capRing_.clear();
    }
    thCapture_ = std::thread(&AudioEngine::captureThread, this);
    return true;
}

void AudioEngine::stopCapture() {
    if (thCapture_.joinable()) {
        capQuit_ = true;
        thCapture_.join();
    }
    if (capPid_ != 0) SetProcSessionMute(capPid_, FALSE);
    capturing_ = false;
    {
        std::lock_guard<std::mutex> lk(capRingMtx_);
        capRing_.clear();
    }
}

// 指定输出设备（传空串回默认设备）
void AudioEngine::setRenderDevice(const std::wstring& id, const std::wstring& name) {
    {
        std::lock_guard<std::mutex> lk(titleMtx_);
        outDevIdOverride_ = id;
        outDevNameOverride_ = name;
    }
    reconnect_ = true;
}

std::wstring AudioEngine::renderDeviceId() const {
    std::lock_guard<std::mutex> lk(titleMtx_);
    return outDevIdOverride_;
}

std::wstring AudioEngine::captureTarget() const {
    std::lock_guard<std::mutex> lk(titleMtx_);
    return capLabel_;
}
std::wstring AudioEngine::captureError() const {
    std::lock_guard<std::mutex> lk(titleMtx_);
    return capErr_;
}
