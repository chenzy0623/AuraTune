// AuraTune 入口
#include <windows.h>
#include <mmsystem.h>

int RunAuraTune(HINSTANCE hInst);

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    return RunAuraTune(hInst);
}
