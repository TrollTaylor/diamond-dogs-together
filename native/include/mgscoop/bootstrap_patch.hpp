#pragma once
#include "mgscoop/script_load_bridge.hpp"

namespace mgscoop {
bool SelectDeathPolicyPatch(const char*, std::size_t, const char*, script_bridge::Replacement&, bool) noexcept;
bool SelectDamageParameterPatch(const char*, std::size_t, const char*, script_bridge::Replacement&, bool) noexcept;
bool SelectMissionTracePatch(const char*, std::size_t, const char*, script_bridge::Replacement&, bool) noexcept;
// Exact current-build Tpp.lua only. The returned buffer has process lifetime
// and is immutable after publication; every request rechecks its source hash.
bool SelectBootstrapPatch(const char* source, std::size_t size, const char* name,
                          script_bridge::Replacement& out, bool archive_mounted = false) noexcept;
// Exact current-build TppPackList.lua only, separately gated allocation trial.
bool SelectPlayerPackagePatch(const char* source, std::size_t size, const char* name,
                              script_bridge::Replacement& out, bool enabled) noexcept;
bool SelectHostTrialPatch(const char* source, std::size_t size, const char* name,
                          script_bridge::Replacement& out, bool enabled) noexcept;
}
