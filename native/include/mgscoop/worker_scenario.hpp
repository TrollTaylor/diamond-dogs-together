#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::worker_scenario {
#ifndef MGS_BACKGROUND_WORKER
#define MGS_BACKGROUND_WORKER 0
#endif
inline constexpr bool Compiled() noexcept { return MGS_BACKGROUND_WORKER != 0; }
struct Authorization { bool offline_v1{}, lab{}, copied_saves{}, exact_executable{}; };
struct RunContext {
    const char* run_id{};
    const char* role{};
    const wchar_t* run_dir{};
    const wchar_t* desktop{};
    std::uint32_t pid{};
    std::uint64_t process_start_filetime{};
};
enum class Operation : std::uint8_t { press, hold, release, mouse, capture };
enum class Device : std::uint8_t { keyboard, mouse };
enum class Key : std::uint8_t {
    w, a, s, d, enter, space, escape, e, r, shift, ctrl, one, two, f, c, q, x, z, tab, lmb, rmb, count
};
struct Command {
    std::uint64_t sequence{}, expires{}, keys{};
    Operation operation{};
    std::uint32_t duration{};
    std::int32_t dx{}, dy{};
};
struct Event {
    std::uint32_t offset{}, value{}, timestamp{}, sequence{};
    std::uint64_t command{}, expires{};
};
struct MouseState { std::int32_t x{}, y{}, z{}; std::uint8_t buttons[8]{}; };
struct CaptureRequest { std::uint64_t sequence{}, expires{}; };
// Pure policy used by detached fixtures. API receipt is distinct from gameplay.
class Scenario {
public:
    bool Accept(const Command&, std::uint64_t now) noexcept;
    void Tick(std::uint64_t now) noexcept;
    void Clear() noexcept;
    void Keyboard(std::uint8_t (&bytes)[256]) const noexcept;
    MouseState Mouse(bool consume) noexcept;
    std::uint32_t Events(Device, Event*, std::uint32_t capacity, bool peek) noexcept;
    CaptureRequest TakeCapture(std::uint64_t now) noexcept;
    std::uint64_t sequence() const noexcept { return last_sequence_; }
    std::uint64_t input_command(Device device) const noexcept {
        return device == Device::keyboard ? keyboard_command_ : mouse_command_;
    }
private:
    void Set(Key, bool, std::uint64_t now, std::uint64_t command, std::uint64_t expires = 0) noexcept;
    bool held_[static_cast<unsigned>(Key::count)]{};
    std::uint64_t releases_[static_cast<unsigned>(Key::count)]{};
    std::uint64_t held_commands_[static_cast<unsigned>(Key::count)]{};
    Event keyboard_events_[64]{}, mouse_events_[64]{};
    std::uint32_t keyboard_count_{}, mouse_count_{}, event_sequence_{};
    std::int32_t dx_{}, dy_{};
    std::uint64_t last_sequence_{}, keyboard_command_{}, mouse_command_{}, capture_at_{}, move_expires_{};
    CaptureRequest capture_{};
};
bool ParseCommand(const char* text, std::size_t size, const char* run_id,
                  const char* role, Command& output) noexcept;
bool ValidRunId(const char*) noexcept;
bool ValidAuthorization(const Authorization&) noexcept;
// Success only preserves a genuinely successful original API result. No
// DIERR_INPUTLOST/DIERR_NOTACQUIRED result is promoted to success.
inline bool MayReplaceState(std::int32_t original_hresult, bool typed_format,
                            bool admitted_thread) noexcept {
    return original_hresult >= 0 && typed_format && admitted_thread;
}

bool Initialize(const Authorization&) noexcept;
bool Requested() noexcept;
bool Active() noexcept;
bool ThreadAdmitted() noexcept;
RunContext GetContext() noexcept;
void SetHooksReady(bool ready) noexcept;
void Refuse(const char* reason) noexcept;
void Poll(std::uint64_t now) noexcept;
bool Keyboard(void* bytes, std::size_t size) noexcept;
bool Mouse(void* bytes, std::size_t size) noexcept;
std::uint32_t Buffered(Device, Event*, std::uint32_t capacity, bool peek) noexcept;
CaptureRequest TakeCapture(std::uint64_t now) noexcept;
void FinishCapture(const CaptureRequest&, bool ok, const char* reason,
                   std::uint32_t width, std::uint32_t height) noexcept;
void InputFailure(const char* api, std::int32_t result, bool typed) noexcept;
void DeviceConfiguration(const char* api, std::int32_t result, bool typed,
                         std::uint32_t requested, std::uint32_t forwarded) noexcept;
#ifdef MGS_WORKER_TESTING
// Detached fixture-only mock. Not exported by a product build.
void ConfigureForTest(const Authorization&, bool private_thread, bool ready) noexcept;
bool AcceptForTest(const Command&, std::uint64_t now) noexcept;
void SetPrivateForTest(bool) noexcept;
struct TestStats { std::uint64_t keyboard_consumed{}, mouse_consumed{}; unsigned diagnostic_count{}; };
TestStats StatsForTest() noexcept;
#endif
} // namespace mgscoop::worker_scenario
