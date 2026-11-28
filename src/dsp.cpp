// AuraTune DSP 引擎实现
#include "dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>

static const float  kPi  = 3.14159265358979f;
static const double kPiD = 3.14159265358979323846;

// ============================================================ Biquad
void Biquad::peaking(float fs, float f, float Q, float dB) {
    if (f <= 0 || f >= fs * 0.49f) return;
    float A  = powf(10.f, dB / 40.f);
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float a0 = 1.f + alpha / A;
    b0 = (1.f + alpha * A) / a0;
    b1 = (-2.f * cw) / a0;
    b2 = (1.f - alpha * A) / a0;
    a1 = (-2.f * cw) / a0;
    a2 = (1.f - alpha / A) / a0;
}

void Biquad::lowshelf(float fs, float f, float Q, float dB) {
    if (f <= 0 || f >= fs * 0.49f) return;
    float A  = powf(10.f, dB / 40.f);
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float sq = 2.f * sqrtf(A) * alpha;
    float a0 = (A + 1.f) + (A - 1.f) * cw + sq;
    b0 =  A * ((A + 1.f) - (A - 1.f) * cw + sq) / a0;
    b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cw) / a0;
    b2 =  A * ((A + 1.f) - (A - 1.f) * cw - sq) / a0;
    a1 = -2.f * ((A - 1.f) + (A + 1.f) * cw) / a0;
    a2 = ((A + 1.f) + (A - 1.f) * cw - sq) / a0;
}

void Biquad::highshelf(float fs, float f, float Q, float dB) {
    if (f <= 0 || f >= fs * 0.49f) return;
    float A  = powf(10.f, dB / 40.f);
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float sq = 2.f * sqrtf(A) * alpha;
    float a0 = (A + 1.f) - (A - 1.f) * cw + sq;
    b0 =  A * ((A + 1.f) + (A - 1.f) * cw + sq) / a0;
    b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cw) / a0;
    b2 =  A * ((A + 1.f) + (A - 1.f) * cw - sq) / a0;
    a1 = 2.f * ((A - 1.f) - (A + 1.f) * cw) / a0;
    a2 = ((A + 1.f) - (A - 1.f) * cw - sq) / a0;
}

void Biquad::lowpass(float fs, float f, float Q) {
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float a0 = 1.f + alpha;
    b0 = (1.f - cw) * 0.5f / a0;
    b1 = (1.f - cw) / a0;
    b2 = b0;
    a1 = (-2.f * cw) / a0;
    a2 = (1.f - alpha) / a0;
}

void Biquad::highpass(float fs, float f, float Q) {
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float a0 = 1.f + alpha;
    b0 = (1.f + cw) * 0.5f / a0;
    b1 = -(1.f + cw) / a0;
    b2 = b0;
    a1 = (-2.f * cw) / a0;
    a2 = (1.f - alpha) / a0;
}

void Biquad::notch(float fs, float f, float Q) {
    if (f <= 0 || f >= fs * 0.49f || Q <= 0) return;
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float a0 = 1.f + alpha;
    b0 = 1.f / a0;
    b1 = (-2.f * cw) / a0;
    b2 = 1.f / a0;
    a1 = (-2.f * cw) / a0;
    a2 = (1.f - alpha) / a0;
}

void Biquad::allpass(float fs, float f, float Q) {
    if (f <= 0 || f >= fs * 0.49f || Q <= 0) return;
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float a0 = 1.f + alpha;
    b0 = (1.f - alpha) / a0;
    b1 = (-2.f * cw) / a0;
    b2 = (1.f + alpha) / a0;
    a1 = (-2.f * cw) / a0;
    a2 = (1.f - alpha) / a0;
}

void Biquad::bandpass(float fs, float f, float Q) {   // 0dB 峰值增益
    if (f <= 0 || f >= fs * 0.49f || Q <= 0) return;
    float w0 = 2.f * kPi * f / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.f * Q);
    float a0 = 1.f + alpha;
    b0 = alpha / a0;
    b1 = 0.f;
    b2 = -alpha / a0;
    a1 = (-2.f * cw) / a0;
    a2 = (1.f - alpha) / a0;
}

// ---- 一阶（6dB/oct）：模拟原型经双线性变换（s = K(1-z⁻¹)/(1+z⁻¹)，K=2，T=1/fs 归一化）
// 低架：H(s) = (s + w0·A) / (s + w0/A)，DC 增益 A²，高频 1，w0 处 A
void Biquad::lowshelf1(float fs, float f, float dB) {
    if (!(f > 0.f) || f >= fs * 0.49f || !std::isfinite(dB)) return;
    const float A  = powf(10.f, dB / 40.f);
    const float w0 = 2.f * kPi * f / fs;
    const float K  = 2.f;
    const float a0 = K + w0 / A;
    b0 = (K + w0 * A) / a0;
    b1 = (w0 * A - K) / a0;
    b2 = 0.f;
    a1 = (w0 / A - K) / a0;
    a2 = 0.f;
}

// 高架：H(s) = (A·s + w0) / (s/A + w0)，DC 增益 1，高频 A²，w0 处 A
void Biquad::highshelf1(float fs, float f, float dB) {
    if (!(f > 0.f) || f >= fs * 0.49f || !std::isfinite(dB)) return;
    const float A  = powf(10.f, dB / 40.f);
    const float w0 = 2.f * kPi * f / fs;
    const float K  = 2.f;
    const float a0 = K / A + w0;
    b0 = (A * K + w0) / a0;
    b1 = (w0 - A * K) / a0;
    b2 = 0.f;
    a1 = (w0 - K / A) / a0;
    a2 = 0.f;
}

