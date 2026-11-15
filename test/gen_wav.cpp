// 生成测试用 WAV：多段信号，用于验证解码/频谱/电平
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include <random>
#include <cstdlib>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF); }
static void put16(std::vector<uint8_t>& v, uint16_t x) { for (int i = 0; i < 2; i++) v.push_back((x >> (8 * i)) & 0xFF); }

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "test_tone.wav";
    const int sr = 48000;
    const int ch = 2;
    const double dur = argc > 2 ? atof(argv[2]) : 12.0;
    const int n = (int)(sr * dur);

    std::vector<int16_t> pcm;
    pcm.reserve(n * ch);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);

    for (int i = 0; i < n; i++) {
        double t = (double)i / sr;
        double l = 0, r = 0;
        if (t < 3.0) {
            // 1kHz 单音 -12dBFS
            l = r = 0.25 * sin(2 * M_PI * 1000.0 * t);
        } else if (t < 6.0) {
            // 60Hz + 120Hz 低频
            l = r = 0.30 * sin(2 * M_PI * 60.0 * t) + 0.15 * sin(2 * M_PI * 120.0 * t);
        } else if (t < 9.0) {
            // 粉噪声（近似）用于频谱
            static double b0 = 0, b1 = 0, b2 = 0;
            double w = uni(rng);
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            double p = (b0 + b1 + b2 + w * 0.1848) * 0.12;
            l = p; r = p * 0.9;
        } else {
            // 20Hz-20kHz 对数扫频
            double k = (t - 9.0) / 3.0;
            double f = 20.0 * pow(1000.0, k);
            static double ph = 0;
            ph += 2 * M_PI * f / sr;
            l = r = 0.3 * sin(ph);
        }
        // 淡入淡出，避免爆音
        double env = 1.0;
        if (t < 0.05) env = t / 0.05;
        if (t > dur - 0.05) env = (dur - t) / 0.05;
        double L = l * env, R = r * env;
        if (L > 0.999) L = 0.999;
        if (L < -0.999) L = -0.999;
        if (R > 0.999) R = 0.999;
        if (R < -0.999) R = -0.999;
        pcm.push_back((int16_t)lrint(L * 32767.0));
        pcm.push_back((int16_t)lrint(R * 32767.0));
    }

    const uint32_t dataBytes = (uint32_t)(pcm.size() * 2);
    std::vector<uint8_t> hdr;
    const char* riff = "RIFF"; hdr.insert(hdr.end(), riff, riff + 4);
    put32(hdr, 36 + dataBytes);
    const char* wave = "WAVEfmt "; hdr.insert(hdr.end(), wave, wave + 8);
    put32(hdr, 16);
    put16(hdr, 1);              // PCM
    put16(hdr, (uint16_t)ch);
    put32(hdr, sr);
    put32(hdr, sr * ch * 2);    // byte rate
    put16(hdr, (uint16_t)(ch * 2));
    put16(hdr, 16);
    const char* dat = "data"; hdr.insert(hdr.end(), dat, dat + 4);
    put32(hdr, dataBytes);

    FILE* f = fopen(out, "wb");
    if (!f) { printf("cannot open %s\n", out); return 1; }
    fwrite(hdr.data(), 1, hdr.size(), f);
    fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
    printf("wrote %s : %d frames, %.1fs\n", out, n, dur);
    return 0;
}
