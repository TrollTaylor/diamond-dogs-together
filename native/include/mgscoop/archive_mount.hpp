#pragma once
#include <cstddef>

// Development startup call only, after the accepted executable's DirectInput
// creation. Owns one immutable archive for process lifetime; no hot unmount.
bool MountPlayerArchive(void* base, std::size_t span, const wchar_t* directory,
                        void (*log)(const char*) noexcept) noexcept;
