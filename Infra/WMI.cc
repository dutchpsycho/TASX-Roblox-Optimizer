#include "WMI.h"

#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <mutex>
#include <condition_variable>
#include <string>
#include <iostream>

#pragma comment(lib, "wbemuuid.lib")

static std::mutex g_mutex;
static std::condition_variable g_cv;

static std::string g_rbxSignal; // "RBX_ON" "RBX_OFF"

static bool g_shutdown = false;

static IWbemLocator* g_pLoc = nullptr;
static IWbemServices* g_pSvc = nullptr;

class CWMIEventSink : public IWbemObjectSink {
    LONG m_lRef;
    bool m_bDone;
public:
    CWMIEventSink() : m_lRef(0), m_bDone(false) {}
    virtual ~CWMIEventSink() {}

    virtual ULONG STDMETHODCALLTYPE AddRef() {
        return InterlockedIncrement(&m_lRef);
    }

    virtual ULONG STDMETHODCALLTYPE Release() {
        LONG lRef = InterlockedDecrement(&m_lRef);
        if (lRef == 0)
            delete this;
        return lRef;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IWbemObjectSink) {
            *ppv = (IWbemObjectSink*)this;
            AddRef();
            return WBEM_S_NO_ERROR;
        }
        return E_NOINTERFACE;
    }

    virtual HRESULT STDMETHODCALLTYPE Indicate(
        LONG lObjectCount,
        IWbemClassObject** apObjArray
    ) {
        for (LONG i = 0; i < lObjectCount; i++) {
            VARIANT vtClass;
            VariantInit(&vtClass);
            HRESULT hr = apObjArray[i]->Get(_bstr_t(L"__Class"), 0, &vtClass, 0, 0);
            if (SUCCEEDED(hr)) {
                std::wstring eventClass = vtClass.bstrVal;
                VariantClear(&vtClass);

                if (eventClass == L"__InstanceCreationEvent") {
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        g_rbxSignal = "RBX_ON";
                    }
                    g_cv.notify_all();
                }

                else if (eventClass == L"__InstanceDeletionEvent") {
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        g_rbxSignal = "RBX_OFF";
                    }

                    g_cv.notify_all();
                }
            }
        }
        return WBEM_S_NO_ERROR;
    }

    virtual HRESULT STDMETHODCALLTYPE SetStatus(
        LONG lFlags,
        HRESULT hResult,
        BSTR strParam,
        IWbemClassObject* pObjParam
    ) {
        if (lFlags == WBEM_STATUS_COMPLETE) {
            std::wcout << L"WMI async call complete. hResult = " << hResult << std::endl;
        }
        return WBEM_S_NO_ERROR;
    }
};

static CWMIEventSink* g_pSink = nullptr;

bool _wmimon() {
    HRESULT hr;

    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::wcerr << L"CoInitializeEx failed. hr = 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = CoInitializeSecurity(
        NULL,
        -1,
        NULL,
        NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE,
        NULL
    );
    if (FAILED(hr)) {
        std::wcerr << L"CoInitializeSecurity failed. hr = 0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return false;
    }

    hr = CoCreateInstance(
        CLSID_WbemLocator,
        0,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&g_pLoc
    );
    if (FAILED(hr)) {
        std::wcerr << L"CoCreateInstance failed. hr = 0x" << std::hex << hr << std::endl;
        CoUninitialize();
        return false;
    }

    hr = g_pLoc->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"),
        NULL,
        NULL,
        0,
        NULL,
        0,
        0,
        &g_pSvc
    );
    if (FAILED(hr)) {
        std::wcerr << L"ConnectServer failed. hr = 0x" << std::hex << hr << std::endl;
        g_pLoc->Release();
        CoUninitialize();
        return false;
    }

    hr = CoSetProxyBlanket(
        g_pSvc,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE
    );
    if (FAILED(hr)) {
        std::wcerr << L"CoSetProxyBlanket failed. hr = 0x" << std::hex << hr << std::endl;
        g_pSvc->Release();
        g_pLoc->Release();
        CoUninitialize();
        return false;
    }

    g_pSink = new CWMIEventSink;
    g_pSink->AddRef();

    hr = g_pSvc->ExecNotificationQueryAsync(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT * FROM __InstanceCreationEvent WITHIN 1 "
            L"WHERE TargetInstance ISA 'Win32_Process' "
            L"AND TargetInstance.Name = 'RobloxPlayerBeta.exe'"),
        WBEM_FLAG_SEND_STATUS,
        NULL,
        g_pSink
    );
    if (FAILED(hr)) {
        std::wcerr << L"ExecNotificationQueryAsync (creation) failed. hr = 0x" << std::hex << hr << std::endl;
        g_pSink->Release();
        g_pSvc->Release();
        g_pLoc->Release();
        CoUninitialize();
        return false;
    }

    hr = g_pSvc->ExecNotificationQueryAsync(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT * FROM __InstanceDeletionEvent WITHIN 1 "
            L"WHERE TargetInstance ISA 'Win32_Process' "
            L"AND TargetInstance.Name = 'RobloxPlayerBeta.exe'"),
        WBEM_FLAG_SEND_STATUS,
        NULL,
        g_pSink
    );
    if (FAILED(hr)) {
        std::wcerr << L"ExecNotificationQueryAsync (deletion) failed. hr = 0x" << std::hex << hr << std::endl;
        g_pSink->Release();
        g_pSvc->Release();
        g_pLoc->Release();
        CoUninitialize();
        return false;
    }

    return true;
}

std::string WaitForRBXEvent() {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait(lock, [] { return !g_rbxSignal.empty() || g_shutdown; });
    std::string signal = g_rbxSignal;
    g_rbxSignal.clear();
    return signal;
}

void _wmishutdown() {
    g_shutdown = true;
    g_cv.notify_all();

    if (g_pSvc && g_pSink) {
        g_pSvc->CancelAsyncCall(g_pSink);
    }

    if (g_pSink) {
        g_pSink->Release();
        g_pSink = nullptr;
    }

    if (g_pSvc) {
        g_pSvc->Release();
        g_pSvc = nullptr;
    }

    if (g_pLoc) {
        g_pLoc->Release();
        g_pLoc = nullptr;
    }

    CoUninitialize();
}