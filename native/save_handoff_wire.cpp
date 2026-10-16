// Original MGS Co-op automatic save handoff: channel-4 wire codec, slot table
// and set hash (join-fix phase 2 P2-4..P2-6; docs/save-handoff.md).
#include "mgscoop/save_handoff_wire.hpp"

#include <cstring>

#include "mgscoop/link_crypto.hpp"

namespace mgscoop::save_handoff_wire {
namespace {

void Put16(std::uint8_t* out, std::uint16_t v) noexcept {
    out[0] = static_cast<std::uint8_t>(v);
    out[1] = static_cast<std::uint8_t>(v >> 8);
}
void Put32(std::uint8_t* out, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint16_t Get16(const std::uint8_t* in) noexcept { return static_cast<std::uint16_t>(in[0] | (in[1] << 8)); }
std::uint32_t Get32(const std::uint8_t* in) noexcept {
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}
bool AllZero(const std::uint8_t* data, std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i)
        if (data[i]) return false;
    return true;
}

constexpr std::uint8_t kFailMax = static_cast<std::uint8_t>(Fail::partner);

constexpr const char* kSlotEntries[kSlots] = {
    "287700/local/TPP_GAME_DATA0", "287700/local/TPP_GAME_DATA1", "287700/local/PERSONAL_DATA0",
    "287700/local/PERSONAL_DATA1", "remote/TPP_GAME_DATA",        "remote/PERSONAL_DATA",
};
constexpr const char* kSlotPaths[kSlots] = {
    "287700\\local\\TPP_GAME_DATA0", "287700\\local\\TPP_GAME_DATA1", "287700\\local\\PERSONAL_DATA0",
    "287700\\local\\PERSONAL_DATA1", "remote\\TPP_GAME_DATA",         "remote\\PERSONAL_DATA",
};
constexpr const char* kSlotNames[kSlots] = {
    "TPP_GAME_DATA0", "TPP_GAME_DATA1", "PERSONAL_DATA0", "PERSONAL_DATA1", "TPP_GAME_DATA", "PERSONAL_DATA",
};

// Field rules shared by Encode and Decode.
bool StateOk(const State& s) noexcept {
    if (s.role != Role::host && s.role != Role::guest) return false;
    if (static_cast<std::uint8_t>(s.phase) > static_cast<std::uint8_t>(Phase::ingame)) return false;
    if (static_cast<std::uint8_t>(s.story) > static_cast<std::uint8_t>(Story::open)) return false;
    if (static_cast<std::uint8_t>(s.mode) > static_cast<std::uint8_t>(Mode::always)) return false;
    if (s.mission > kMaxMission || s.bound_mission > kMaxMission) return false;
    if (s.need > 1) return false;
    if (static_cast<std::uint8_t>(s.elig) > static_cast<std::uint8_t>(Elig::off)) return false;
    if (!s.set_known && !AllZero(s.set, kShaSize)) return false;
    return true;
}
// The OFFER file list: n 1..6, slots strictly ascending and <= 5, sizes
// 1..kMaxFileBytes, slot 0 or 1 present, the sum equal to total <= kMaxTotal.
bool OfferOk(const Offer& o) noexcept {
    if (o.n == 0 || o.n > kMaxFiles) return false;
    if (o.total > kMaxTotal || o.bound_mission > kMaxMission) return false;
    std::uint64_t sum = 0;
    bool primary = false;
    for (unsigned i = 0; i < o.n; ++i) {
        const FileEntry& f = o.files[i];
        if (f.slot >= kSlots) return false;
        if (i && f.slot <= o.files[i - 1].slot) return false;
        if (f.size == 0 || f.size > kMaxFileBytes) return false;
        if (f.slot == 0 || f.slot == 1) primary = true;
        sum += f.size;
    }
    return primary && sum == o.total;
}
bool WantMaskOk(std::uint8_t mask) noexcept { return mask != 0 && (mask & ~0x3Fu) == 0; }
bool ChunkOk(const Chunk& c) noexcept {
    if (c.slot >= kSlots || c.len == 0 || c.len > kChunk) return false;
    return static_cast<std::uint64_t>(c.offset) + c.len <= kMaxFileBytes;
}
bool DeclineOk(DeclineReason r) noexcept {
    const auto v = static_cast<std::uint8_t>(r);
    return v >= 1 && v <= static_cast<std::uint8_t>(DeclineReason::disk_space);
}
bool ResultOk(const Result& r) noexcept {
    const auto status = static_cast<std::uint8_t>(r.status);
    const auto reason = static_cast<std::uint8_t>(r.reason);
    if (status > static_cast<std::uint8_t>(ResultStatus::failed)) return false;
    if (reason > kFailMax) return false;
    if (r.status == ResultStatus::failed) return reason != 0;
    return reason == 0;
}
bool AbortOk(AbortReason r) noexcept {
    const auto v = static_cast<std::uint8_t>(r);
    return v >= 1 && v <= static_cast<std::uint8_t>(AbortReason::protocol);
}
bool EndOk(const End& e) noexcept { return e.total != 0 && e.total <= kMaxTotal; }

void PutHeader(std::uint8_t* out, Kind kind, std::uint32_t xfer) noexcept {
    out[0] = kMagic;
    out[1] = kVersion;
    out[2] = static_cast<std::uint8_t>(kind);
    out[3] = 0;
    Put32(out + 4, xfer);
}

bool HexDigits(const char* text, std::uint8_t* out, std::size_t bytes) noexcept {
    for (std::size_t i = 0; i < bytes; ++i) {
        unsigned v = 0;
        for (unsigned k = 0; k < 2; ++k) {
            const char c = text[i * 2 + k];
            unsigned d;
            if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
            else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
            else return false;
            v = (v << 4) | d;
        }
        out[i] = static_cast<std::uint8_t>(v);
    }
    return true;
}

template <typename Ch>
bool MatchesProgress(const Ch* name) noexcept {
    if (!name) return false;
    static constexpr const char* kStems[] = {"TPP_GAME_DATA", "PERSONAL_DATA"};
    for (const char* stem : kStems) {
        std::size_t i = 0;
        while (stem[i] && name[i] == static_cast<Ch>(stem[i])) ++i;
        if (stem[i]) continue;
        if (name[i] == 0) return true;
        return name[i] >= static_cast<Ch>('0') && name[i] <= static_cast<Ch>('9') && name[i + 1] == 0;
    }
    return false;
}

}  // namespace

const char* KindName(Kind kind) noexcept {
    switch (kind) {
    case Kind::state: return "state";
    case Kind::offer: return "offer";
    case Kind::want: return "want";
    case Kind::decline: return "decline";
    case Kind::chunk: return "chunk";
    case Kind::end: return "end";
    case Kind::result: return "result";
    case Kind::abort: return "abort";
    }
    return "?";
}
const char* RoleName(Role role) noexcept {
    switch (role) {
    case Role::host: return "host";
    case Role::guest: return "guest";
    default: return "none";
    }
}
const char* ModeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::stage: return "stage";
    case Mode::scene: return "scene";
    case Mode::always: return "always";
    default: return "off";
    }
}
const char* EligName(Elig elig) noexcept {
    switch (elig) {
    case Elig::eligible: return "eligible";
    case Elig::story_opening: return "story_opening";
    case Elig::story_unknown: return "story_unknown";
    case Elig::continue_unknown: return "continue_unknown";
    case Elig::continue_unsupported: return "continue_unsupported";
    case Elig::saving: return "saving";
    case Elig::unexpected_files: return "unexpected_files";
    case Elig::too_large: return "too_large";
    case Elig::off: return "off";
    default: return "unknown";
    }
}
const char* DeclineName(DeclineReason reason) noexcept {
    switch (reason) {
    case DeclineReason::same: return "same";
    case DeclineReason::same_scene: return "same_scene";
    case DeclineReason::off: return "off";
    case DeclineReason::budget: return "budget";
    case DeclineReason::user: return "user";
    case DeclineReason::not_title: return "not_title";
    case DeclineReason::bad_offer: return "bad_offer";
    case DeclineReason::disk_space: return "disk_space";
    default: return "none";
    }
}
const char* ResultStatusName(ResultStatus status) noexcept {
    switch (status) {
    case ResultStatus::staged: return "staged";
    case ResultStatus::exiting: return "exiting";
    case ResultStatus::failed: return "failed";
    }
    return "?";
}
const char* AbortName(AbortReason reason) noexcept {
    switch (reason) {
    case AbortReason::link_lost: return "link_lost";
    case AbortReason::user: return "user";
    case AbortReason::stall: return "stall";
    case AbortReason::snapshot_changed: return "snapshot_changed";
    case AbortReason::not_title: return "not_title";
    case AbortReason::shutdown: return "shutdown";
    case AbortReason::protocol: return "protocol";
    default: return "none";
    }
}
const char* FailName(Fail fail) noexcept {
    switch (fail) {
    case Fail::hash_mismatch: return "hash_mismatch";
    case Fail::disk: return "disk";
    case Fail::disk_space: return "disk_space";
    case Fail::stall: return "stall";
    case Fail::link_lost: return "link_lost";
    case Fail::protocol: return "protocol";
    case Fail::snapshot_changed: return "snapshot_changed";
    case Fail::not_title: return "not_title";
    case Fail::timeout: return "timeout";
    case Fail::shutdown: return "shutdown";
    case Fail::bad_offer: return "bad_offer";
    case Fail::partner: return "partner";
    default: return "none";
    }
}
const char* DecodeName(Decode result) noexcept {
    switch (result) {
    case Decode::ok: return "ok";
    case Decode::short_: return "short";
    case Decode::magic: return "magic";
    case Decode::version: return "version";
    case Decode::kind: return "kind";
    case Decode::size: return "size";
    case Decode::bounds: return "bounds";
    }
    return "?";
}

