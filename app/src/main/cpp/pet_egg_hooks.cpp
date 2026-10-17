#include "pet_egg_hooks.hpp"
#include "include/log.h"

#include <atomic>
#include <cstdint>
#include <dobby.h>

namespace XposedPoco {
namespace {

using PetGameManagerOpenEggFn = void (*)(
    void* manager,
    void* eggItem,
    void* success,
    void* failure,
    void* methodInfo
);

using NetworkModuleOpenEggFn = void (*)(
    void* module,
    int32_t petId,
    int32_t eggSequence,
    void* success,
    void* failure,
    void* onGiftError,
    void* methodInfo
);

using DummyKenshoGameApiOpenEggFn = void (*)(
    void* module,
    int32_t petId,
    int32_t eggNumber,
    void* success,
    void* failure,
    void* onGiftError
);

constexpr uintptr_t kPetGameManagerOpenEggRva = 0x277A1A0;
constexpr uintptr_t kNetworkModuleOpenEggRva = 0x15C2F64;
constexpr uintptr_t kDummyKenshoGameApiOpenEggRva = 0x27ACC14;

PetGameManagerOpenEggFn g_originalPetGameManagerOpenEgg = nullptr;
NetworkModuleOpenEggFn g_originalNetworkModuleOpenEgg = nullptr;
DummyKenshoGameApiOpenEggFn g_originalDummyKenshoGameApiOpenEgg = nullptr;

std::atomic<bool> g_installAttempted{false};

void HookPetGameManagerOpenEgg(
    void* manager,
    void* eggItem,
    void* success,
    void* failure,
    void* methodInfo
) {
    LOGI(
        "PET_EGG_HOOK PetGameManager.OpenEgg this=%p egg=%p success=%p failure=%p methodInfo=%p",
        manager, eggItem, success, failure, methodInfo
    );

    auto original = g_originalPetGameManagerOpenEgg;
    if (original != nullptr) {
        original(manager, eggItem, success, failure, methodInfo);
    } else {
        LOGE("PET_EGG_HOOK PetGameManager.OpenEgg original is null; call skipped");
    }
}

void HookNetworkModuleOpenEgg(
    void* module,
    int32_t petId,
    int32_t eggSequence,
    void* success,
    void* failure,
    void* onGiftError,
    void* methodInfo
) {
    LOGI(
        "PET_EGG_HOOK NetworkModule.OpenEgg this=%p petId=%d eggSequence=%d "
        "success=%p failure=%p onGiftError=%p methodInfo=%p",
        module, petId, eggSequence, success, failure, onGiftError, methodInfo
    );

    auto original = g_originalNetworkModuleOpenEgg;
    if (original != nullptr) {
        original(module, petId, eggSequence, success, failure, onGiftError, methodInfo);
    } else {
        LOGE("PET_EGG_HOOK NetworkModule.OpenEgg original is null; call skipped");
    }
}

void HookDummyKenshoGameApiOpenEgg(
    void* module,
    int32_t petId,
    int32_t eggNumber,
    void* success,
    void* failure,
    void* onGiftError,
    void* methodInfo
) {
    LOGI(
        "PET_EGG_HOOK DummyKenshoGameAPI.OpenEgg this=%p petId=%d eggNumber=%d "
        "success=%p failure=%p onGiftError=%p methodInfo=%p",
        module, petId, eggNumber, success, failure, onGiftError, methodInfo
    );

    auto original = g_originalDummyKenshoGameApiOpenEgg;
    if (original != nullptr) {
        original(module, petId, eggNumber, success, failure, onGiftError, methodInfo);
    } else {
        LOGE("PET_EGG_HOOK DummyKenshoGameAPI.OpenEgg original is null; call skipped");
    }
}

template <typename HookFn, typename OriginalFn>
bool InstallOne(
    uintptr_t targetAddress,
    HookFn hook,
    OriginalFn* original,
    const char* label
) {
    void* target = reinterpret_cast<void*>(targetAddress);
    const int status = DobbyHook(
        target,
        reinterpret_cast<void*>(hook),
        reinterpret_cast<void**>(original)
    );

    LOGI(
        "PET_EGG_HOOK install %s status=%d target=%p original=%p",
        label,
        status,
        target,
        reinterpret_cast<void*>(*original)
    );

    return status == 0 && *original != nullptr;
}

} // namespace

bool InstallPetEggOpenHooks(uintptr_t il2cppBase) {
    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true)) {
        LOGI("PET_EGG_HOOK install already attempted");
        return false;
    }

    if (il2cppBase == 0) {
        LOGE("PET_EGG_HOOK cannot install: libil2cpp base is zero");
        return false;
    }

    LOGI("PET_EGG_HOOK install begin il2cppBase=%p",
         reinterpret_cast<void*>(il2cppBase));

    const bool petGameManagerOk = InstallOne(
        il2cppBase + kPetGameManagerOpenEggRva,
        HookPetGameManagerOpenEgg,
        &g_originalPetGameManagerOpenEgg,
        "PetGameManager.OpenEgg"
    );

    const bool networkModuleOk = InstallOne(
        il2cppBase + kNetworkModuleOpenEggRva,
        HookNetworkModuleOpenEgg,
        &g_originalNetworkModuleOpenEgg,
        "NetworkModule.OpenEgg"
    );

    const bool dummyApiOk = InstallOne(
        il2cppBase + kDummyKenshoGameApiOpenEggRva,
        HookDummyKenshoGameApiOpenEgg,
        &g_originalDummyKenshoGameApiOpenEgg,
        "DummyKenshoGameAPI.OpenEgg"
    );

    LOGI(
        "PET_EGG_HOOK install summary PetGameManager=%d NetworkModule=%d DummyKenshoGameAPI=%d",
        petGameManagerOk ? 1 : 0,
        networkModuleOk ? 1 : 0,
        dummyApiOk ? 1 : 0
    );

    return petGameManagerOk || networkModuleOk || dummyApiOk;
}

} // namespace XposedPoco