void Biquad::lowpass1(float fs, float f) {
    if (!(f > 0.f) || f >= fs * 0.49f) return;
    const float w0 = 2.f * kPi * f / fs;
    const float K  = 2.f;
    const float a0 = K + w0;
    b0 = w0 / a0;
    b1 = w0 / a0;
    b2 = 0.f;
    a1 = (w0 - K) / a0;
    a2 = 0.f;
}

void Biquad::highpass1(float fs, float f) {
    if (!(f > 0.f) || f >= fs * 0.49f) return;
    const float w0 = 2.f * kPi * f / fs;
    const float K  = 2.f;
    const float a0 = K + w0;
    b0 = K / a0;
    b1 = -K / a0;
    b2 = 0.f;
    a1 = (w0 - K) / a0;
    a2 = 0.f;
}

// ============================================================ Compressor
void Compressor::process(float* buf, int frames, int stride, float threshDb, float ratio,
                         float attackMs, float releaseMs, float fs) {
    float att = expf(-1.f / (attackMs  * 0.001f * fs));
    float rel = expf(-1.f / (releaseMs * 0.001f * fs));
    float slope = 1.f - 1.f / ratio;
    for (int i = 0; i < frames; ++i) {
        float l = buf[i * stride], r = buf[i * stride + 1];
        float p = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        float pdb = p > 1e-6f ? 20.f * log10f(p) : -120.f;
        float over = pdb - threshDb;
        float gr = over > 0.f ? over * slope : 0.f;
        if (over > -6.f && over <= 0.f) {
            float k = (over + 6.f) / 6.f;          // 软膝
            gr = k * k * 6.f * slope * 0.5f;
        }
        if (gr > envDb) envDb = gr + (envDb - gr) * att;
        else            envDb = gr + (envDb - gr) * rel;
        float g = powf(10.f, -envDb / 20.f);
        buf[i * stride]     = l * g;
        buf[i * stride + 1] = r * g;
    }
}

// ============================================================ Limiter
void Limiter::reset() {
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i) dl[c][i] = 0;
    idx = 0; gain = 1.0f;
}

void Limiter::process(float* buf, int frames, int stride, float ceilDb, float fs) {
    float ceil_ = powf(10.f, ceilDb / 20.f);
    float rel   = expf(-1.f / (0.15f * fs));
    for (int i = 0; i < frames; ++i) {
        float l = buf[i * stride], r = buf[i * stride + 1];
        dl[0][idx] = l; dl[1][idx] = r;
        float p = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        float tgt = p > ceil_ ? ceil_ / p : 1.0f;
        if (tgt < gain) gain = tgt;
        else            gain = tgt + (gain - tgt) * rel;
        int oi = (idx + 1) & 63;
        float yl = dl[0][oi] * gain;
        float yr = dl[1][oi] * gain;
        yl = yl > 0.999f ? 0.999f : (yl < -0.999f ? -0.999f : yl);
        yr = yr > 0.999f ? 0.999f : (yr < -0.999f ? -0.999f : yr);
        buf[i * stride]     = yl;
        buf[i * stride + 1] = yr;
        idx = (idx + 1) & 63;
    }
}

// ============================================================ FFT（radix-2 迭代版）
// 原地复数 FFT；inverse=true 时做逆变换（含 1/N 缩放）。
static void fft(std::vector<float>& re, std::vector<float>& im, bool inverse) {
    const size_t n = re.size();
    if (n < 2 || im.size() != n || (n & (n - 1)) != 0) return;

    // 位反转置换
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }

    // 旋转因子表（按当前 n 缓存）
    static std::vector<float> twC, twS;
    static size_t twN = 0;
    if (twN != n) {
        twC.resize(n / 2);
        twS.resize(n / 2);
        for (size_t k = 0; k < n / 2; ++k) {
            double a = 2.0 * kPiD * (double)k / (double)n;
            twC[k] = (float)std::cos(a);
            twS[k] = (float)std::sin(a);
        }
        twN = n;
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len >> 1;
        const size_t step = n / len;
        for (size_t base = 0; base < n; base += len) {
            for (size_t k = 0; k < half; ++k) {
                const float wr =  twC[k * step];
                const float wi = inverse ? twS[k * step] : -twS[k * step];
                const float xr = re[base + k + half], xi = im[base + k + half];
                const float vr = xr * wr - xi * wi;
                const float vi = xr * wi + xi * wr;
                const float ur = re[base + k], ui = im[base + k];
                re[base + k]        = ur + vr;
                im[base + k]        = ui + vi;
                re[base + k + half] = ur - vr;
                im[base + k + half] = ui - vi;
            }
        }
    }

    if (inverse) {
        const float s = 1.f / (float)n;
        for (size_t i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
    }
}

