// AuraTune 低延迟输出：WASAPI 共享模式 + IAudioClient3 最小周期 + 频谱/电平
#include "audio.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiosessiontypes.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <ksmedia.h>
static const GUID AURA_FLOAT_SUB = {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
#undef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT AURA_FLOAT_SUB
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>

static void DbgLog(const wchar_t* fmt, ...) {
    if (GetFileAttributesW(L"E:\\AuraTune_Source\\build\\dbg.flag") == INVALID_FILE_ATTRIBUTES) return;
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, L"E:\\AuraTune_Source\\build\\dbg.log", L"a") != 0 || !fp) return;
    va_list ap; va_start(ap, fmt);
    vfwprintf(fp, fmt, ap);
    va_end(ap);
    fclose(fp);
}

static const GUID IID_IAudioClient3L = {0x7ED4EE07,0x8E67,0x4CD4,{0x8C,0x1A,0x2B,0x7A,0x59,0x87,0xAD,0x42}};

static WAVEFORMATEXTENSIBLE MakeFloatFormat2(int rate, int ch) {
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

static bool GetFriendlyName2(IMMDevice* dev, std::wstring& out) {
    IPropertyStore* ps = nullptr;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &ps)) || !ps) return false;
    PROPVARIANT v;
    PropVariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
        out = v.pwszVal;
        ok = true;
    }
    PropVariantClear(&v);
    ps->Release();
    return ok;
}

static std::wstring DefaultRenderId() {
    IMMDeviceEnumerator* en = nullptr;
    std::wstring id;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&en))) return id;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
        LPWSTR s = nullptr;
        if (SUCCEEDED(dev->GetId(&s)) && s) { id = s; CoTaskMemFree(s); }
        dev->Release();
    }
    en->Release();
    return id;
}

void AudioEngine::closeRender() {
    if (ac_) {
        IAudioClient* ac = (IAudioClient*)ac_;
        ac->Stop();
        ac->Release();
        ac_ = nullptr;
    }
    if (rc_) {
        ((IAudioRenderClient*)rc_)->Release();
        rc_ = nullptr;
    }
    if (hEv_) { CloseHandle(hEv_); hEv_ = nullptr; }
    bufFrames_ = 0;
}

