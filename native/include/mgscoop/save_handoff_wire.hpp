#pragma once
// Automatic save handoff wire format (docs/save-handoff.md; join-fix phase 2
// P2-4..P2-6): the messages the host and the guest exchange on reliable link
// channel 4, the progress-file slot table and the set hash shared with the
// PowerShell tooling and tests/fixtures/handoff-set.json. Pure codec
// (mgs_coop_core): no Windows headers, no allocation, no I/O.
//
// Every message starts with an 8-byte header, little-endian:
//   [0] magic 0x53 'S'  [1] version 1  [2] kind  [3] flags = 0
//   [4..7] xfer u32 (nonzero, BCryptGenRandom per offer; 0 only for STATE)
// Kinds and exact sizes:
//   1 STATE   64 B   (both directions, ReliableMode::latest)
//       [8] role (1 host, 2 guest) [9] phase (0 unknown, 1 loading, 2 title, 3 ingame)
//       [10] story (0 unknown, 1 opening, 2 open) [11] mode (0 off, 1 stage, 2 scene, 3 always)
//       [12..15] mission u32 [16..19] bound_mission u32 (0 unknown)
//       [20] set_known 0|1 [21] need 0|1 (guest only) [22] elig (Elig; host) [23] 0
//       [24..55] set_sha256 [56..63] 0
//   2 OFFER   52 + 40n B (n = 1..6, at most 292; host -> guest)
//       [8] n [9] 0 [10..11] 0 [12..15] total u32 (<= kMaxTotal, the sum of the sizes)
//       [16..19] bound_mission u32 [20..51] set_sha256
//       then n entries {[0] slot 0..5, [1..3] 0, [4..7] size 1..kMaxFileBytes, [8..39] sha256}
//       in strictly ascending slot order; slot 0 or 1 must be present
//   3 WANT    12 B (guest -> host)  [8] slot mask (bit n = slot n, only offered slots) [9..11] 0
//   4 DECLINE 12 B (guest -> host)  [8] DeclineReason [9..11] 0
//   5 CHUNK   16 + len B (host -> guest; at most 1016)
//       [8] slot [9] 0 [10..11] len 1..kChunk [12..15] offset u32, then len payload bytes
//       (sent in offered slot order, offsets contiguous from 0)
//   6 END     12 B (host -> guest)  [8..11] total u32
//   7 RESULT  12 B (guest -> host)  [8] ResultStatus [9] Fail (0 unless failed) [10..11] 0
//   8 ABORT   12 B (either)         [8] AbortReason [9..11] 0
// A violation is dropped and counted; the glue logs
// "save_handoff=bad_msg; why=<DecodeName>" at most once per 10 s.
#include <cstddef>
#include <cstdint>

