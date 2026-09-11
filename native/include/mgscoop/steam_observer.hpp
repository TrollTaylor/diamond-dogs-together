#pragma once
#ifdef _WIN32
// Called once outside DllMain, after the original DirectInput call. Gates itself
// on executable identity. Observes a public import; calls no game functions.
void InstallSteamCallbackObserver() noexcept;
#endif
