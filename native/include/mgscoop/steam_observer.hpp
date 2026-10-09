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
// Steam self-test stage T (docs/steam-online.md 14): the wrapped RunCallbacks
// call count, and the game's networking-getter calls forwarded to the real
// SteamNetworking (0 while the lab gate hands out the emulator).
unsigned long long SteamObserverCallbackCount() noexcept;
unsigned long long SteamObserverGetterForwards() noexcept;
#endif