// ============================================================ WAV 读取（IR）
// 支持 PCM 16/24/32bit 与 IEEE float 32bit；单声道复制到两声道，多声道取前两路。
static bool readWavIr(const std::wstring& path, std::vector<float> out[2],
                      unsigned* outRate, std::wstring* err) {
    out[0].clear();
    out[1].clear();
    if (outRate) *outRate = 0;

    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) {
        if (err) *err = L"无法打开 IR 文件";
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 44) {
        fclose(fp);
        if (err) *err = L"IR 文件过短，不是合法 WAV";
        return false;
    }
    std::vector<unsigned char> buf((size_t)sz);
    size_t got = fread(buf.data(), 1, (size_t)sz, fp);
    fclose(fp);
    if (got < 44) {
        if (err) *err = L"IR 文件读取失败";
        return false;
    }
    buf.resize(got);

    auto rd16 = [&](size_t p) -> unsigned {
        return (unsigned)buf[p] | ((unsigned)buf[p + 1] << 8);
    };
    auto rd32 = [&](size_t p) -> unsigned {
        return (unsigned)buf[p] | ((unsigned)buf[p + 1] << 8) |
               ((unsigned)buf[p + 2] << 16) | ((unsigned)buf[p + 3] << 24);
    };

    if (memcmp(&buf[0], "RIFF", 4) != 0 || memcmp(&buf[8], "WAVE", 4) != 0) {
        if (err) *err = L"不是 RIFF/WAVE 文件";
        return false;
    }

    int      fmtTag = 0, channels = 0, bits = 0;
    unsigned rate = 0;
    size_t   dataPos = 0, dataLen = 0;
    bool     haveFmt = false, haveData = false;

    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        char id[5] = {0, 0, 0, 0, 0};
        memcpy(id, &buf[pos], 4);
        unsigned csz = rd32(pos + 4);
        size_t body = pos + 8;
        if (memcmp(id, "fmt ", 4) == 0 && body + 16 <= buf.size()) {
            fmtTag   = (int)rd16(body);
            channels = (int)rd16(body + 2);
            rate     = rd32(body + 4);
            bits     = (int)rd16(body + 14);
            if (fmtTag == 0xFFFE && csz >= 40 && body + 26 <= buf.size())
                fmtTag = (int)rd16(body + 24);      // WAVE_FORMAT_EXTENSIBLE 子格式前两字节
            haveFmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            dataPos = body;
            dataLen = csz;
            if (dataPos > buf.size()) dataPos = buf.size();
            if (dataPos + dataLen > buf.size()) dataLen = buf.size() - dataPos;
            haveData = true;
            break;
        }
        pos = body + (size_t)csz + (csz & 1);
    }

    if (!haveFmt || !haveData || channels < 1) {
        if (err) *err = L"WAV 缺少 fmt/data 块";
        return false;
    }
    if (fmtTag == 3) {
        if (bits != 32) { if (err) *err = L"仅支持 32bit 浮点 IR"; return false; }
    } else if (fmtTag == 1) {
        if (bits != 16 && bits != 24 && bits != 32) {
            if (err) *err = L"仅支持 PCM 16/24/32bit 或 float32 IR";
            return false;
        }
    } else {
        if (err) *err = L"不支持的 WAV 编码（仅 PCM / IEEE float）";
        return false;
    }
    if (rate == 0) rate = (unsigned)(kSampleRate);

    const int    bps       = bits / 8;
    const size_t frameSize = (size_t)bps * (size_t)channels;
    if (frameSize == 0) {
        if (err) *err = L"WAV 帧长为 0";
        return false;
    }
    int nFrames = (int)(dataLen / frameSize);
    if (nFrames <= 0) {
        if (err) *err = L"WAV 没有采样数据";
        return false;
    }
    int take = nFrames < kIrTaps ? nFrames : kIrTaps;

    out[0].resize((size_t)take);
    out[1].resize((size_t)take);
    for (int i = 0; i < take; ++i) {
        size_t p = dataPos + (size_t)i * frameSize;
        for (int c = 0; c < 2; ++c) {
            int src = (channels == 1) ? 0 : (c < channels ? c : channels - 1);
            const unsigned char* q = &buf[p + (size_t)src * (size_t)bps];
            float v = 0.f;
            if (fmtTag == 3) {
                float f;
                memcpy(&f, q, 4);
                v = f;
            } else if (bps == 2) {
                int16_t s;
                memcpy(&s, q, 2);
                v = (float)s / 32768.f;
            } else if (bps == 3) {
                int32_t s = (int32_t)((unsigned)q[0] | ((unsigned)q[1] << 8) | ((unsigned)q[2] << 16));
                if (s & 0x800000) s -= 0x1000000;
                v = (float)s / 8388608.f;
            } else {   // bps == 4, PCM 32bit
                int32_t s;
                memcpy(&s, q, 4);
                v = (float)((double)s / 2147483648.0);
            }
            out[c][(size_t)i] = v;
        }
    }
    if (outRate) *outRate = rate;
    return true;
}

