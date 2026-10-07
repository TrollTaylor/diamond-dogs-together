#pragma once
// Natural body-0 End audit only. No native getter, submission, mutation, hook
// installation, or automatic-backend capability is provided by this module.
#include <array>
#include <cstddef>
#include <cstdint>
namespace mgscoop::partner_upper_end_observer {
constexpr std::uint64_t kEnd = 0xde97403fc93c;
constexpr std::uint64_t kLeaseMs = 250;
constexpr unsigned kObservationCap = 64;
constexpr unsigned kSubmissionPairCap = 4, kUpdatePairCap = 28;
constexpr std::uint64_t kEndWindowMs = 2000, kUpdateIntervalMs = 75;
struct Pin { std::uintptr_t rva; std::size_t size; const char* sha256; };
// Verify these before installation of existing hooks changes their prologues.
inline constexpr Pin kPins[] = {
    {0x105d1e0, 0x273, "3cf9bf088329922fb804ad0cd723cef166151f4c17ab1a0fec7d9d8ccbeee835"},
    {0xaf2ba0, 0x8e, "bf4f8d6085f686bad8a6eecd5863a6a8b0b39c0f0df19e3803e6e7f648a709ff"},
    {0xaf3270, 0x86, "c43f632d623f66ee4164a26d69fa52be99df8b869535532e4f0f4cbe86f125de"},
    {0xaf0340, 0x5d, "2ceda96c5086a8a142d8460b117c92e78bd2b55ef74db4c35ee934b4ae9b90c4"},
    {0xb6c410, 0x1f2, "6ffea8164e703db1f24e1e9f0e3f53e0a0a2083d2fdc52cacb1361969e3adf6e"},
    {0x1c6d900, 0x1f6, "9b01a4ce9860e4111a4228c2982c8a6e6b9ee12e9e2ba3d1b93a9049050c85fd"},
    {0x1c73dc0, 0xa4c, "76e8bb34dba4a38cdefb2ffef732acc674bb0f56941ea9693810fdc649f0d03d"}
};
struct Authorization { bool offline = false, copied_saves = false, exact_executable = false, exact_code_pins = false; };
using ReadFn = bool (*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
struct Reader { void* context = nullptr; ReadFn read = nullptr; };
struct Owner {
    std::uint64_t generation = 0, body_epoch = 0;
    std::uint32_t mission = 0, owner_thread = 0;
    std::uint16_t equip = 0; std::uint8_t stance = 0, flags = 0;
};
struct Identity {
    std::uintptr_t P = 0, Q = 0, S = 0, block = 0, ps = 0, cs = 0;
    std::uintptr_t entries = 0, entry = 0, allocation = 0, controllers = 0, buffer = 0, records = 0;
    std::uintptr_t lower = 0, upper = 0, bank = 0, lower_table = 0, upper_table = 0;
    std::uint32_t lower_count = 0, upper_count = 0;
};
struct Node {
    std::uintptr_t pointer = 0, child = 0;
    std::uint32_t index = 0xffffffffu, child_index = 0xffffffffu;
    std::uint64_t code = 0; std::uint8_t type = 0;
};
struct Channel {
    Node current{}, queued{}, alternate{};
    std::array<std::uint8_t, 10> flags{}; // 17D,17E,17F,180,181,182,185,186,187,188; observations only
    std::array<std::uint8_t, 40> record{};
};
struct Snapshot {
    Identity identity{}; Channel lower{}, upper{};
    Node end_node{}, fire_selector{}; // exact index103 End and index65 -> child116 ancestry
    std::array<std::uint16_t, 4> masks{};
    std::uint32_t entry_flags = 0;
};
struct Lease { Owner owner{}; Identity identity{}; std::uint64_t refreshed_ms = 0; };
struct Submission {
    std::uintptr_t cs = 0, caller = 0;
    std::uint32_t body_id = 0, channel = 0, thread = 0, blend_bits = 0, option = 0;
    std::uint64_t state = 0; bool slot190 = false;
};
struct Update { std::uintptr_t entry = 0, caller = 0; std::uint32_t thread = 0; };
enum class Phase : std::uint8_t { submission_before, submission_after, update_before, update_after };
struct Ticket {
    bool qualified = false; Phase phase = Phase::submission_before;
    std::uint64_t serial = 0, correlated_end = 0, lifecycle = 0;
    unsigned pair = 0xffffffffu;
    Lease lease{}; Submission submission{}; Update update{};
};
struct Observation {
    Phase phase = Phase::submission_before;
    std::uint64_t serial = 0, correlated_end = 0, lifecycle = 0, now_ms = 0, generation = 0, body_epoch = 0;
    Owner owner{}; Submission submission{}; Update update{}; Snapshot snapshot{};
    std::uintptr_t original_rax = 0;
    // No accepted/completed/stopped field: flags and graph departures cannot establish those statuses.
};
bool SameIdentity(const Identity&, const Identity&) noexcept;
// Bounded read-only resolver. Outputs remain unchanged on any refusal.
bool ReadSnapshot(Reader, std::uintptr_t image_base, std::uint32_t mission, Snapshot&) noexcept;
const char* SubmissionRefusal(const Lease&, const Submission&, const Snapshot&, std::uint64_t now,
                              std::uint64_t current_generation, std::uintptr_t image_base) noexcept;
const char* UpdateRefusal(const Lease&, const Update&, const Snapshot&, std::uint64_t now,
                          std::uint64_t current_generation, std::uintptr_t image_base) noexcept;
bool Compiled() noexcept;
bool Configure(std::uintptr_t image_base, Authorization, Reader) noexcept;
// The caller independently establishes the natural Lua owner. Workers may
// observe submissions/updates but cannot publish or refresh this owner lease.
// Generations increase across Clear/body changes; refresh retains a stable identity.
bool PublishOwner(const Owner&, std::uint32_t actual_thread, std::uint64_t now) noexcept;
Ticket BeforeSubmission(const Submission&, std::uint64_t now) noexcept;
void AfterSubmission(const Ticket&, std::uintptr_t original_rax, std::uint64_t now) noexcept;
// API only: no new B6C410 hook is installed here. Requires the independently
// proved Entry* ABI, exact caller, and actual natural phase provenance.
// No reads before a qualified End. First two updates are captured immediately,
// then at most one pair per 75ms within 2s of that End. Four submission pairs
// retain their own budget so startup/phase traffic cannot consume End records.
Ticket BeforeUpdate(const Update&, std::uint64_t now) noexcept;
void AfterUpdate(const Ticket&, std::uintptr_t original_rax, std::uint64_t now) noexcept;
bool Drain(Observation&) noexcept;
void Clear() noexcept;
} // namespace mgscoop::partner_upper_end_observer
