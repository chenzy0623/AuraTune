// DSP 引擎：前级 → IR 卷积 → 图形 EQ(10/15/31) → 参量 EQ → 低音增强 → 清晰度
//          → 氛围扩散 → LR4 分频/低音声道 → 压缩 → 空间音频(8D/10D/8字)
//          → 立体声宽度 → bs2b 串扰 → 音量/平衡 → 前瞻限幅
#pragma once
#include "app.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------- 常量
constexpr int kIrFft   = 8192;   // 卷积 FFT 点数
constexpr int kIrBlock = 4096;   // 卷积分块长度（= kIrFft/2）
constexpr int kIrTaps  = 4096;   // 最多保留的 IR tap 数
constexpr int kSpMaxItd = 24;    // 空间音频最大 ITD（采样）
constexpr int kSpBuf    = 64;    // 空间音频 ITD 环形缓冲长度

// ---------------------------------------------------------------- 双二阶
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void  reset()  { x1 = x2 = y1 = y2 = 0; }
    void  bypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    inline float run(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
    // RBJ cookbook
    void peaking (float fs, float f, float Q, float dB);
    void lowshelf(float fs, float f, float Q, float dB);
    void highshelf(float fs, float f, float Q, float dB);
    void lowpass (float fs, float f, float Q);
    void highpass(float fs, float f, float Q);
    void notch   (float fs, float f, float Q);
    void allpass (float fs, float f, float Q);
    void bandpass(float fs, float f, float Q);
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
    void  prepare(float fs);
    void  configure(const Settings& s);       // 仅在与上次不同时重算系数
    void  process(float* buf, int frames, int stride);
    float grDb() const { return grDb_; }
    void  resetAll();

    // ------------------------------------------------------------ IR 卷积
    // 解析 WAV（PCM 16/24/32bit、IEEE float32，单/立体声，任意采样率），
    // 截断到 4096 tap、线性重采样到当前 fs、峰值归一化到 1.0。
    bool  loadIr(const std::wstring& path, std::wstring* err);
    void  clearIr();
    bool  hasIr() const { return irLen_ > 0; }

private:
    void  ensureIrBuffers();
    void  irPush(float* buf, int frames, int stride);   // 干/湿混合
    void  irComputeBlock();                             // 一块 4096 点的 FFT 卷积

    float  fs_ = (float)kSampleRate;

    // ---------------------------------------------------------- 图形 EQ
    Biquad eq_[2][kMaxBands];
    float  eqTotal_[kMaxBands] = {0};   // 用户 + 校正后的实际 dB
    float  lastBand_[kMaxBands];        // 系数缓存（1e9 = 未初始化）
    int    lastBandCount_ = 0;
    int    eqCount_ = kBands;

    // ---------------------------------------------------------- 参量 EQ
    Biquad      par_[2][kMaxParams];
    ParamFilter lastPar_[kMaxParams];
    bool        parOn_[kMaxParams] = {};
    bool        parInit_ = false;

    // ---------------------------------------------------------- 低音/清晰度/氛围
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

    // ---------------------------------------------------------- 低频分频（LR4）
    Biquad xoLo_[2][2], xoHi_[2][2];
    bool   xoOn_ = false;
    float  xoFreq_ = -1.f;      // -1 = 尚未设置
    float  subGainLin_ = 1.f;

    // ---------------------------------------------------------- 压缩
    bool  compOn_ = false;
    float compTh_ = -12.f, compRatio_ = 2.f, compAtk_ = 20.f, compRel_ = 250.f;
    float compMakeup_ = 1.f;
    int   compMode_ = COMP_OFF;

    // ---------------------------------------------------------- 空间音频
    bool   spOn_ = false;
    int    spMode_ = SP_OFF;
    float  spRate_ = 0.12f, spDepth_ = 0.5f;
    float  spPhase_ = 0.f, spT_ = 0.f;
    float  spDl_[2][kSpBuf] = {};
    int    spIdx_ = 0;
    Biquad spHp_[2];

    // ---------------------------------------------------------- IR 卷积
    std::vector<float> irIn_;       // 2 * kIrBlock  输入累积
    std::vector<float> irOut_;      // 2 * kIrBlock  已完成块（供输出）
    std::vector<float> irAcc_;      // 2 * kIrFft    重叠相加累加器
    std::vector<float> irSpecRe_;   // 2 * kIrFft    IR 频谱（实部，左右各一套）
    std::vector<float> irSpecIm_;   // 2 * kIrFft    IR 频谱（虚部）
    std::vector<float> fftRe_, fftIm_;  // 临时工作区
    int    irLen_ = 0;
    int    irFill_ = 0, irOutCount_ = 0, irOutPos_ = 0;
    bool   irOn_ = false;
    float  irMix_ = 1.f;

    // ---------------------------------------------------------- 全局
    float  bassDb_ = 0, vocalDb_ = 0, ambDb_ = 0, width_ = 1, xfeed_ = 0, dyn_ = 0;
    float  volDb_ = 0, bal_ = 0;
    float  preampLin_ = 0.55f;
    float  curBass_ = 0, curVocal_ = 0, curAmb_ = 0, curWidth_ = 1, curXf_ = 0, curBal_ = 0;

    Compressor comp_;
    Limiter    lim_;
    float  grDb_ = 0;
};
