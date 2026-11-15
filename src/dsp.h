// DSP 引擎：10 段 EQ + 低音/清晰度/氛围 + 立体声宽度 + bs2b 式串扰 + 压缩 + 前瞻限幅
#pragma once
#include "app.h"

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void  reset() { x1 = x2 = y1 = y2 = 0; }
    inline float run(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
    void peaking (float fs, float f, float Q, float dB);
    void lowshelf(float fs, float f, float Q, float dB);
    void highshelf(float fs, float f, float Q, float dB);
    void lowpass (float fs, float f, float Q);
    void highpass(float fs, float f, float Q);
};

// 立体声联动压缩器
struct Compressor {
    float envDb = 0;
    void  reset() { envDb = 0; }
    void  process(float* buf, int frames, int stride, float threshDb, float ratio,
                  float attackMs, float releaseMs, float fs);
};

// 前瞻限幅器
struct Limiter {
    float dl[2][64] = {};
    int   idx = 0;
    float gain = 1.0f;
    void  reset();
    void  process(float* buf, int frames, int stride, float ceilDb, float fs);
};

class DspChain {
public:
    void prepare(float fs);
    void configure(const Settings& s);       // 仅在与上次不同时重算系数
    void process(float* buf, int frames, int stride);
    float grDb() const { return grDb_; }
    void  resetAll();
private:
    float  fs_ = (float)kSampleRate;

    Biquad eq_[2][kBands];
    Biquad bassShelf_[2], bassPeak_[2], vocalPeak_[2];
    Biquad air_[2];
    Biquad xfLp_[2];

    // bs2b 式串扰延迟
    float  xfDelay_[2][16] = {};
    int    xfIdx_ = 0;
    // 氛围扩散延迟 (~7ms / 11ms)
    float  ambL_[2][1024] = {};
    float  ambR_[2][1024] = {};
    int    ambIdx_ = 0;
    Biquad ambLp_[2][2];

    float  eqGain_[kBands] = {0};
    float  corr_[kBands]   = {0};
    float  bassDb_ = 0, vocalDb_ = 0, ambDb_ = 0, width_ = 1, xfeed_ = 0, dyn_ = 0;
    float  volDb_ = 0, bal_ = 0;
    float  curBass_ = 0, curVocal_ = 0, curAmb_ = 0, curWidth_ = 1, curXf_ = 0, curBal_ = 0;
    float  lastBand_[kBands] = {1e9f,1e9f,1e9f,1e9f,1e9f,1e9f,1e9f,1e9f,1e9f,1e9f};

    Compressor comp_;
    Limiter    lim_;
    float  grDb_ = 0;
};
