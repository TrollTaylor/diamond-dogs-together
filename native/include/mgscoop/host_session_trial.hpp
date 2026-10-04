#pragma once
#include <cstddef>
#include <cstdint>

// One process-lifetime experiment, called synchronously from the exact patched
// TppMain.OnAllocate receipt. Never accepts peers or overrides game predicates.
bool TryCreateHostSession(void* verified_base, std::size_t verified_span,
                          void (*log)(const char*) noexcept, bool heap_test=false,
                          bool create_enabled=false) noexcept;
bool TryCreateClientSession(void* base, std::size_t span, std::uint64_t host,
                            void (*log)(const char*) noexcept, bool inspect_only=false) noexcept;
// Static reason the most recent TryCreateClientSession returned false (empty
// after success). `already_logged` reports whether that refusal wrote its own
// client_join=refused line (the host-identity refusals do).
const char* LastClientJoinRefusal(bool* already_logged = nullptr) noexcept;
// Clears the process-lifetime one-shot join guard so a reset can re-arm the
// guest for another attempt. Call only from the owned mission thread after the
// previous attempt's session has been released and its fields restored.
void ResetClientAttemptLatch() noexcept;

// Read-only sampling at bounded lifecycle receipts. Never attempts creation.
void ObserveHostSessionPreflight(void* verified_base, std::size_t verified_span,
                                void (*log)(const char*) noexcept) noexcept;

// Install Dynamite's second-player loading fix before player construction.
bool PrepareDynamitePlayerLoading(void* verified_base, std::size_t span,
                                  void (*log)(const char*) noexcept) noexcept;