const char* SlotEntry(std::uint8_t slot) noexcept { return slot < kSlots ? kSlotEntries[slot] : nullptr; }
const char* SlotPath(std::uint8_t slot) noexcept { return slot < kSlots ? kSlotPaths[slot] : nullptr; }
const char* SlotName(std::uint8_t slot) noexcept { return slot < kSlots ? kSlotNames[slot] : nullptr; }
bool IsProgressName(const char* name) noexcept { return MatchesProgress(name); }
bool IsProgressName(const wchar_t* name) noexcept { return MatchesProgress(name); }

bool SetSha256(const FileEntry* entries, unsigned n, std::uint8_t (&out)[kShaSize]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (!entries || n == 0 || n > kMaxFiles) return false;
    for (unsigned i = 0; i < n; ++i) {
        if (entries[i].slot >= kSlots) return false;
        if (i && entries[i].slot <= entries[i - 1].slot) return false;
    }
    link::Sha256 ctx;
    link::Sha256Init(ctx);
    for (unsigned i = 0; i < n; ++i) {
        std::uint8_t row[1 + 4];
        row[0] = entries[i].slot;
        Put32(row + 1, entries[i].size);
        link::Sha256Update(ctx, row, sizeof(row));
        link::Sha256Update(ctx, entries[i].sha256, kShaSize);
    }
    link::Sha256Final(ctx, out);
    return true;
}

