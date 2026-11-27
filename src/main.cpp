// AuraTune 入口
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>

int RunAuraTune(HINSTANCE hInst);

// 崩溃时把调用栈写进 build\crash.log，便于用 llvm-addr2line 反查（调试用）
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    void* frames[96] = { nullptr };
    USHORT n = CaptureStackBackTrace(0, 96, frames, nullptr);
    FILE* f = nullptr;
    _wfopen_s(&f, L"E:\\AuraTune_Source\\build\\crash.log", L"w");
    if (f) {
        fprintf(f, "code=%08lX addr=%p frames=%u\n",
                (unsigned long)ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress, (unsigned)n);
        for (USHORT i = 0; i < n; ++i) fprintf(f, "  #%02u %p\n", (unsigned)i, frames[i]);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetUnhandledExceptionFilter(CrashFilter);
    return RunAuraTune(hInst);
}
