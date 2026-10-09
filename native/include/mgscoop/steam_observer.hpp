#pragma once
#include <cstddef>
#ifdef _WIN32
// Called once outside DllMain, after the original DirectInput call. Gates itself
// on executable identity. Observes a public import; calls no game functions.
void InstallSteamCallbackObserver() noexcept;
// The local Steam persona (raw UTF-8) from the steam_api64 whose fingerprint
// matched at install (one local GetPersonaName query per call; the partner
// name tag calls it once at install); false (out empty) when unavailable.
bool SteamLocalPersonaName(char* out, std::size_t capacity) noexcept;
#endif