void FormatShaHex(const std::uint8_t (&sha)[kShaSize], char (&out)[kShaHexChars + 1]) noexcept {
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < kShaSize; ++i) {
        out[i * 2] = kHex[sha[i] >> 4];
        out[i * 2 + 1] = kHex[sha[i] & 15];
    }
    out[kShaHexChars] = 0;
}

bool ParseShaHex(const char* text, std::size_t size, std::uint8_t (&out)[kShaSize]) noexcept {
    if (!text || size != kShaHexChars) return false;
    std::uint8_t sha[kShaSize];
    if (!HexDigits(text, sha, kShaSize)) return false;
    std::memcpy(out, sha, sizeof(out));
    return true;
}

std::size_t Encode(const Message& m, std::uint8_t* out, std::size_t capacity) noexcept {
    if (!out) return 0;
    if (m.kind == Kind::state ? m.xfer != 0 : m.xfer == 0) return 0;
    std::size_t size = 0;
    switch (m.kind) {
    case Kind::state: {
        const State& s = m.state;
        if (!StateOk(s) || capacity < kStateSize) return 0;
        size = kStateSize;
        std::memset(out, 0, size);
        out[8] = static_cast<std::uint8_t>(s.role);
        out[9] = static_cast<std::uint8_t>(s.phase);
        out[10] = static_cast<std::uint8_t>(s.story);
        out[11] = static_cast<std::uint8_t>(s.mode);
        Put32(out + 12, s.mission);
        Put32(out + 16, s.bound_mission);
        out[20] = s.set_known ? 1 : 0;
        out[21] = s.need;
        out[22] = static_cast<std::uint8_t>(s.elig);
        if (s.set_known) std::memcpy(out + 24, s.set, kShaSize);
        break;
    }
    case Kind::offer: {
        const Offer& o = m.offer;
        if (!OfferOk(o)) return 0;
        size = kOfferBaseSize + kOfferEntrySize * o.n;
        if (capacity < size) return 0;
        std::memset(out, 0, size);
        out[8] = o.n;
        Put32(out + 12, o.total);
        Put32(out + 16, o.bound_mission);
        std::memcpy(out + 20, o.set, kShaSize);
        for (unsigned i = 0; i < o.n; ++i) {
            std::uint8_t* e = out + kOfferBaseSize + kOfferEntrySize * i;
            e[0] = o.files[i].slot;
            Put32(e + 4, o.files[i].size);
            std::memcpy(e + 8, o.files[i].sha256, kShaSize);
        }
        break;
    }
    case Kind::want:
        if (!WantMaskOk(m.want.mask) || capacity < kSmallSize) return 0;
        size = kSmallSize;
        std::memset(out, 0, size);
        out[8] = m.want.mask;
        break;
    case Kind::decline:
        if (!DeclineOk(m.decline.reason) || capacity < kSmallSize) return 0;
        size = kSmallSize;
        std::memset(out, 0, size);
        out[8] = static_cast<std::uint8_t>(m.decline.reason);
        break;
    case Kind::chunk: {
        const Chunk& c = m.chunk;
        if (!ChunkOk(c) || !c.payload) return 0;
        size = kChunkHeaderSize + c.len;
        if (capacity < size) return 0;
        std::memmove(out + kChunkHeaderSize, c.payload, c.len);
        std::memset(out, 0, kChunkHeaderSize);
        out[8] = c.slot;
        Put16(out + 10, c.len);
        Put32(out + 12, c.offset);
        break;
    }
    case Kind::end:
        if (!EndOk(m.end) || capacity < kSmallSize) return 0;
        size = kSmallSize;
        std::memset(out, 0, size);
        Put32(out + 8, m.end.total);
        break;
    case Kind::result:
        if (!ResultOk(m.result) || capacity < kSmallSize) return 0;
        size = kSmallSize;
        std::memset(out, 0, size);
        out[8] = static_cast<std::uint8_t>(m.result.status);
        out[9] = static_cast<std::uint8_t>(m.result.reason);
        break;
    case Kind::abort:
        if (!AbortOk(m.abort.reason) || capacity < kSmallSize) return 0;
        size = kSmallSize;
        std::memset(out, 0, size);
        out[8] = static_cast<std::uint8_t>(m.abort.reason);
        break;
    default:
        return 0;
    }
    PutHeader(out, m.kind, m.xfer);
    return size;
}