namespace mgscoop::save_handoff_wire {

inline constexpr std::uint8_t kMagic = 0x53;
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint8_t kChannel = 4;  // link_protocol kReliableChannels becomes 5 (track B)
inline constexpr std::size_t kHeaderSize = 8;
inline constexpr std::size_t kChunk = 1000;                     // CHUNK payload bytes at most
inline constexpr unsigned kMaxFiles = 6;                        // slots 0..5
inline constexpr std::uint32_t kMaxFileBytes = 4u * 1024u * 1024u;
inline constexpr std::uint32_t kMaxTotal = 8u * 1024u * 1024u;
inline constexpr std::size_t kShaSize = 32;
inline constexpr std::size_t kShaHexChars = 64;                 // upper-case hex, like Get-FileHash
inline constexpr std::size_t kStateSize = 64;
inline constexpr std::size_t kOfferBaseSize = 52;
inline constexpr std::size_t kOfferEntrySize = 40;
inline constexpr std::size_t kOfferMaxSize = kOfferBaseSize + kOfferEntrySize * kMaxFiles;  // 292
inline constexpr std::size_t kSmallSize = 12;                   // WANT, DECLINE, END, RESULT, ABORT
inline constexpr std::size_t kChunkHeaderSize = 16;
inline constexpr std::size_t kMaxMessage = kChunkHeaderSize + kChunk;  // 1016 (<= link kMaxReliableMessage 1024)

// The first build that speaks channel 4: no channel-4 byte is sent unless the
// link peer's remote_build passes RemoteBuildAtLeast(kHandoffMin*) (P2-4).
inline constexpr unsigned kHandoffMinMajor = 0, kHandoffMinMinor = 13, kHandoffMinPatch = 0;

enum class Kind : std::uint8_t { state = 1, offer = 2, want = 3, decline = 4, chunk = 5, end = 6, result = 7, abort = 8 };
enum class Role : std::uint8_t { none = 0, host = 1, guest = 2 };
enum class Phase : std::uint8_t { unknown = 0, loading = 1, title = 2, ingame = 3 };
enum class Story : std::uint8_t { unknown = 0, opening = 1, open = 2 };
// lab.ini save_handoff (lab_config SaveHandoff has the same order).
enum class Mode : std::uint8_t { off = 0, stage = 1, scene = 2, always = 3 };
// Host eligibility (P2-7 HostEligible); the first failing reason in the order
// off, story_unknown, story_opening, unexpected_files, too_large, saving,
// continue_unknown, continue_unsupported.
enum class Elig : std::uint8_t {
    unknown = 0, eligible = 1, story_opening = 2, story_unknown = 3, continue_unknown = 4, continue_unsupported = 5,
    saving = 6, unexpected_files = 7, too_large = 8, off = 9
};
enum class DeclineReason : std::uint8_t {
    none = 0, same = 1, same_scene = 2, off = 3, budget = 4, user = 5, not_title = 6, bad_offer = 7, disk_space = 8
};
enum class ResultStatus : std::uint8_t { staged = 0, exiting = 1, failed = 2 };
enum class AbortReason : std::uint8_t {
    none = 0, link_lost = 1, user = 2, stall = 3, snapshot_changed = 4, not_title = 5, shutdown = 6, protocol = 7
};
// Why a transfer failed: RESULT reason, toast G7/H6 a, View.why for the failed
// token and the "save_handoff=failed; why=%s" log. Track A may append codes.
enum class Fail : std::uint8_t {
    none = 0,
    hash_mismatch = 1,     // a received file or the set does not match the OFFER
    disk = 2,              // a stage write, rename or manifest failed
    disk_space = 3,
    stall = 4,             // no CHUNK for 30 s
    link_lost = 5,         // the link session ended mid-transfer
    protocol = 6,          // an out-of-order or inconsistent message
    snapshot_changed = 7,  // host: the files changed between the reads
    not_title = 8,         // guest: left the title during the countdown
    timeout = 9,           // host: no RESULT within 60 s
    shutdown = 10,         // the game is closing
    bad_offer = 11,        // guest: the OFFER failed validation
    partner = 12,          // host: the guest reported RESULT failed / ABORT
};

const char* KindName(Kind kind) noexcept;               // state, offer, ... ("?" when unknown)
const char* RoleName(Role role) noexcept;               // none, host, guest
const char* ModeName(Mode mode) noexcept;               // off, stage, scene, always
const char* EligName(Elig elig) noexcept;               // unknown, eligible, story_opening, ...
const char* DeclineName(DeclineReason reason) noexcept;  // none, same, same_scene, ...
const char* ResultStatusName(ResultStatus status) noexcept;  // staged, exiting, failed
const char* AbortName(AbortReason reason) noexcept;     // none, link_lost, user, ...
const char* FailName(Fail fail) noexcept;               // none, hash_mismatch, disk, ...

// ---- P2-6 slot table and set hash.
inline constexpr unsigned kSlots = 6;
// "287700/local/TPP_GAME_DATA0" .. "remote/PERSONAL_DATA" (share-save.json
// entry form); nullptr for slot >= kSlots.
//   0 287700/local/TPP_GAME_DATA0   1 287700/local/TPP_GAME_DATA1
//   2 287700/local/PERSONAL_DATA0   3 287700/local/PERSONAL_DATA1
//   4 remote/TPP_GAME_DATA          5 remote/PERSONAL_DATA
const char* SlotEntry(std::uint8_t slot) noexcept;
// The same with backslashes ("287700\\local\\TPP_GAME_DATA0"); nullptr for slot >= kSlots.
const char* SlotPath(std::uint8_t slot) noexcept;
// The file name alone ("TPP_GAME_DATA0"); nullptr for slot >= kSlots.
const char* SlotName(std::uint8_t slot) noexcept;
// Slots 4 and 5 live in <save_dir>\remote (save_redirect), 0..3 in <save_dir>\287700\local.
inline constexpr bool SlotIsRemote(std::uint8_t slot) noexcept { return slot == 4 || slot == 5; }
// ^(TPP_GAME_DATA[0-9]?|PERSONAL_DATA[0-9]?)$, case-sensitive (Share-Save's
// pattern; TPP_CONFIG_DATA*, TPP_GRAPHICS_CONFIG and MGO_GAME_DATA* never
// match). A name outside the slot table that matches means elig=unexpected_files.
bool IsProgressName(const char* name) noexcept;
bool IsProgressName(const wchar_t* name) noexcept;

struct FileEntry {
    std::uint8_t slot;                // 0..5
    std::uint32_t size;               // 1..kMaxFileBytes
    std::uint8_t sha256[kShaSize];    // SHA-256 of the raw file
};
// set_sha256 = SHA-256 over the present slots in ascending order, each
// contributing u8 slot, u32 size LE and its raw 32-byte file SHA-256. False
// (out zeroed) when n is 0 or > kMaxFiles or the slots are not strictly ascending.
bool SetSha256(const FileEntry* entries, unsigned n, std::uint8_t (&out)[kShaSize]) noexcept;
// Upper-case hex of a SHA-256 (64 characters plus NUL).
void FormatShaHex(const std::uint8_t (&sha)[kShaSize], char (&out)[kShaHexChars + 1]) noexcept;
// Exactly 64 hex digits (either case). Outputs only on success.
bool ParseShaHex(const char* text, std::size_t size, std::uint8_t (&out)[kShaSize]) noexcept;

// ---- Messages.
struct State {
    Role role;
    Phase phase;
    Story story;
    Mode mode;
    std::uint32_t mission;
    std::uint32_t bound_mission;      // provenance of the current set, 0 unknown
    bool set_known;
    std::uint8_t need;                // guest: GuestNeed 0|1; host 0
    Elig elig;                        // host: HostEligible; guest unknown
    std::uint8_t set[kShaSize];       // zero unless set_known
};
struct Offer {
    std::uint8_t n;                   // 1..kMaxFiles
    std::uint32_t total;              // the sum of the sizes, <= kMaxTotal
    std::uint32_t bound_mission;
    std::uint8_t set[kShaSize];
    FileEntry files[kMaxFiles];       // [0, n) in strictly ascending slot order
};
struct Want { std::uint8_t mask; };
struct Decline { DeclineReason reason; };
struct Chunk {
    std::uint8_t slot;
    std::uint16_t len;                // 1..kChunk
    std::uint32_t offset;
    const std::uint8_t* payload;      // Decode: points into the decoded buffer; Encode: len bytes read from here
};
struct End { std::uint32_t total; };
struct Result { ResultStatus status; Fail reason; };
struct Abort { AbortReason reason; };
// One decoded message: `kind` names the member that is valid.
struct Message {
    Kind kind;
    std::uint32_t xfer;               // 0 for STATE, nonzero otherwise
    State state;
    Offer offer;
    Want want;
    Decline decline;
    Chunk chunk;
    End end;
    Result result;
    Abort abort;
};

// Encodes `message` (the member `kind` names) with the exact P2-5 layout.
// Returns the size (<= kMaxMessage), or 0 (nothing written) when a field is
// out of range (the same rules Decode enforces) or capacity is too small.
std::size_t Encode(const Message& message, std::uint8_t* out, std::size_t capacity) noexcept;

// Strict decode. short_: fewer than kHeaderSize bytes; magic; version; kind:
// unknown kind or flags != 0; size: not the exact size of the kind (OFFER
// 52 + 40n, CHUNK 16 + len); bounds: a field out of range (xfer 0 on a kind
// other than STATE or nonzero on STATE; an enum value out of range; n outside
// 1..6; unsorted or duplicate slots; slot > 5; size 0 or > kMaxFileBytes;
// total > kMaxTotal or not equal to the sum; neither slot 0 nor 1; CHUNK len 0
// or > kChunk; a WANT mask of 0 or with bits above 5; a nonzero reserved or
// pad byte). `out` is written only on ok.
enum class Decode : std::uint8_t { ok, short_, magic, version, kind, size, bounds };
const char* DecodeName(Decode result) noexcept;  // ok, short, magic, version, kind, size, bounds
Decode DecodeMessage(const std::uint8_t* data, std::size_t size, Message& out) noexcept;

// The "X.Y.Z" prefix of a link remote_build ("0.13.0+c07d0a2") compared with
// major.minor.patch; false for nullptr, "" or anything that does not start
// with three dot-separated decimal numbers.
bool RemoteBuildAtLeast(const char* build, unsigned major_version, unsigned minor_version,
                        unsigned patch_version) noexcept;

}  // namespace mgscoop::save_handoff_wire
