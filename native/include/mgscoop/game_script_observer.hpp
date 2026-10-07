#pragma once
#include <cstddef>
// Admission signal after the exact executable observer and offline marker gate.
bool OfflineScriptObserverEnabled() noexcept;

// Called only after the existing exact-executable/loader-location gates and
// process-lifetime pin, from the successful DirectInput8Create return path.
// Opt-in development hook: pass-through or exact-source bootstrap insertion.
void InstallGameScriptObserver(void* verified_base, std::size_t verified_span,
                               const wchar_t* verified_directory) noexcept;