Decode DecodeMessage(const std::uint8_t* data, std::size_t size, Message& out) noexcept {
    if (!data || size < kHeaderSize) return Decode::short_;
    if (data[0] != kMagic) return Decode::magic;
    if (data[1] != kVersion) return Decode::version;
    if (data[2] < static_cast<std::uint8_t>(Kind::state) || data[2] > static_cast<std::uint8_t>(Kind::abort) || data[3] != 0)
        return Decode::kind;
    Message m{};
    m.kind = static_cast<Kind>(data[2]);
    m.xfer = Get32(data + 4);
    // Exact sizes first (OFFER and CHUNK need their count / length field).
    switch (m.kind) {
    case Kind::state:
        if (size != kStateSize) return Decode::size;
        break;
    case Kind::offer: {
        if (size < kOfferBaseSize) return Decode::size;
        const std::uint8_t n = data[8];
        if (n == 0 || n > kMaxFiles) return Decode::bounds;
        if (size != kOfferBaseSize + kOfferEntrySize * n) return Decode::size;
        break;
    }
    case Kind::chunk: {
        if (size < kChunkHeaderSize) return Decode::size;
        const std::uint16_t len = Get16(data + 10);
        if (len == 0 || len > kChunk) return Decode::bounds;
        if (size != kChunkHeaderSize + len) return Decode::size;
        break;
    }
    default:
        if (size != kSmallSize) return Decode::size;
        break;
    }
    if (m.kind == Kind::state ? m.xfer != 0 : m.xfer == 0) return Decode::bounds;
    switch (m.kind) {
    case Kind::state: {
        State& s = m.state;
        s.role = static_cast<Role>(data[8]);
        s.phase = static_cast<Phase>(data[9]);
        s.story = static_cast<Story>(data[10]);
        s.mode = static_cast<Mode>(data[11]);
        s.mission = Get32(data + 12);
        s.bound_mission = Get32(data + 16);
        if (data[20] > 1) return Decode::bounds;
        s.set_known = data[20] == 1;
        s.need = data[21];
        s.elig = static_cast<Elig>(data[22]);
        if (data[23] || !AllZero(data + 56, 8)) return Decode::bounds;
        std::memcpy(s.set, data + 24, kShaSize);
        if (!StateOk(s)) return Decode::bounds;
        break;
    }
    case Kind::offer: {
        Offer& o = m.offer;
        if (data[9] || data[10] || data[11]) return Decode::bounds;
        o.n = data[8];
        o.total = Get32(data + 12);
        o.bound_mission = Get32(data + 16);
        std::memcpy(o.set, data + 20, kShaSize);
        for (unsigned i = 0; i < o.n; ++i) {
            const std::uint8_t* e = data + kOfferBaseSize + kOfferEntrySize * i;
            if (e[1] || e[2] || e[3]) return Decode::bounds;
            o.files[i].slot = e[0];
            o.files[i].size = Get32(e + 4);
            std::memcpy(o.files[i].sha256, e + 8, kShaSize);
        }
        if (!OfferOk(o)) return Decode::bounds;
        break;
    }
    case Kind::want:
        if (data[9] || data[10] || data[11]) return Decode::bounds;
        m.want.mask = data[8];
        if (!WantMaskOk(m.want.mask)) return Decode::bounds;
        break;
    case Kind::decline:
        if (data[9] || data[10] || data[11]) return Decode::bounds;
        m.decline.reason = static_cast<DeclineReason>(data[8]);
        if (!DeclineOk(m.decline.reason)) return Decode::bounds;
        break;
    case Kind::chunk: {
        Chunk& c = m.chunk;
        if (data[9]) return Decode::bounds;
        c.slot = data[8];
        c.len = Get16(data + 10);
        c.offset = Get32(data + 12);
        c.payload = data + kChunkHeaderSize;
        if (!ChunkOk(c)) return Decode::bounds;
        break;
    }
    case Kind::end:
        m.end.total = Get32(data + 8);
        if (!EndOk(m.end)) return Decode::bounds;
        break;
    case Kind::result:
        if (data[10] || data[11]) return Decode::bounds;
        m.result.status = static_cast<ResultStatus>(data[8]);
        m.result.reason = static_cast<Fail>(data[9]);
        if (!ResultOk(m.result)) return Decode::bounds;
        break;
    case Kind::abort:
        if (data[9] || data[10] || data[11]) return Decode::bounds;
        m.abort.reason = static_cast<AbortReason>(data[8]);
        if (!AbortOk(m.abort.reason)) return Decode::bounds;
        break;
    }
    out = m;
    return Decode::ok;
}

bool RemoteBuildAtLeast(const char* build, unsigned major_version, unsigned minor_version,
                        unsigned patch_version) noexcept {
    if (!build) return false;
    unsigned parts[3] = {0, 0, 0};
    const char* p = build;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned digits = 0;
        unsigned value = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 9) return false;
            value = value * 10 + static_cast<unsigned>(*p - '0');
            ++p;
        }
        if (!digits) return false;
        parts[i] = value;
        if (i < 2) {
            if (*p != '.') return false;
            ++p;
        }
    }
    // "X.Y.Z" is a prefix: what follows must not continue the patch number
    // (the digit loop consumed every digit), so '+', '-', ' ', '.' or the end.
    if (parts[0] != major_version) return parts[0] > major_version;
    if (parts[1] != minor_version) return parts[1] > minor_version;
    return parts[2] >= patch_version;
}

}  // namespace mgscoop::save_handoff_wire
