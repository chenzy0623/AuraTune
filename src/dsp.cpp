#include "dsp.h"
#include <cmath>

static const float kPi = 3.14159265358979f;

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

void DspChain::resetAll() {
    for (int c = 0; c < 2; ++c) {
        for (int b = 0; b < kBands; ++b) eq_[c][b].reset();
        bassShelf_[c].reset(); bassPeak_[c].reset(); vocalPeak_[c].reset();
        air_[c].reset();
        xfLp_[c].reset();
        for (int i = 0; i < 16; ++i) xfDelay_[c][i] = 0;
        for (int i = 0; i < 1024; ++i) { ambL_[c][i] = 0; ambR_[c][i] = 0; }
    }
    xfIdx_ = 0;
    ambIdx_ = 0;
    comp_.reset();
    lim_.reset();
}

void DspChain::prepare(float fs) {
    fs_ = fs;
    for (int c = 0; c < 2; ++c) {
        xfLp_[c].lowpass(fs_, 700.f, 0.5f);      // bs2b: 700Hz 一阶低通
        air_[c].highshelf(fs_, 11000.f, 0.7f, 0.f);
        for (int b = 0; b < kBands; ++b) eq_[c][b].peaking(fs_, kBandFreq[b], 1.05f, 0.f);
        bassShelf_[c].lowshelf(fs_, 105.f, 0.7f, 0.f);
        bassPeak_[c].peaking(fs_, 62.f, 0.9f, 0.f);
        vocalPeak_[c].peaking(fs_, 2600.f, 0.75f, 0.f);
        ambLp_[c][0].lowpass(fs_, 6500.f, 0.6f);
        ambLp_[c][1].lowpass(fs_, 6500.f, 0.6f);
    }
    resetAll();
}

void DspChain::configure(const Settings& s) {
    // 用户 EQ + 耳机校正曲线 叠加
    const Correction& cor = kCorrections[
        (s.correction >= 0 && s.correction < kCorrectionCount) ? s.correction : 0];
    for (int b = 0; b < kBands; ++b) {
        float total = s.eq[b] + cor.eq[b];
        if (total > 15.f) total = 15.f;
        if (total < -15.f) total = -15.f;
        corr_[b]  = total;
        eqGain_[b] = s.eq[b];
        if (total != lastBand_[b]) {
            lastBand_[b] = total;
            for (int c = 0; c < 2; ++c)
                eq_[c][b].peaking(fs_, kBandFreq[b], (b == 0 || b == kBands - 1) ? 0.7f : 1.05f, total);
        }
    }
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
    width_ = s.width;
    xfeed_ = s.crossfeed;
    dyn_   = s.dynamics;
    volDb_ = s.vol;
    bal_   = s.balance;
}

void DspChain::process(float* buf, int frames, int stride) {
    const float sm = 0.0009f;
    float volLin = powf(10.f, volDb_ / 20.f);
    float xfGain = powf(10.f, (-4.5f - xfeed_ * 5.0f) / 20.f);   // bs2b: -4.5..-9.5dB 串扰电平
    float ambG   = curAmb_ > 0.05f ? curAmb_ / 6.f * 0.20f : 0.f;

    for (int i = 0; i < frames; ++i) {
        float l = buf[i * stride] * 0.55f;
        float r = buf[i * stride + 1] * 0.55f;

        curBass_  += (bassDb_  - curBass_)  * sm;
        curVocal_ += (vocalDb_ - curVocal_) * sm;
        curAmb_   += (ambDb_   - curAmb_)   * sm;
        curWidth_ += (width_   - curWidth_) * sm;
        curXf_    += (xfeed_   - curXf_)    * sm;
        curBal_   += (bal_     - curBal_)   * sm;

        // 1. 10 段 EQ（含耳机校正）
        for (int b = 0; b < kBands; ++b) {
            if (corr_[b] != 0.f) {
                l = eq_[0][b].run(l);
                r = eq_[1][b].run(r);
            }
        }
        // 2. 低音增强
        if (curBass_ > 0.01f || curBass_ < -0.01f) {
            l = bassPeak_[0].run(bassShelf_[0].run(l));
            r = bassPeak_[1].run(bassShelf_[1].run(r));
        }
        // 3. 清晰度
        if (curVocal_ > 0.01f || curVocal_ < -0.01f) {
            l = vocalPeak_[0].run(l);
            r = vocalPeak_[1].run(r);
        }
        // 4. 氛围：空气感 + 交叉早期反射扩散
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
        buf[i * stride]     = l;
        buf[i * stride + 1] = r;
    }

    // 5. 动态增强（压缩）
    if (dyn_ > 0.02f) {
        float th = -6.f - 16.f * dyn_;
        float ratio = 1.3f + 3.2f * dyn_;
        comp_.process(buf, frames, stride, th, ratio, 6.f, 180.f, fs_);
        grDb_ = comp_.envDb;
        float makeup = powf(10.f, (dyn_ * 5.f) / 20.f);
        for (int i = 0; i < frames; ++i) {
            buf[i * stride]     *= makeup;
            buf[i * stride + 1] *= makeup;
        }
    } else {
        grDb_ = 0;
    }

    // 6. 宽度 / 7. 串扰 / 8. 音量
    for (int i = 0; i < frames; ++i) {
        float l = buf[i * stride], r = buf[i * stride + 1];
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

    // 9. 限幅
    lim_.process(buf, frames, stride, -0.6f, fs_);
}