bool AudioEngine::openRender() {
    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&en);
    if (FAILED(hr)) return false;
    IMMDevice* dev = nullptr;
    hr = en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev);
    if (FAILED(hr) || !dev) { en->Release(); return false; }

    {
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id)) && id) { devId_ = id; CoTaskMemFree(id); }
        std::wstring nm;
        if (GetFriendlyName2(dev, nm)) {
            std::lock_guard<std::mutex> lk(titleMtx_);
            devName_ = nm;
        }
    }

    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac_);
    dev->Release();
    en->Release();
    if (FAILED(hr) || !ac_) { ac_ = nullptr; return false; }

    IAudioClient* ac = (IAudioClient*)ac_;
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(ac->GetMixFormat(&mix)) || !mix) { closeRender(); return false; }

    const int  rate   = (int)mix->nSamplesPerSec;
    const int  ch     = (int)mix->nChannels;
    const bool native = (ch == 2 && mix->wBitsPerSample == 32);
    CoTaskMemFree(mix);

    fs_ = rate;
    dsp_.prepare((float)rate);

    WAVEFORMATEXTENSIBLE wf = MakeFloatFormat2(rate, 2);
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (!native) flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    bool inited = false;
    IAudioClient3* ac3 = nullptr;
    HRESULT hrQi = ac->QueryInterface(IID_IAudioClient3L, (void**)&ac3);
    if (SUCCEEDED(hrQi) && ac3) {
        UINT32 defP = 0, fundP = 0, minP = 0, maxP = 0;
        HRESULT hrPer = ac3->GetSharedModeEnginePeriod((WAVEFORMATEX*)&wf, &defP, &fundP, &minP, &maxP);
        DbgLog(L"AC3 period hr=0x%08lX def=%u fund=%u min=%u max=%u\n",
               (unsigned long)hrPer, defP, fundP, minP, maxP);
        if (SUCCEEDED(hrPer)) {
            UINT32 period = defP;
            if (bufferMode_ == 0 && minP > 0) period = minP;
            if (period == 0) period = fundP;
            if (period > 0) {
                HRESULT hrIni = ac3->InitializeSharedAudioStream(flags, period, (WAVEFORMATEX*)&wf, nullptr);
                DbgLog(L"AC3 init period=%u hr=0x%08lX\n", period, (unsigned long)hrIni);
                if (SUCCEEDED(hrIni)) inited = true;
            }
        }
        ac3->Release();
    } else {
        DbgLog(L"AC3 QI hr=0x%08lX\n", (unsigned long)hrQi);
    }
    if (!inited) {
        REFERENCE_TIME dur = 100000;     // 10ms
        if (bufferMode_ == 0)      dur = 30000;
        else if (bufferMode_ == 2) dur = 200000;
        hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, dur, 0, (WAVEFORMATEX*)&wf, nullptr);
        if (FAILED(hr)) { closeRender(); return false; }
    }

    hEv_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!hEv_ || FAILED(ac->SetEventHandle(hEv_))) { closeRender(); return false; }
    if (FAILED(ac->GetBufferSize(&bufFrames_)) || bufFrames_ == 0) { closeRender(); return false; }
    if (FAILED(ac->GetService(__uuidof(IAudioRenderClient), (void**)&rc_)) || !rc_) { closeRender(); return false; }
    if (FAILED(ac->Start())) { closeRender(); return false; }

    REFERENCE_TIME lat = 0;
    ac->GetStreamLatency(&lat);
    latencyMs_ = (float)((double)lat / 10000.0) + (float)bufFrames_ * 1000.0f / (float)rate;
    deviceOk_ = true;
    DbgLog(L"OPEN tid=%lu rate=%d buf=%u native=%d mode=%d lat=%.1f\n",
           (unsigned long)GetCurrentThreadId(), rate, (unsigned)bufFrames_, (int)native, (int)bufferMode_, (double)latencyMs_);
    return true;
}

void AudioEngine::renderThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DbgLog(L"RT start tid=%lu\n", (unsigned long)GetCurrentThreadId());

    Settings local;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        local = params_;
    }
    dsp_.configure(local);

    std::vector<float> capTmp;

    while (!quit_) {
        if (!openRender()) {
            deviceOk_ = false;
            Sleep(400);
            continue;
        }
        IAudioClient*      ac = (IAudioClient*)ac_;
        IAudioRenderClient* rc = (IAudioRenderClient*)rc_;
        if (capTmp.size() < (size_t)bufFrames_ * 2) capTmp.assign((size_t)bufFrames_ * 2, 0.f);

        int tick = 0;
        while (!quit_ && !reconnect_) {
            DWORD wr = WaitForSingleObject(hEv_, 200);
            if (wr != WAIT_OBJECT_0) {
                if (++tick % 10 == 0) {
                    std::wstring id = DefaultRenderId();
                    if (!id.empty() && !devId_.empty() && id != devId_) reconnect_ = true;
                }
                continue;
            }
            if (paramsDirty_.exchange(false)) {
                std::lock_guard<std::mutex> lk(mtx_);
                local = params_;
                dsp_.configure(local);
            }

            UINT32 pad = 0;
            if (FAILED(ac->GetCurrentPadding(&pad))) break;
            if (pad > bufFrames_) pad = bufFrames_;
            const UINT32 avail = bufFrames_ - pad;
            if (avail == 0) continue;

            BYTE* pData = nullptr;
            if (FAILED(rc->GetBuffer(avail, &pData)) || !pData) break;
            float* dst = (float*)pData;
            memset(dst, 0, (size_t)avail * 2 * sizeof(float));

            size_t got = 0;
            {
                std::lock_guard<std::mutex> lk(playMtx_);
                got = playerRing_.pop(dst, avail);
            }
            if (got) framesRendered_ += (long long)got;

            memset(capTmp.data(), 0, (size_t)avail * 2 * sizeof(float));
            {
                std::lock_guard<std::mutex> lk(capRingMtx_);
                capRing_.pop(capTmp.data(), avail);
            }

            const float g = masterGain_.load();
            for (UINT32 i = 0; i < avail * 2; ++i) dst[i] = (dst[i] + capTmp[i]) * g;

            if (!local.bypass) {
                dsp_.process(dst, (int)avail, 2);
            } else {
                for (UINT32 i = 0; i < avail * 2; ++i) {
                    if (dst[i] >  0.999f) dst[i] =  0.999f;
                    if (dst[i] < -0.999f) dst[i] = -0.999f;
                }
            }
            rc->ReleaseBuffer(avail, 0);

            computeMeter(dst, (int)avail);

            if (++tick % 200 == 0) {
                std::wstring id = DefaultRenderId();
                if (!id.empty() && !devId_.empty() && id != devId_) reconnect_ = true;
            }
        }
        reconnect_ = false;
        closeRender();
        deviceOk_ = false;
    }
    closeRender();
    CoUninitialize();
}

