// 引擎自测：不经 UI 直接驱动 AudioEngine，打印真实数字
#include <windows.h>
#include <cstdio>
#include <string>
#include "../src/audio.h"

int main(int argc, char** argv) {
    const wchar_t* path = argc > 1 ? nullptr : L"E:\\AuraTune_Source\\test\\test_long.wav";
    std::wstring p;
    if (argc > 1) {
        // 简易 utf8 -> wide
        int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        p.resize(n);
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &p[0], n);
    } else {
        p = path;
    }

    AudioEngine eng;
    eng.start();
    printf("device : %ls\n", eng.deviceName().c_str());
    eng.setVolumePercent(80);

    eng.openFile(p);
    for (int i = 0; i < 60; i++) {
        Sleep(200);
        if (eng.hasTrack()) break;
    }
    printf("hasTrack=%d dur=%.2f fs=%d bufFrames=%d lat=%.1f\n", (int)eng.hasTrack(), eng.duration(), eng.sampleRate(), eng.bufferFrames(), eng.latencyMs());
    eng.play();

    for (int i = 0; i < 12; i++) {
        Sleep(500);
        MeterData m = eng.meter();
        printf("t=%4.1fs fs=%d buf=%d pos=%6.2f playing=%d dev=%d active=%d rms=%.4f/%.4f peak=%.3f/%.3f gr=%.1f bars=",
               (i + 1) * 0.5, eng.sampleRate(), eng.bufferFrames(), eng.position(), (int)m.playing, (int)eng.deviceOk(), (int)m.active,
               m.rmsL, m.rmsR, m.peakL, m.peakR, m.grDb);
        for (int b = 0; b < 12; b++) printf("%.2f ", m.bars[b]);
        printf("... b40=%.2f lat=%.1fms\n", m.bars[40], eng.latencyMs());
    }

    eng.stop();
    return 0;
}
