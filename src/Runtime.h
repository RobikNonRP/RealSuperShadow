#pragma once

#include <Windows.h>
#include <detours.h>
#include <miller-sdk.h>

namespace rss
{
    // Captured from the player state entry hook and refreshed from LevelInfo.
    inline hh::game::GameObject* g_playerObject = nullptr;
    inline hh::fnd::Handle<hh::game::GameObject> g_playerHandle;

    inline bool g_hookInstalled = false;
    inline bool g_lastSwapSucceeded = false;
    inline bool g_superOnDoomSurfEnabled = true;
}

namespace rss { void UpdateP2RFPlayerLifecycle(); }
namespace rss { void InitializeBetaConfigOnce(); }
namespace rss::mainline_aura
{
    void Update(bool logicalSuperActive, hh::game::GameObject* player,
        bool playerVisualsReady);
}
