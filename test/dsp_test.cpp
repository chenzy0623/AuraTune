// AuraTune DSP 测试程序（临时验证用）
// 只链接 src/dsp.cpp；app.h 里的 BandFreqs/BandFreqAt/EqGainAt/kCorrections
// 在本文件内用等价实现 stub 掉（app.cpp 依赖 shlwapi/tlhelp32，不便链接）。
#include "../src/dsp.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const double kPI = M_PI;

// ==================================================== app.cpp 的 stub
static const float kFreq10[10] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
static const float kFreq15[15] = {25, 40, 63, 100, 160, 250, 400, 630, 1000,
                                  1600, 2500, 4000, 6300, 10000, 16000};
static const float kFreq31[31] = {20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200,
                                  250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
                                  2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000,
                                  12500, 16000, 20000};

const float* BandFreqs(int count, int* n) {
    if (count == 15) { if (n) *n = 15; return kFreq15; }
    if (count == 31) { if (n) *n = 31; return kFreq31; }
    if (n) *n = 10;
    return kFreq10;
}

float BandFreqAt(int count, int i) {
    int n = 0;
    const float* t = BandFreqs(count, &n);
    if (i < 0) i = 0;
    if (i > n - 1) i = n - 1;
    return t[i];
}

float EqGainAt(const float* eq, int count, float f) {
    int n = 0;
    const float* t = BandFreqs(count, &n);
    if (f <= t[0]) return eq[0];
    if (f >= t[n - 1]) return eq[n - 1];
    for (int i = 0; i + 1 < n; ++i) {
        if (f >= t[i] && f <= t[i + 1]) {
            const double lo = std::log((double)t[i]), hi = std::log((double)t[i + 1]);
            const double x  = (std::log((double)f) - lo) / (hi - lo);
            return (float)(eq[i] + (eq[i + 1] - eq[i]) * x);
        }
    }
    return 0.f;
}

const Correction kCorrections[] = {
    { L"关闭", L"", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
    { L"Harman 近似", L"", {5, 4, 2, 0, -0.5f, 0, 1.5f, 2, -1, 0.5f} },
};
const int kCorrectionCount = 2;

// ==================================================== 测试工具
static int gPass = 0, gFail = 0;

static void report(bool ok, const std::string& name, const char* fmt, ...) {
    char detail[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    printf("[%s] %-52s | %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail);
    if (ok) ++gPass; else ++gFail;
}

static double rmsOf(const std::vector<float>& x, int from, int n) {
    if (n <= 0) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) { const double v = x[(size_t)(from + i)]; s += v * v; }
    return std::sqrt(s / (double)n);
}

// 单频点幅度（峰值），用于测某频率的增益
static double ampAt(const std::vector<float>& x, int from, int n, double f, double fs) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i) {
        const double ph = 2.0 * kPI * f * (double)(from + i) / fs;
        const double v  = (double)x[(size_t)(from + i)];
        re += v * std::cos(ph);
        im += v * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (double)n;
}

static double dbOf(double a) { return 20.0 * std::log10(a + 1e-12); }

// 输入/输出对齐用：找出使 RMS 误差最小的延迟
static int bestLag(const std::vector<float>& out, const std::vector<float>& in,
                   int maxLag, int from, int n) {
    int best = 0; double bestErr = 1e30;
    for (int lag = 0; lag <= maxLag; ++lag) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) {
            const double d = (double)out[(size_t)(from + i)] - (double)in[(size_t)(from + i - lag)];
            s += d * d;
        }
        if (s < bestErr) { bestErr = s; best = lag; }
    }
    return best;
}

static void genSine(std::vector<float>& l, std::vector<float>& r, int n,
                    double fs, double f, double aL, double aR, double ph0 = 0.0) {
    l.assign((size_t)n, 0.f);
    r.assign((size_t)n, 0.f);
    for (int i = 0; i < n; ++i) {
        const double v = std::sin(2.0 * kPI * f * (double)i / fs + ph0);
        l[(size_t)i] = (float)(v * aL);
        r[(size_t)i] = (float)(v * aR);
    }
}

