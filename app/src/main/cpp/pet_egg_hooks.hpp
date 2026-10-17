#pragma once

#include <cstdint>

namespace XposedPoco {

// Install diagnostic-only Dobby hooks for the three concrete OpenEgg candidates.
// Call after libil2cpp.so is loaded. Hooks log arguments and always forward to
// the original function; they do not invoke or replace any delegates.
bool InstallPetEggOpenHooks(uintptr_t il2cppBase);

} // namespace XposedPoco