// ============================================================ 辅助
static float clampf_(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// 段数 → 基础 Q（1/10 倍频程 ≈ 1.05，2/3 倍频程 ≈ 1.41，1/3 倍频程 ≈ 4.32）
static float eqQBase(int count) {
    if (count == 15) return 1.41f;
    if (count == 31) return 4.32f;
    return 1.05f;
}

static float eqQFor(int count, int b) {
    const float q = eqQBase(count);
    if (b <= 0)      return 0.7f;          // 最低段：宽一点，避免极低频过冲
    if (b >= count - 1) return 0.7f * q;   // 最高段同样放宽
    return q;
}

static int sanitizeEqCount(int c) {
    return (c == 15 || c == 31) ? c : kBands;
}

// ============================================================ DspChain
void DspChain::ensureIrBuffers() {
    const size_t inN  = (size_t)2 * kIrBlock;
    const size_t accN = (size_t)2 * kIrFft;
    if (irIn_.size()  != inN)  irIn_.assign(inN, 0.f);
    if (irOut_.size() != inN)  irOut_.assign(inN, 0.f);
    if (irAcc_.size() != accN) irAcc_.assign(accN, 0.f);
}

void DspChain::resetAll() {
    for (int c = 0; c < 2; ++c) {
        for (int b = 0; b < kMaxBands; ++b)  { eq_[c][b].reset(); }
        for (int p = 0; p < kMaxParams; ++p) { par_[c][p].reset(); par2_[c][p].reset(); }
        bassShelf_[c].reset(); bassPeak_[c].reset(); vocalPeak_[c].reset();
        air_[c].reset();
        xfLp_[c].reset();
        spHp_[c].reset();
        for (int k = 0; k < 2; ++k) { xoLo_[c][k].reset(); xoHi_[c][k].reset(); ambLp_[c][k].reset(); }
        for (int i = 0; i < 16; ++i) xfDelay_[c][i] = 0.f;
        for (int i = 0; i < 1024; ++i) { ambL_[c][i] = 0.f; ambR_[c][i] = 0.f; }
        for (int i = 0; i < kSpBuf; ++i) spDl_[c][i] = 0.f;
    }
    xfIdx_  = 0;
    ambIdx_ = 0;
    spIdx_  = 0;
    spPhase_ = 0.f;
    spT_     = 0.f;
    curBass_ = curVocal_ = curAmb_ = 0.f;
    curWidth_ = 1.f; curXf_ = 0.f; curBal_ = 0.f;
    comp_.reset();
    lim_.reset();
    grDb_ = 0.f;

    if (!irIn_.empty())  std::fill(irIn_.begin(),  irIn_.end(),  0.f);
    if (!irOut_.empty()) std::fill(irOut_.begin(), irOut_.end(), 0.f);
    if (!irAcc_.empty()) std::fill(irAcc_.begin(), irAcc_.end(), 0.f);
    irFill_ = 0; irOutCount_ = 0; irOutPos_ = 0;
}

void DspChain::prepare(float fs) {
    if (!(fs > 8000.f && fs < 400000.f)) fs = (float)kSampleRate;
    fs_ = fs;

    ensureIrBuffers();

    for (int c = 0; c < 2; ++c) {
        xfLp_[c].lowpass(fs_, 700.f, 0.5f);      // bs2b: 700Hz 一阶低通
        air_[c].highshelf(fs_, 11000.f, 0.7f, 0.f);
        bassShelf_[c].lowshelf(fs_, 105.f, 0.7f, 0.f);
        bassPeak_[c].peaking(fs_, 62.f, 0.9f, 0.f);
        vocalPeak_[c].peaking(fs_, 2600.f, 0.75f, 0.f);
        ambLp_[c][0].lowpass(fs_, 6500.f, 0.6f);
        ambLp_[c][1].lowpass(fs_, 6500.f, 0.6f);
        spHp_[c].highpass(fs_, 120.f, 0.7071f);
        for (int b = 0; b < kMaxBands; ++b) eq_[c][b].bypass();
        for (int p = 0; p < kMaxParams; ++p) { par_[c][p].bypass(); par2_[c][p].bypass(); }
        for (int k = 0; k < 2; ++k) { xoLo_[c][k].bypass(); xoHi_[c][k].bypass(); }
    }
    // 使 configure() 全量重算
    for (int b = 0; b < kMaxBands; ++b) { lastBand_[b] = 1e9f; eqTotal_[b] = 0.f; }
    lastBandCount_ = 0;
    eqCount_  = kBands;
    parInit_  = false;
    for (int p = 0; p < kMaxParams; ++p) { parOn_[p] = false; par2On_[p] = false; lastPar_[p] = ParamFilter(); }
    xoFreq_   = -1.f;
    xoOn_     = false;
    compOn_   = false;
    compMode_ = COMP_OFF;
    spOn_     = false;
    spMode_   = SP_OFF;
    irOn_     = false;

    resetAll();
}

void DspChain::configure(const Settings& s) {
    // ---------------------------------------------------------- 1. 图形 EQ
    const int count = sanitizeEqCount(s.eqCount);
    if (count != lastBandCount_) {
        lastBandCount_ = count;
        eqCount_ = count;
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < kMaxBands; ++b) { eq_[c][b].reset(); eq_[c][b].bypass(); }
        for (int b = 0; b < kMaxBands; ++b) lastBand_[b] = 1e9f;   // 全量重算
    }

    const Correction& cor = kCorrections[
        (s.correction >= 0 && s.correction < kCorrectionCount) ? s.correction : 0];

    for (int b = 0; b < kMaxBands; ++b) {
        if (b >= count) { eqTotal_[b] = 0.f; lastBand_[b] = 0.f; continue; }
        const float f  = BandFreqAt(count, b);
        if (!(f > 0.f)) { eqTotal_[b] = 0.f; lastBand_[b] = 0.f; continue; }
        float total = s.eq[b] + EqGainAt(cor.eq, kBands, f);
        total = clampf_(total, -15.f, 15.f);
        if (fabsf(total) < 1e-4f) total = 0.f;      // 0dB 直接跳过（不动滤波器状态）
        eqTotal_[b] = total;
        if (total != lastBand_[b]) {
            lastBand_[b] = total;
            if (total == 0.f) {                     // 回到 0dB：直通 + 清状态
                for (int c = 0; c < 2; ++c) { eq_[c][b].bypass(); eq_[c][b].reset(); }
            } else {
                const float q = eqQFor(count, b);
                for (int c = 0; c < 2; ++c) eq_[c][b].peaking(fs_, f, q, total);
            }
        }
    }

    // ---------------------------------------------------------- 2. 参量 EQ
    const bool parForce = !parInit_;
    parInit_ = true;
    for (int i = 0; i < kMaxParams; ++i) {
        ParamFilter p = s.params[i];
        if (p.type < 0 || p.type >= kParamTypeCount) p.type = PT_PEAK;
        const bool changed = parForce || p.on != lastPar_[i].on || p.type != lastPar_[i].type ||
                             p.freq != lastPar_[i].freq || p.gain != lastPar_[i].gain ||
                             p.q != lastPar_[i].q;
        lastPar_[i] = p;
        parOn_[i]   = p.on;
        par2On_[i]  = p.on && (p.type == PT_LOW_PASS_LR4 || p.type == PT_HIGH_PASS_LR4 ||
                               p.type == PT_TILT);
        if (!changed) continue;
        for (int c = 0; c < 2; ++c) {
            Biquad& f  = par_[c][i];
            Biquad& f2 = par2_[c][i];
            if (!p.on) { f.bypass(); f.reset(); f2.bypass(); f2.reset(); continue; }
            const float ff = clampf_(p.freq, 10.f, fs_ * 0.45f);
            const float qq = clampf_(p.q, 0.05f, 20.f);
            f2.bypass(); f2.reset();        // 缺省：第二级直通（仅 LR4 / TILT 启用）
            switch (p.type) {
                case PT_LOW_SHELF:  f.lowshelf (fs_, ff, qq, p.gain); break;
                case PT_HIGH_SHELF: f.highshelf(fs_, ff, qq, p.gain); break;
                case PT_LOW_PASS:   f.lowpass  (fs_, ff, qq); break;
                case PT_HIGH_PASS:  f.highpass (fs_, ff, qq); break;
                case PT_NOTCH:      f.notch    (fs_, ff, qq); break;
                case PT_ALL_PASS:   f.allpass  (fs_, ff, qq); break;
                case PT_BAND_PASS:  f.bandpass (fs_, ff, qq); break;
                // ---- v2.2 新增：一阶（6dB/oct）----
                case PT_LOW_SHELF_1:  f.lowshelf1 (fs_, ff, clampf_(p.gain, -36.f, 36.f)); break;
                case PT_HIGH_SHELF_1: f.highshelf1(fs_, ff, clampf_(p.gain, -36.f, 36.f)); break;
                case PT_LOW_PASS_1:   f.lowpass1  (fs_, ff); break;
                case PT_HIGH_PASS_1:  f.highpass1 (fs_, ff); break;
                // ---- v2.2 新增：LR4 = 两级二阶 Butterworth（Q=0.7071）串联 ----
                case PT_LOW_PASS_LR4:
                    f.lowpass (fs_, ff, 0.70710678f);
                    f2.lowpass(fs_, ff, 0.70710678f);
                    break;
                case PT_HIGH_PASS_LR4:
                    f.highpass (fs_, ff, 0.70710678f);
                    f2.highpass(fs_, ff, 0.70710678f);
                    break;
                // ---- v2.2 新增：TILT = 低架(-g/2) 串 高架(+g/2)，gain 为总倾斜量（±18dB）----
                case PT_TILT: {
                    float g = p.gain;
                    if (!std::isfinite(g)) g = 0.f;
                    const float half = clampf_(g, -18.f, 18.f) * 0.5f;
                    f .lowshelf (fs_, ff, 0.70710678f, -half);
                    f2.highshelf(fs_, ff, 0.70710678f,  half);
                    break;
                }
                case PT_PEAK:
                default:            f.peaking (fs_, ff, qq, p.gain); break;
            }
        }
    }

    // ---------------------------------------------------------- 3. 低音 / 清晰度 / 氛围
    if (s.bass != bassDb_) {
        bassDb_ = s.bass;
        for (int c = 0; c < 2; ++c) {
            bassShelf_[c].lowshelf(fs_, 105.f, 0.7f, bassDb_);
            bassPeak_[c].peaking(fs_, 62.f, 0.9f, bassDb_ * 0.55f);
        }
    }
    if (s.vocal != vocalDb_) {
        vocalDb_ = s.vocal;
        for (int c = 0; c < 2; ++c) vocalPeak_[c].peaking(fs_, 2600.f, 0.75f, vocalDb_);
    }
    if (s.ambience != ambDb_) {
        ambDb_ = s.ambience;
        for (int c = 0; c < 2; ++c) air_[c].highshelf(fs_, 11000.f, 0.7f, ambDb_ * 0.8f);
    }

    // ---------------------------------------------------------- 4. 低频分频（LR4）
    float xof = s.xoFreq;
    if (!(xof > 0.f) || !std::isfinite(xof)) xof = 0.f;
    if (xof > 0.f) xof = clampf_(xof, 20.f, std::min(300.f, fs_ * 0.45f));
    const bool wantXo = xof > 0.f;
    if (xof != xoFreq_) {
        xoFreq_ = xof;
        if (wantXo) {
            for (int c = 0; c < 2; ++c) {
                for (int k = 0; k < 2; ++k) {
                    xoLo_[c][k].lowpass (fs_, xof, 0.7071f);   // LR4 = 两级 Butterworth
                    xoHi_[c][k].highpass(fs_, xof, 0.7071f);
                    xoLo_[c][k].reset();
                    xoHi_[c][k].reset();
                }
            }
        }
    }
    if (!wantXo && xoOn_) {                                        // 切回 0：清干净
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 2; ++k) { xoLo_[c][k].reset(); xoHi_[c][k].reset(); }
    }
    xoOn_ = wantXo;
    subGainLin_ = powf(10.f, clampf_(s.subGain, -24.f, 24.f) / 20.f);

    // ---------------------------------------------------------- 5. 压缩模式
    dyn_ = clampf_(s.dynamics, 0.f, 1.f);
    compMode_ = (s.compMode >= COMP_OFF && s.compMode <= COMP_FIRM) ? s.compMode : COMP_OFF;
    float th = -12.f, ratio = 2.f, atk = 20.f, rel = 250.f;
    bool  active = false;
    if (compMode_ != COMP_OFF) {
        switch (compMode_) {
            case COMP_SOFT:   th = -12.f; ratio = 2.0f; atk = 20.f; rel = 250.f; break;
            case COMP_MEDIUM: th = -18.f; ratio = 3.5f; atk =  8.f; rel = 150.f; break;
            default:          th = -24.f; ratio = 6.0f; atk =  3.f; rel =  90.f; break;
        }
        th = th + (dyn_ - 0.5f) * 6.f;                 // 力度旋钮微调阈值
        compMakeup_ = powf(10.f, (dyn_ * 5.f) / 20.f);
        active = true;
    } else if (dyn_ > 0.02f) {                          // 向后兼容：仅 dynamics 旋钮
        th    = -6.f - 16.f * dyn_;
        ratio = 1.3f + 3.2f * dyn_;
        atk   = 6.f;
        rel   = 180.f;
        compMakeup_ = powf(10.f, (dyn_ * 5.f) / 20.f);
        active = true;
    }
    if (active) {
        compTh_ = th; compRatio_ = ratio; compAtk_ = atk; compRel_ = rel;
    }
    if (!active && compOn_) comp_.reset();
    compOn_ = active;

    // ---------------------------------------------------------- 6. 空间音频
    int smode = s.spatialMode;
    if (smode < SP_OFF || smode > SP_FIG8) smode = SP_OFF;
    if (smode != spMode_) {
        spMode_ = smode;
        spPhase_ = 0.f;
        spT_     = 0.f;
        spIdx_   = 0;
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < kSpBuf; ++i) spDl_[c][i] = 0.f;
            spHp_[c].reset();
        }
    }
    spOn_    = smode != SP_OFF;
    spRate_  = clampf_(s.spatialRate, 0.01f, 2.f);
    spDepth_ = clampf_(s.spatialDepth, 0.f, 1.f);

    // ---------------------------------------------------------- 7. 卷积
    irOn_  = s.irOn && irLen_ > 0;
    irMix_ = clampf_(s.irMix, 0.f, 1.f);

    // ---------------------------------------------------------- 8. 全局
    width_ = s.width;
    xfeed_ = s.crossfeed;
    volDb_ = s.vol;
    bal_   = s.balance;
    preampLin_ = 0.55f * powf(10.f, clampf_(s.preamp, -30.f, 30.f) / 20.f);
}