// 无任何音染的基准设置（0.55 固定前级用 preamp 抵消）
static Settings neutralSettings() {
    Settings s;
    s.mode = MODE_MUSIC;
    s.eqCount = kBands;
    for (int i = 0; i < kMaxBands; ++i) s.eq[i] = 0.f;
    s.bass = 0.f; s.vocal = 0.f; s.ambience = 0.f;
    s.width = 1.f; s.dynamics = 0.f; s.crossfeed = 0.f;
    s.correction = 0; s.vol = 0.f; s.balance = 0.f;
    s.bypass = false;
    s.preamp = (float)(20.0 * std::log10(1.0 / 0.55));
    for (int i = 0; i < kMaxParams; ++i) s.params[i] = ParamFilter();
    s.xoFreq = 0.f; s.subGain = 0.f;
    s.compMode = COMP_OFF;
    s.spatialMode = SP_OFF;
    s.spatialRate = 0.12f; s.spatialDepth = 0.5f;
    s.irOn = false; s.irMix = 1.f;
    return s;
}

struct RunResult {
    std::vector<float> l, r;
};

// 以 512 帧一块跑完整链路（等价真实引擎的调用方式）
static void runChain(DspChain& dsp, const Settings& s,
                     const std::vector<float>& inL, const std::vector<float>& inR,
                     RunResult& out, int block = 512) {
    const int n = (int)inL.size();
    out.l.assign((size_t)n, 0.f);
    out.r.assign((size_t)n, 0.f);
    std::vector<float> buf((size_t)block * 2, 0.f);
    for (int pos = 0; pos < n; pos += block) {
        const int nf = std::min(block, n - pos);
        for (int i = 0; i < nf; ++i) {
            buf[(size_t)(i * 2)]     = inL[(size_t)(pos + i)];
            buf[(size_t)(i * 2 + 1)] = inR[(size_t)(pos + i)];
        }
        dsp.configure(s);
        dsp.process(buf.data(), nf, 2);
        for (int i = 0; i < nf; ++i) {
            out.l[(size_t)(pos + i)] = buf[(size_t)(i * 2)];
            out.r[(size_t)(pos + i)] = buf[(size_t)(i * 2 + 1)];
        }
    }
}

// 造一个 16bit 单声道 WAV：第 0 个 tap = 1.0，其余 0（4 tap 全通冲激）
static bool writeDeltaWav(const char* path, int taps, unsigned rate) {
    std::vector<short> data((size_t)taps, 0);
    data[0] = 32767;
    const unsigned dataBytes = (unsigned)(data.size() * 2);
    unsigned char hdr[44];
    const unsigned riffSize = 36 + dataBytes;
    const unsigned fmtSize  = 16;
    const unsigned short pcm = 1, ch = 1, bits = 16, align = 2;
    const unsigned byteRate = rate * 2;
    memcpy(hdr + 0,  "RIFF", 4);
    memcpy(hdr + 4,  &riffSize, 4);
    memcpy(hdr + 8,  "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    memcpy(hdr + 16, &fmtSize, 4);
    memcpy(hdr + 20, &pcm, 2);
    memcpy(hdr + 22, &ch, 2);
    memcpy(hdr + 24, &rate, 4);
    memcpy(hdr + 28, &byteRate, 4);
    memcpy(hdr + 32, &align, 2);
    memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &dataBytes, 4);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(hdr, 1, 44, f);
    fwrite(data.data(), 2, data.size(), f);
    fclose(f);
    return true;
}

static std::string narrow(const std::wstring& w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) s.push_back((char)(w[i] & 0xFF));
    return s;
}

