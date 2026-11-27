#include <windows.h>
#include <objbase.h>
#include <cstdio>
int main() {
    static const CLSID A = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
    wchar_t buf[64]; StringFromGUID2(A, buf, 64);
    wprintf(L"literal  = %ls\n", buf);
    CLSID B{}; CLSIDFromString(L"{870AF99C-171D-4f9e-AF0D-E63DF40C2BC9}", &B);
    StringFromGUID2(B, buf, 64); wprintf(L"parsed   = %ls\n", buf);
    printf("equal=%d\n", memcmp(&A,&B,sizeof(GUID))==0);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUnknown* p=nullptr;
    HRESULT hr = CoCreateInstance(B, nullptr, CLSCTX_ALL, IID_IUnknown, (void**)&p);
    printf("create(parsed, IUnknown) hr=0x%08lX p=%p\n",(unsigned long)hr,p);
    return 0;
}
