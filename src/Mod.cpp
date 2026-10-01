#include "Runtime.h"
#include <Windows.h>
#include <detours.h>
#include <cstring>
#include <atomic>
#include <cstddef>

namespace
{
    using SetVisibleFn = void(__fastcall*)(hh::gfx::GOCVisual*, bool);
    SetVisibleFn g_originalSetVisible = nullptr;
    volatile LONG g_hooksAttempted = 0;
    volatile LONG g_visualReady = 0;
    volatile LONG g_logicalSuper = 0;
    volatile LONG g_autoDoomWingsSuper = 0;
    hh::game::GameObject* g_visualOwner = nullptr;
    hh::game::GameObject* g_createAttemptPlayer = nullptr;
    hh::gfx::GOCVisualModel* g_superVisual = nullptr;
    hh::gfx::GOCVisualModel* g_normalVisual = nullptr;
    hh::fnd::Handle<hh::gfx::GOCVisualModel> g_superHandle;
    hh::fnd::Handle<hh::gfx::GOCVisualModel> g_normalHandle;
    volatile LONG g_nativeBodyRequestedVisible = -1;
    volatile LONG g_superEnabled = 0;
    bool g_morphActive = false;
    bool g_doomSurfActive = false;
    bool g_superWasActiveAtDoomSurfEntry = false;
    bool g_previousDoomWingsActive = false;
    bool g_doomWingsStateInitialized = false;
    ULONGLONG g_wingOffPendingSince = 0;
    bool g_wingOffWasDeferred = false;
    constexpr ULONGLONG kWingOffConfirmMs = 500;
    struct WingRecord
    {
        hh::gfx::GOCVisualModel* visual = nullptr;
        bool nativeVisible = false;
        bool suppressed = false;
        bool scaleSaved = false;
        csl::math::Vector3 originalScale{};
    };
    WingRecord g_wings[32]{};
    std::size_t g_wingCount = 0;
    thread_local bool g_internalWingVisibilityChange = false;
    using CreateEffectExFn = void(__fastcall*)(hh::eff::GOCEffect*, const hh::eff::EffectCreateInfo*, hh::eff::EffectHandle*);
    CreateEffectExFn g_originalCreateEffectEx = nullptr;
    volatile LONG g_effectHookAttempted = 0;
    volatile LONG g_r4BoostResourceCheckDone = 0;
    volatile LONG g_r4BoostResourceReady = 0;
    volatile LONG g_r5SpinRootCheckDone = 0;
    volatile LONG g_r5SpinRootReady = 0;
    volatile LONG g_beta07StompResourcesChecked = 0;
    volatile LONG g_beta07StompResourcesReady = 0;
    constexpr const char* kBeta07StompLoopRoot = "ef_rss_super_stomp_loop01";
    constexpr const char* kBeta07StompEndRoot = "ef_rss_super_stomp_end01";
    constexpr const char* kBoostSuperChildren[] = {
        "ec_ss_boost01_mesh01", "ec_ss_boost01_mesh04",
        "ec_ss_boost01_mesh02", "ec_ss_boost01_mesh04_add",
        "ec_ss_boost01_mesh03_add", "ec_ss_boost01_hibana01",
        "ec_ss_boost01_light01", "ec_ss_boost01_pointlight01",
        "ec_ss_boost01_round_blue01", "ec_ss_boost01_lightning_line01",
        "ec_ss_boost01_blue_line01", "ec_ss_boost01_line_random",
    };
    constexpr const char* kR5SpinBallChildren[] = {
        "ec_rs_spinatk01_mesh_wind01", "ec_rs_spinatk01_side01",
        "ec_rs_spinatk01_light01", "ec_rs_spinatk01_circle01",
        "ec_rs_spinatk01_mesh01", "ec_rs_spinatk01_pointlight01",
        "ec_sd_spinatk01_dist01",
    };
    std::atomic<hh::gfx::GOCVisual*> g_hiddenBody{ nullptr };
    thread_local bool g_internalBodyVisibilityChange = false;

    bool RecomputeLogicalSuper();
    void SetDoomWingsAutoSuper(bool enabled);
    void ApplyR2BodyVisibility();
    void UpdateR2State();
    void ResetR2PlayerOwnedState();
    bool IsWingOffConfirmationDeferred();
    void MaintainP2RFVisibility();
    bool CreateP2RFSuperVisual(hh::game::GameObject* player);
    bool SetVisualVisibilityInternally(hh::gfx::GOCVisual* visual, bool visible)
    {
        if (!visual || visual->IsVisible() == visible)
            return false;
        const bool previousGuard = g_internalBodyVisibilityChange;
        g_internalBodyVisibilityChange = true;
        visual->SetVisible(visible);
        g_internalBodyVisibilityChange = previousGuard;
        return true;
    }

    bool NormalVisualHandleMatches()
    {
        return g_normalVisual && *g_normalHandle == g_normalVisual;
    }