// ==================================================== 测试 1：31 段全 0dB → 输出≈输入
static void test1_flat31() {
    const double fs = 48000.0;
    const int n = 48000;
    std::vector<float> inL, inR;
    genSine(inL, inR, n + 2000, fs, 1000.0, 0.3, 0.3);

    Settings s = neutralSettings();
    s.eqCount = 31;
    for (int i = 0; i < kMaxBands; ++i) s.eq[i] = 0.f;   // 31 段全 0dB

    DspChain dsp;
    dsp.prepare((float)fs);
    RunResult out;
    runChain(dsp, s, inL, inR, out);

    const int from = 8000, cnt = n - from;
    const int lag = bestLag(out.l, inL, 200, from, cnt);
    double err = 0.0;
    for (int i = 0; i < cnt; ++i) {
        const double d = (double)out.l[(size_t)(from + i)] - (double)inL[(size_t)(from + i - lag)];
        err += d * d;
    }
    err = std::sqrt(err / (double)cnt);
    const double inRms = rmsOf(inL, from - lag, cnt);
    const double relErr = err / (inRms + 1e-12);

    char name[128];
    snprintf(name, sizeof(name), "T1 31-band 全 0dB 输出≈输入 (RMS 误差<1%%)");
    report(relErr < 0.01, name,
           "relErr=%.6f%% (inRMS=%.5f, errRMS=%.7f, lag=%d)", relErr * 100.0, inRms, err, lag);
}

// ==================================================== 测试 2：某段 +12dB
static void test2_boost31() {
    const double fs = 48000.0;
    const int n = 48000;
    const int band = 17;                      // 31 段表里 1000Hz
    const float fc = BandFreqAt(31, band);
    std::vector<float> inL, inR;
    genSine(inL, inR, n + 2000, fs, 1000.0, 0.05, 0.05);

    Settings s = neutralSettings();
    s.eqCount = 31;
    for (int i = 0; i < kMaxBands; ++i) s.eq[i] = 0.f;
    s.eq[band] = 12.f;

    DspChain dsp;
    dsp.prepare((float)fs);
    RunResult out;
    runChain(dsp, s, inL, inR, out);

    const int from = 8192, cnt = 12000;
    const double aIn  = ampAt(inL, from, cnt, 1000.0, fs);
    const double aOut = ampAt(out.l, from, cnt, 1000.0, fs);
    const double g1k  = dbOf(aOut / aIn);

    // 另跑一次 100Hz 正弦，验证其它频段几乎不受影响
    std::vector<float> loL, loR;
    genSine(loL, loR, n + 2000, fs, 100.0, 0.05, 0.05);
    RunResult outLo;
    runChain(dsp, s, loL, loR, outLo);
    const double g100 = dbOf(ampAt(outLo.l, from, cnt, 100.0, fs) /
                             (ampAt(loL, from, cnt, 100.0, fs) + 1e-12));

    char name[128];
    snprintf(name, sizeof(name), "T2 31-band 第%d段(f=%.0fHz)+12dB 该频点增益变大", band, fc);
    report(g1k > 9.0 && g1k < 15.0 && std::fabs(g100) < 1.5, name,
           "gain@1k=%.2fdB (期望≈+12), gain@100Hz=%.2fdB (期望≈0)", g1k, g100);
}

// ==================================================== 测试 3：参量 lowshelf
static void test3_parShelf() {
    const double fs = 48000.0;
    const int n = 48000;
    std::vector<float> inL, inR;
    genSine(inL, inR, n + 2000, fs, 50.0, 0.1, 0.1);

    Settings s = neutralSettings();
    s.params[0].on = true;
    s.params[0].type = PT_LOW_SHELF;
    s.params[0].freq = 200.f;
    s.params[0].gain = 12.f;
    s.params[0].q = 0.707f;

    DspChain dsp;
    dsp.prepare((float)fs);
    RunResult out;
    runChain(dsp, s, inL, inR, out);

    const int from = 8192, cnt = 12000;
    const double g50 = dbOf(ampAt(out.l, from, cnt, 50.0, fs) / ampAt(inL, from, cnt, 50.0, fs));

    // 再跑一次用于检查高频不受影响
    std::vector<float> in2L, in2R;
    genSine(in2L, in2R, n + 2000, fs, 5000.0, 0.1, 0.1);
    RunResult out2;
    runChain(dsp, s, in2L, in2R, out2);
    const double g5k = dbOf(ampAt(out2.l, from, cnt, 5000.0, fs) / ampAt(in2L, from, cnt, 5000.0, fs));

    report(g50 > 6.0 && std::fabs(g5k) < 1.5,
           "T3 参量 EQ lowshelf(200Hz,+12dB) 生效",
           "gain@50Hz=%.2fdB (期望>+6), gain@5kHz=%.2fdB (期望≈0)", g50, g5k);
}

