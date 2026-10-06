#include <Windows.h>
#include <Unknwn.h>

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>

#include <fstream>
#include <mutex>

#include <MinHook.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")


// ============================================================
// Configuration
// ============================================================

constexpr DWORD kTargetSampleRate = 48000;
constexpr WORD  kTargetChannels = 2;
constexpr WORD  kTargetBits = 32;


// ============================================================
// Real dinput8.dll forwarding
// ============================================================

using DirectInput8CreateFn = HRESULT(WINAPI*)(
    HINSTANCE,
    DWORD,
    REFIID,
    LPVOID*,
    LPUNKNOWN
    );

static HMODULE g_realDinput8 = nullptr;
static DirectInput8CreateFn g_realDirectInput8Create = nullptr;


// ============================================================
// Logging
// ============================================================

static std::ofstream g_log;
static std::mutex g_logMutex;


static void Log(const char* message)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    if (!g_log.is_open())
    {
        g_log.open(
            "OkamiHDAudioFix.log",
            std::ios::out | std::ios::app
        );
    }

    if (!g_log.is_open())
        return;

    SYSTEMTIME st{};
    GetLocalTime(&st);

    g_log
        << "["
        << st.wYear
        << "-";

    if (st.wMonth < 10)
        g_log << "0";

    g_log
        << st.wMonth
        << "-";

    if (st.wDay < 10)
        g_log << "0";

    g_log
        << st.wDay
        << " ";

    if (st.wHour < 10)
        g_log << "0";

    g_log
        << st.wHour
        << ":";

    if (st.wMinute < 10)
        g_log << "0";

    g_log
        << st.wMinute
        << ":";

    if (st.wSecond < 10)
        g_log << "0";

    g_log
        << st.wSecond
        << "] "
        << message
        << "\n";

    g_log.flush();
}


// ============================================================
// WASAPI function types
// ============================================================

using AudioClientInitializeFn =
HRESULT(STDMETHODCALLTYPE*)(
    IAudioClient* self,
    AUDCLNT_SHAREMODE ShareMode,
    DWORD StreamFlags,
    REFERENCE_TIME hnsBufferDuration,
    REFERENCE_TIME hnsPeriodicity,
    const WAVEFORMATEX* pFormat,
    LPCGUID AudioSessionGuid
    );

using AudioClientGetMixFormatFn =
HRESULT(STDMETHODCALLTYPE*)(
    IAudioClient* self,
    WAVEFORMATEX** ppDeviceFormat
    );


// ============================================================
// Original WASAPI methods
// ============================================================

static AudioClientInitializeFn
g_originalInitialize = nullptr;

static AudioClientGetMixFormatFn
g_originalGetMixFormat = nullptr;


// ============================================================
// Format correction
// ============================================================

static bool NeedsFormatFix(
    const WAVEFORMATEX* format
)
{
    if (!format)
        return false;

    return
        format->nChannels != kTargetChannels ||
        format->nSamplesPerSec != kTargetSampleRate ||
        format->wBitsPerSample != kTargetBits;
}


static void FixWaveFormat(
    WAVEFORMATEX* format
)
{
    if (!format)
        return;

    if (!NeedsFormatFix(format))
        return;

    format->nChannels =
        kTargetChannels;

    format->nSamplesPerSec =
        kTargetSampleRate;

    format->wBitsPerSample =
        kTargetBits;

    format->nBlockAlign =
        static_cast<WORD>(
            kTargetChannels *
            (kTargetBits / 8)
            );

    format->nAvgBytesPerSec =
        kTargetSampleRate *
        format->nBlockAlign;


    if (
        format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= 22
        )
    {
        auto* ext =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
                format
                );

        ext->Samples.wValidBitsPerSample =
            kTargetBits;

        ext->dwChannelMask =
            SPEAKER_FRONT_LEFT |
            SPEAKER_FRONT_RIGHT;

        ext->SubFormat =
            KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    Log(
        "Adjusted game audio format to stereo 48 kHz float."
    );
}


// ============================================================
// GetMixFormat hook
// ============================================================

static HRESULT STDMETHODCALLTYPE HookedGetMixFormat(
    IAudioClient* self,
    WAVEFORMATEX** ppDeviceFormat
)
{
    HRESULT result =
        g_originalGetMixFormat(
            self,
            ppDeviceFormat
        );

    if (
        SUCCEEDED(result) &&
        ppDeviceFormat &&
        *ppDeviceFormat
        )
    {
        FixWaveFormat(
            *ppDeviceFormat
        );
    }

    return result;
}


// ============================================================
// Initialize hook
// ============================================================

static HRESULT STDMETHODCALLTYPE HookedInitialize(
    IAudioClient* self,
    AUDCLNT_SHAREMODE ShareMode,
    DWORD StreamFlags,
    REFERENCE_TIME hnsBufferDuration,
    REFERENCE_TIME hnsPeriodicity,
    const WAVEFORMATEX* pFormat,
    LPCGUID AudioSessionGuid
)
{
    if (
        ShareMode ==
        AUDCLNT_SHAREMODE_SHARED
        )
    {
        StreamFlags |=
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;

        StreamFlags |=
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    }

    HRESULT result =
        g_originalInitialize(
            self,
            ShareMode,
            StreamFlags,
            hnsBufferDuration,
            hnsPeriodicity,
            pFormat,
            AudioSessionGuid
        );

    if (FAILED(result))
    {
        Log(
            "ERROR: WASAPI audio initialization failed."
        );
    }

    return result;
}


// ============================================================
// Install hooks
// ============================================================

