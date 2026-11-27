// 进程回环激活探针：loop_probe <pid> [include|exclude]
#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <roapi.h>
#include <cstdio>
#include <cstdlib>

typedef enum _AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;
typedef enum _PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;
typedef struct _AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;
typedef struct _AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union { AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams; };
} AUDIOCLIENT_ACTIVATION_PARAMS;

class Handler : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HRESULT hr = E_FAIL;
    IUnknown* res = nullptr;
    IUnknown* ftm = nullptr;   // 自由线程封送器：让这个回调对象对其它套间可用
    ULONG ref = 1;
    Handler() { CoCreateFreeThreadedMarshaler(this, &ftm); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler))) {
            *ppv = this; AddRef(); return S_OK;
        }
        if (ftm && IsEqualIID(riid, __uuidof(IMarshal))) {
            *ppv = ftm; ftm->AddRef(); return S_OK;
        }
        if (ftm && IsEqualIID(riid, IID_IAgileObject)) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement((LONG*)&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement((LONG*)&ref);
        if (!r) { CloseHandle(ev); if (ftm) ftm->Release(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hrA = E_FAIL; IUnknown* unk = nullptr;
        if (op) op->GetActivateResult(&hrA, &unk);
        hr = hrA;
        if (SUCCEEDED(hrA)) res = unk; else if (unk) unk->Release();
        SetEvent(ev);
        return S_OK;
    }
};

static HRESULT Try(DWORD pid, PROCESS_LOOPBACK_MODE mode, const wchar_t* iidName, REFIID iid) {
    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode = mode;

    // 用 CoTaskMemAlloc 分配参数块（试验：某些系统要求堆内存）
    BYTE* mem = (BYTE*)CoTaskMemAlloc(sizeof(ap));
    memcpy(mem, &ap, sizeof(ap));

    PROPVARIANT pv; PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = mem;

    Handler* h = new Handler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(L"VAD\\Process_Loopback", iid, &pv, h, &op);
    printf("  call hr=0x%08lX", (unsigned long)hr);
    HRESULT act = E_FAIL;
    if (SUCCEEDED(hr)) {
        WaitForSingleObject(h->ev, 5000);
        act = h->hr;
        printf(" activate=0x%08lX got=%p", (unsigned long)act, h->res);
    }
    printf("  [%ls mode=%d]\n", iidName, (int)mode);
    if (h->res) h->res->Release();
    h->res = nullptr;
    if (op) op->Release();
    h->Release();
    pv.vt = VT_EMPTY;
    CoTaskMemFree(mem);
    return act;
}

static HRESULT TryDevice() {
    // 对照实验：用同一套异步激活机制激活「默认输出设备」（不传激活参数）
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&en))) return E_FAIL;
    IMMDevice* dev = nullptr;
    HRESULT hr = en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev);
    LPWSTR id = nullptr;
    if (SUCCEEDED(hr) && dev) hr = dev->GetId(&id);
    PROPVARIANT pv; PropVariantInit(&pv);
    Handler* h = new Handler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    if (SUCCEEDED(hr) && id)
        hr = ActivateAudioInterfaceAsync(id, __uuidof(IAudioClient), nullptr, h, &op);
    printf("  device call hr=0x%08lX", (unsigned long)hr);
    HRESULT act = E_FAIL;
    if (SUCCEEDED(hr)) {
        WaitForSingleObject(h->ev, 5000);
        act = h->hr;
        printf(" activate=0x%08lX got=%p", (unsigned long)act, h->res);
    }
    printf("\n");
    if (h->res) h->res->Release();
    h->res = nullptr;
    if (op) op->Release();
    h->Release();
    if (id) CoTaskMemFree(id);
    if (dev) dev->Release();
    if (en) en->Release();
    return act;
}

int main(int argc, char** argv) {
    const bool sta = argc > 3 && strcmp(argv[3], "sta") == 0;
    const bool ro = argc > 3 && strcmp(argv[3], "ro") == 0;
    if (ro) {
        HRESULT rhr = RoInitialize(RO_INIT_MULTITHREADED);
        printf("RoInitialize hr=0x%08lX\n", (unsigned long)rhr);
    } else {
        CoInitializeEx(nullptr, sta ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    }
    printf("apartment=%s\n", ro ? "RO-MTA" : (sta ? "STA" : "MTA"));
    const DWORD pid = argc > 1 ? (DWORD)atoi(argv[1]) : 0;
    const bool excl = argc > 2 && strcmp(argv[2], "exclude") == 0;
    printf("target pid=%lu mode=%s\n", (unsigned long)pid, excl ? "exclude" : "include");

    static const GUID IID_AC3 = {0x7ED4EE07,0x8E67,0x4CD4,{0x8C,0x1A,0x2B,0x7A,0x59,0x87,0xAD,0x42}};
    Try(pid, excl ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
                  : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE,
        L"IAudioClient", __uuidof(IAudioClient));
    Try(pid, PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE, L"IAudioClient3", IID_AC3);
    Try(pid, PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE, L"IAudioClient", __uuidof(IAudioClient));
    TryDevice();
    CoUninitialize();
    return 0;
}