// ------------------------------------------------------------ IR 卷积
void DspChain::irComputeBlock() {
    const int N = kIrFft, B = kIrBlock;
    ensureIrBuffers();
    if ((int)irSpecRe_.size() < 2 * N) return;

    fftRe_.resize((size_t)N);
    fftIm_.resize((size_t)N);

    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < B; ++i) { fftRe_[i] = irIn_[(size_t)c * B + i]; fftIm_[i] = 0.f; }
        for (int i = B; i < N; ++i) { fftRe_[i] = 0.f; fftIm_[i] = 0.f; }
        fft(fftRe_, fftIm_, false);

        const float* sr = &irSpecRe_[(size_t)c * N];
        const float* si = &irSpecIm_[(size_t)c * N];
        for (int k = 0; k < N; ++k) {
            const float re = fftRe_[k] * sr[k] - fftIm_[k] * si[k];
            const float im = fftRe_[k] * si[k] + fftIm_[k] * sr[k];
            fftRe_[k] = re;
            fftIm_[k] = im;
        }
        fft(fftRe_, fftIm_, true);

        float* acc = &irAcc_[(size_t)c * N];
        for (int i = 0; i < N; ++i) acc[i] += fftRe_[i];
        for (int i = 0; i < B; ++i) {
            irOut_[(size_t)c * B + i] = acc[i];      // 本块输出
            acc[i] = acc[i + B];                     // 重叠部分留给下一块
            acc[i + B] = 0.f;
        }
    }
}

