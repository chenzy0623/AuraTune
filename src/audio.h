// 音频引擎：WASAPI 共享模式输出 + Media Foundation 解码 + 进程回环捕获（游戏模式）
#pragma once
#include "app.h"
#include "dsp.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

struct MeterData {
    float rmsL = 0, rmsR = 0, peakL = 0, peakR = 0, grDb = 0;
    float bars[kSpectrumBars] = {};
    bool  playing = false, capturing = false, active = false;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool start();          // 打开默认输出设备并启动渲染线程
    void stop();

    // ---- 播放器 ----
    bool  openFile(const std::wstring& path);
    void  closeFile();
    void  play();
    void  pause();
    void  togglePlay();
    bool  isPlaying() const;
    void  seekTo(double sec);
    double position() const;
    double duration() const;
    std::wstring title() const;
    bool  hasTrack() const;

    // ---- 设置 ----
    void updateSettings(const Settings& s);
    void setVolumePercent(int pct, bool applyNow = true);   // 立即生效
    int  volumePercent() const { return volPct_; }

    // ---- 游戏捕获 ----
    bool  startCapture(DWORD pid, const std::wstring& label);
    void  stopCapture();
    bool  isCapturing() const { return capturing_; }
    std::wstring captureTarget() const;
    std::wstring captureError() const;

    // ---- 设备 ----
    std::wstring deviceName() const;
    void  requestReconnect();
    bool  deviceOk() const { return deviceOk_; }

    MeterData meter() const;
    float     dspGr() const;

    // 缓冲策略：0=超低(2周期) 1=低(4周期) 2=稳(8周期)
    void  setBufferMode(int m) { bufferMode_ = m; }
    int   bufferMode() const { return bufferMode_; }
    float latencyMs() const { return latencyMs_; }
    int sampleRate() const { return fs_; }
    int bufferFrames() const { return (int)bufFrames_; }

private:
    void renderThread();
    void decoderThread();
    void captureThread();
    bool openRender();
    void closeRender();
    bool openCapture(DWORD pid);
    void computeMeter(float* buf, int frames);

    // 渲染
    std::thread         thRender_, thDecoder_, thCapture_;
    std::atomic<bool>   quit_{ false }, reconnect_{ false };
    std::atomic<bool>   deviceOk_{ false };
    std::atomic<int>    bufferMode_{ 1 };
    void*               ac_ = nullptr;      // IAudioClient*
    void*               rc_ = nullptr;      // IAudioRenderClient*
    HANDLE              hEv_ = nullptr;
    UINT32              bufFrames_ = 0;
    std::wstring        devId_;
    volatile long       renderFrames_ = 0;

    // 共享参数
    mutable std::mutex  mtx_;
    Settings            params_;
    std::atomic<bool>   paramsDirty_{ true };
    std::atomic<int>    volPct_{ 80 };
    std::atomic<float>  masterGain_{ 0.8f };
    std::atomic<int>    fs_{ 48000 };           // 输出设备采样率
    DspChain            dsp_;

    // 播放器
    std::mutex          playMtx_;
    struct Ring {
        std::vector<float> buf;
        size_t cap = 0, rd = 0, wr = 0;
        void init(size_t frames) { cap = frames; buf.assign(frames * 2, 0.f); rd = wr = 0; }
        size_t avail() const { return wr - rd; }
        size_t space() const { return cap - (wr - rd); }
        void clear() { rd = wr = 0; }
        size_t push(const float* src, size_t frames) {
            size_t n = space(); if (frames < n) n = frames;
            for (size_t i = 0; i < n; ++i) {
                size_t j = (wr + i) % cap;
                buf[j * 2] = src[i * 2]; buf[j * 2 + 1] = src[i * 2 + 1];
            }
            wr += n; return n;
        }
        size_t pop(float* dst, size_t frames) {
            size_t n = avail(); if (frames < n) n = frames;
            for (size_t i = 0; i < n; ++i) {
                size_t j = (rd + i) % cap;
                dst[i * 2] = buf[j * 2]; dst[i * 2 + 1] = buf[j * 2 + 1];
            }
            rd += n; return n;
        }
    };
    Ring                playerRing_, capRing_;
    std::mutex          capRingMtx_;
    std::wstring        pendingPath_;
    bool                pendingClose_ = false;
    std::atomic<bool>   playReq_{ false }, paused_{ true };
    std::atomic<double> seekReq_{ -1.0 };
    std::atomic<long long> framesRendered_{ 0 };
    std::atomic<double> startOffset_{ 0.0 }, trackDuration_{ 0.0 };
    std::atomic<bool>   trackOpen_{ false }, eof_{ false };
    std::wstring        trackPath_, trackTitle_;
    mutable std::mutex  titleMtx_;
    std::wstring        devName_;
    std::atomic<float>  latencyMs_{ 0.f };

    // Media Foundation 解码器（解码线程独占，除 openFile 交接）
    void*               reader_ = nullptr;      // IMFSourceReader*
    std::mutex          readerMtx_;

    // 捕获
    std::atomic<bool>   capturing_{ false }, capQuit_{ false }, capStop_{ false };
    std::atomic<DWORD>  capPid_{ 0 };
    std::wstring        capLabel_, capErr_;
    void*               capClient_ = nullptr;   // IAudioClient*

    // 电平 / 频谱
    mutable std::mutex  meterMtx_;
    MeterData           meter_;
    float               fftIn_[2048] = {};
    int                 fftPos_ = 0;
    int                 fftTick_ = 0;
};