// ==================================================== 测试 4：IR 卷积
static void test4_ir() {
    const double fs = 48000.0;
    const int n = 48000;
    const char* wav = "E:/AuraTune_Source/test/ir_delta.wav";
    const bool wrote = writeDeltaWav(wav, 4, 48000);
    if (!wrote) { report(false, "T4 IR 卷积（4 tap 全通冲激）", "无法写临时 WAV: %s", wav); return; }

    Settings s = neutralSettings();
    s.irOn = true;
    s.irMix = 1.f;
    s.irPath = L"E:\\AuraTune_Source\\test\\ir_delta.wav";

    DspChain dsp;
    dsp.prepare((float)fs);
    std::wstring err;
    const bool ok = dsp.loadIr(s.irPath, &err);

    std::vector<float> inL, inR;
    genSine(inL, inR, n, fs, 1000.0, 0.3, 0.3);
    RunResult out;
    runChain(dsp, s, inL, inR, out);

    const int from = 16384, cnt = 12000;      // 跳过前两个 4096 块（输出还未就绪）
    const double aIn  = ampAt(inL, from, cnt, 1000.0, fs);
    const double aOut = ampAt(out.l, from, cnt, 1000.0, fs);
    const double ratio = aOut / aIn;

    report(ok && dsp.hasIr() && std::fabs(ratio - 0.7) < 0.01,
           "T4 IR 卷积：4 tap 全通冲激输出≈输入×0.7(湿声余量)",
           "loadIr=%d hasIr=%d ratio=%.4f (期望 0.7000, irMix=1 时 dry*(1-mix)+wet*mix*0.7) err='%s'",
           (int)ok, (int)dsp.hasIr(), ratio, narrow(err).c_str());

    dsp.clearIr();
    report(!dsp.hasIr(), "T4b clearIr() 清空 IR", "hasIr=%d (期望 0)", (int)dsp.hasIr());
}

// ==================================================== 测试 5：分频 → 低频单声道
static void test5_crossover() {
    const double fs = 48000.0;
    const int n = 48000;
    std::vector<float> inL, inR;
    genSine(inL, inR, n + 2000, fs, 40.0, 0.30, 0.075);   // 左右本来差 4 倍

    const int from = 8192, cnt = 12000;

    // (a) 分频关闭：R 只有 L 的 1/4
    Settings off = neutralSettings();
    DspChain d0;
    d0.prepare((float)fs);
    RunResult o0;
    runChain(d0, off, inL, inR, o0);
    const double offRatio = ampAt(o0.r, from, cnt, 40.0, fs) / (ampAt(o0.l, from, cnt, 40.0, fs) + 1e-12);

    // (b) 分频开启：40Hz 并成单声道，L/R 都应出现
    Settings on = neutralSettings();
    on.xoFreq = 80.f;
    on.subGain = 0.f;
    DspChain d1;
    d1.prepare((float)fs);
    RunResult o1;
    runChain(d1, on, inL, inR, o1);
    const double aL = ampAt(o1.l, from, cnt, 40.0, fs);
    const double aR = ampAt(o1.r, from, cnt, 40.0, fs);
    const double onRatio = aR / (aL + 1e-12);

    // (c) 低频部分 L/R 应几乎相同：差值(RMS) 远小于低频电平
    double diff = 0.0;
    for (int i = 0; i < cnt; ++i) {
        const double d = (double)o1.l[(size_t)(from + i)] - (double)o1.r[(size_t)(from + i)];
        diff += d * d;
    }
    diff = std::sqrt(diff / (double)cnt);
    const double ref = rmsOf(o1.l, from, cnt);

    report(onRatio > 0.55 && diff < 0.25 * ref,
           "T5 分频(80Hz LR4) 后低频变单声道 (L/R 相同)",
           "关: R/L=%.3f | 开: R/L=%.3f (期望>0.55) | RMS(L-R)/RMS(L)=%.3f (期望<0.25)",
           offRatio, onRatio, diff / (ref + 1e-12));
}

