#pragma once

// Original offline debug UI. Graphics/input only; game actions use debug_menu.
namespace mgscoop::debug_overlay {
// Called after the real DirectInput factory succeeds, outside loader lock.
void Install() noexcept;
// Also available to game-owned input wrappers. False on normal launches.
bool CapturingInput() noexcept;

#ifdef MGS_DEBUG_OVERLAY_TESTING
struct TestStats {
    unsigned presents;
    unsigned frames;
    unsigned resizes;
    unsigned input_messages;
    unsigned characters;
    bool initialized;
    bool open;
};
void SetOpenForTest(bool value) noexcept;
void SetForegroundForTest(bool value) noexcept;
bool HookInputFixtureForTest(void* state, void* data) noexcept;
bool HookPadFixtureForTest(void* function) noexcept;
void RawKeyForTest(unsigned key) noexcept;
TestStats StatsForTest() noexcept;
const char* FooterStatusForTest() noexcept;
void ShutdownForTest() noexcept;
#endif
}