static bool InstallWasapiHooks(
    IAudioClient* audioClient
)
{
    if (!audioClient)
    {
        Log(
            "ERROR: Invalid IAudioClient."
        );

        return false;
    }


    void** vtable =
        *reinterpret_cast<void***>(
            audioClient
            );


    if (!vtable)
    {
        Log(
            "ERROR: Invalid IAudioClient vtable."
        );

        return false;
    }


    void* initializeAddress =
        vtable[3];

    void* getMixFormatAddress =
        vtable[8];


    MH_STATUS status =
        MH_Initialize();


    if (
        status != MH_OK &&
        status != MH_ERROR_ALREADY_INITIALIZED
        )
    {
        Log(
            "ERROR: MinHook initialization failed."
        );

        return false;
    }


    status =
        MH_CreateHook(
            initializeAddress,
            reinterpret_cast<void*>(
                &HookedInitialize
                ),
            reinterpret_cast<void**>(
                &g_originalInitialize
                )
        );


    if (status != MH_OK)
    {
        Log(
            "ERROR: Failed to hook IAudioClient::Initialize."
        );

        return false;
    }


    status =
        MH_CreateHook(
            getMixFormatAddress,
            reinterpret_cast<void*>(
                &HookedGetMixFormat
                ),
            reinterpret_cast<void**>(
                &g_originalGetMixFormat
                )
        );


    if (status != MH_OK)
    {
        Log(
            "ERROR: Failed to hook IAudioClient::GetMixFormat."
        );

        return false;
    }


    status =
        MH_EnableHook(
            MH_ALL_HOOKS
        );


    if (status != MH_OK)
    {
        Log(
            "ERROR: Failed to enable WASAPI hooks."
        );

        return false;
    }


    Log(
        "WASAPI hooks installed successfully."
    );

    return true;
}


// ============================================================
// WASAPI bootstrap
// ============================================================

static DWORD WINAPI WasapiHookThread(
    LPVOID
)
{
    Log(
        "Audio fix initialization started."
    );


    HRESULT hr =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );


    bool shouldUninitialize =
        SUCCEEDED(hr);


    if (
        FAILED(hr) &&
        hr != RPC_E_CHANGED_MODE
        )
    {
        Log(
            "ERROR: COM initialization failed."
        );

        return 0;
    }


    IMMDeviceEnumerator* enumerator =
        nullptr;

    IMMDevice* device =
        nullptr;

    IAudioClient* audioClient =
        nullptr;


    hr =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(
                &enumerator
                )
        );


    if (FAILED(hr))
    {
        Log(
            "ERROR: Failed to create MMDeviceEnumerator."
        );

        goto Cleanup;
    }


    hr =
        enumerator->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &device
        );


    if (FAILED(hr))
    {
        Log(
            "ERROR: Failed to get default audio endpoint."
        );

        goto Cleanup;
    }


    hr =
        device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(
                &audioClient
                )
        );


    if (FAILED(hr))
    {
        Log(
            "ERROR: Failed to create temporary IAudioClient."
        );

        goto Cleanup;
    }


    if (!InstallWasapiHooks(audioClient))
    {
        Log(
            "ERROR: Failed to install WASAPI hooks."
        );
    }


Cleanup:

    if (audioClient)
    {
        audioClient->Release();
    }


    if (device)
    {
        device->Release();
    }


    if (enumerator)
    {
        enumerator->Release();
    }


    if (shouldUninitialize)
    {
        CoUninitialize();
    }


    return 0;
}


// ============================================================
// Load real Windows dinput8.dll
// ============================================================

static void LoadRealDinput8()
{
    if (g_realDinput8)
        return;


    wchar_t systemPath[MAX_PATH]{};


    if (
        GetSystemDirectoryW(
            systemPath,
            MAX_PATH
        ) == 0
        )
    {
        Log(
            "ERROR: Could not locate Windows system directory."
        );

        return;
    }


    wcscat_s(
        systemPath,
        L"\\dinput8.dll"
    );


    g_realDinput8 =
        LoadLibraryW(
            systemPath
        );


    if (!g_realDinput8)
    {
        Log(
            "ERROR: Failed to load system dinput8.dll."
        );

        return;
    }


    g_realDirectInput8Create =
        reinterpret_cast<DirectInput8CreateFn>(
            GetProcAddress(
                g_realDinput8,
                "DirectInput8Create"
            )
            );


    if (!g_realDirectInput8Create)
    {
        Log(
            "ERROR: Failed to resolve DirectInput8Create."
        );

        return;
    }


    Log(
        "System dinput8.dll loaded."
    );
}


// ============================================================
// Proxy export
// ============================================================

extern "C"
__declspec(dllexport)
HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD version,
    REFIID riidltf,
    LPVOID* out,
    LPUNKNOWN outer
)
{
    static LONG startupLogged = 0;


    if (
        InterlockedCompareExchange(
            &startupLogged,
            1,
            0
        ) == 0
        )
    {
        Log(
            "OkamiHDAudioFix loaded."
        );
    }


    LoadRealDinput8();


    if (!g_realDirectInput8Create)
    {
        return E_FAIL;
    }


    static LONG hookThreadStarted = 0;


    if (
        InterlockedCompareExchange(
            &hookThreadStarted,
            1,
            0
        ) == 0
        )
    {
        HANDLE thread =
            CreateThread(
                nullptr,
                0,
                WasapiHookThread,
                nullptr,
                0,
                nullptr
            );


        if (thread)
        {
            CloseHandle(thread);
        }
        else
        {
            Log(
                "ERROR: Failed to start audio fix thread."
            );
        }
    }


    return g_realDirectInput8Create(
        hinst,
        version,
        riidltf,
        out,
        outer
    );
}


// ============================================================
// DLL entry point
// ============================================================

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID
)
{
    if (
        reason ==
        DLL_PROCESS_ATTACH
        )
    {
        DisableThreadLibraryCalls(
            hModule
        );
    }

    return TRUE;
}