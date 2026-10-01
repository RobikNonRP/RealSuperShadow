#include <Windows.h>
#include <detours.h>
#include "Runtime.h"
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <string>

static constexpr std::uintptr_t kStateSquatEnterRva = 0x006CC070;
static HMODULE g_moduleHandle = nullptr;
static volatile LONG g_configInitialized = 0;
using StateSquatEnterFn = void(__fastcall*)(app::player::PlayerStateBase*, app::player::PlayerHsmContext*, int);
static StateSquatEnterFn g_originalStateSquatEnter = nullptr;

static void __fastcall StateSquatEnterHook(app::player::PlayerStateBase* self, app::player::PlayerHsmContext* ctx, int prevState)
{
    if (!self || !ctx || !g_originalStateSquatEnter) return;
    g_originalStateSquatEnter(self, ctx, prevState);
    auto* player = ctx->playerObject;
    if (player)
    {
        rss::g_playerObject = player;
        rss::g_playerHandle = player;
    }
}

static bool IsExecutableAddress(void* p)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return mbi.State == MEM_COMMIT && (mbi.Protect & executable) != 0;
}

static bool InstallPlayerCaptureHook()
{
    const auto moduleBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!moduleBase) return false;
    auto* target = reinterpret_cast<void*>(moduleBase + kStateSquatEnterRva);
    if (!IsExecutableAddress(target)) return false;
    g_originalStateSquatEnter = reinterpret_cast<StateSquatEnterFn>(target);
    if (DetourTransactionBegin() != NO_ERROR) return false;
    DetourUpdateThread(GetCurrentThread());
    const LONG result = DetourAttach(reinterpret_cast<PVOID*>(&g_originalStateSquatEnter), StateSquatEnterHook);
    if (result != NO_ERROR)
    {
        DetourTransactionAbort();
        return false;
    }
    return DetourTransactionCommit() == NO_ERROR;
}

static void LoadBeta07Config(HMODULE module)
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(_countof(path)));
    if (!length || length >= _countof(path))
        return;
    std::wstring configPath(path, length);
    const auto slash = configPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return;
    configPath.resize(slash + 1);
    configPath += L"RealSuperShadow.ini";

    wchar_t setting[32] = L"true";
    GetPrivateProfileStringW(L"SuperVisuals", L"SuperOnDoomSurf", L"true",
        setting, static_cast<DWORD>(_countof(setting)), configPath.c_str());
    std::wstring normalized(setting);
    for (auto& ch : normalized)
        ch = static_cast<wchar_t>(towlower(ch));
    rss::g_superOnDoomSurfEnabled = normalized == L"true" || normalized == L"1" ||
        normalized == L"yes" || normalized == L"on";
}

namespace rss
{
    void InitializeBetaConfigOnce()
    {
        if (InterlockedCompareExchange(&g_configInitialized, 1, 0) == 0)
            LoadBeta07Config(g_moduleHandle);
    }
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hInstance);
        g_moduleHandle = hInstance;
        rss::g_hookInstalled = InstallPlayerCaptureHook();
    }
    return TRUE;
}