// ==================================================== 测试 6：空间 8D
static void test6_spatial() {
    const double fs = 48000.0;
    const int n = (int)(fs * 4.5);       // 4.5s，0.5Hz → 2 个完整周期
    std::vector<float> inL, inR;
    genSine(inL, inR, n, fs, 1000.0, 0.2, 0.2);

    Settings s = neutralSettings();
    s.spatialMode = SP_8D;
    s.spatialRate = 0.5f;
    s.spatialDepth = 1.0f;

    DspChain dsp;
    dsp.prepare((float)fs);
    RunResult out;
    runChain(dsp, s, inL, inR, out);

    const int win = 2400;                 // 50ms
    const int start = 8192;
    double maxd = -1e9, mind = 1e9, sumL = 0, sumR = 0;
    int nWin = 0;
    for (int k = start; k + win <= n; k += win) {
        const double a = rmsOf(out.l, k, win);
        const double b = rmsOf(out.r, k, win);
        const double d = (a - b) / (a + b + 1e-12);
        if (d > maxd) maxd = d;
        if (d < mind) mind = d;
        sumL += a; sumR += b; ++nWin;
    }
    const double meanL = sumL / nWin, meanR = sumR / nWin;

    report(maxd > 0.10 && mind < -0.10 && std::fabs(meanL - meanR) < 0.02,
           "T6 空间 8D：左右电平随时间此消彼长",
           "max(L-R)/(L+R)=%+.3f min=%+.3f | 平均 L=%.4f R=%.4f (期望均衡)",
           maxd, mind, meanL, meanR);
}

// ==================================================== 测试 7：压缩
static void test7_comp() {
    const double fs = 48000.0;
    const int n = 96000;                 // 2s
    std::vector<float> inL, inR;
    genSine(inL, inR, n, fs, 1000.0, 0.9, 0.9);   // 大声信号

    Settings s = neutralSettings();
    s.compMode = COMP_FIRM;
    s.dynamics = 1.0f;                   // th = -24 + (1.0-0.5)*6 = -21dB

    DspChain dsp;
    dsp.prepare((float)fs);
    RunResult out;
    runChain(dsp, s, inL, inR, out);

    const int from = n - 4800, cnt = 4800;
    const double inRms  = rmsOf(inL, from, cnt);
    const double outRms = rmsOf(out.l, from, cnt);
    const double ratio  = outRms / (inRms + 1e-12);
    const double gr     = dsp.grDb();

    report(gr > 3.0 && ratio < 0.5,
           "T7 压缩 FIRM(th=-21dB,ratio=6) 大声信号被压低",
           "grDb=%.2fdB (期望>3) outRMS/inRMS=%.3f (期望<0.5)", gr, ratio);
}

