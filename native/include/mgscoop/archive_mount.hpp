#pragma once
#include <cstddef>
// Verified resident GANI catalogue allowance after successful archive mount;
// normal profile21, experimental receiver profile31, zero before ready.
// This is not the archive's QAR entry count.
// This process-lifetime bound does not establish actor/package readiness.
unsigned MountedPlayerMotionCount() noexcept;
// True only after the exact private capacity catalogue was actually mounted.
bool MountedBuddyCapacity() noexcept;

// Development startup call only, after the accepted executable's DirectInput
// creation. Owns one immutable archive for process lifetime; no hot unmount.
bool MountPlayerArchive(void* base, std::size_t span, const wchar_t* directory,
                        void (*log)(const char*) noexcept,
                        bool allow_experimental = false) noexcept;

// Private unique bank archive identity only; does not prove native selection.
bool MountedMgoUniqueRestContext(const wchar_t* directory) noexcept;
