#pragma once
#ifdef _WIN32
// Called once outside DllMain, after the original DirectInput call. Gates itself
// on executable identity. Observes a public import; calls no game functions.
void InstallSteamCallbackObserver() noexcept;
// Steam self-test stage T (docs/steam-online.md 14): the wrapped RunCallbacks
// call count, and the game's networking-getter calls forwarded to the real
// SteamNetworking (0 while the lab gate hands out the emulator).
unsigned long long SteamObserverCallbackCount() noexcept;
unsigned long long SteamObserverGetterForwards() noexcept;
#endif
