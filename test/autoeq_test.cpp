// AutoEQ 档案导入自测（控制台）：验证 ParametricEQ.txt / GraphicEQ.txt 的解析
#include "app.h"
#include <cstdio>
#include <cwchar>

static void Dump(const Settings& s, const wchar_t* tag) {
    wprintf(L"--- %s\n", tag);
    wprintf(L"preamp=%.2f  correction=%d  eqCount=%d\n", s.preamp, s.correction, s.eqCount);
    int on = 0;
    for (int i = 0; i < kMaxParams; ++i) {
        if (!s.params[i].on) continue;
        ++on;
        wprintf(L"F%d on type=%s(%d) f=%.1f g=%.1f q=%.2f\n", i + 1,
                kParamTypeName[s.params[i].type], s.params[i].type,
                s.params[i].freq, s.params[i].gain, s.params[i].q);
    }
    if (on == 0) {
        wprintf(L"eq:");
        for (int b = 0; b < s.eqCount; ++b) wprintf(L" %+.1f", s.eq[b]);
        wprintf(L"\n");
    }
}

int main() {
    int fails = 0;
    {
        Settings s;
        std::wstring msg;
        int rc = s.loadAutoEq(L"E:\\tmp2\\ParametricEQ.txt", &msg);
        wprintf(L"ParametricEQ rc=%d msg=%s\n", rc, msg.c_str());
        Dump(s, L"parametric");
        if (rc != 0) ++fails;
        if (s.params[0].on && s.params[0].type == PT_PEAK && s.params[0].gain > 5.4f && s.params[0].gain < 5.6f) wprintf(L"OK preamp/gain parsed\n");
        else { wprintf(L"FAIL gain\n"); ++fails; }
        if (s.params[1].type == PT_LOW_SHELF && s.params[2].type == PT_HIGH_SHELF) wprintf(L"OK LS/HS mapped\n");
        else { wprintf(L"FAIL shelf mapping\n"); ++fails; }
        if (s.preamp > -7.0f && s.preamp < -6.8f) wprintf(L"OK preamp -6.9\n");
        else { wprintf(L"FAIL preamp %.2f\n", s.preamp); ++fails; }
        if (s.correction == 0) wprintf(L"OK correction off\n");
        else { wprintf(L"FAIL correction\n"); ++fails; }
    }
    {
        Settings s;
        std::wstring msg;
        int rc = s.loadAutoEq(L"E:\\tmp2\\GraphicEQ.txt", &msg);
        wprintf(L"GraphicEQ rc=%d msg=%s\n", rc, msg.c_str());
        Dump(s, L"graphic");
        if (rc != 0) ++fails;
        // 2.2kHz 附近应该接近 +2.2dB
        float at2k = EqGainAt(s.eq, s.eqCount, 2200.f);
        wprintf(L"curve@2200Hz = %+.2f dB\n", at2k);
        if (at2k > 1.6f && at2k < 2.6f) wprintf(L"OK graphic interpolation\n");
        else { wprintf(L"FAIL graphic %+.2f\n", at2k); ++fails; }
        if (s.preamp > -0.01f && s.preamp < 0.01f) wprintf(L"OK graphic no preamp\n");
        else { wprintf(L"FAIL graphic preamp\n"); ++fails; }
    }
    {
        Settings s;
        std::wstring msg;
        int rc = s.loadAutoEq(L"E:\\tmp2\\notexist.txt", &msg);
        wprintf(L"missing file rc=%d msg=%s\n", rc, msg.c_str());
        if (rc == 1) wprintf(L"OK missing -> 1\n");
        else { wprintf(L"FAIL missing\n"); ++fails; }
    }
    wprintf(L"\n%s (%d failures)\n", fails ? L"FAILED" : L"ALL PASS", fails);
    return fails ? 1 : 0;
}