void DspChain::irPush(float* buf, int frames, int stride) {
    ensureIrBuffers();
    if (irLen_ <= 0 || (int)irSpecRe_.size() < 2 * kIrFft) return;

    const int   B       = kIrBlock;
    const float mix     = irMix_;
    const float dryMix  = 1.f - mix;
    const float wetGain = mix * 0.7f;          // 0.7 余量防止 IR 增益过冲

    for (int i = 0; i < frames; ++i) {
        irIn_[(size_t)0 * B + irFill_] = buf[i * stride];
        irIn_[(size_t)1 * B + irFill_] = buf[i * stride + 1];
        ++irFill_;
        if (irFill_ >= B) {
            irComputeBlock();
            irFill_     = 0;
            irOutCount_ = B;
            irOutPos_   = 0;
        }

        float l = buf[i * stride];
        float r = buf[i * stride + 1];
        if (irOutCount_ > 0) {                  // 有湿信号可用；否则先输出干声
            const float wl = irOut_[(size_t)0 * B + irOutPos_];
            const float wr = irOut_[(size_t)1 * B + irOutPos_];
            ++irOutPos_;
            --irOutCount_;
            l = l * dryMix + wl * wetGain;
            r = r * dryMix + wr * wetGain;
        }
        buf[i * stride]     = l;
        buf[i * stride + 1] = r;
    }
}