    bool SuperVisualHandleMatches()
    {
        return g_superVisual && *g_superHandle == g_superVisual;
    }

    bool R2PlayerVisualsReady()
    {
        return InterlockedCompareExchange(&g_visualReady, 0, 0) != 0 &&
            g_visualOwner && g_visualOwner == rss::g_playerObject &&
            NormalVisualHandleMatches() && SuperVisualHandleMatches();
    }

    void EnsureSuperBodyMirror(bool visible)
    {
        if (!SuperVisualHandleMatches())
            return;
        SetVisualVisibilityInternally(g_superVisual, visible);
    }

    int FindWingRecord(hh::gfx::GOCVisual* visual)
    {
        for (std::size_t i = 0; i < g_wingCount; ++i)
            if (g_wings[i].visual == visual)
                return static_cast<int>(i);
        return -1;
    }

    void SetWingVisibleInternally(hh::gfx::GOCVisual* visual, bool visible)
    {
        if (!visual || visual->IsVisible() == visible || !g_originalSetVisible)
            return;
        const bool previous = g_internalWingVisibilityChange;
        g_internalWingVisibilityChange = true;
        g_originalSetVisible(visual, visible);
        g_internalWingVisibilityChange = previous;
    }

    void ApplyR2BodyVisibility()
    {
        if (InterlockedCompareExchange(&g_visualReady, 0, 0) == 0 ||
            !NormalVisualHandleMatches() || !SuperVisualHandleMatches())
            return;

        const bool logicalSuper = InterlockedCompareExchange(&g_logicalSuper, 0, 0) != 0;
        const bool nativeVisible = InterlockedCompareExchange(&g_nativeBodyRequestedVisible, 0, 0) != 0;
        const bool surfSuperChoice = g_doomSurfActive &&
            g_superWasActiveAtDoomSurfEntry && rss::g_superOnDoomSurfEnabled;
        const bool selectedSuper = g_doomSurfActive ? surfSuperChoice : logicalSuper;
        const bool normalVisible = !g_morphActive && nativeVisible &&
            !selectedSuper;
        const bool superVisible = !g_morphActive && nativeVisible &&
            selectedSuper;

        g_hiddenBody.store((g_morphActive || selectedSuper)
            ? static_cast<hh::gfx::GOCVisual*>(g_normalVisual) : nullptr);
        SetVisualVisibilityInternally(g_normalVisual, normalVisible);
        EnsureSuperBodyMirror(superVisible);

    }

    void OnNativeNormalBodyVisibility(bool requestedVisible)
    {
        const LONG requested = requestedVisible ? 1 : 0;
        const LONG previous = InterlockedExchange(&g_nativeBodyRequestedVisible, requested);
        if (previous == requested)
            return;
        if (InterlockedCompareExchange(&g_visualReady, 0, 0) != 0 &&
            (!NormalVisualHandleMatches() || !SuperVisualHandleMatches()))
        {
            InterlockedExchange(&g_superEnabled, 0);
            g_hiddenBody.store(nullptr);
            return;
        }
        ApplyR2BodyVisibility();
    }

