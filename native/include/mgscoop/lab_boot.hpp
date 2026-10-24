#pragma once
// The developer test boot's Lua receipts (docs/test-boot.md). Pure helpers: no
// Win32 calls and no game memory; game_script_observer owns the gate, the
// threading and the log. lua/coop/debug_boot.lua compiles them with loadstring
// (the chunk name equals the source).
//   return 'MGS_COOP_BOOT_CFG_V1'
//     -> return 0      inert: not the owned private worker of a background-worker
//                      build in lab mode with copied saves, or nothing named
//     -> return 1,<fast>,<mission>,<pos>,<x_mm>,<y_mm>,<z_mm>,<yaw_mdeg>,<clock_s>,<weather>,<role>
//        fast 0|1 (lab_fast_boot), mission 0 or the lab_boot target, pos 0|1,
//        clock_s -1 (keep) or 0..86399, weather 0 (keep) .. 5 (LabBootWeatherName),
//        role 1 host, 2 guest
//   return 'MGS_COOP_BOOT_LOG_V1' -- <payload>   1..160 bytes; [A-Za-z0-9_=:,.;-] kept, others '_'
//     -> return true, and the log line "lab_boot=<payload with ';' as '; '>\r\n"
#include <cstddef>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::lab_boot {

inline constexpr char kCfgSource[] = "return 'MGS_COOP_BOOT_CFG_V1'";
inline constexpr char kLogPrefix[] = "return 'MGS_COOP_BOOT_LOG_V1' -- ";
inline constexpr std::size_t kMaxLogPayload = 160;
inline constexpr std::size_t kMaxReceipt = 240;
// "return 1," and ten values of at most 11 characters with their commas.
inline constexpr std::size_t kReplyCapacity = 160;
// "lab_boot=" + payload with ';' -> "; " + CRLF + NUL.
inline constexpr std::size_t kLogLineCapacity = 9 + 2 * kMaxLogPayload + 3;

enum class Receipt : unsigned char { none, cfg, log };
// Exact receipts only (bytewise name == source, NUL-free, size bound).
Receipt Classify(const char* source, std::size_t size, const char* name) noexcept;

// What the loader knows when Lua asks; every field must hold.
struct Gate {
    bool worker_compiled = false;  // worker_scenario::Compiled()
    bool worker_active = false;    // the owned private worker admitted this process
    bool lab_mode = false;         // lab.ini mode=lab (play forces the keys off anyway)
    bool lab_active = false;       // lab.ini present and the lab paths fit
    bool offline = false;          // the offline-v1 marker
    bool copied_saves = false;     // lab::SavesRedirected()
};
bool Admitted(const Gate& gate) noexcept;
// NUL-terminated "return 0" or "return 1,..."; 0 when it does not fit.
std::size_t FormatCfgReply(bool admitted, bool fast, const LabBoot& boot, bool host, char* out, std::size_t capacity) noexcept;
// The whole LOG_V1 source in, "lab_boot=...\r\n" out; 0 when the payload is
// empty, too long or the receipt is not a LOG_V1 one.
std::size_t FormatLogLine(const char* source, std::size_t size, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::lab_boot
