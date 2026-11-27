// cap_null —— 试验：以 nullptr 激活参数打开 "VAD\Process_Loopback"，
// 看能否真的抓到系统/其它进程的音频（本进程不渲染声音，所以不会自激）。
// 用法：cap_null [秒数]
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <ksmedia.h>
#include <cstdio>
#include <cmath>
#include <objbase.h>

static const GUID AURA_FLOAT_SUB = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
#undef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT AURA_FLOAT_SUB

class Handler : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HRESULT hr = E_FAIL;
    IUnknown* res = nullptr;
    IUnknown* ftm = nullptr;
    ULONG ref = 1;
    Handler() { CoCreateFreeThreadedMarshaler(this, &ftm); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler))) {
            *ppv = this; AddRef(); return S_OK;
        }
        if (ftm && IsEqualIID(riid, __uuidof(IMarshal))) { *ppv = ftm; ftm->AddRef(); return S_OK; }
        if (ftm && IsEqualIID(riid, IID_IAgileObject)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement((LONG*)&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement((LONG*)&ref);
        if (!r) { CloseHandle(ev); if (ftm) ftm->Release(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hrA = E_FAIL, hrO = E_FAIL;
        IUnknown* unk = nullptr;
        op->GetActivateResult(&hrA, &unk);
        hrO = hrA;
        if (SUCCEEDED(hrA)) res = unk; else if (unk) unk->Release();
        hr = hrO;
        SetEvent(ev);
        return S_OK;
    }
};

static WAVEFORMATEXTENSIBLE Fmt(int rate, int ch) {
    WAVEFORMATEXTENSIBLE w{};
    w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w.Format.nChannels = (WORD)ch;
    w.Format.nSamplesPerSec = rate;
    w.Format.wBitsPerSample = 32;
    w.Format.nBlockAlign = (WORD)(ch * 4);
    w.Format.nAvgBytesPerSec = rate * ch * 4;
    w.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    w.Samples.wValidBitsPerSample = 32;
    w.dwChannelMask = (ch == 2) ? 3 : 4;
    w.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return w;
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const int secs = argc > 1 ? atoi(argv[1]) : 3;

    Handler* h = new Handler();
    IActivateAudioInterfaceAsyncOperation* opA = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(L"VAD\\Process_Loopback", __uuidof(IAudioClient), nullptr, h, &opA);
    printf("call hr=0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr)) { h->Release(); CoUninitialize(); return 1; }
    WaitForSingleObject(h->ev, 5000);
    printf("activate hr=0x%08lX got=%p\n", (unsigned long)h->hr, h->res);
    if (opA) opA->Release();
    if (!h->res) { h->Release(); CoUninitialize(); return 2; }

    IAudioClient* ac = nullptr;
    h->res->QueryInterface(__uuidof(IAudioClient), (void**)&ac);
    h->res->Release();
    h->Release();
    if (!ac) { printf("no IAudioClient\n"); CoUninitialize(); return 3; }

    WAVEFORMATEXTENSIBLE fmt = Fmt(48000, 2);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HRESULT hr2 = ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 1000000, 0, &fmt.Format, nullptr);
    printf("initialize hr=0x%08lX\n", (unsigned long)hr2);
    if (FAILED(hr2)) { ac->Release(); CoUninitialize(); return 4; }
    ac->SetEventHandle(ev);
    UINT32 bufFrames = 0;
    ac->GetBufferSize(&bufFrames);
    printf("bufFrames=%u\n", (unsigned)bufFrames);

    IAudioCaptureClient* cc = nullptr;
    hr2 = ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc);
    printf("getservice hr=0x%08lX\n", (unsigned long)hr2);
    if (FAILED(hr2) || !cc) { ac->Release(); CoUninitialize(); return 5; }
    ac->Start();
    printf("started, capturing %d s...\n", secs);

    double sum = 0.0;
    float peak = 0.f;
    long long total = 0;
    int packets = 0;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < (DWORD)(secs * 1000)) {
        UINT32 packet = 0;
        if (FAILED(cc->GetNextPacketSize(&packet))) break;
        while (packet) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            if (frames) {
                packets++;
                total += frames;
                const float* f = (const float*)data;
                for (UINT32 i = 0; i < frames * 2; ++i) {
                    float v = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.f : f[i];
                    sum += (double)v * v;
                    if (fabsf(v) > peak) peak = fabsf(v);
                }
            }
            cc->ReleaseBuffer(frames);
            if (FAILED(cc->GetNextPacketSize(&packet))) break;
        }
        Sleep(5);
    }
    double rms = total ? sqrt(sum / (double)total) : 0.0;
    printf("packets=%d frames=%lld rms=%.5f peak=%.5f\n", packets, total, rms, peak);
    ac->Stop();
    cc->Release();
    ac->Release();
    CloseHandle(ev);
    CoUninitialize();
    return 0;
}
