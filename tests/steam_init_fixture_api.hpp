#pragma once
#include <cstdint>
#include <windows.h>

enum class FixtureMode : int { True, False, Nested, OtherThread, Repeated, WrongOwner, NoHit, ReturnTimeout };
using FixtureInvoke = std::uint64_t (*)();
constexpr DWORD fixture_unwind_exception = 0xe0424242;
constexpr std::uint64_t fixture_upper_bits = 0xa5a50000;
struct FixtureApiState {
    LONG calls;
    LONG nested_calls;
    LONG other_thread_calls;
    LONG wait_entered;
    LONG resumed_after_detach;
    std::uint64_t inner_raw;
};
#ifdef STEAM_INIT_FIXTURE_API_BUILD
#define FIXTURE_API __declspec(dllexport)
#else
#define FIXTURE_API __declspec(dllimport)
#endif
extern "C" FIXTURE_API void FixtureConfigure(FixtureMode mode, FixtureInvoke invoke);
extern "C" FIXTURE_API void FixtureReadState(FixtureApiState* state);
extern "C" FIXTURE_API bool FixtureSteamInit();