// ---------------------------------------------------------------- 频谱 / 电平
static void fftRadix2(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / (double)len;
        const float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.f, ci = 0.f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float xr = re[b] * cr - im[b] * ci;
                const float xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr;        im[a] += xi;
                const float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

void AudioEngine::computeMeter(float* buf, int frames) {
    if (frames <= 0) return;
    float sL = 0.f, sR = 0.f, pL = 0.f, pR = 0.f;
    for (int i = 0; i < frames; ++i) {
        const float l = buf[i * 2], r = buf[i * 2 + 1];
        sL += l * l; sR += r * r;
        const float al = fabsf(l), ar = fabsf(r);
        if (al > pL) pL = al;
        if (ar > pR) pR = ar;
    }
    const float rl = sqrtf(sL / (float)frames);
    const float rr = sqrtf(sR / (float)frames);

    for (int i = 0; i < frames; ++i) {
        fftIn_[fftPos_] = (buf[i * 2] + buf[i * 2 + 1]) * 0.5f;
        fftPos_ = (fftPos_ + 1) & 2047;
    }

    float bars[kSpectrumBars];
    memcpy(bars, meter_.bars, sizeof(bars));

    if (++fftTick_ >= 2) {
        fftTick_ = 0;
        static float re[2048], im[2048];
        for (int i = 0; i < 2048; ++i) {
            const float w = 0.5f - 0.5f * cosf(6.2831853f * (float)i / 2047.0f);
            const int idx = (fftPos_ + i) & 2047;
            re[i] = fftIn_[idx] * w;
            im[i] = 0.f;
        }
        fftRadix2(re, im, 2048);
        const float binHz = (float)fs_ / 2048.0f;
        for (int b = 0; b < kSpectrumBars; ++b) {
            const float f0 = 32.0f * powf(16000.0f / 32.0f, (float)b / (float)kSpectrumBars);
            const float f1 = 32.0f * powf(16000.0f / 32.0f, (float)(b + 1) / (float)kSpectrumBars);
            int i0 = (int)(f0 / binHz), i1 = (int)(f1 / binHz);
            if (i0 < 1) i0 = 1;
            if (i1 <= i0) i1 = i0 + 1;
            if (i1 > 1023) i1 = 1024;
            float m = 0.f;
            for (int i = i0; i < i1; ++i) {
                const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]);
                if (mag > m) m = mag;
            }
            float db = 20.f * log10f(m / 512.f + 1e-9f);
            float v = (db + 78.f) / 78.f;
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            v = powf(v, 0.72f);
            bars[b] = bars[b] * 0.55f + v * 0.45f;
        }
    }

    std::lock_guard<std::mutex> lk(meterMtx_);
    meter_.rmsL = meter_.rmsL * 0.55f + rl * 0.45f;
    meter_.rmsR = meter_.rmsR * 0.55f + rr * 0.45f;
    if (pL > meter_.peakL) meter_.peakL = pL; else meter_.peakL *= 0.90f;
    if (pR > meter_.peakR) meter_.peakR = pR; else meter_.peakR *= 0.90f;
    meter_.grDb = dsp_.grDb();
    memcpy(meter_.bars, bars, sizeof(bars));
    meter_.capturing = capturing_;
    meter_.playing = isPlaying();
    meter_.active = (rl > 1e-4f || rr > 1e-4f);
}