bool DspChain::loadIr(const std::wstring& path, std::wstring* err) {
    std::vector<float> ch[2];
    unsigned rate = 0;
    if (!readWavIr(path, ch, &rate, err)) return false;

    const int n0 = (int)ch[0].size();
    if (n0 <= 0) { if (err) *err = L"IR 没有采样数据"; return false; }
    if (rate == 0) rate = (unsigned)(fs_ + 0.5f);

    int nOut = (int)std::lround((double)n0 * (double)fs_ / (double)rate);
    if (nOut < 1) nOut = 1;
    if (nOut > kIrTaps) nOut = kIrTaps;

    std::vector<float> tmp[2];
    tmp[0].resize((size_t)nOut);
    tmp[1].resize((size_t)nOut);

    if (nOut == n0) {
        tmp[0] = ch[0];
        tmp[1] = ch[1];
    } else {                                    // 线性插值重采样
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < nOut; ++i) {
                double sp = (double)i * (double)rate / (double)fs_;
                int i0 = (int)std::floor(sp);
                if (i0 < 0) i0 = 0;
                if (i0 > n0 - 1) i0 = n0 - 1;
                const int i1 = (i0 + 1 < n0) ? i0 + 1 : i0;
                float t = (float)(sp - (double)i0);
                t = clampf_(t, 0.f, 1.f);
                tmp[c][(size_t)i] = ch[c][(size_t)i0] * (1.f - t) + ch[c][(size_t)i1] * t;
            }
        }
    }

    // 峰值归一化到 1.0
    float peak = 0.f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < nOut; ++i) {
            const float a = fabsf(tmp[c][(size_t)i]);
            if (a > peak) peak = a;
        }
    if (peak < 1e-9f) { if (err) *err = L"IR 全为 0，无法归一化"; return false; }
    const float g = 1.f / peak;

    // IR 频谱（左右各一套）
    const int N = kIrFft;
    irSpecRe_.assign((size_t)2 * N, 0.f);
    irSpecIm_.assign((size_t)2 * N, 0.f);
    fftRe_.assign((size_t)N, 0.f);
    fftIm_.assign((size_t)N, 0.f);

    for (int c = 0; c < 2; ++c) {
        std::fill(fftRe_.begin(), fftRe_.end(), 0.f);
        std::fill(fftIm_.begin(), fftIm_.end(), 0.f);
        for (int i = 0; i < nOut; ++i) fftRe_[i] = tmp[c][(size_t)i] * g;
        fft(fftRe_, fftIm_, false);
        for (int k = 0; k < N; ++k) {
            irSpecRe_[(size_t)c * N + k] = fftRe_[k];
            irSpecIm_[(size_t)c * N + k] = fftIm_[k];
        }
    }

    irLen_ = nOut;
    ensureIrBuffers();
    std::fill(irIn_.begin(),  irIn_.end(),  0.f);
    std::fill(irOut_.begin(), irOut_.end(), 0.f);
    std::fill(irAcc_.begin(), irAcc_.end(), 0.f);
    irFill_ = 0; irOutCount_ = 0; irOutPos_ = 0;
    return true;
}

void DspChain::clearIr() {
    irLen_ = 0;
    irSpecRe_.clear();
    irSpecIm_.clear();
    if (!irIn_.empty())  std::fill(irIn_.begin(),  irIn_.end(),  0.f);
    if (!irOut_.empty()) std::fill(irOut_.begin(), irOut_.end(), 0.f);
    if (!irAcc_.empty()) std::fill(irAcc_.begin(), irAcc_.end(), 0.f);
    irFill_ = 0; irOutCount_ = 0; irOutPos_ = 0;
    irOn_ = false;
}

