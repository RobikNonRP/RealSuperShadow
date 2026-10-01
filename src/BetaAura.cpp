#include "Runtime.h"

#include <array>
#include <cstring>

namespace
{
    constexpr const char* kAuraEffect = "ec_sd_dpower_aura_body01_lightning01";
    std::array<hh::eff::EffectHandle, 1> g_handles{};
    hh::game::GameObject* g_player = nullptr;
    bool g_active = false;
    bool g_attempted = false;
    bool g_resourceCheckDone = false;
    bool g_resourceValid = false;

    void* GetResEffectInternal(hh::eff::ResEffect* resource)
    {
        if (!resource)
            return nullptr;
        auto* module = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        auto* target = module ? module + 0xAAA530 : nullptr;
        MEMORY_BASIC_INFORMATION mbi{};
        constexpr unsigned char signature[] = { 0xE9, 0xCB, 0x24, 0xB7, 0x0F };
        if (!target || !VirtualQuery(target, &mbi, sizeof(mbi)) ||
            mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
            !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
            std::memcmp(target, signature, sizeof(signature)))
            return nullptr;

        using GetInternalFn = void*(__fastcall*)(hh::eff::ResEffect*);
        __try
        {
            return reinterpret_cast<GetInternalFn>(target)(resource);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ValidateResourceOnce()
    {
        if (g_resourceCheckDone)
            return g_resourceValid;
        g_resourceCheckDone = true;

        auto* manager = hh::fnd::ResourceManager::GetInstance();
        auto* resource = manager
            ? manager->GetResource<hh::eff::ResEffect>(kAuraEffect) : nullptr;
        const auto* data = resource
            ? static_cast<const unsigned char*>(resource->GetData()) : nullptr;
        g_resourceValid = resource && GetResEffectInternal(resource) && data &&
            resource->GetSize() >= 8 && std::memcmp(data, "CMT", 3) == 0 &&
            data[6] == 0x12;
        return g_resourceValid;
    }

    void StopAura()
    {
        auto& handle = g_handles[0];
        if (handle.IsAlive())
            handle.Stop();
        handle = hh::eff::EffectHandle{};
        g_active = false;
    }

    bool StartAura(hh::game::GameObject* player)
    {
        if (!player || !ValidateResourceOnce())
            return false;
        auto* owner = player->GetComponent<hh::eff::GOCEffect>();
        auto* frame = owner ? static_cast<hh::fnd::HFrame*>(owner->frame) : nullptr;
        if (!owner || !frame)
            return false;

        hh::eff::EffectTransFrameCreateInfo info{kAuraEffect};
        info.unk1a = true;
        info.transInfo.frame = frame;
        info.transInfo.scale = true;
        owner->CreateEffectEx(info, &g_handles[0]);
        return g_handles[0].IsAlive();
    }
}

namespace rss::mainline_aura
{
    void Update(bool logicalSuperActive, hh::game::GameObject* player,
        bool playerVisualsReady)
    {
        if (player != g_player)
        {
            StopAura();
            g_player = player;
            g_attempted = false;
        }

        if (!player || !playerVisualsReady || !logicalSuperActive)
        {
            if (g_active || g_handles[0].IsAlive())
                StopAura();
            g_attempted = false;
            return;
        }

        if (!g_attempted)
        {
            g_attempted = true;
            g_active = StartAura(player);
        }
    }

}