    void __fastcall BodyVisibilityGuard(hh::gfx::GOCVisual* visual, bool visible)
    {
        if (g_internalBodyVisibilityChange || g_internalWingVisibilityChange)
        {
            if (g_originalSetVisible)
                g_originalSetVisible(visual, visible);
            return;
        }

        const int wingIndex = R2PlayerVisualsReady() && !g_internalWingVisibilityChange
            ? FindWingRecord(visual) : -1;
        if (wingIndex >= 0)
        {
            const bool previousVisible = g_wings[wingIndex].nativeVisible;
            g_wings[wingIndex].nativeVisible = visible;
            const bool autoBefore = InterlockedCompareExchange(&g_autoDoomWingsSuper, 0, 0) != 0;
            if (previousVisible != visible && visible)
            {
                g_wingOffPendingSince = 0;
                g_wingOffWasDeferred = false;
                if (!autoBefore)
                    SetDoomWingsAutoSuper(true);
            }
            else if (previousVisible != visible)
            {
                if (!g_wingOffPendingSince)
                    g_wingOffPendingSince = GetTickCount64();
            }
            else if (visible && !autoBefore)
            {
                SetDoomWingsAutoSuper(true);
            }
            if (InterlockedCompareExchange(&g_logicalSuper, 0, 0) != 0)
                visible = false;
        }

        const bool playerVisualsReady = R2PlayerVisualsReady();
        if (playerVisualsReady && visual && visual == g_normalVisual)
            OnNativeNormalBodyVisibility(visible);

        if (playerVisualsReady && visible && visual == g_hiddenBody.load())
            visible = false;

        if (g_originalSetVisible)
            g_originalSetVisible(visual, visible);
    }
    bool InstallBodyHook()
    {
        if (g_originalSetVisible) return true;
        constexpr unsigned char signature[] = {
            0x44,0x0F,0xB6,0x81,0x80,0x00,0x00,0x00,
            0x41,0x0F,0xB6,0xC0,0x41,0x80,0xC8,0x01};
        auto* target = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)) + 0x9EC9D0;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(target, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
            !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
            (mbi.Protect & PAGE_GUARD) || std::memcmp(target, signature, sizeof(signature)))
            return false;
        if (DetourTransactionBegin() != NO_ERROR) return false;
        g_originalSetVisible = reinterpret_cast<SetVisibleFn>(target);
        LONG result = DetourUpdateThread(GetCurrentThread());
        if (result == NO_ERROR)
            result = DetourAttach(reinterpret_cast<PVOID*>(&g_originalSetVisible), BodyVisibilityGuard);
        if (result != NO_ERROR) DetourTransactionAbort();
        else result = DetourTransactionCommit();
        if (result != NO_ERROR) g_originalSetVisible = nullptr;
        return result == NO_ERROR;
    }

    bool HasPlayerStatePrefix(const char* prefix)
    {
        if (!rss::g_playerObject || !prefix)
            return false;
        auto* hsm = rss::g_playerObject->GetComponent<app::player::GOCPlayerHsm>();
        if (!hsm)
            return false;
        const int depth = hsm->hsm.currentDepth;
        if (depth < 0 || depth > 64)
            return false;
        for (int i = 0; i <= depth; ++i)
        {
            auto* state = hsm->hsm.GetCurrentState(i);
            if (state && state->name &&
                std::strncmp(state->name, prefix, std::strlen(prefix)) == 0)
                return true;
        }
        return false;
    }

    bool IsMorphStateActive()
    {
        return HasPlayerStatePrefix("StateDAmoeba");
    }

    bool IsDoomSurfStateActive()
    {
        // All known ride, finish, vertical-jump, and air states share this prefix.
        return HasPlayerStatePrefix("StateDSurf");
    }

    bool IsWingOffConfirmationDeferred()
    {
        if (g_doomSurfActive || g_morphActive ||
            InterlockedCompareExchange(&g_nativeBodyRequestedVisible, 0, 0) == 0 ||
            HasPlayerStatePrefix("StateDSurf") ||
            HasPlayerStatePrefix("StateDAmoeba") ||
            HasPlayerStatePrefix("StateDivingSpecialDashRing") ||
            HasPlayerStatePrefix("StateDoubleJump") ||
            HasPlayerStatePrefix("StateSpinAttack") ||
            HasPlayerStatePrefix("StateHoming") ||
            HasPlayerStatePrefix("StateDWingStart") ||
            HasPlayerStatePrefix("StateDWingGuard") ||
            HasPlayerStatePrefix("StateDWingFlinch"))
            return true;

        auto* hsm = rss::g_playerObject
            ? rss::g_playerObject->GetComponent<app::player::GOCPlayerHsm>() : nullptr;
        auto* context = hsm
            ? static_cast<app::player::PlayerHsmContext*>(hsm->hsmContext) : nullptr;
        auto* status = context ? context->blackboardStatus : nullptr;
        return status && status->GetWorldFlag(app::player::BlackboardStatus::WorldFlag::OUT_OF_CONTROL);
    }

    int FindWingModel(hh::gfx::GOCVisualModel* visual)
    {
        for (std::size_t i = 0; i < g_wingCount; ++i)
            if (g_wings[i].visual == visual)
                return static_cast<int>(i);
        return -1;
    }

    bool AreNativeDoomWingsRequestedVisible()
    {
        for (std::size_t i = 0; i < g_wingCount; ++i)
            if (g_wings[i].nativeVisible)
                return true;
        return false;
    }

    bool IsDoomWingModel(hh::gfx::GOCVisualModel* visual)
    {
        if (!visual || !visual->model)
            return false;
        const char* name = visual->model->GetName();
        return name && std::strncmp(name, "chr_shadow_dwing", 16) == 0;
    }

    void RefreshDoomWingVisuals()
    {
        if (!R2PlayerVisualsReady())
        {
            ResetR2PlayerOwnedState();
            return;
        }
        auto* playerVisualComponent = rss::g_playerObject->GetComponent<app::player::GOCPlayerVisual>();
        auto* playerVisual = playerVisualComponent
            ? playerVisualComponent->GetCurrentPlayerVisual() : nullptr;
        if (!playerVisual || !playerVisual->componentCollection)
        {
            g_wingCount = 0;
            std::memset(g_wings, 0, sizeof(g_wings));
            return;
        }

        WingRecord discovered[32]{};
        std::size_t discoveredCount = 0;
        auto& components = playerVisual->componentCollection->components;
        for (std::size_t i = 0; i < components.size() && discoveredCount < 32; ++i)
        {
            auto* visual = static_cast<hh::gfx::GOCVisualModel*>(components[i].visual);
            if (!IsDoomWingModel(visual))
                continue;
            const int previousIndex = FindWingModel(visual);
            if (previousIndex >= 0)
            {
                discovered[discoveredCount++] = g_wings[previousIndex];
            }
            else
            {
                auto& record = discovered[discoveredCount++];
                record.visual = visual;
                record.nativeVisible = visual->IsVisible();
                record.originalScale = visual->localTransform.scale;
                record.scaleSaved = true;
            }
        }
        for (std::size_t i = 0; i < 32; ++i)
            g_wings[i] = discovered[i];
        g_wingCount = discoveredCount;
    }

    void MaintainDoomWingVisuals()
    {
        if (!R2PlayerVisualsReady())
            return;
        const bool suppress = InterlockedCompareExchange(&g_logicalSuper, 0, 0) != 0;
        constexpr float epsilonScaleValue = 0.0001f;
        const csl::math::Vector3 epsilonScale{
            epsilonScaleValue, epsilonScaleValue, epsilonScaleValue };
        for (std::size_t i = 0; i < g_wingCount; ++i)
        {
            auto& record = g_wings[i];
            auto* visual = record.visual;
            if (!visual)
                continue;
            if (suppress)
            {
                if (!record.scaleSaved)
                {
                    record.originalScale = visual->localTransform.scale;
                    record.scaleSaved = true;
                }
                if (!record.suppressed)
                    visual->SetLocalScale(epsilonScale);
                record.suppressed = true;
                SetWingVisibleInternally(visual, false);
            }
            else
            {
                if (record.suppressed && record.scaleSaved)
                    visual->SetLocalScale(record.originalScale);
                record.suppressed = false;
                record.scaleSaved = false;
                SetWingVisibleInternally(visual, record.nativeVisible);
            }
        }
    }

    bool TryGetEffectResourceName(const hh::eff::EffectCreateInfo* createInfo,
        const char** outName)
    {
        if (outName) *outName = nullptr;
        if (!createInfo || !outName)
            return false;
        __try
        {
            const char* name = createInfo->resource;
            if (!name)
                return false;
            for (std::size_t i = 0; i < 128; ++i)
            {
                if (name[i] == '\0')
                {
                    *outName = name;
                    return i > 0;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }

    void* GetResEffectInternal(hh::eff::ResEffect* resource)
    {
        if (!resource)
            return nullptr;
        // Existing guarded SXSG ResEffect::GetInternal accessor.
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

    bool IsValidV12EffectResource(hh::fnd::ResourceManager* manager,
        const char* name)
    {
        auto* resource = manager
            ? manager->GetResource<hh::eff::ResEffect>(name) : nullptr;
        auto* internal = GetResEffectInternal(resource);
        const auto* data = resource
            ? static_cast<const unsigned char*>(resource->GetData()) : nullptr;
        const auto size = resource ? resource->GetSize() : 0;
        return internal && data && size >= 8 &&
            std::memcmp(data, "CMT", 3) == 0 && data[6] == 0x12;
    }

    bool CheckFinalBoostRoot()
    {
        const LONG checked = InterlockedCompareExchange(&g_r4BoostResourceCheckDone, 0, 0);
        if (checked)
            return InterlockedCompareExchange(&g_r4BoostResourceReady, 0, 0) != 0;

        auto* manager = hh::fnd::ResourceManager::GetInstance();
        bool valid = IsValidV12EffectResource(manager, "ef_rss_super_boost01");
        for (const auto* child : kBoostSuperChildren)
            if (valid)
                valid = IsValidV12EffectResource(manager, child);

        InterlockedExchange(&g_r4BoostResourceReady, valid ? 1 : 0);
        InterlockedExchange(&g_r4BoostResourceCheckDone, 1);
        return valid;
    }

    bool CheckR5SpinBallRoot()
    {
        if (InterlockedCompareExchange(&g_r5SpinRootCheckDone, 0, 0))
            return InterlockedCompareExchange(&g_r5SpinRootReady, 0, 0) != 0;

        auto* manager = hh::fnd::ResourceManager::GetInstance();
        bool valid = IsValidV12EffectResource(manager,
            "ef_rss_super_spinatk01_gold");
        for (const auto* child : kR5SpinBallChildren)
            if (valid)
                valid = IsValidV12EffectResource(manager, child);

        InterlockedExchange(&g_r5SpinRootReady, valid ? 1 : 0);
        InterlockedExchange(&g_r5SpinRootCheckDone, 1);
        return valid;
    }

    bool CheckBeta07StompResources()
    {
        if (InterlockedCompareExchange(&g_beta07StompResourcesChecked, 0, 0))
            return InterlockedCompareExchange(&g_beta07StompResourcesReady, 0, 0) != 0;

        auto* manager = hh::fnd::ResourceManager::GetInstance();
        bool valid = IsValidV12EffectResource(manager, kBeta07StompLoopRoot) &&
            IsValidV12EffectResource(manager, kBeta07StompEndRoot) &&
            IsValidV12EffectResource(manager, "ec_rss_beta07_stomp_loop_ring_gold") &&
            IsValidV12EffectResource(manager, "ec_rss_beta07_stomp_loop_core_gold") &&
            IsValidV12EffectResource(manager, "ec_rss_beta07_stomp_end_ring_gold") &&
            IsValidV12EffectResource(manager, "ec_rss_beta07_stomp_end_core_gold");
        InterlockedExchange(&g_beta07StompResourcesReady, valid ? 1 : 0);
        InterlockedExchange(&g_beta07StompResourcesChecked, 1);
        return valid;
    }

    template<typename T>
    void CreateEffectExWithGoldenRoot(hh::eff::GOCEffect* owner,
        const hh::eff::EffectCreateInfo* createInfo, hh::eff::EffectHandle* handle,
        const char* root)
    {
        T redirected = *reinterpret_cast<const T*>(createInfo);
        redirected.resource = root;
        g_originalCreateEffectEx(owner, &redirected, handle);
    }

    bool CreateEffectExWithGoldenRootByType(hh::eff::GOCEffect* owner,
        const hh::eff::EffectCreateInfo* createInfo, hh::eff::EffectHandle* handle,
        const char* root)
    {
        using namespace hh::eff;
        switch (createInfo->transType)
        {
        case EffectTransType::FRAME:
            CreateEffectExWithGoldenRoot<EffectTransFrameCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::NODE:
            CreateEffectExWithGoldenRoot<EffectTransNodeCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::NODE_AND_FRAME:
            CreateEffectExWithGoldenRoot<EffectTransNodeAndFrameCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::NODE_POSITION:
            CreateEffectExWithGoldenRoot<EffectTransNodePositionCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::MODEL:
            CreateEffectExWithGoldenRoot<EffectTransModelCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::MODEL_SPACE_NODE:
            CreateEffectExWithGoldenRoot<EffectTransModelSpaceNodeCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::WORLD_POSITION:
            CreateEffectExWithGoldenRoot<EffectTransWorldPositionCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::FRAME_OVERRIDE_ROTATION_SCALE:
            CreateEffectExWithGoldenRoot<EffectTransFrameOverrideRotationScaleCreateInfo>(owner, createInfo, handle, root); return true;
        case EffectTransType::FRAME_POSITION:
            CreateEffectExWithGoldenRoot<EffectTransFramePositionCreateInfo>(owner, createInfo, handle, root); return true;
        default:
            return false;
        }
    }

    bool IsSuppressedDoomWingEffect(const char* name)
    {
        return name && (!std::strcmp(name, "ef_sd_dwing_flytrail_water01") ||
            !std::strcmp(name, "ef_sd_dwing_flytrail_wind01") ||
            !std::strcmp(name, "ef_sd_dwing_flywind01"));
    }

    void __fastcall CreateEffectExDoomWingSuppression(hh::eff::GOCEffect* owner,
        const hh::eff::EffectCreateInfo* createInfo, hh::eff::EffectHandle* handle)
    {
        const char* name = nullptr;
        const bool ready = R2PlayerVisualsReady();
        const bool safeName = TryGetEffectResourceName(createInfo, &name);
        const bool logicalSuper =
            InterlockedCompareExchange(&g_logicalSuper, 0, 0) != 0;
        const bool currentPlayerEffect = ready && owner &&
            owner->owner == rss::g_playerObject &&
            rss::g_playerObject == g_visualOwner;

        if (safeName && logicalSuper && name &&
            std::strcmp(name, "ef_sd_boost01") == 0)
        {
            if (!currentPlayerEffect)
            {
                g_originalCreateEffectEx(owner, createInfo, handle);
                return;
            }

            if (CheckFinalBoostRoot() &&
                CreateEffectExWithGoldenRootByType(owner, createInfo, handle,
                    "ef_rss_super_boost01"))
                return;
        }

        if (safeName && logicalSuper && currentPlayerEffect && name &&
            std::strcmp(name, "ef_sd_spinatk01") == 0 &&
            CheckR5SpinBallRoot() &&
            CreateEffectExWithGoldenRootByType(owner, createInfo, handle,
                "ef_rss_super_spinatk01_gold"))
            return;

        if (safeName && logicalSuper && currentPlayerEffect && name &&
            (!std::strcmp(name, "ef_sd_stomp_loop01") ||
                !std::strcmp(name, "ef_sd_stomp_end01")) &&
            CheckBeta07StompResources())
        {
            if (std::strcmp(name, "ef_sd_stomp_loop01") == 0 &&
                CreateEffectExWithGoldenRootByType(owner, createInfo, handle,
                    kBeta07StompLoopRoot))
                return;
            if (std::strcmp(name, "ef_sd_stomp_end01") == 0 &&
                CreateEffectExWithGoldenRootByType(owner, createInfo, handle,
                    kBeta07StompEndRoot))
                return;
        }

        if (safeName && logicalSuper && currentPlayerEffect && name &&
            std::strcmp(name, "ef_sd_dwing_start02") == 0)
            return;

        const bool playerEffect = currentPlayerEffect && logicalSuper;
        if (!playerEffect || !IsSuppressedDoomWingEffect(name))
        {
            g_originalCreateEffectEx(owner, createInfo, handle);
            return;
        }

        hh::eff::EffectHandle temporaryHandle;
        auto* outputHandle = handle ? handle : &temporaryHandle;
        g_originalCreateEffectEx(owner, createInfo, outputHandle);
        if (outputHandle->IsAlive())
            outputHandle->Stop();

    }

    bool InstallDoomWingEffectHook()
    {
        if (g_originalCreateEffectEx)
            return true;
        if (InterlockedCompareExchange(&g_effectHookAttempted, 1, 0) != 0)
            return false;

        // Historical SXSG CreateEffectEx entry and byte signature, kept guarded.
        constexpr unsigned char signature[] = { 0xE9, 0xDB, 0x0C, 0xCF, 0x0F };
        auto* target = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)) + 0xAB3580;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(target, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
            !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
            (mbi.Protect & PAGE_GUARD) || std::memcmp(target, signature, sizeof(signature)))
        {
            return false;
        }
        if (DetourTransactionBegin() != NO_ERROR)
            return false;
        g_originalCreateEffectEx = reinterpret_cast<CreateEffectExFn>(target);
        LONG result = DetourUpdateThread(GetCurrentThread());
        if (result == NO_ERROR)
            result = DetourAttach(reinterpret_cast<PVOID*>(&g_originalCreateEffectEx),
                CreateEffectExDoomWingSuppression);
        if (result != NO_ERROR)
            DetourTransactionAbort();
        else
            result = DetourTransactionCommit();
        if (result != NO_ERROR)
            g_originalCreateEffectEx = nullptr;
        return result == NO_ERROR;
    }

    void ResetR2PlayerOwnedState()
    {
        g_wingCount = 0;
        std::memset(g_wings, 0, sizeof(g_wings));
        g_morphActive = false;
        g_doomSurfActive = false;
        g_superWasActiveAtDoomSurfEntry = false;
        g_wingOffPendingSince = 0;
        g_wingOffWasDeferred = false;
        g_previousDoomWingsActive = false;
        g_doomWingsStateInitialized = false;
    }

    void InvalidateR2PlayerOnTeardown()
    {
        if (g_visualOwner || g_createAttemptPlayer)
        {
            ResetR2PlayerOwnedState();
            InterlockedExchange(&g_visualReady, 0);
            InterlockedExchange(&g_superEnabled, 0);
            g_hiddenBody.store(nullptr);
            g_normalVisual = nullptr;
            g_superVisual = nullptr;
            g_normalHandle = static_cast<hh::gfx::GOCVisualModel*>(nullptr);
            g_superHandle = static_cast<hh::gfx::GOCVisualModel*>(nullptr);
            InterlockedExchange(&g_nativeBodyRequestedVisible, -1);
            g_visualOwner = nullptr;
            g_createAttemptPlayer = nullptr;
        }
        rss::g_playerObject = nullptr;
        rss::g_playerHandle = static_cast<hh::game::GameObject*>(nullptr);
    }

    bool RecomputeLogicalSuper()
    {
        const bool effective = InterlockedCompareExchange(&g_autoDoomWingsSuper, 0, 0) != 0;
        InterlockedExchange(&g_logicalSuper, effective ? 1 : 0);
        if (!R2PlayerVisualsReady())
        {
            InterlockedExchange(&g_superEnabled, 0);
            g_hiddenBody.store(nullptr);
            return !effective;
        }
        const bool normalValid = NormalVisualHandleMatches();
        const bool superValid = SuperVisualHandleMatches();
        if (!normalValid || !superValid)
        {
            InterlockedExchange(&g_superEnabled, 0);
            g_hiddenBody.store(nullptr);
            return false;
        }
        InterlockedExchange(&g_superEnabled, effective ? 1 : 0);
        if (!effective)
            g_hiddenBody.store(nullptr);
        ApplyR2BodyVisibility();
        return true;
    }

    void SetDoomWingsAutoSuper(bool enabled)
    {
        const LONG oldValue = InterlockedExchange(&g_autoDoomWingsSuper, enabled ? 1 : 0);
        if ((oldValue != 0) == enabled)
            return;
        (void)RecomputeLogicalSuper();
    }

    void UpdateR2State()
    {
        if (!R2PlayerVisualsReady())
        {
            ResetR2PlayerOwnedState();
            return;
        }
        RefreshDoomWingVisuals();
        const bool wingsActive = AreNativeDoomWingsRequestedVisible();

        const bool surfActive = IsDoomSurfStateActive();
        if (surfActive && !g_doomSurfActive)
        {
            g_superWasActiveAtDoomSurfEntry =
                InterlockedCompareExchange(&g_logicalSuper, 0, 0) != 0;
            g_doomSurfActive = true;
            ApplyR2BodyVisibility();
        }
        else if (!surfActive && g_doomSurfActive)
        {
            g_doomSurfActive = false;
            g_superWasActiveAtDoomSurfEntry = false;
            g_previousDoomWingsActive = wingsActive;
            g_doomWingsStateInitialized = true;
            ApplyR2BodyVisibility();
        }

        const bool morphActive = IsMorphStateActive();
        if (morphActive != g_morphActive)
        {
            g_morphActive = morphActive;
            ApplyR2BodyVisibility();
        }

        if (!g_doomWingsStateInitialized)
        {
            g_previousDoomWingsActive = wingsActive;
            g_doomWingsStateInitialized = true;
        }
        else
        {
            g_previousDoomWingsActive = wingsActive;
        }

        if (wingsActive)
        {
            g_wingOffPendingSince = 0;
            g_wingOffWasDeferred = false;
            if (InterlockedCompareExchange(&g_autoDoomWingsSuper, 0, 0) == 0)
            {
                SetDoomWingsAutoSuper(true);
            }
        }
        else if (InterlockedCompareExchange(&g_autoDoomWingsSuper, 0, 0) != 0)
        {
            const ULONGLONG now = GetTickCount64();
            if (!g_wingOffPendingSince)
            {
                g_wingOffPendingSince = now;
            }

            if (IsWingOffConfirmationDeferred())
            {
                g_wingOffPendingSince = now;
                if (!g_wingOffWasDeferred)
                g_wingOffWasDeferred = true;
            }
            else
            {
                if (g_wingOffWasDeferred)
                    g_wingOffPendingSince = now;
                g_wingOffWasDeferred = false;
                if (now - g_wingOffPendingSince >= kWingOffConfirmMs)
                {
                    g_wingOffPendingSince = 0;
                    SetDoomWingsAutoSuper(false);
                }
            }
        }
        else
        {
            g_wingOffPendingSince = 0;
            g_wingOffWasDeferred = false;
        }

        MaintainDoomWingVisuals();
        ApplyR2BodyVisibility();
    }

    void MaintainP2RFVisibility()
    {
        if (InterlockedCompareExchange(&g_visualReady, 0, 0) == 0)
            return;

        const bool normalValid = NormalVisualHandleMatches();
        const bool superValid = SuperVisualHandleMatches();
        if (!normalValid || !superValid)
        {
            auto* owner = g_visualOwner;
            ResetR2PlayerOwnedState();
            InterlockedExchange(&g_visualReady, 0);
            InterlockedExchange(&g_superEnabled, 0);
            g_hiddenBody.store(nullptr);
            g_normalVisual = nullptr;
            g_superVisual = nullptr;
            g_normalHandle = static_cast<hh::gfx::GOCVisualModel*>(nullptr);
            g_superHandle = static_cast<hh::gfx::GOCVisualModel*>(nullptr);
            InterlockedExchange(&g_nativeBodyRequestedVisible, -1);
            g_createAttemptPlayer = nullptr;
            if (owner && owner == rss::g_playerObject)
            {
                g_createAttemptPlayer = owner;
                (void)CreateP2RFSuperVisual(owner);
            }
            return;
        }

        if (InterlockedCompareExchange(&g_superEnabled, 0, 0) == 0)
            return;

        if (!g_normalVisual->model || !g_superVisual->model ||
            std::strcmp(g_normalVisual->model->GetName(), "chr_shadow") != 0 ||
            std::strcmp(g_superVisual->model->GetName(), "chr_supershadow") != 0)
        {
            InterlockedExchange(&g_superEnabled, 0);
            g_hiddenBody.store(nullptr);
            return;
        }

        ApplyR2BodyVisibility();

        // The live player attachment is Normal.frame2. Preserve the established
        // master-pose relationship and repair only a detached/mismatched frame.
        auto* normalLiveFrame = static_cast<hh::fnd::HFrame*>(g_normalVisual->frame2);
        auto* superLiveFrame = static_cast<hh::fnd::HFrame*>(g_superVisual->frame2);
        if (normalLiveFrame && superLiveFrame != normalLiveFrame)
        {
            g_superVisual->SetFrame(normalLiveFrame);
            g_superVisual->SetLocalTransform(g_normalVisual->localTransform);
        }
        g_superVisual->SetWorldMatrix(g_normalVisual->worldMatrix);
    }
    bool CreateP2RFSuperVisual(hh::game::GameObject* player)
    {
        if (!player)
        {
            return false;
        }

        auto* playerVisualComponent = player->GetComponent<app::player::GOCPlayerVisual>();
        if (!playerVisualComponent)
        {
            return false;
        }
        auto* playerVisual = playerVisualComponent->GetCurrentPlayerVisual();
        if (!playerVisual || !playerVisual->componentCollection)
        {
            return false;
        }

        hh::gfx::GOCVisualModel* normalVisual = nullptr;
        for (std::size_t i = 0; i < playerVisual->componentCollection->components.size(); ++i)
        {
            auto* visual = static_cast<hh::gfx::GOCVisualModel*>(
                playerVisual->componentCollection->components[i].visual);
            if (visual && visual->model &&
                std::strcmp(visual->model->GetName(), "chr_shadow") == 0)
            {
                normalVisual = visual;
                break;
            }
        }
        if (!normalVisual)
        {
            return false;
        }

        g_normalVisual = normalVisual;
        g_normalHandle = g_normalVisual;
        InterlockedExchange(&g_nativeBodyRequestedVisible, normalVisual->IsVisible() ? 1 : 0);
        auto* superVisual = player->CreateComponent<hh::gfx::GOCVisualModel>();
        if (!superVisual)
        {
            return false;
        }
        auto* resourceManager = hh::fnd::ResourceManager::GetInstance();
        auto* model = resourceManager
            ? resourceManager->GetResource<hh::gfx::ResModel>("chr_supershadow")
            : nullptr;
        if (!model)
        {
            return false;
        }

        auto description = normalVisual->description;
        description.model = model;
        description.frame = normalVisual->frame1;
        description.masterPoseComponentName = normalVisual->nameHash;
        superVisual->Setup(description);

        superVisual->SetNameHash("rss_supershadow");

        player->AddComponent(superVisual);

        auto* normalLiveFrame = static_cast<hh::fnd::HFrame*>(normalVisual->frame2);
        auto* superLiveFrame = static_cast<hh::fnd::HFrame*>(superVisual->frame2);
        if (normalLiveFrame && superLiveFrame != normalLiveFrame)
            superVisual->SetFrame(normalLiveFrame);
        superVisual->SetLocalTransform(normalVisual->localTransform);
        if (normalVisual->pImplementation)
            superVisual->SetRootNode(normalVisual->pImplementation->rootNode);
        if (superVisual->masterPoseComponent != normalVisual)
        {
            SetVisualVisibilityInternally(superVisual, false);
            return false;
        }

        g_superVisual = superVisual;
        g_superHandle = g_superVisual;
        SetVisualVisibilityInternally(superVisual, false);
        g_visualOwner = player;
        InterlockedExchange(&g_visualReady, 1);
        const bool logicalSuper = InterlockedCompareExchange(&g_logicalSuper, 0, 0) != 0;
        if (logicalSuper && InterlockedCompareExchange(&g_nativeBodyRequestedVisible, 0, 0) != 0)
            (void)RecomputeLogicalSuper();
        return true;
    }
}
namespace rss
{
    void UpdateP2RFPlayerLifecycle()
    {
        auto* gameManager = hh::game::GameManager::GetInstance();
        auto* levelInfo = gameManager
            ? hh::game::GameManager::GetMainGameManagerService<app::level::LevelInfo>()
            : nullptr;
        if (!levelInfo)
        {
            InvalidateR2PlayerOnTeardown();
            return;
        }

        auto playerHandle = levelInfo->GetPlayerObject(0);
        if (playerHandle == nullptr)
        {
            InvalidateR2PlayerOnTeardown();
            return;
        }
        const auto& gameObjectHandle = reinterpret_cast<
            const hh::fnd::Handle<hh::game::GameObject>&>(playerHandle);
        auto* player = hh::game::GameObjectSystem::GetGameObjectByHandle(gameObjectHandle);
        if (!player)
        {
            InvalidateR2PlayerOnTeardown();
            return;
        }

        g_playerObject = player;
        g_playerHandle = player;
        if (g_visualOwner && player != g_visualOwner)
        {
            InterlockedExchange(&g_visualReady, 0);
            InterlockedExchange(&g_superEnabled, 0);
            g_hiddenBody.store(nullptr);
            g_normalVisual = nullptr;
            g_superVisual = nullptr;
            g_normalHandle = static_cast<hh::gfx::GOCVisualModel*>(nullptr);
            g_superHandle = static_cast<hh::gfx::GOCVisualModel*>(nullptr);
            InterlockedExchange(&g_nativeBodyRequestedVisible, -1);
            g_visualOwner = nullptr;
            g_createAttemptPlayer = nullptr;
            ResetR2PlayerOwnedState();
            // g_logicalSuper intentionally persists across the player lifecycle.
        }

        if (InterlockedCompareExchange(&g_visualReady, 0, 0) != 0 ||
            g_createAttemptPlayer == player)
            return;
        g_createAttemptPlayer = player; // At most one creation attempt per distinct player.
        (void)CreateP2RFSuperVisual(player);
    }
}
#define EXPORT extern "C" __declspec(dllexport)
EXPORT void Init() {}
EXPORT void PostInit() {}
EXPORT void OnFrame()
{
    rss::InitializeBetaConfigOnce();
    if (InterlockedCompareExchange(&g_hooksAttempted, 1, 0) == 0)
    {
        (void)InstallBodyHook();
    }
    rss::UpdateP2RFPlayerLifecycle();
    (void)InstallDoomWingEffectHook();
    UpdateR2State();
    MaintainP2RFVisibility();
    rss::mainline_aura::Update(
        InterlockedCompareExchange(&g_logicalSuper, 0, 0) != 0,
        rss::g_playerObject, R2PlayerVisualsReady());
}