// ------------------------------------------------------------ 主处理
void DspChain::process(float* buf, int frames, int stride) {
    if (!buf || frames <= 0) return;
    if (stride < 2) stride = 2;

    const float sm  = 0.0009f;
    const float volLin = powf(10.f, volDb_ / 20.f);
    const float xfGain = powf(10.f, (-4.5f - xfeed_ * 5.0f) / 20.f);   // bs2b 串扰电平
    const float ambG   = curAmb_ > 0.05f ? curAmb_ / 6.f * 0.20f : 0.f;

    // ---- 1. 前级（0.55 固定 + preamp dB）
    if (preampLin_ != 1.f) {
        for (int i = 0; i < frames; ++i) {
            buf[i * stride]     *= preampLin_;
            buf[i * stride + 1] *= preampLin_;
        }
    }

    // ---- 2. 卷积（IR）
    if (irOn_ && irLen_ > 0) irPush(buf, frames, stride);

    // ---- 3. 逐采样链路
    for (int i = 0; i < frames; ++i) {
        float l = buf[i * stride];
        float r = buf[i * stride + 1];

        curBass_  += (bassDb_  - curBass_)  * sm;
        curVocal_ += (vocalDb_ - curVocal_) * sm;
        curAmb_   += (ambDb_   - curAmb_)   * sm;
        curWidth_ += (width_   - curWidth_) * sm;
        curXf_    += (xfeed_   - curXf_)    * sm;
        curBal_   += (bal_     - curBal_)   * sm;

        // 3a. 图形 EQ（10/15/31 段，含耳机校正）
        for (int b = 0; b < eqCount_; ++b) {
            if (eqTotal_[b] == 0.f) continue;
            l = eq_[0][b].run(l);
            r = eq_[1][b].run(r);
        }

        // 3b. 参量 EQ（par_ 为第一级；par2_ 为第二级，仅 LR4 / TILT 参与）
        for (int p = 0; p < kMaxParams; ++p) {
            if (!parOn_[p]) continue;
            l = par_[0][p].run(l);
            r = par_[1][p].run(r);
            if (par2On_[p]) {
                l = par2_[0][p].run(l);
                r = par2_[1][p].run(r);
            }
        }

        // 3c. 低音增强
        if (curBass_ > 0.01f || curBass_ < -0.01f) {
            l = bassPeak_[0].run(bassShelf_[0].run(l));
            r = bassPeak_[1].run(bassShelf_[1].run(r));
        }

        // 3d. 清晰度
        if (curVocal_ > 0.01f || curVocal_ < -0.01f) {
            l = vocalPeak_[0].run(l);
            r = vocalPeak_[1].run(r);
        }

        // 3e. 氛围：空气感 + 交叉早期反射扩散
        if (curAmb_ > 0.05f) {
            l = air_[0].run(l);
            r = air_[1].run(r);
            float tL = ambL_[0][(ambIdx_ - 336) & 1023];
            float tR = ambR_[1][(ambIdx_ - 528) & 1023];
            l += ambLp_[0][0].run(tR) * ambG;
            r += ambLp_[1][1].run(tL) * ambG;
            ambL_[0][ambIdx_] = l;
            ambR_[1][ambIdx_] = r;
            ambIdx_ = (ambIdx_ + 1) & 1023;
        }

        // 3f. 分频 / 独立低音声道（LR4，低频并成单声道）
        if (xoOn_) {
            const float loL = xoLo_[0][1].run(xoLo_[0][0].run(l));
            const float loR = xoLo_[1][1].run(xoLo_[1][0].run(r));
            const float hiL = xoHi_[0][1].run(xoHi_[0][0].run(l));
            const float hiR = xoHi_[1][1].run(xoHi_[1][0].run(r));
            const float mono = (loL + loR) * 0.5f * subGainLin_;
            l = hiL + mono;
            r = hiR + mono;
        }

        buf[i * stride]     = l;
        buf[i * stride + 1] = r;
    }

    // ---- 4. 压缩
    if (compOn_) {
        comp_.process(buf, frames, stride, compTh_, compRatio_, compAtk_, compRel_, fs_);
        grDb_ = comp_.envDb;
        if (compMakeup_ != 1.f) {
            for (int i = 0; i < frames; ++i) {
                buf[i * stride]     *= compMakeup_;
                buf[i * stride + 1] *= compMakeup_;
            }
        }
    } else {
        grDb_ = 0.f;
    }

    // ---- 5. 空间音频 / 6. 宽度 / 7. 串扰 / 8. 音量 + 平衡
    for (int i = 0; i < frames; ++i) {
        float l = buf[i * stride], r = buf[i * stride + 1];

        // 5. 空间音频（8D / 10D / 8 字）：ILD + ITD，对侧声道延时并加 120Hz 高通
        if (spOn_) {
            spPhase_ += spRate_ / fs_;
            if (spPhase_ >= 1.f) spPhase_ -= 1.f;
            spT_ += 1.f / fs_;

            const float ph = 2.f * kPi * spPhase_;
            float az;                       // 声像角 -1(左)..+1(右)
            if (spMode_ == SP_FIG8) {
                az = sinf(2.f * ph);                       // 8 字轨迹
            } else if (spMode_ == SP_10D) {
                az = sinf(ph) + 0.12f * sinf(2.f * kPi * 0.15f * spT_);   // 叠 0.15Hz 闪烁
            } else {
                az = sinf(ph);                             // 8D
            }
            az = clampf_(az, -1.f, 1.f);

            const float ildK = (spMode_ == SP_10D) ? 0.85f : 0.6f;        // 10D 的 ILD 更强
            const float gl = 1.f + spDepth_ * ildK * az;
            const float gr = 1.f - spDepth_ * ildK * az;

            int d = (int)(fabsf(az) * spDepth_ * (float)kSpMaxItd);
            d = d < 0 ? 0 : (d > kSpMaxItd ? kSpMaxItd : d);

            spDl_[0][spIdx_] = l;
            spDl_[1][spIdx_] = r;
            const int rd = (spIdx_ - d) & (kSpBuf - 1);
            if (az > 0.f) {                 // 右声道（对侧）延时 + 高通
                r = spHp_[1].run(spDl_[1][rd]);
            } else if (az < 0.f) {          // 左声道（对侧）延时 + 高通
                l = spHp_[0].run(spDl_[0][rd]);
            }
            spIdx_ = (spIdx_ + 1) & (kSpBuf - 1);

            l *= gl;
            r *= gr;
        }

        // 6. 立体声宽度
        if (curWidth_ > 0.01f) {
            float m = (l + r) * 0.5f;
            float s = (l - r) * 0.5f * curWidth_;
            l = m + s; r = m - s;
        }
        if (curXf_ > 0.005f) {
            float dl0 = xfDelay_[1][xfIdx_];
            float dl1 = xfDelay_[0][xfIdx_];
            xfDelay_[0][xfIdx_] = l;
            xfDelay_[1][xfIdx_] = r;
            xfIdx_ = (xfIdx_ + 1) & 15;
            l += xfGain * xfLp_[0].run(dl0);
            r += xfGain * xfLp_[1].run(dl1);
        }
        float gl = volLin * (curBal_ <= 0.f ? 1.f : 1.f - curBal_);
        float gr = volLin * (curBal_ >= 0.f ? 1.f : 1.f + curBal_);
        buf[i * stride]     = l * gl;
        buf[i * stride + 1] = r * gr;
    }

    // ---- 9. 限幅
    lim_.process(buf, frames, stride, -0.6f, fs_);
}
