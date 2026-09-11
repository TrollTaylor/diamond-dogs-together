#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::allocation_bridge {
using Allocate = void* (*)(std::uint64_t size, std::uint64_t alignment, std::uint32_t tag);
struct Site {
    std::uintptr_t return_address;
    std::uint64_t size;
    std::uint64_t alignment;
    std::uint32_t tag;
};
struct Plan { Allocate heap; Site sites[2]; };
enum class InstallResult { installed, invalid, bytes_differ, already_installed, hook_failed };

// Caller must establish executable identity, all body/ABI/lifetime checks, and
// quiescence before installing. This generic bridge does not certify a game.
InstallResult Install(Allocate original, const unsigned char* expected,
                      std::size_t count, const Plan& plan) noexcept;
// External caller quiescence is required; active==0 alone is not a lease.
// Outstanding scopes on any thread refuse removal. Keep the bridge module
// loaded until all scopes/callers have stopped and removal has succeeded.
bool RemoveAfterCallersStopped() noexcept;

// Stack-only, thread-affine scope around a verified session-creation call.
// A nested scope cannot acquire ownership or change the outer selection.
class Scope {
public:
    Scope() noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;
    bool acquired() const noexcept { return acquired_; }
private:
    bool acquired_{};
};
}
