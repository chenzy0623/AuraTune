// AuraTune 解码：Media Foundation Source Reader（mp3/flac/wav/aac/m4a/wma/ogg）
#include "audio.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cmath>

static std::wstring FileStem(const std::wstring& path) {
    size_t a = path.find_last_of(L"\\/");
    std::wstring name = (a == std::wstring::npos) ? path : path.substr(a + 1);
    size_t d = name.find_last_of(L'.');
    if (d != std::wstring::npos && d > 0) name = name.substr(0, d);
    return name;
}

static bool RequestType(IMFSourceReader* rd, int rate) {
    IMFMediaType* mt = nullptr;
    if (FAILED(MFCreateMediaType(&mt))) return false;
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
    mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    const HRESULT hr = rd->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mt);
    mt->Release();
    return SUCCEEDED(hr);
}

// srcRate: 解码器实际输出采样率；resample: 是否需要重采样到设备采样率
static IMFSourceReader* OpenReader(const std::wstring& path, int fs, double* dur,
                                   int* srcRate, bool* resample) {
    *dur = 0.0;
    *srcRate = fs;
    *resample = false;

    IMFSourceReader* rd = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &rd)) || !rd) return nullptr;

    if (!RequestType(rd, fs)) {
        // 回退：先用部分类型让解码器自选，再按其采样率请求 float32
        IMFMediaType* part = nullptr;
        if (FAILED(MFCreateMediaType(&part))) { rd->Release(); return nullptr; }
        part->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        const HRESULT hp = rd->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, part);
        part->Release();
        if (FAILED(hp)) { rd->Release(); return nullptr; }

        UINT32 rate = 48000;
        IMFMediaType* cur = nullptr;
        if (SUCCEEDED(rd->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur)) && cur) {
            UINT32 r = 0;
            if (SUCCEEDED(cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r)) && r > 0) rate = r;
            cur->Release();
        }
        if (!RequestType(rd, (int)rate)) { rd->Release(); return nullptr; }
        *srcRate = (int)rate;
        *resample = ((int)rate != fs);
    }

    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(rd->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v))) {
        if (v.vt == VT_UI8) *dur = (double)v.uhVal.QuadPart / 1.0e7;
        else if (v.vt == VT_I8) *dur = (double)v.hVal.QuadPart / 1.0e7;
    }
    PropVariantClear(&v);
    return rd;
}

// 流式线性重采样（只在解码器原生率与设备率不同时使用）
struct LinResampler {
    double ratio = 1.0;      // src/dst
    double t = 0.0;
    float  last[2] = { 0.f, 0.f };
    bool   have = false;
    std::vector<float> out;

    void prepare(int src, int dst) {
        ratio = (double)src / (double)dst;
        t = 0.0; have = false; out.clear();
    }
    // 输入 n 帧 f32 立体声，输出写入 out（原地累积，调用后取 out.data()/out.size()/2）
    void process(const float* in, size_t n) {
        out.clear();
        for (size_t i = 0; i < n; ++i) {
            const float cur[2] = { in[i * 2], in[i * 2 + 1] };
            if (!have) { last[0] = cur[0]; last[1] = cur[1]; have = true; continue; }
            while (t < 1.0) {
                out.push_back((float)(last[0] + (cur[0] - last[0]) * t));
                out.push_back((float)(last[1] + (cur[1] - last[1]) * t));
                t += 1.0 / ratio;
            }
            t -= 1.0;
            last[0] = cur[0]; last[1] = cur[1];
        }
    }
};

void AudioEngine::decoderThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMFSourceReader* reader = nullptr;
    int  srcRate = 48000;
    bool resampleNeeded = false;
    LinResampler rs;

    while (!quit_) {
        bool closeReq = false;
        std::wstring pend;
        {
            std::lock_guard<std::mutex> lk(playMtx_);
            pend = pendingPath_;
            closeReq = pendingClose_;
            pendingPath_.clear();
            pendingClose_ = false;
        }

        if (closeReq || !pend.empty()) {
            if (reader) { reader->Release(); reader = nullptr; }
            trackOpen_ = false;
            trackDuration_ = 0.0;
            framesRendered_ = 0;
            startOffset_ = 0.0;
            eof_ = false;
            playReq_ = false;
            paused_ = true;
            {
                std::lock_guard<std::mutex> lk(playMtx_);
                playerRing_.clear();
            }
            if (closeReq) continue;
        }

        if (!pend.empty()) {
            int guard = 0;
            while (!deviceOk_.load() && !quit_.load() && guard++ < 200) Sleep(10);
            const int fs = fs_.load();
            if (fs <= 0) continue;
            double dur = 0.0;
            reader = OpenReader(pend, fs, &dur, &srcRate, &resampleNeeded);
            if (!reader) {
                std::lock_guard<std::mutex> lk(titleMtx_);
                capErr_ = L"无法解码该文件（格式不受支持）";
                continue;
            }
            rs.prepare(srcRate, fs);
            {
                std::lock_guard<std::mutex> lk(titleMtx_);
                trackTitle_ = FileStem(pend);
            }
            trackDuration_ = dur;
            framesRendered_ = 0;
            startOffset_ = 0.0;
            eof_ = false;
            trackOpen_ = true;
            playReq_ = true;
            paused_ = false;
            continue;
        }

        if (!reader) { Sleep(15); continue; }
        if (!playReq_.load() || paused_.load() || eof_.load()) { Sleep(8); continue; }

        const double sk = seekReq_.exchange(-1.0);
        if (sk >= 0.0) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            pv.vt = VT_I8;
            pv.hVal.QuadPart = (LONGLONG)(sk * 1.0e7);
            reader->SetCurrentPosition(GUID_NULL, pv);
            PropVariantClear(&pv);
            rs.prepare(srcRate, fs_.load());
            std::lock_guard<std::mutex> lk(playMtx_);
            playerRing_.clear();
        }

        bool spaceOk = false;
        {
            std::lock_guard<std::mutex> lk(playMtx_);
            spaceOk = playerRing_.space() >= 8192;
        }
        if (!spaceOk) { Sleep(6); continue; }

        DWORD flags = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) {
            reader->Release();
            reader = nullptr;
            trackOpen_ = false;
            eof_ = true;
            playReq_ = false;
            paused_ = true;
            continue;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) sample->Release();
            eof_ = true;
            playReq_ = false;
            paused_ = true;
            continue;
        }
        if (!sample) { Sleep(2); continue; }

        IMFMediaBuffer* mb = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&mb)) && mb) {
            BYTE* p = nullptr;
            DWORD len = 0;
            const bool locked = SUCCEEDED(mb->Lock(&p, nullptr, &len));
            if (locked && p && len >= 8) {
                const size_t frames = len / 8;      // f32 立体声
                if (resampleNeeded) {
                    rs.process((const float*)p, frames);
                    if (!rs.out.empty()) {
                        std::lock_guard<std::mutex> lk(playMtx_);
                        playerRing_.push(rs.out.data(), rs.out.size() / 2);
                    }
                } else {
                    std::lock_guard<std::mutex> lk(playMtx_);
                    playerRing_.push((const float*)p, frames);
                }
            }
            if (locked) mb->Unlock();
            mb->Release();
        }
        sample->Release();
    }

    if (reader) reader->Release();
    CoUninitialize();
}
