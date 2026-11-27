#include <windows.h>
#include <objbase.h>
#include <cstdio>
int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CLSID cl{}; CLSIDFromString(L"{870AF99C-171D-4f9e-AF0D-E63DF40C2BC9}", &cl);
    IUnknown* p=nullptr;
    HRESULT hr = CoCreateInstance(cl, nullptr, CLSCTX_ALL, IID_IUnknown, (void**)&p);
    printf("create hr=0x%08lX p=%p\n",(unsigned long)hr,p);
    if(!p) return 1;
    struct T { const wchar_t* n; const wchar_t* s; } t[] = {
        {L"IPolicyConfig",      L"{f8679f50-850a-41cf-9c72-43081e508a0a}"},
        {L"IPolicyConfigVista", L"{568b9108-44bf-40b4-9006-86afe5b5a620}"},
        {L"IPolicyConfig10",    L"{f8679f50-850a-41cf-9c72-43081e508a0b}"},
    };
    for (auto& e : t) {
        IID iid{}; CLSIDFromString((LPOLESTR)e.s, &iid);
        void* q=nullptr;
        HRESULT r = p->QueryInterface(iid, &q);
        printf("QI %-20ls hr=0x%08lX q=%p\n", e.n, (unsigned long)r, q);
        if (q) ((IUnknown*)q)->Release();
    }
    p->Release();
    return 0;
}