// ==================================================== 测试 8：变帧长 + 参数切换鲁棒性
// 真实引擎按 480~1056 帧可变长度调用，且用户会实时切段数/分频/空间/IR。
static void test8_robust() {
    const double fs = 48000.0;
    const int n = (int)(fs * 3.0);

    DspChain dsp;
    dsp.prepare((float)fs);
    std::wstring err;
    const bool irOk = dsp.loadIr(L"E:\\AuraTune_Source\\test\\ir_delta.wav", &err);

    Settings s = neutralSettings();
    s.eqCount = 15;
    s.eq[3] = 6.f; s.eq[7] = -4.f;
    s.bass = 3.f; s.vocal = 2.f; s.ambience = 2.f;
    s.width = 1.3f; s.crossfeed = 0.5f; s.dynamics = 0.6f;
    s.compMode = COMP_MEDIUM;
    s.spatialMode = SP_10D;
    s.xoFreq = 100.f; s.subGain = 6.f;
    s.irOn = irOk; s.irMix = 0.5f;
    s.preamp = 3.f;

    const int blocks[] = {480, 1056, 512, 960, 1024, 441, 480, 1056};
    std::vector<float> buf((size_t)1056 * 2, 0.f);
    std::vector<float> got;
    got.reserve((size_t)n);

    int pos = 0, bi = 0;
    unsigned rng = 12345u;
    while (pos < n) {
        const int nf = std::min(blocks[bi % 8], n - pos);
        ++bi;

        // 每隔若干块切一次参数，模拟用户操作
        if (bi % 7 == 0) {
            static const int counts[3] = {10, 15, 31};
            s.eqCount = counts[(bi / 7) % 3];
        }
        if (bi % 11 == 0) {
            static const float xos[3] = {0.f, 80.f, 120.f};
            s.xoFreq = xos[(bi / 11) % 3];
        }
        if (bi % 13 == 0) {
            s.spatialMode = (bi / 13) % 4;      // SP_OFF / 8D / 10D / 8字
        }
        if (bi % 17 == 0) {
            s.irOn = !s.irOn;
        }

        for (int i = 0; i < nf; ++i) {
            const double t  = (double)(pos + i) / fs;
            rng = rng * 1664525u + 1013904223u;
            const double nz = ((double)((rng >> 9) & 0xFFFF) / 32768.0 - 1.0) * 0.02;
            const double v  = 0.25 * std::sin(2.0 * kPI * 100.0 * t) +
                              0.20 * std::sin(2.0 * kPI * 1000.0 * t) + nz;
            buf[(size_t)(i * 2)]     = (float)v;
            buf[(size_t)(i * 2 + 1)] = (float)(v * 0.8);
        }
        dsp.configure(s);
        dsp.process(buf.data(), nf, 2);
        for (int i = 0; i < nf; ++i) {
            got.push_back(buf[(size_t)(i * 2)]);
            got.push_back(buf[(size_t)(i * 2 + 1)]);
        }
        pos += nf;
    }

    int bad = 0;
    double peak = 0.0;
    for (size_t i = 0; i < got.size(); ++i) {
        const float v = got[i];
        if (!std::isfinite(v)) ++bad;
        const double a = std::fabs((double)v);
        if (a > peak) peak = a;
    }
    const double energy = rmsOf(got, 0, (int)got.size());

    report(bad == 0 && peak <= 0.9991 && energy > 0.01,
           "T8 变帧长(480~1056)+实时切段数/分频/空间/IR：无 NaN、无爆表",
           "samples=%d NaN/Inf=%d peak=%.4f (限幅 0.999) rms=%.4f irLoaded=%d",
           (int)got.size(), bad, peak, energy, (int)irOk);
}

// ==================================================== main
int main() {
    printf("==== AuraTune DSP 测试 (fs=48000) ====\n");
    printf("chain: preamp -> IR -> graphEQ -> paramEQ -> bass -> clarity -> ambience"
           " -> crossover -> compressor -> spatial -> width -> crossfeed -> vol -> limiter\n\n");

    test1_flat31();
    test2_boost31();
    test3_parShelf();
    test4_ir();
    test5_crossover();
    test6_spatial();
    test7_comp();
    test8_robust();

    printf("\n==== 结果: %d passed, %d failed ====\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
