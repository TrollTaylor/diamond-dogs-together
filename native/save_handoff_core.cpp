// Original MGS Co-op automatic save handoff: the host/guest core, decisions,
// provenance and JSON documents (join-fix phase 2 P2-7..P2-10;
// docs/save-handoff.md). Pure and deterministic: the caller supplies the
// clock, executes the Actions and reports their results.
#include "mgscoop/save_handoff_core.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mgscoop::save_handoff_core {
namespace {

using save_handoff_state::LuaCode;
using save_handoff_state::Toast;
using save_handoff_state::Token;
using Sha = std::uint8_t[wire::kShaSize];

bool SameSet(const Sha& a, const Sha& b) noexcept { return std::memcmp(a, b, wire::kShaSize) == 0; }
void CopySet(Sha& out, const Sha& in) noexcept { std::memcpy(out, in, wire::kShaSize); }
std::uint8_t U8(Token token) noexcept { return static_cast<std::uint8_t>(token); }
template <typename E>
std::uint8_t U8(E value) noexcept {
    return static_cast<std::uint8_t>(value);
}
// Machine::toasted for continue_unknown while the host settles in game (G11 /
// H8 instead of G4 / H3); not a wire::Elig value.
constexpr std::uint8_t kSettleToast = 0x40;
std::uint32_t SettleSeconds() noexcept { return static_cast<std::uint32_t>((kSettleBindMs + 999) / 1000); }

void CopyText(char* out, std::size_t capacity, const char* text) noexcept {
    if (!capacity) return;
    std::size_t n = 0;
    if (text)
        for (; text[n] && n + 1 < capacity; ++n) {
            const auto c = static_cast<unsigned char>(text[n]);
            out[n] = (c < 0x20 || c >= 0x7F) ? '?' : static_cast<char>(c);
        }
    out[n] = 0;
}

std::uint8_t LuaFor(Token token) noexcept {
    switch (token) {
    case Token::wait_host: return U8(LuaCode::wait_host);
    case Token::receiving: return U8(LuaCode::receiving);
    case Token::staged:
    case Token::countdown: return U8(LuaCode::staged);
    case Token::exiting: return U8(LuaCode::exiting);
    case Token::same:
    case Token::kept:
    case Token::applied: return U8(LuaCode::kept);
    case Token::refused:
    case Token::failed: return U8(LuaCode::failed);
    default: return U8(LuaCode::none);
    }
}

wire::AbortReason AbortFor(wire::Fail fail) noexcept {
    switch (fail) {
    case wire::Fail::stall: return wire::AbortReason::stall;
    case wire::Fail::link_lost: return wire::AbortReason::link_lost;
    case wire::Fail::snapshot_changed: return wire::AbortReason::snapshot_changed;
    case wire::Fail::not_title: return wire::AbortReason::not_title;
    case wire::Fail::shutdown: return wire::AbortReason::shutdown;
    default: return wire::AbortReason::protocol;
    }
}
wire::Fail FailFor(wire::AbortReason reason) noexcept {
    switch (reason) {
    case wire::AbortReason::link_lost: return wire::Fail::link_lost;
    case wire::AbortReason::stall: return wire::Fail::stall;
    case wire::AbortReason::snapshot_changed: return wire::Fail::snapshot_changed;
    case wire::AbortReason::not_title: return wire::Fail::not_title;
    case wire::AbortReason::shutdown: return wire::Fail::shutdown;
    case wire::AbortReason::protocol: return wire::Fail::protocol;
    default: return wire::Fail::partner;
    }
}

bool SameState(const wire::State& a, const wire::State& b) noexcept {
    return a.role == b.role && a.phase == b.phase && a.story == b.story && a.mode == b.mode && a.mission == b.mission &&
           a.bound_mission == b.bound_mission && a.set_known == b.set_known && a.need == b.need && a.elig == b.elig &&
           SameSet(a.set, b.set);
}
bool SameView(const save_handoff_state::View& a, const save_handoff_state::View& b) noexcept {
    return a.available == b.available && a.role == b.role && a.token == b.token && a.pct == b.pct && a.why == b.why &&
           a.lua_code == b.lua_code && a.host_mission == b.host_mission && a.bytes == b.bytes && a.can_keep == b.can_keep;
}

// ---- JSON (bounded, ASCII). Writer: compact output, fails closed.
class Writer {
public:
    Writer(char* out, std::size_t capacity) noexcept : out_(out), capacity_(capacity), ok_(out && capacity) {}
    void Put(char c) noexcept {
        if (!ok_) return;
        if (size_ + 1 >= capacity_) {
            ok_ = false;
            return;
        }
        out_[size_++] = c;
    }
    void Raw(const char* text) noexcept {
        while (*text) Put(*text++);
    }
    void Str(const char* text) noexcept {
        Put('"');
        for (; *text; ++text) {
            const auto c = static_cast<unsigned char>(*text);
            if (c == '"' || c == '\\') {
                Put('\\');
                Put(static_cast<char>(c));
            } else {
                Put((c < 0x20 || c >= 0x7F) ? '?' : static_cast<char>(c));
            }
        }
        Put('"');
    }
    void U(std::uint64_t value) noexcept {
        char digits[24];
        unsigned n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);
        while (n) Put(digits[--n]);
    }
    void Sha256(const Sha& sha) noexcept {
        char hex[wire::kShaHexChars + 1];
        wire::FormatShaHex(sha, hex);
        Put('"');
        Raw(hex);
        Put('"');
    }
    void X8(std::uint32_t value) noexcept {
        static constexpr char kHex[] = "0123456789ABCDEF";
        Put('"');
        for (int i = 7; i >= 0; --i) Put(kHex[(value >> (i * 4)) & 15u]);
        Put('"');
    }
    std::size_t Done() noexcept {
        if (!ok_) {
            if (out_ && capacity_) out_[0] = 0;
            return 0;
        }
        out_[size_] = 0;
        return size_;
    }

private:
    char* out_;
    std::size_t capacity_;
    std::size_t size_ = 0;
    bool ok_;
};

// Reader: a strict subset of JSON (ASCII strings with the common escapes and
//  ..~, unsigned integers, nested values skipped). A UTF-8 BOM and
// trailing whitespace or NULs are accepted (PowerShell writers).
class Reader {
public:
    Reader(const char* text, std::size_t size) noexcept : p_(text), end_(text ? text + size : text) {
        if (p_ && end_ - p_ >= 3 && static_cast<unsigned char>(p_[0]) == 0xEF && static_cast<unsigned char>(p_[1]) == 0xBB &&
            static_cast<unsigned char>(p_[2]) == 0xBF)
            p_ += 3;
    }
    bool Valid() const noexcept { return p_ != nullptr; }
    void Ws() noexcept {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n')) ++p_;
    }
    bool Lit(char c) noexcept {
        Ws();
        if (p_ < end_ && *p_ == c) {
            ++p_;
            return true;
        }
        return false;
    }
    // A string into out[capacity] (nullptr skips). A longer string is still
    // consumed: *overflow is set (or the call fails when overflow is nullptr).
    bool Str(char* out, std::size_t capacity, bool* overflow) noexcept {
        if (!Lit('"')) return false;
        std::size_t n = 0;
        bool over = false;
        while (p_ < end_) {
            auto c = static_cast<unsigned char>(*p_++);
            if (c == '"') {
                if (out && capacity) out[n < capacity ? n : capacity - 1] = 0;
                if (overflow) *overflow = over;
                return !over || overflow;
            }
            if (c < 0x20 || c >= 0x7F) return false;
            if (c == '\\') {
                if (p_ >= end_) return false;
                const char e = *p_++;
                if (e == '"' || e == '\\' || e == '/') {
                    c = static_cast<unsigned char>(e);
                } else if (e == 'u') {
                    if (end_ - p_ < 4) return false;
                    unsigned v = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = *p_++;
                        unsigned d;
                        if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
                        else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
                        else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
                        else return false;
                        v = (v << 4) | d;
                    }
                    if (v < 0x20 || v >= 0x7F) return false;
                    c = static_cast<unsigned char>(v);
                } else {
                    return false;  // control characters never appear in these documents
                }
            }
            if (out) {
                if (n + 1 < capacity) out[n] = static_cast<char>(c);
                else over = true;
            }
            ++n;
        }
        return false;
    }
    bool Uint(std::uint64_t& value) noexcept {
        Ws();
        if (p_ >= end_ || *p_ < '0' || *p_ > '9') return false;
        if (*p_ == '0' && end_ - p_ > 1 && p_[1] >= '0' && p_[1] <= '9') return false;
        std::uint64_t v = 0;
        unsigned digits = 0;
        while (p_ < end_ && *p_ >= '0' && *p_ <= '9') {
            if (++digits > 15) return false;
            v = v * 10 + static_cast<unsigned>(*p_++ - '0');
        }
        if (p_ < end_ && (*p_ == '.' || *p_ == 'e' || *p_ == 'E')) return false;
        value = v;
        return true;
    }
    bool Skip(unsigned depth = 0) noexcept {
        Ws();
        if (p_ >= end_ || depth > 16) return false;
        const char c = *p_;
        if (c == '"') return Str(nullptr, 0, nullptr);
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++p_;
            if (Lit(close)) return true;
            for (;;) {
                if (c == '{') {
                    if (!Str(nullptr, 0, nullptr) || !Lit(':')) return false;
                }
                if (!Skip(depth + 1)) return false;
                if (Lit(',')) continue;
                return Lit(close);
            }
        }
        if (c == 't') return Word("true");
        if (c == 'f') return Word("false");
        if (c == 'n') return Word("null");
        if (c == '-' || (c >= '0' && c <= '9')) {
            if (*p_ == '-') ++p_;
            const char* start = p_;
            while (p_ < end_ && ((*p_ >= '0' && *p_ <= '9') || *p_ == '.' || *p_ == 'e' || *p_ == 'E' || *p_ == '+' || *p_ == '-'))
                ++p_;
            return p_ != start;
        }
        return false;
    }
    bool AtEnd() noexcept {
        Ws();
        while (p_ < end_ && *p_ == 0) ++p_;
        return p_ == end_;
    }

private:
    bool Word(const char* word) noexcept {
        for (; *word; ++word) {
            if (p_ >= end_ || *p_ != *word) return false;
            ++p_;
        }
        return true;
    }
    const char* p_;
    const char* end_;
};

template <typename F>
bool Members(Reader& r, F&& member) noexcept {
    if (!r.Lit('{')) return false;
    if (r.Lit('}')) return true;
    for (;;) {
        char key[40];
        bool over = false;
        if (!r.Str(key, sizeof(key), &over) || !r.Lit(':')) return false;
        if (over) {
            if (!r.Skip()) return false;
        } else if (!member(key, r)) {
            return false;
        }
        if (r.Lit(',')) continue;
        return r.Lit('}');
    }
}
template <typename F>
bool Elements(Reader& r, F&& element) noexcept {
    if (!r.Lit('[')) return false;
    if (r.Lit(']')) return true;
    for (;;) {
        if (!element(r)) return false;
        if (r.Lit(',')) continue;
        return r.Lit(']');
    }
}
bool ReadExact(Reader& r, const char* expected) noexcept {
    char text[48];
    return r.Str(text, sizeof(text), nullptr) && std::strcmp(text, expected) == 0;
}
bool ReadV1(Reader& r) noexcept {
    std::uint64_t v = 0;
    return r.Uint(v) && v == 1;
}
bool ReadSha(Reader& r, Sha& out) noexcept {
    char text[wire::kShaHexChars + 2];
    return r.Str(text, sizeof(text), nullptr) && wire::ParseShaHex(text, std::strlen(text), out);
}
bool ReadX8(Reader& r, std::uint32_t& out) noexcept {
    char text[10];
    if (!r.Str(text, sizeof(text), nullptr) || std::strlen(text) != 8) return false;
    std::uint32_t v = 0;
    for (int i = 0; i < 8; ++i) {
        const char h = text[i];
        std::uint32_t d;
        if (h >= '0' && h <= '9') d = static_cast<std::uint32_t>(h - '0');
        else if (h >= 'A' && h <= 'F') d = static_cast<std::uint32_t>(h - 'A' + 10);
        else if (h >= 'a' && h <= 'f') d = static_cast<std::uint32_t>(h - 'a' + 10);
        else return false;
        v = (v << 4) | d;
    }
    if (!v) return false;
    out = v;
    return true;
}
// A non-empty printable string that fits (no overflow).
bool ReadText(Reader& r, char* out, std::size_t capacity) noexcept {
    return r.Str(out, capacity, nullptr) && out[0] != 0;
}
bool ReadU32(Reader& r, std::uint32_t& out, std::uint64_t low, std::uint64_t high) noexcept {
    std::uint64_t v = 0;
    if (!r.Uint(v) || v < low || v > high) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}
// An aside folder leaf: printable, no separators or wildcards, not "." / "..".
bool LeafOk(const char* leaf) noexcept {
    if (!leaf[0] || std::strcmp(leaf, ".") == 0 || std::strcmp(leaf, "..") == 0) return false;
    for (const char* p = leaf; *p; ++p)
        if (std::strchr("\\/:*?\"<>|", *p)) return false;
    return true;
}
std::size_t Refuse(char* out, std::size_t capacity) noexcept {
    if (out && capacity) out[0] = 0;
    return 0;
}
int SlotForEntry(const char* entry) noexcept {
    for (std::uint8_t slot = 0; slot < wire::kSlots; ++slot)
        if (std::strcmp(entry, wire::SlotEntry(slot)) == 0) return slot;
    return -1;
}

}  // namespace

// ---- Scenes.
std::uint8_t Loc(std::uint32_t mission) noexcept {
    switch (mission) {
    case 30010:
    case 40010: return 10;
    case 30020:
    case 40020: return 20;
    case 30050:
    case 40050: return 50;
    default: return 0;
    }
}
bool SettleMission(std::uint32_t mission) noexcept { return mission == 30010 || mission == 30020 || mission == 30050; }

// ---- Decisions.
wire::Elig HostEligible(const HostFacts& f) noexcept {
    if (f.mode == wire::Mode::off) return wire::Elig::off;
    if (!f.report_fresh || f.story == wire::Story::unknown) return wire::Elig::story_unknown;
    if (f.story != wire::Story::open) return wire::Elig::story_opening;
    if (f.unexpected) return wire::Elig::unexpected_files;
    if (f.total > wire::kMaxTotal) return wire::Elig::too_large;
    if (f.saving) return wire::Elig::saving;
    if (!f.set_known || f.bound_mission == 0) return wire::Elig::continue_unknown;
    if (!Supported(f.bound_mission)) return wire::Elig::continue_unsupported;
    return wire::Elig::eligible;
}

const char* NeedWhyName(NeedWhy why) noexcept {
    switch (why) {
    case NeedWhy::off: return "off";
    case NeedWhy::same: return "same";
    case NeedWhy::same_scene: return "same_scene";
    case NeedWhy::story: return "story";
    case NeedWhy::unsupported: return "unsupported";
    case NeedWhy::location: return "location";
    case NeedWhy::always: return "always";
    }
    return "?";
}

std::uint8_t GuestNeed(wire::Mode policy, const SideFacts& own, const SideFacts& host, NeedWhy& why) noexcept {
    if (policy == wire::Mode::off) {
        why = NeedWhy::off;
        return 0;
    }
    if (own.set_known && host.set_known && SameSet(own.set, host.set)) {
        why = NeedWhy::same;
        return 0;
    }
    if (policy == wire::Mode::always) {
        why = NeedWhy::always;
        return 1;
    }
    if (own.story != wire::Story::open) {
        why = NeedWhy::story;
        return 1;
    }
    if (own.bound_mission != 0 && !Supported(own.bound_mission)) {
        why = NeedWhy::unsupported;
        return 1;
    }
    const std::uint32_t host_where = host.bound_mission ? host.bound_mission : host.mission;
    if (Loc(own.mission) != Loc(host_where)) {
        why = NeedWhy::location;
        return 1;
    }
    why = NeedWhy::same_scene;
    return 0;
}

bool ExitGate(const ExitFacts& f) noexcept {
    if (f.mode != wire::Mode::scene && f.mode != wire::Mode::always) return false;
    if (!f.staged || !f.request_written || !f.result_sent) return false;
    if (!f.report_fresh || !f.title || f.saving || f.ingame) return false;
    if (f.partner_mission || f.terminating || f.declined) return false;
    return f.title_held_ms >= kCountdownMs;
}

const char* SourceName(Source source) noexcept {
    switch (source) {
    case Source::continue_: return "continue";
    case Source::settle: return "settle";
    default: return "none";
    }
}

const char* ActionName(ActionKind kind) noexcept {
    switch (kind) {
    case ActionKind::none: return "none";
    case ActionKind::send: return "send";
    case ActionKind::hash_set: return "hash_set";
    case ActionKind::snapshot: return "snapshot";
    case ActionKind::release_snapshot: return "release_snapshot";
    case ActionKind::stage_write: return "stage_write";
    case ActionKind::write_manifest: return "write_manifest";
    case ActionKind::commit_stage: return "commit_stage";
    case ActionKind::write_request: return "write_request";
    case ActionKind::write_rejoin: return "write_rejoin";
    case ActionKind::write_provenance: return "write_provenance";
    case ActionKind::delete_stage: return "delete_stage";
    case ActionKind::post_exit: return "post_exit";
    case ActionKind::toast: return "toast";
    case ActionKind::log: return "log";
    case ActionKind::publish: return "publish";
    }
    return "?";
}

// ---- Provenance.
void Provenance::Bind(const std::uint8_t (&set)[wire::kShaSize], std::uint32_t mission, Source source,
                      const char* utc) noexcept {
    unsigned keep = count_;
    for (unsigned i = 0; i < count_; ++i)
        if (SameSet(entries_[i].set, set)) {
            keep = i;
            break;
        }
    if (keep == count_ && count_ == kProvenanceEntries) keep = count_ - 1;  // drop the least recently bound
    // Shift [0, keep) down one place; slot 0 takes the new binding.
    for (unsigned i = keep; i > 0; --i) entries_[i] = entries_[i - 1];
    if (keep == count_) ++count_;
    ProvenanceEntry& e = entries_[0];
    CopySet(e.set, set);
    e.mission = mission;
    e.source = source;
    CopyText(e.utc, sizeof(e.utc), utc);
}

const ProvenanceEntry* Provenance::Find(const std::uint8_t (&set)[wire::kShaSize]) const noexcept {
    for (unsigned i = 0; i < count_; ++i)
        if (SameSet(entries_[i].set, set)) return &entries_[i];
    return nullptr;
}
unsigned Provenance::Count() const noexcept { return count_; }
const ProvenanceEntry& Provenance::At(unsigned index) const noexcept {
    return entries_[index < count_ ? index : 0];
}
void Provenance::Clear() noexcept {
    for (auto& e : entries_) e = ProvenanceEntry{};
    count_ = 0;
}

std::size_t Provenance::Format(char* out, std::size_t capacity) const noexcept {
    Writer w(out, capacity);
    w.Raw("{\"format\":\"mgs-coop-save-provenance\",\"v\":1,\"entries\":[");
    for (unsigned i = 0; i < count_; ++i) {
        const ProvenanceEntry& e = entries_[i];
        if (i) w.Put(',');
        w.Raw("{\"set\":");
        w.Sha256(e.set);
        w.Raw(",\"mission\":");
        w.U(e.mission);
        w.Raw(",\"source\":");
        w.Str(SourceName(e.source));
        w.Raw(",\"utc\":");
        w.Str(e.utc);
        w.Put('}');
    }
    w.Raw("]}");
    return w.Done();
}

bool Provenance::Parse(const char* text, std::size_t size) noexcept {
    Clear();
    Reader r(text, size);
    if (!r.Valid()) return false;
    ProvenanceEntry parsed[kProvenanceEntries]{};
    unsigned n = 0;
    bool format = false, v = false, entries = false, too_many = false;
    const bool ok = Members(r, [&](const char* key, Reader& in) noexcept {
        if (!std::strcmp(key, "format")) return format = ReadExact(in, "mgs-coop-save-provenance");
        if (!std::strcmp(key, "v")) return v = ReadV1(in);
        if (std::strcmp(key, "entries")) return in.Skip();
        entries = true;
        return Elements(in, [&](Reader& el) noexcept {
            ProvenanceEntry e{};
            bool has_set = false, has_mission = false, has_source = false, has_utc = false;
            if (!Members(el, [&](const char* k, Reader& m) noexcept {
                    if (!std::strcmp(k, "set")) return has_set = ReadSha(m, e.set);
                    if (!std::strcmp(k, "mission")) return has_mission = ReadU32(m, e.mission, 1, wire::kMaxMission);
                    if (!std::strcmp(k, "source")) {
                        char source[16];
                        if (!m.Str(source, sizeof(source), nullptr)) return false;
                        if (!std::strcmp(source, "continue")) e.source = Source::continue_;
                        else if (!std::strcmp(source, "settle")) e.source = Source::settle;
                        else return false;
                        return has_source = true;
                    }
                    if (!std::strcmp(k, "utc")) return has_utc = ReadText(m, e.utc, sizeof(e.utc));
                    return m.Skip();
                }))
                return false;
            if (!has_set || !has_mission || !has_source || !has_utc) return false;
            for (unsigned i = 0; i < n; ++i)
                if (SameSet(parsed[i].set, e.set)) return true;  // an older duplicate: the first (most recent) wins
            if (n == kProvenanceEntries) {
                too_many = true;
                return false;
            }
            parsed[n++] = e;
            return true;
        });
    });
    if (!ok || too_many || !format || !v || !entries || !r.AtEnd()) return false;
    for (unsigned i = 0; i < n; ++i) entries_[i] = parsed[i];
    count_ = n;
    return true;
}

// ---- Staging documents.
std::size_t FormatManifest(const Manifest& m, char* out, std::size_t capacity) noexcept {
    Writer w(out, capacity);
    if (m.n == 0 || m.n > wire::kMaxFiles || m.xfer == 0) return Refuse(out, capacity);
    w.Raw("{\"format\":\"mgs-coop-save-handoff\",\"v\":1,\"xfer\":");
    w.X8(m.xfer);
    w.Raw(",\"created_utc\":");
    w.Str(m.created_utc);
    w.Raw(",\"host_build\":");
    w.Str(m.host_build);
    w.Raw(",\"host_mission\":");
    w.U(m.host_mission);
    w.Raw(",\"set_sha256\":");
    w.Sha256(m.set);
    w.Raw(",\"files\":[");
    for (unsigned i = 0; i < m.n; ++i) {
        const wire::FileEntry& f = m.files[i];
        const char* entry = wire::SlotEntry(f.slot);
        if (!entry) return Refuse(out, capacity);
        if (i) w.Put(',');
        w.Raw("{\"entry\":");
        w.Str(entry);
        w.Raw(",\"bytes\":");
        w.U(f.size);
        w.Raw(",\"sha256\":");
        w.Sha256(f.sha256);
        w.Put('}');
    }
    w.Raw("]}");
    return w.Done();
}

bool ParseManifest(const char* text, std::size_t size, Manifest& out) noexcept {
    Reader r(text, size);
    if (!r.Valid()) return false;
    Manifest m{};
    bool format = false, v = false, xfer = false, created = false, build = false, mission = false, set = false, files = false;
    bool slot_seen[wire::kSlots] = {};
    wire::FileEntry list[wire::kMaxFiles]{};
    unsigned n = 0;
    const bool ok = Members(r, [&](const char* key, Reader& in) noexcept {
        if (!std::strcmp(key, "format")) return format = ReadExact(in, "mgs-coop-save-handoff");
        if (!std::strcmp(key, "v")) return v = ReadV1(in);
        if (!std::strcmp(key, "xfer")) return xfer = ReadX8(in, m.xfer);
        if (!std::strcmp(key, "created_utc")) return created = ReadText(in, m.created_utc, sizeof(m.created_utc));
        if (!std::strcmp(key, "host_build")) return build = in.Str(m.host_build, sizeof(m.host_build), nullptr);
        if (!std::strcmp(key, "host_mission")) return mission = ReadU32(in, m.host_mission, 0, wire::kMaxMission);
        if (!std::strcmp(key, "set_sha256")) return set = ReadSha(in, m.set);
        if (std::strcmp(key, "files")) return in.Skip();
        files = true;
        return Elements(in, [&](Reader& el) noexcept {
            wire::FileEntry f{};
            int slot = -1;
            bool has_bytes = false, has_sha = false;
            if (!Members(el, [&](const char* k, Reader& field) noexcept {
                    if (!std::strcmp(k, "entry")) {
                        char entry[40];
                        if (!field.Str(entry, sizeof(entry), nullptr)) return false;
                        slot = SlotForEntry(entry);
                        return slot >= 0;
                    }
                    if (!std::strcmp(k, "bytes")) return has_bytes = ReadU32(field, f.size, 1, wire::kMaxFileBytes);
                    if (!std::strcmp(k, "sha256")) return has_sha = ReadSha(field, f.sha256);
                    return field.Skip();
                }))
                return false;
            if (slot < 0 || !has_bytes || !has_sha || n == wire::kMaxFiles || slot_seen[slot]) return false;
            slot_seen[slot] = true;
            f.slot = static_cast<std::uint8_t>(slot);
            list[n++] = f;
            return true;
        });
    });
    if (!ok || !format || !v || !xfer || !created || !build || !mission || !set || !files || !r.AtEnd()) return false;
    if (n == 0 || (!slot_seen[0] && !slot_seen[1])) return false;
    // Ascending slot order (the writer's order; other writers are accepted).
    unsigned k = 0;
    std::uint64_t total = 0;
    for (std::uint8_t slot = 0; slot < wire::kSlots; ++slot)
        for (unsigned i = 0; i < n; ++i)
            if (list[i].slot == slot) {
                m.files[k++] = list[i];
                total += list[i].size;
            }
    m.n = static_cast<std::uint8_t>(n);
    if (total > wire::kMaxTotal) return false;
    Sha computed;
    if (!wire::SetSha256(m.files, m.n, computed) || !SameSet(computed, m.set)) return false;
    out = m;
    return true;
}

std::size_t FormatRequest(const Request& q, char* out, std::size_t capacity) noexcept {
    Writer w(out, capacity);
    if (q.xfer == 0 || q.rejoin > 1 || (q.mode != wire::Mode::scene && q.mode != wire::Mode::always))
        return Refuse(out, capacity);
    w.Raw("{\"format\":\"mgs-coop-save-handoff-request\",\"v\":1,\"pid\":");
    w.U(q.pid);
    w.Raw(",\"process_start_utc\":");
    w.Str(q.process_start_utc);
    w.Raw(",\"xfer\":");
    w.X8(q.xfer);
    w.Raw(",\"set_sha256\":");
    w.Sha256(q.set);
    w.Raw(",\"mode\":");
    w.Str(wire::ModeName(q.mode));
    w.Raw(",\"rejoin\":");
    w.U(q.rejoin);
    w.Raw(",\"created_utc\":");
    w.Str(q.created_utc);
    w.Put('}');
    return w.Done();
}

bool ParseRequest(const char* text, std::size_t size, Request& out) noexcept {
    Reader r(text, size);
    if (!r.Valid()) return false;
    Request q{};
    bool format = false, v = false, pid = false, start = false, xfer = false, set = false, mode = false, rejoin = false,
         created = false;
    const bool ok = Members(r, [&](const char* key, Reader& in) noexcept {
        if (!std::strcmp(key, "format")) return format = ReadExact(in, "mgs-coop-save-handoff-request");
        if (!std::strcmp(key, "v")) return v = ReadV1(in);
        if (!std::strcmp(key, "pid")) return pid = ReadU32(in, q.pid, 0, 0xFFFFFFFFu);
        if (!std::strcmp(key, "process_start_utc"))
            return start = ReadText(in, q.process_start_utc, sizeof(q.process_start_utc));
        if (!std::strcmp(key, "xfer")) return xfer = ReadX8(in, q.xfer);
        if (!std::strcmp(key, "set_sha256")) return set = ReadSha(in, q.set);
        if (!std::strcmp(key, "mode")) {
            char text_mode[16];
            if (!in.Str(text_mode, sizeof(text_mode), nullptr)) return false;
            if (!std::strcmp(text_mode, "scene")) q.mode = wire::Mode::scene;
            else if (!std::strcmp(text_mode, "always")) q.mode = wire::Mode::always;
            else return false;
            return mode = true;
        }
        if (!std::strcmp(key, "rejoin")) {
            std::uint32_t value = 0;
            if (!ReadU32(in, value, 0, 1)) return false;
            q.rejoin = static_cast<std::uint8_t>(value);
            return rejoin = true;
        }
        if (!std::strcmp(key, "created_utc")) return created = ReadText(in, q.created_utc, sizeof(q.created_utc));
        return in.Skip();
    });
    if (!ok || !format || !v || !pid || !start || !xfer || !set || !mode || !rejoin || !created || !r.AtEnd()) return false;
    out = q;
    return true;
}

std::size_t FormatApplied(const Applied& a, char* out, std::size_t capacity) noexcept {
    Writer w(out, capacity);
    if (!LeafOk(a.aside)) return Refuse(out, capacity);
    w.Raw("{\"format\":\"mgs-coop-save-handoff-applied\",\"v\":1,\"set_sha256\":");
    w.Sha256(a.set);
    w.Raw(",\"applied_utc\":");
    w.Str(a.applied_utc);
    w.Raw(",\"aside\":");
    w.Str(a.aside);
    w.Put('}');
    return w.Done();
}

bool ParseApplied(const char* text, std::size_t size, Applied& out) noexcept {
    Reader r(text, size);
    if (!r.Valid()) return false;
    Applied a{};
    bool format = false, v = false, set = false, utc = false, aside = false;
    const bool ok = Members(r, [&](const char* key, Reader& in) noexcept {
        if (!std::strcmp(key, "format")) return format = ReadExact(in, "mgs-coop-save-handoff-applied");
        if (!std::strcmp(key, "v")) return v = ReadV1(in);
        if (!std::strcmp(key, "set_sha256")) return set = ReadSha(in, a.set);
        if (!std::strcmp(key, "applied_utc")) return utc = ReadText(in, a.applied_utc, sizeof(a.applied_utc));
        if (!std::strcmp(key, "aside")) return aside = ReadText(in, a.aside, sizeof(a.aside)) && LeafOk(a.aside);
        return in.Skip();
    });
    if (!ok || !format || !v || !set || !utc || !aside || !r.AtEnd()) return false;
    out = a;
    return true;
}

// ---- Core plumbing.
void Core::Reset(const Config& config) noexcept {
    config_ = config;
    config_.process_start_utc[kUtcCapacity - 1] = 0;
    view_ = save_handoff_state::View{};
    published_ = save_handoff_state::View{};
    published_once_ = false;
    provenance_.Clear();
    manifest_ = Manifest{};
    request_ = Request{};
    stats_ = Stats{};
    m_ = Machine{};
    head_ = count_ = 0;
    awaiting_ = ActionKind::none;
    awaiting_xfer_ = 0;
    const bool on = config_.role != wire::Role::none && config_.mode != wire::Mode::off;
    view_.available = on;
    view_.role = U8(config_.role);
    SetToken(on ? Token::idle : Token::off, 0);
    Flush();
}

Action* Core::Emit(ActionKind kind) noexcept {
    if (count_ >= kActionCapacity) {
        ++stats_.dropped_actions;
        return nullptr;
    }
    Action& a = ring_[(head_ + count_) % kActionCapacity];
    ++count_;
    a.kind = kind;
    a.xfer = 0;
    a.message = wire::Kind::state;
    a.latest = false;
    a.slot = 0;
    a.offset = 0;
    a.size = 0;
    a.rejoin = 0;
    a.code = 0;
    a.a = a.b = 0;
    a.view = save_handoff_state::View{};
    a.text[0] = 0;
    return &a;
}

bool Core::PopAction(Action& out) noexcept {
    if (!count_) return false;
    out = ring_[head_];
    head_ = (head_ + 1) % kActionCapacity;
    --count_;
    switch (out.kind) {
    case ActionKind::stage_write:
    case ActionKind::write_manifest:
    case ActionKind::commit_stage:
    case ActionKind::write_request:
    case ActionKind::delete_stage:
        awaiting_ = out.kind;
        awaiting_xfer_ = out.xfer;
        break;
    default: break;
    }
    return true;
}

void Core::Log(const char* format, ...) noexcept {
    Action* a = Emit(ActionKind::log);
    if (!a) return;
    va_list args;
    va_start(args, format);
    std::vsnprintf(a->text, sizeof(a->text), format, args);
    va_end(args);
}

void Core::PostToast(Toast code, std::uint32_t a, std::uint32_t b) noexcept {
    Action* t = Emit(ActionKind::toast);
    if (!t) return;
    t->code = static_cast<std::uint8_t>(code);
    t->a = a;
    t->b = b;
}

bool Core::Send(const wire::Message& message) noexcept {
    if (!m_.build_ok || m_.shutdown) return false;
    std::uint8_t buffer[wire::kMaxMessage];
    const std::size_t size = wire::Encode(message, buffer, sizeof(buffer));
    if (!size) return false;
    Action* a = Emit(ActionKind::send);
    if (!a) return false;
    std::memcpy(a->data, buffer, size);
    a->size = static_cast<std::uint16_t>(size);
    a->message = message.kind;
    // latest would replace a still-unsent CHUNK/OFFER/... queued before it.
    a->latest = message.kind == wire::Kind::state && m_.last_send_state;
    m_.last_send_state = message.kind == wire::Kind::state;
    a->xfer = message.xfer;
    if (message.kind == wire::Kind::chunk) {
        a->slot = message.chunk.slot;
        a->offset = message.chunk.offset;
    }
    return true;
}

void Core::SendSmall(wire::Kind kind, std::uint32_t xfer, std::uint8_t value, std::uint8_t reason) noexcept {
    wire::Message m{};
    m.kind = kind;
    m.xfer = xfer;
    switch (kind) {
    case wire::Kind::want: m.want.mask = value; break;
    case wire::Kind::decline: m.decline.reason = static_cast<wire::DeclineReason>(value); break;
    case wire::Kind::result:
        m.result.status = static_cast<wire::ResultStatus>(value);
        m.result.reason = static_cast<wire::Fail>(reason);
        break;
    case wire::Kind::abort: m.abort.reason = static_cast<wire::AbortReason>(value); break;
    default: return;
    }
    Send(m);
}

void Core::SetToken(Token token, std::uint8_t why) noexcept {
    view_.token = U8(token);
    view_.why = why;
}

void Core::Flush() noexcept {
    const bool guest = config_.role == wire::Role::guest;
    const auto token = static_cast<Token>(view_.token);
    view_.lua_code = guest ? LuaFor(token) : 0;
    view_.can_keep = guest && !m_.declined &&
                     (token == Token::wait_host || token == Token::receiving || token == Token::staged ||
                      token == Token::countdown);
    view_.travel_unlocked = false;
    if (published_once_ && SameView(view_, published_)) return;
    Action* a = Emit(ActionKind::publish);
    if (!a) return;
    a->view = view_;
    published_ = view_;
    published_once_ = true;
}

void Core::UtcNow(char (&out)[kUtcCapacity]) const noexcept {
    out[0] = 0;
    if (config_.utc_now) config_.utc_now(out);
    out[kUtcCapacity - 1] = 0;
    if (!out[0]) CopyText(out, kUtcCapacity, "1970-01-01T00:00:00Z");
}

std::uint32_t Core::NextXfer() noexcept {
    for (int tries = 0; tries < 8 && config_.random_u32; ++tries) {
        const std::uint32_t v = config_.random_u32();
        if (v && v != m_.xfer) return v;
    }
    for (;;) {
        m_.xfer_seq = m_.xfer_seq * 1664525u + 1013904223u;
        if (m_.xfer_seq && m_.xfer_seq != m_.xfer) return m_.xfer_seq;
    }
}

bool Core::Fresh(std::uint64_t now_ms, std::uint64_t limit_ms) const noexcept {
    if (!m_.have_report) return false;
    return now_ms <= m_.report.at_ms || now_ms - m_.report.at_ms <= limit_ms;
}

std::uint32_t Core::OwnBound() const noexcept {
    if (!m_.set_known) return 0;
    const ProvenanceEntry* e = provenance_.Find(m_.set.set);
    return e ? e->mission : 0;
}

wire::State Core::BuildState(std::uint64_t now_ms) const noexcept {
    wire::State s{};
    s.role = config_.role;
    const bool fresh = Fresh(now_ms, kPhaseFreshMs);
    const Report& r = m_.report;
    s.phase = !fresh ? wire::Phase::unknown
              : r.title ? wire::Phase::title
              : r.ingame ? wire::Phase::ingame
                         : wire::Phase::loading;
    s.story = fresh && U8(r.story) <= U8(wire::Story::open) ? r.story : wire::Story::unknown;
    s.mode = config_.mode;
    s.mission = m_.have_report && r.mission <= wire::kMaxMission ? r.mission : 0;
    s.bound_mission = OwnBound();
    s.set_known = m_.set_known;
    if (m_.set_known) CopySet(s.set, m_.set.set);
    s.need = config_.role == wire::Role::guest && m_.need_known && !m_.declined ? m_.need : 0;
    s.elig = config_.role == wire::Role::host ? m_.elig : wire::Elig::unknown;
    return s;
}

void Core::Bind(const std::uint8_t (&set)[wire::kShaSize], std::uint32_t mission, Source source) noexcept {
    if (mission == 0 || mission > wire::kMaxMission) return;
    const ProvenanceEntry* e = provenance_.Find(set);
    if (e && e->mission == mission) return;  // known: no churn
    char utc[kUtcCapacity];
    UtcNow(utc);
    provenance_.Bind(set, mission, source, utc);
    char hex[wire::kShaHexChars + 1];
    wire::FormatShaHex(set, hex);
    Log("save_handoff=provenance; set=%.8s; mission=%u; source=%s", hex, mission, SourceName(source));
    Emit(ActionKind::write_provenance);
}

void Core::CountOffer(const std::uint8_t (&set)[wire::kShaSize]) noexcept {
    if (!SameSet(m_.offers_set, set)) {
        CopySet(m_.offers_set, set);
        m_.offers = 0;
    }
    ++m_.offers;
}
unsigned Core::OffersFor(const std::uint8_t (&set)[wire::kShaSize]) const noexcept {
    return SameSet(m_.offers_set, set) ? m_.offers : 0;
}

// ---- Inputs.
void Core::OnLua(const Report& report, std::uint64_t now_ms) noexcept {
    if (m_.shutdown) return;
    Report r = report;
    if (!r.at_ms) r.at_ms = now_ms;
    const bool gap = !m_.have_report || r.at_ms < m_.report.at_ms || r.at_ms - m_.report.at_ms > kPhaseFreshMs;
    // P2-8 (b): in game in 30010/30020/30050, not saving, continuously.
    const bool settle = r.ingame && !r.title && SettleMission(r.mission) && !r.saving;
    if (!settle || gap || (m_.st_active && m_.st_mission != r.mission)) {
        m_.st_active = m_.st_have = m_.st_bound = false;
    }
    if (settle && !m_.st_active) {
        m_.st_active = true;
        m_.st_mission = r.mission;
        m_.st_have = m_.st_bound = false;
    }
    m_.report = r;
    m_.have_report = true;
    Flush();
}

void Core::OnContinue(std::uint32_t target, std::uint64_t now_ms) noexcept {
    if (m_.shutdown || target == 0 || target > wire::kMaxMission) return;
    m_.cb_active = true;
    m_.cb_first = false;
    m_.cb_target = target;
    m_.cb_at = now_ms;
    Log("save_handoff=continue; target=%u", target);
    // Exiting, but the game kept running and loads the guest's own save (a
    // Continue Lua allowed without op 3: lock off, no link): the request must
    // not be applied at the next exit.
    if (config_.role == wire::Role::guest && m_.guest == GuestPhase::exiting) GuestCancelExit(wire::AbortReason::not_title, now_ms);
    if (!m_.hash_pending) {
        Emit(ActionKind::hash_set);
        m_.hash_pending = true;
        m_.hash_at = now_ms;
    }
    Flush();
}

void Core::OnDecline(DeclineSource source, std::uint64_t now_ms) noexcept {
    if (config_.role != wire::Role::guest || m_.shutdown) return;
    m_.declined = true;
    Log("save_handoff=declined; source=%s", source == DeclineSource::f9 ? "f9" : "second_continue");
    switch (m_.guest) {
    case GuestPhase::receiving:
    case GuestPhase::finishing:
        SendSmall(wire::Kind::abort, m_.xfer, U8(wire::AbortReason::user), 0);
        Log("save_handoff=abort; why=%s; xfer=%08X", wire::AbortName(wire::AbortReason::user), m_.xfer);
        if (Action* d = Emit(ActionKind::delete_stage)) d->xfer = m_.xfer;
        m_.guest = GuestPhase::idle;
        m_.stage_ok = m_.request_written = m_.result_sent = m_.result_retry = false;
        view_.pct = 0;
        SetToken(Token::kept, U8(wire::DeclineReason::user));
        m_.sticky = true;
        PostToast(Toast::g8_kept, 0, 0);
        break;
    case GuestPhase::staged:
    case GuestPhase::countdown: GuestAbortStage(wire::AbortReason::user, now_ms); break;
    // WM_CLOSE is posted but the game still runs (no_window, a slow close): the
    // veto holds, request.json goes before the tooling can apply it.
    case GuestPhase::exiting: GuestCancelExit(wire::AbortReason::user, now_ms); break;
    case GuestPhase::idle:
        if (static_cast<Token>(view_.token) == Token::wait_host) {
            SetToken(Token::kept, U8(wire::DeclineReason::user));
            m_.sticky = true;
            PostToast(Toast::g8_kept, 0, 0);
        }
        break;
    }
    Flush();
}

void Core::OnPeer(const std::uint8_t* bytes, std::size_t size, std::uint64_t now_ms) noexcept {
    if (m_.shutdown) return;
    wire::Message msg{};
    const wire::Decode result = wire::DecodeMessage(bytes, size, msg);
    const char* bad = nullptr;
    if (result != wire::Decode::ok) {
        stats_.last_bad = result;
        bad = wire::DecodeName(result);
    } else if (msg.kind == wire::Kind::state) {
        const wire::Role expect = config_.role == wire::Role::host ? wire::Role::guest : wire::Role::host;
        if (msg.state.role != expect) {
            bad = "role";
        } else {
            m_.peer = msg.state;
            m_.peer_seen = true;
            if (!m_.peer_logged || !SameState(m_.peer_log, msg.state)) {
                char hex[wire::kShaHexChars + 1];
                wire::FormatShaHex(msg.state.set, hex);
                Log("save_handoff=state_rx; role=%s; phase=%u; story=%u; mission=%u; bound=%u; need=%u; elig=%u; set=%.8s",
                    wire::RoleName(msg.state.role), unsigned{U8(msg.state.phase)}, unsigned{U8(msg.state.story)},
                    msg.state.mission, msg.state.bound_mission, unsigned{msg.state.need}, unsigned{U8(msg.state.elig)}, hex);
                m_.peer_log = msg.state;
                m_.peer_logged = true;
            }
        }
    } else {
        const bool host = config_.role == wire::Role::host;
        const bool to_host = msg.kind == wire::Kind::want || msg.kind == wire::Kind::decline ||
                             msg.kind == wire::Kind::result || msg.kind == wire::Kind::abort;
        const bool to_guest = msg.kind == wire::Kind::offer || msg.kind == wire::Kind::chunk ||
                              msg.kind == wire::Kind::end || msg.kind == wire::Kind::abort;
        if (host && to_host) HostMessage(msg, now_ms);
        else if (!host && to_guest) GuestMessage(msg, now_ms);
        else bad = "direction";
    }
    if (bad) {
        ++stats_.bad_msg;
        if (!m_.bad_logged || now_ms - m_.bad_log_at >= kBadMsgLogMs) {
            Log("save_handoff=bad_msg; why=%s; count=%u", bad, stats_.bad_msg);
            m_.bad_logged = true;
            m_.bad_log_at = now_ms;
        }
    }
    Flush();
}

void Core::OnSession(std::uint32_t session, std::uint64_t now_ms) noexcept {
    if (m_.shutdown) return;
    m_.linked = true;
    m_.session = session;
    m_.last_send_state = false;  // a new session starts with empty reliable channels
    m_.peer_seen = false;
    m_.need_known = false;
    m_.state_force = true;
    m_.offers = 0;
    m_.toasted = 0xFF;
    m_.g12_toasted = false;
    m_.stop = false;
    m_.retry_at = 0;
    if (config_.role == wire::Role::host) {
        // No resume: the guest drops its partial; a held snapshot may serve the next OFFER.
        if (m_.host != HostPhase::idle && m_.host != HostPhase::done && m_.host != HostPhase::snapshot)
            Log("save_handoff=abort; why=%s; xfer=%08X", wire::AbortName(wire::AbortReason::link_lost), m_.xfer);
        if (m_.host != HostPhase::snapshot) m_.host = HostPhase::idle;
        m_.sticky = false;
        view_.pct = 0;
    } else if (config_.role == wire::Role::guest) {
        if (m_.guest == GuestPhase::receiving) GuestDropPartial(wire::AbortName(wire::AbortReason::link_lost));
        if (static_cast<Token>(view_.token) == Token::failed) m_.sticky = false;
    }
    (void)now_ms;
    Flush();
}

void Core::OnPeerClose(std::uint64_t now_ms) noexcept {
    if (m_.shutdown) return;
    m_.linked = false;
    m_.peer_seen = false;
    m_.need_known = false;
    if (config_.role == wire::Role::host) {
        if (m_.host == HostPhase::offered || m_.host == HostPhase::sending || m_.host == HostPhase::end_sent) {
            HostFail(wire::Fail::link_lost, wire::AbortReason::none, now_ms);
        } else if (m_.host == HostPhase::snapshot) {
            m_.host = HostPhase::idle;
        } else if (m_.host == HostPhase::done && static_cast<Token>(view_.token) == Token::partner_staged &&
                   (m_.peer.mode == wire::Mode::scene || m_.peer.mode == wire::Mode::always)) {
            // Online 2026-10-22 P4: a scene/always guest with a verified stage
            // that leaves is restarting on this save (its countdown exit, a
            // manual quit or a crash: the tooling applies the request on any
            // exit). Its RESULT exiting can be lost when its link shuts down
            // right after it (Steam parks at the WM_CLOSE), so the host infers it.
            SetToken(Token::partner_restarting, 0);
            m_.sticky = true;
            Log("save_handoff=partner_restarting; how=link_closed");
            PostToast(Toast::h2_partner_restarting, m_.offer.bound_mission, 0);
        }
    } else if (config_.role == wire::Role::guest) {
        if (m_.guest == GuestPhase::receiving) GuestFail(wire::Fail::link_lost, wire::Kind::state, now_ms);
    }
    Flush();
}

void Core::OnBuildOk(bool ok, const char* remote_build) noexcept {
    char build[kBuildChars];
    CopyText(build, sizeof(build), remote_build);
    const bool changed = !m_.build_known || ok != m_.build_ok || std::strcmp(build, m_.remote_build) != 0;
    if (!changed) return;
    std::memcpy(m_.remote_build, build, sizeof(build));
    if (!m_.build_known || ok != m_.build_ok) Log("save_handoff=peer_build; ok=%u; build=%.24s", ok ? 1u : 0u, build);
    m_.build_known = true;
    if (ok && !m_.build_ok) m_.state_force = true;
    m_.build_ok = ok;
    Flush();
}

void Core::OnSetHashed(const SetInfo& info, std::uint64_t now_ms) noexcept {
    if (m_.shutdown) return;
    m_.hash_pending = false;
    m_.set_seen = true;
    m_.set_at = now_ms;
    Sha computed;
    const bool good = info.ok && info.stable && info.n >= 1 && info.n <= wire::kMaxFiles &&
                      wire::SetSha256(info.files, info.n, computed) && SameSet(computed, info.set);
    m_.set = info;
    m_.set_known = good;
    // P2-8 (a) Continue bind: two equal reads at least kSnapshotGapMs apart.
    if (m_.cb_active && now_ms - m_.cb_at > kContinueBindMs) m_.cb_active = false;
    if (m_.cb_active && good) {
        if (!m_.cb_first) {
            m_.cb_first = true;
            CopySet(m_.cb_set, info.set);
            m_.cb_first_at = now_ms;
        } else if (now_ms - m_.cb_first_at >= kSnapshotGapMs) {
            if (SameSet(m_.cb_set, info.set)) {
                Bind(info.set, m_.cb_target, Source::continue_);
                m_.cb_active = false;
            } else {
                CopySet(m_.cb_set, info.set);
                m_.cb_first_at = now_ms;
            }
        }
    }
    // P2-8 (b) settle bind: the same set across kSettleBindMs of settled reports.
    if (m_.st_active) {
        if (!good) {
            m_.st_have = m_.st_bound = false;
        } else if (!m_.st_have || !SameSet(m_.st_set, info.set)) {
            m_.st_have = true;
            m_.st_bound = false;
            CopySet(m_.st_set, info.set);
            m_.st_set_at = now_ms;
        } else if (!m_.st_bound && now_ms - m_.st_set_at >= kSettleBindMs) {
            Bind(info.set, m_.st_mission, Source::settle);
            m_.st_bound = true;
        }
    }
    Flush();
}

void Core::OnSnapshotRead(bool ok, const Snapshot* snapshot, std::uint64_t now_ms) noexcept {
    if (m_.shutdown) {
        if (ok && snapshot) Emit(ActionKind::release_snapshot);
        return;
    }
    if (config_.role != wire::Role::host || m_.host != HostPhase::snapshot) {
        if (ok && snapshot) Emit(ActionKind::release_snapshot);
        Flush();
        return;
    }
    bool good = ok && snapshot && snapshot->info.ok && snapshot->info.n >= 1 && snapshot->info.n <= wire::kMaxFiles &&
                snapshot->info.total <= wire::kMaxTotal && SameSet(snapshot->info.set, m_.snap_set);
    if (good) {
        Sha computed;
        good = wire::SetSha256(snapshot->info.files, snapshot->info.n, computed) && SameSet(computed, snapshot->info.set);
        for (unsigned i = 0; good && i < snapshot->info.n; ++i) good = snapshot->data[snapshot->info.files[i].slot] != nullptr;
        const ProvenanceEntry* e = provenance_.Find(snapshot->info.set);
        good = good && e && Supported(e->mission);
    }
    if (!good) {
        if (ok && snapshot) Emit(ActionKind::release_snapshot);
        Log("save_handoff=failed; why=%s", wire::FailName(wire::Fail::snapshot_changed));
        m_.host = HostPhase::idle;
        m_.retry_at = now_ms + kRetryGapMs;
        CountOffer(m_.snap_set);
        m_.set_seen = false;  // re-hash before deciding again
        Flush();
        return;
    }
    m_.snap = *snapshot;
    m_.snap_held = true;
    m_.snap_at = now_ms;
    m_.host = HostPhase::idle;
    if (GuestWants() && m_.elig == wire::Elig::eligible) StartOffer(now_ms);
    Flush();
}

void Core::OnStageDone(bool ok, std::uint64_t now_ms) noexcept {
    const ActionKind kind = awaiting_;
    const std::uint32_t xfer = awaiting_xfer_;
    awaiting_ = ActionKind::none;
    if (m_.shutdown || config_.role != wire::Role::guest) return;
    switch (kind) {
    case ActionKind::stage_write:
        if (!ok && m_.guest == GuestPhase::receiving && xfer == m_.xfer) GuestFail(wire::Fail::disk, wire::Kind::result, now_ms);
        break;
    case ActionKind::write_manifest:
        if (m_.guest != GuestPhase::finishing || xfer != m_.xfer) break;
        if (!ok) GuestFail(wire::Fail::disk, wire::Kind::result, now_ms);
        else if (Action* c = Emit(ActionKind::commit_stage)) c->xfer = m_.xfer;
        break;
    case ActionKind::commit_stage:
        if (m_.guest != GuestPhase::finishing || xfer != m_.xfer) break;
        if (!ok) GuestFail(wire::Fail::disk, wire::Kind::result, now_ms);
        else StageCommitted(now_ms);
        break;
    case ActionKind::write_request:
        if (m_.guest != GuestPhase::finishing || xfer != m_.stage_xfer || !m_.stage_ok) break;
        if (!ok) {
            GuestFail(wire::Fail::disk, wire::Kind::result, now_ms);
            break;
        }
        m_.request_written = true;
        m_.guest = GuestPhase::countdown;
        m_.countdown_at = now_ms;
        m_.title_lost = false;
        m_.g2_left = static_cast<unsigned>(kCountdownMs / 1000);
        SetToken(Token::countdown, 0);
        Log("save_handoff=countdown; ms=%u", static_cast<unsigned>(kCountdownMs));
        PostToast(Toast::g2_countdown, m_.g2_left, 0);
        break;
    case ActionKind::delete_stage:
        if (!ok) Log("save_handoff=delete_failed; xfer=%08X", xfer);
        break;
    default: break;
    }
    Flush();
}

void Core::OnSendResult(const Action& sent, bool queued, std::uint64_t now_ms) noexcept {
    if (m_.shutdown || sent.kind != ActionKind::send) return;
    const bool host = config_.role == wire::Role::host;
    if (queued) {
        if (sent.message == wire::Kind::chunk) ++stats_.chunks_sent;
        // A queued CHUNK proves the link takes data (a reliable slot frees only on an ack).
        if (host && sent.message == wire::Kind::chunk && m_.host == HostPhase::sending && sent.xfer == m_.xfer)
            m_.progress_at = now_ms;
        if (!host && sent.message == wire::Kind::result && sent.xfer == m_.xfer &&
            sent.data[8] == U8(wire::ResultStatus::staged) && m_.stage_ok) {
            m_.result_sent = true;
            m_.result_retry = false;
        }
        return;
    }
    m_.last_send_state = false;  // the channel's newest message is unknown now
    switch (sent.message) {
    case wire::Kind::state:
        std::memset(m_.state_last, 0, sizeof(m_.state_last));  // differs: resent after kStateMinGapMs
        break;
    case wire::Kind::chunk:
        if (host && m_.host == HostPhase::sending && sent.xfer == m_.xfer) RewindTo(sent.slot, sent.offset);
        break;
    case wire::Kind::offer:
        if (host && m_.host == HostPhase::offered && sent.xfer == m_.xfer) {
            m_.host = HostPhase::idle;
            m_.retry_at = now_ms + kRetryGapMs;
            SetToken(Token::idle, 0);
        }
        break;
    case wire::Kind::end:
        if (host && m_.host == HostPhase::end_sent && sent.xfer == m_.xfer) m_.host = HostPhase::sending;
        break;
    case wire::Kind::want:
        if (!host && m_.guest == GuestPhase::receiving && sent.xfer == m_.xfer) GuestDropPartial("link_lost");
        break;
    case wire::Kind::result:
        if (!host && sent.xfer == m_.xfer && sent.data[8] == U8(wire::ResultStatus::staged) && m_.stage_ok)
            m_.result_retry = true;
        // RESULT exiting refused (not connected): nothing to wait for before the WM_CLOSE.
        if (!host && sent.xfer == m_.xfer && sent.data[8] == U8(wire::ResultStatus::exiting) && m_.exit_pending)
            m_.exit_unsent = true;
        break;
    default: break;
    }
    Flush();
}

void Core::OnExitPosted(bool window_found, std::uint64_t now_ms) noexcept {
    if (config_.role != wire::Role::guest || m_.guest != GuestPhase::exiting) return;
    m_.exit_posted = true;
    m_.exit_window = window_found;
    m_.exit_at = now_ms;
    Log("save_handoff=exit; how=%s", window_found ? "wm_close" : "no_window");
    if (!window_found) PostToast(Toast::g9_quit_manually, 0, 0);
    Flush();
}

void Core::OnPartnerMission(std::uint32_t mission, bool terminating, std::uint64_t now_ms) noexcept {
    m_.partner_mission = mission != 0;
    m_.terminating = terminating;
    (void)now_ms;
}

void Core::OnAppliedSeen(const Applied& applied, std::uint64_t now_ms) noexcept {
    if (config_.role != wire::Role::guest || m_.shutdown) return;
    char hex[wire::kShaHexChars + 1];
    wire::FormatShaHex(applied.set, hex);
    Log("save_handoff=applied_seen; set=%.8s", hex);
    m_.applied = true;
    m_.sticky = false;
    if (m_.guest == GuestPhase::idle) SetToken(Token::applied, 0);
    PostToast(Toast::g10_applied, 0, 0);
    (void)now_ms;
    Flush();
}

bool Core::LoadProvenance(const char* text, std::size_t size) noexcept {
    if (provenance_.Parse(text, size)) return true;
    Log("save_handoff=provenance_reset; why=malformed");
    Emit(ActionKind::write_provenance);
    return false;
}

void Core::OnShutdown(std::uint64_t now_ms) noexcept {
    if (m_.shutdown) return;
    if (config_.role == wire::Role::host &&
        (m_.host == HostPhase::offered || m_.host == HostPhase::sending || m_.host == HostPhase::end_sent)) {
        SendSmall(wire::Kind::abort, m_.xfer, U8(wire::AbortReason::shutdown), 0);
        Log("save_handoff=abort; why=%s; xfer=%08X", wire::AbortName(wire::AbortReason::shutdown), m_.xfer);
        m_.host = HostPhase::idle;
    }
    if (config_.role == wire::Role::guest && m_.guest == GuestPhase::receiving) {
        // The partial is removed by the next Configure (known names only).
        SendSmall(wire::Kind::abort, m_.xfer, U8(wire::AbortReason::shutdown), 0);
        Log("save_handoff=abort; why=%s; xfer=%08X", wire::AbortName(wire::AbortReason::shutdown), m_.xfer);
        m_.guest = GuestPhase::idle;
    }
    ReleaseSnapshot();
    m_.shutdown = true;
    (void)now_ms;
    Flush();
}

void Core::Tick(std::uint64_t now_ms, unsigned queued) noexcept {
    if (m_.shutdown) return;
    if (m_.st_active && !Fresh(now_ms, kPhaseFreshMs)) m_.st_active = m_.st_have = m_.st_bound = false;
    if (m_.cb_active && now_ms - m_.cb_at > kContinueBindMs) m_.cb_active = false;
    TickHash(now_ms);
    if (config_.role == wire::Role::host) HostTick(now_ms, queued);
    else if (config_.role == wire::Role::guest) GuestTick(now_ms, queued);
    TickState(now_ms);
    Flush();
}

void Core::TickHash(std::uint64_t now_ms) noexcept {
    if (m_.hash_pending && now_ms - m_.hash_at < kHashTimeoutMs) return;
    bool want = false;
    if (m_.cb_active && (!m_.cb_first || now_ms - m_.cb_first_at >= kSnapshotGapMs)) want = true;
    else if (Fresh(now_ms, kPhaseFreshMs) && !m_.report.saving && (!m_.set_seen || now_ms - m_.set_at >= kRehashMs)) want = true;
    if (!want) return;
    if (!Emit(ActionKind::hash_set)) return;
    m_.hash_pending = true;
    m_.hash_at = now_ms;
}

void Core::TickState(std::uint64_t now_ms) noexcept {
    if (!m_.linked || !m_.build_ok) return;
    wire::Message msg{};
    msg.kind = wire::Kind::state;
    msg.state = BuildState(now_ms);
    std::uint8_t encoded[wire::kStateSize];
    if (wire::Encode(msg, encoded, sizeof(encoded)) != wire::kStateSize) return;
    const bool changed = !m_.state_sent || std::memcmp(encoded, m_.state_last, sizeof(encoded)) != 0;
    const std::uint64_t since = now_ms - m_.state_at;
    const bool due = m_.state_force || !m_.state_sent || (changed && since >= kStateMinGapMs) || since >= kStateHeartbeatMs;
    if (!due || !Send(msg)) return;
    std::memcpy(m_.state_last, encoded, sizeof(encoded));
    m_.state_sent = true;
    m_.state_at = now_ms;
    m_.state_force = false;
}

// ---- Host.
bool Core::GuestWants() const noexcept {
    const wire::State& p = m_.peer;
    return m_.peer_seen && p.role == wire::Role::guest && p.phase == wire::Phase::title && p.mode != wire::Mode::off &&
           p.need == 1 && p.set_known && m_.set_known && !SameSet(p.set, m_.set.set);
}

void Core::HostTick(std::uint64_t now_ms, unsigned queued) noexcept {
    HostFacts f{};
    f.mode = config_.mode;
    f.report_fresh = Fresh(now_ms, kPhaseFreshMs);
    // Hidden runs 247ae49a / 1eff8016 (0.13.6): in the start-up mission 1 (or 0)
    // the story sequence is not loaded yet (its default reads as the opening),
    // so the own story is unknown there (transient, no H4 / G5) until a real mission.
    f.story = m_.report.mission < 10000 ? wire::Story::unknown : m_.report.story;
    f.saving = m_.report.saving;
    f.set_known = m_.set_known;
    f.unexpected = m_.set.unexpected;
    f.total = m_.set.total;
    f.bound_mission = OwnBound();
    // Before the first hash nothing is known yet: unknown (transient), not continue_unknown.
    m_.elig = m_.set_seen ? HostEligible(f) : wire::Elig::unknown;
    // A held snapshot serves retries of the same set for kSnapshotKeepMs.
    if (m_.snap_held && (m_.host == HostPhase::idle || m_.host == HostPhase::done) &&
        (now_ms - m_.snap_at >= kSnapshotKeepMs || !m_.set_known || !SameSet(m_.set.set, m_.snap.info.set)))
        ReleaseSnapshot();
    switch (m_.host) {
    case HostPhase::idle:
        if (GuestWants() && m_.elig == wire::Elig::eligible && m_.build_ok && m_.linked && now_ms >= m_.retry_at &&
            !(m_.stop && SameSet(m_.stop_set, m_.set.set)) && OffersFor(m_.set.set) < kMaxOffersPerSet) {
            if (m_.snap_held && SameSet(m_.snap.info.set, m_.set.set)) {
                StartOffer(now_ms);
            } else if (Emit(ActionKind::snapshot)) {
                m_.host = HostPhase::snapshot;
                m_.phase_at = now_ms;
                CopySet(m_.snap_set, m_.set.set);
            }
        } else {
            HostRest(now_ms);
        }
        break;
    case HostPhase::snapshot:
        if (now_ms - m_.phase_at >= kHashTimeoutMs) m_.host = HostPhase::idle;  // the report never came
        break;
    case HostPhase::offered:
        if (now_ms - m_.phase_at >= kOfferIdleMs) HostFail(wire::Fail::timeout, wire::AbortReason::stall, now_ms);
        break;
    case HostPhase::sending: {
        // The guest crashed or the link died without a CLOSE: every CHUNK (and
        // END) is refused or the queue never drains. Mirrors the guest's stall.
        if (now_ms - m_.progress_at >= kChunkStallMs) {
            HostFail(wire::Fail::stall, wire::AbortReason::stall, now_ms);
            break;
        }
        unsigned sent = 0;
        while (m_.cur_file < m_.offer.n && sent < kChunksPerTick && queued + sent < kMaxQueued) {
            const wire::FileEntry& file = m_.offer.files[m_.cur_file];
            const std::uint32_t left = file.size - m_.cur_off;
            wire::Message msg{};
            msg.kind = wire::Kind::chunk;
            msg.xfer = m_.xfer;
            msg.chunk.slot = file.slot;
            msg.chunk.len = static_cast<std::uint16_t>(left < wire::kChunk ? left : wire::kChunk);
            msg.chunk.offset = m_.cur_off;
            msg.chunk.payload = m_.snap.data[file.slot] + m_.cur_off;
            if (!Send(msg)) break;
            ++sent;
            m_.sent_bytes += msg.chunk.len;
            m_.cur_off += msg.chunk.len;
            if (m_.cur_off == file.size) {
                m_.cur_off = 0;
                do ++m_.cur_file;
                while (m_.cur_file < m_.offer.n && !(m_.want_mask & (1u << m_.offer.files[m_.cur_file].slot)));
            }
        }
        // END goes in a later tick than the last CHUNK, so a refused CHUNK
        // (reported after this drain) rewinds before END is queued.
        if (!sent && m_.cur_file >= m_.offer.n) {
            wire::Message end{};
            end.kind = wire::Kind::end;
            end.xfer = m_.xfer;
            end.end.total = m_.want_total;
            if (Send(end)) {
                m_.host = HostPhase::end_sent;
                m_.phase_at = now_ms;
            }
        }
        view_.pct = static_cast<std::uint8_t>(m_.want_total ? m_.sent_bytes * 100 / m_.want_total : 0);
        break;
    }
    case HostPhase::end_sent:
        if (now_ms - m_.phase_at >= kResultWaitMs) HostFail(wire::Fail::timeout, wire::AbortReason::stall, now_ms);
        break;
    case HostPhase::done: break;
    }
}

void Core::HostRest(std::uint64_t now_ms) noexcept {
    if (m_.host != HostPhase::idle || m_.sticky) return;
    if (!m_.peer_seen) {
        SetToken(Token::idle, 0);
        return;
    }
    if (m_.peer.need != 1 || m_.peer.mode == wire::Mode::off) {
        SetToken(m_.peer.set_known ? Token::same : Token::idle, 0);
        return;
    }
    switch (m_.elig) {
    case wire::Elig::eligible: SetToken(Token::idle, 0); break;  // the OFFER follows at the guest's title
    case wire::Elig::unknown:
    case wire::Elig::saving:
    case wire::Elig::story_unknown: break;  // transient: keep the token
    default: {
        SetToken(Token::refused, U8(m_.elig));
        // Online 2026-10-22 P3: in game in 30010/30020/30050 the settle bind is
        // under way (H8), so "go to Mother Base or free roam" (H3) is wrong there.
        const bool settling = m_.elig == wire::Elig::continue_unknown && Fresh(now_ms, kPhaseFreshMs) && m_.report.ingame &&
                              !m_.report.title && SettleMission(m_.report.mission);
        const std::uint8_t key = settling ? kSettleToast : U8(m_.elig);
        if (m_.toasted != key) {
            m_.toasted = key;
            if (settling) PostToast(Toast::h8_settling, SettleSeconds(), 0);
            else if (m_.elig == wire::Elig::continue_unknown) PostToast(Toast::h3_partner_elsewhere, 0, 0);
            else if (m_.elig == wire::Elig::story_opening) PostToast(Toast::h4_story_opening, 0, 0);
            else if (m_.elig == wire::Elig::continue_unsupported) PostToast(Toast::h7_unsupported, OwnBound(), 0);
        }
        break;
    }
    }
}

void Core::StartOffer(std::uint64_t now_ms) noexcept {
    const SetInfo& info = m_.snap.info;
    wire::Offer& o = m_.offer;
    o = wire::Offer{};
    o.n = info.n;
    o.total = static_cast<std::uint32_t>(info.total);
    const ProvenanceEntry* e = provenance_.Find(info.set);
    o.bound_mission = e ? e->mission : 0;
    CopySet(o.set, info.set);
    for (unsigned i = 0; i < info.n; ++i) o.files[i] = info.files[i];
    CountOffer(o.set);
    wire::Message msg{};
    msg.kind = wire::Kind::offer;
    msg.xfer = NextXfer();
    msg.offer = o;
    m_.xfer = msg.xfer;
    if (!Send(msg)) {
        m_.host = HostPhase::idle;
        m_.retry_at = now_ms + kRetryGapMs;
        return;
    }
    ++stats_.offers;
    m_.host = HostPhase::offered;
    m_.phase_at = now_ms;
    m_.sticky = false;
    SetToken(Token::offered, 0);
    view_.pct = 0;
    view_.bytes = o.total;
    view_.host_mission = o.bound_mission;
    char hex[wire::kShaHexChars + 1];
    wire::FormatShaHex(o.set, hex);
    Log("save_handoff=offer; xfer=%08X; files=%u; bytes=%u; set=%.8s; mission=%u", m_.xfer, unsigned{o.n}, o.total, hex,
        o.bound_mission);
}

void Core::ReleaseSnapshot() noexcept {
    if (!m_.snap_held) return;
    Emit(ActionKind::release_snapshot);
    m_.snap_held = false;
    m_.snap = Snapshot{};
}

void Core::HostFail(wire::Fail fail, wire::AbortReason abort, std::uint64_t now_ms) noexcept {
    if (abort != wire::AbortReason::none && m_.xfer) {
        SendSmall(wire::Kind::abort, m_.xfer, U8(abort), 0);
        Log("save_handoff=abort; why=%s; xfer=%08X", wire::AbortName(abort), m_.xfer);
    }
    Log("save_handoff=failed; why=%s", wire::FailName(fail));
    m_.host = HostPhase::idle;
    m_.retry_at = now_ms + kRetryGapMs;
    m_.sticky = true;
    view_.pct = 0;
    SetToken(Token::failed, U8(fail));
    PostToast(Toast::h6_failed, U8(fail), 0);
}

void Core::HostEnd(Token token, std::uint8_t why, bool stop) noexcept {
    m_.host = HostPhase::idle;
    m_.sticky = token == Token::kept || token == Token::failed;
    view_.pct = 0;
    SetToken(token, why);
    if (stop) {
        m_.stop = true;
        CopySet(m_.stop_set, m_.offer.set);
    }
    ReleaseSnapshot();
}

void Core::RewindTo(std::uint8_t slot, std::uint32_t offset) noexcept {
    unsigned index = m_.offer.n;
    for (unsigned i = 0; i < m_.offer.n; ++i)
        if (m_.offer.files[i].slot == slot) index = i;
    if (index == m_.offer.n || offset > m_.offer.files[index].size) return;
    if (index > m_.cur_file || (index == m_.cur_file && offset >= m_.cur_off)) return;
    m_.cur_file = index;
    m_.cur_off = offset;
    std::uint64_t before = offset;
    for (unsigned i = 0; i < index; ++i)
        if (m_.want_mask & (1u << m_.offer.files[i].slot)) before += m_.offer.files[i].size;
    m_.sent_bytes = before;
}

void Core::HostMessage(const wire::Message& msg, std::uint64_t now_ms) noexcept {
    const bool current = msg.xfer == m_.xfer && m_.xfer != 0;
    switch (msg.kind) {
    case wire::Kind::want: {
        if (!current || m_.host != HostPhase::offered) {
            ++stats_.stale;
            return;
        }
        std::uint8_t offered = 0;
        for (unsigned i = 0; i < m_.offer.n; ++i) offered = static_cast<std::uint8_t>(offered | (1u << m_.offer.files[i].slot));
        if (msg.want.mask & ~offered) {
            HostFail(wire::Fail::protocol, wire::AbortReason::protocol, now_ms);
            return;
        }
        m_.want_mask = msg.want.mask;
        m_.want_total = 0;
        m_.cur_file = m_.offer.n;
        for (unsigned i = 0; i < m_.offer.n; ++i)
            if (m_.want_mask & (1u << m_.offer.files[i].slot)) {
                m_.want_total += m_.offer.files[i].size;
                if (m_.cur_file == m_.offer.n) m_.cur_file = i;
            }
        m_.cur_off = 0;
        m_.sent_bytes = 0;
        m_.host = HostPhase::sending;
        m_.phase_at = m_.progress_at = now_ms;
        SetToken(Token::sending, 0);
        view_.pct = 0;
        Log("save_handoff=want_rx; xfer=%08X; mask=%02X; bytes=%u", m_.xfer, unsigned{m_.want_mask}, m_.want_total);
        PostToast(Toast::h1_sending, 0, 0);
        return;
    }
    case wire::Kind::decline: {
        if (!current || m_.host != HostPhase::offered) {
            ++stats_.stale;
            return;
        }
        const wire::DeclineReason r = msg.decline.reason;
        Log("save_handoff=decline_rx; reason=%s", wire::DeclineName(r));
        switch (r) {
        case wire::DeclineReason::user:
            HostEnd(Token::kept, U8(r), true);
            PostToast(Toast::h5_partner_kept, 0, 0);
            break;
        case wire::DeclineReason::same:
        case wire::DeclineReason::same_scene: HostEnd(Token::same, 0, true); break;
        case wire::DeclineReason::off:
        case wire::DeclineReason::not_title: HostEnd(Token::idle, 0, true); break;
        default: {
            const wire::Fail fail = r == wire::DeclineReason::bad_offer    ? wire::Fail::bad_offer
                                    : r == wire::DeclineReason::disk_space ? wire::Fail::disk_space
                                                                           : wire::Fail::partner;
            Log("save_handoff=failed; why=%s", wire::FailName(fail));
            HostEnd(Token::failed, U8(fail), true);
            PostToast(Toast::h6_failed, U8(fail), 0);
            break;
        }
        }
        return;
    }
    case wire::Kind::result: {
        if (!current || m_.host == HostPhase::idle || m_.host == HostPhase::snapshot) {
            ++stats_.stale;
            return;
        }
        Log("save_handoff=result_rx; status=%s", wire::ResultStatusName(msg.result.status));
        if (msg.result.status == wire::ResultStatus::staged) {
            if (m_.host == HostPhase::done) return;
            HostEnd(Token::partner_staged, 0, false);
            m_.host = HostPhase::done;
            m_.sticky = true;
        } else if (msg.result.status == wire::ResultStatus::exiting) {
            HostEnd(Token::partner_restarting, 0, false);
            m_.host = HostPhase::done;
            m_.sticky = true;
            PostToast(Toast::h2_partner_restarting, m_.offer.bound_mission, 0);
        } else {
            HostFail(msg.result.reason, wire::AbortReason::none, now_ms);
        }
        return;
    }
    case wire::Kind::abort: {
        if (!current || m_.host == HostPhase::idle || m_.host == HostPhase::snapshot) {
            ++stats_.stale;
            return;
        }
        Log("save_handoff=abort_rx; why=%s; xfer=%08X", wire::AbortName(msg.abort.reason), m_.xfer);
        if (msg.abort.reason == wire::AbortReason::user) {
            HostEnd(Token::kept, U8(wire::DeclineReason::user), true);
            PostToast(Toast::h5_partner_kept, 0, 0);
        } else {
            HostFail(FailFor(msg.abort.reason), wire::AbortReason::none, now_ms);
        }
        return;
    }
    default: return;
    }
}

// ---- Guest.
void Core::GuestNeedUpdate(std::uint64_t now_ms) noexcept {
    // Both sets known and a fresh own report; otherwise the last decision stands.
    if (!m_.peer_seen || m_.peer.role != wire::Role::host || !m_.peer.set_known || !m_.set_known ||
        !Fresh(now_ms, kPhaseFreshMs))
        return;
    SideFacts own{}, host{};
    own.set_known = true;
    CopySet(own.set, m_.set.set);
    own.story = m_.report.story;
    own.mission = m_.report.mission;
    own.bound_mission = OwnBound();
    host.set_known = m_.peer.set_known;
    CopySet(host.set, m_.peer.set);
    host.story = m_.peer.story;
    host.mission = m_.peer.mission;
    host.bound_mission = m_.peer.bound_mission;
    NeedWhy why = NeedWhy::off;
    const std::uint8_t need = GuestNeed(config_.mode, own, host, why);
    // Review (online 2026-10-22): the start-up mission 1 (or 0) is no location;
    // Loc 0 against the host's would give need=1 why=location to a guest whose
    // title is at the host's place (G11 / H8 at its boot). The story rule is no
    // better there: the boot reads the story sequence's default (the opening)
    // before the save loads (hidden runs 247ae49a / 1eff8016: story=1, then 2, in
    // mission 1). Both wait for a real mission (the title); the others decide at once.
    if ((why == NeedWhy::location || why == NeedWhy::story) && own.mission < 10000) return;
    if (!m_.need_logged || need != m_.need || why != m_.need_why) {
        Log("save_handoff=need; need=%u; why=%s", unsigned{need}, NeedWhyName(why));
        m_.need_logged = true;
    }
    m_.need = need;
    m_.need_why = why;
    m_.need_known = true;
}

void Core::GuestRest(std::uint64_t now_ms) noexcept {
    if (m_.guest != GuestPhase::idle || m_.sticky) return;
    if (m_.applied && (!m_.need_known || !m_.need)) {
        SetToken(Token::applied, 0);
        return;
    }
    if (!m_.peer_seen || !m_.need_known) {
        SetToken(Token::idle, 0);
        return;
    }
    view_.host_mission = m_.peer.bound_mission;
    if (!m_.need) {
        SetToken(Token::same, 0);
        return;
    }
    using save_handoff_state::WaitWhy;
    const wire::Elig elig = m_.peer.elig;
    // Online 2026-10-22 P3: the host is in game in 30010/30020/30050 with a set
    // not bound yet: its settle bind is under way (G11), it need not go anywhere (G4).
    const bool settling = elig == wire::Elig::continue_unknown && m_.peer.phase == wire::Phase::ingame &&
                          SettleMission(m_.peer.mission);
    // P6: the OFFER comes only at this guest's title (GuestWants); in game it is told so (G12).
    // Not in the start-up mission 1 (ingame=1 there): that guest reaches its title anyway.
    const bool away = Fresh(now_ms, kPhaseFreshMs) && m_.report.ingame && !m_.report.title && m_.report.mission >= 10000;
    const std::uint8_t key = settling ? kSettleToast : U8(elig);
    const bool first = m_.toasted != key;
    switch (elig) {
    case wire::Elig::story_opening:
        SetToken(Token::refused, U8(elig));
        if (first) PostToast(Toast::g5_host_story_opening, 0, 0);
        break;
    case wire::Elig::unexpected_files:
    case wire::Elig::too_large:
    case wire::Elig::off: SetToken(Token::refused, U8(elig)); break;
    case wire::Elig::continue_unknown:
        SetToken(Token::wait_host, U8(settling ? WaitWhy::settling : WaitWhy::host_elsewhere));
        if (first) {
            // The seconds are the host's window: only a host with this build's 10 s.
            if (settling)
                PostToast(Toast::g11_host_settling, m_.peer.mission,
                          wire::RemoteBuildAtLeast(m_.remote_build, kSettle10MinMajor, kSettle10MinMinor, kSettle10MinPatch)
                              ? SettleSeconds()
                              : 0);
            else PostToast(Toast::g4_wait_host, 0, 0);
        }
        break;
    case wire::Elig::continue_unsupported:
        SetToken(Token::wait_host, 0);
        if (first) PostToast(Toast::g6_host_unsupported, m_.peer.bound_mission, 0);
        break;
    case wire::Elig::eligible:
        // The OFFER follows at the title; in game the guest must go there first.
        SetToken(Token::wait_host, U8(away ? WaitWhy::title : WaitWhy::host_elsewhere));
        if (away && !m_.g12_toasted) {
            m_.g12_toasted = true;
            PostToast(Toast::g12_go_to_title, m_.peer.bound_mission, 0);
        }
        break;
    default: SetToken(Token::wait_host, 0); break;  // transient
    }
    if (elig == wire::Elig::story_opening || elig == wire::Elig::continue_unknown || elig == wire::Elig::continue_unsupported)
        m_.toasted = key;
}

void Core::GuestTick(std::uint64_t now_ms, unsigned queued) noexcept {
    GuestNeedUpdate(now_ms);
    if ((m_.guest == GuestPhase::staged || m_.guest == GuestPhase::countdown) && m_.result_retry) {
        m_.result_retry = false;
        SendSmall(wire::Kind::result, m_.xfer, U8(wire::ResultStatus::staged), 0);
    }
    switch (m_.guest) {
    case GuestPhase::idle: GuestRest(now_ms); break;
    case GuestPhase::receiving:
        if (now_ms - m_.last_chunk >= kChunkStallMs) GuestFail(wire::Fail::stall, wire::Kind::abort, now_ms);
        break;
    case GuestPhase::finishing:
    case GuestPhase::staged: break;
    case GuestPhase::countdown: {
        const bool title = Fresh(now_ms, kExitFreshMs) && m_.report.title && !m_.report.ingame;
        if (title) {
            m_.title_lost = false;
        } else if (!m_.title_lost) {
            m_.title_lost = true;
            m_.lost_at = now_ms;
        }
        if (m_.title_lost && now_ms - m_.lost_at > kTitleLostMs) {
            GuestAbortStage(wire::AbortReason::not_title, now_ms);
            break;
        }
        const std::uint64_t held = now_ms - m_.countdown_at;
        const auto left = static_cast<unsigned>(held >= kCountdownMs ? 0 : (kCountdownMs - held + 999) / 1000);
        if (left && left != m_.g2_left) {
            m_.g2_left = left;
            PostToast(Toast::g2_countdown, left, 0);
        }
        ExitFacts f{};
        f.mode = config_.mode;
        f.staged = m_.stage_ok;
        f.request_written = m_.request_written;
        f.result_sent = m_.result_sent;
        f.report_fresh = Fresh(now_ms, kExitFreshMs);
        f.title = m_.report.title;
        f.saving = m_.report.saving;
        f.ingame = m_.report.ingame;
        f.partner_mission = m_.partner_mission;
        f.terminating = m_.terminating;
        f.title_held_ms = held;
        f.declined = m_.declined;
        if (ExitGate(f)) DoExit(now_ms);
        break;
    }
    case GuestPhase::exiting:
        // P4: the WM_CLOSE once RESULT exiting (and everything before it) is
        // acknowledged, it was refused unsent, or kExitFlushMs passed.
        if (m_.exit_pending && (queued == 0 || m_.exit_unsent || now_ms - m_.exit_req_at >= kExitFlushMs)) {
            m_.exit_pending = false;
            m_.exit_at = now_ms;
            Log("save_handoff=exit_flush; ms=%u; acked=%u", static_cast<unsigned>(now_ms - m_.exit_req_at),
                queued == 0 && !m_.exit_unsent ? 1u : 0u);
            Emit(ActionKind::post_exit);
            break;
        }
        // no_window only (G9 already shown; the game keeps running and ticking).
        // After a WM_CLOSE the overlay's PartnerShutdown stops this core (and a
        // Steam link parks), so the glue's own timer logs exit_slow and shows G9.
        if (m_.exit_posted && !m_.exit_window && !m_.exit_slow && now_ms - m_.exit_at >= kExitSlowMs) {
            m_.exit_slow = true;
            Log("save_handoff=exit_slow");
        }
        break;
    }
}

void Core::DoExit(std::uint64_t now_ms) noexcept {
    m_.exit_pending = true;  // before the send: its refusal (OnSendResult) clears the wait
    m_.exit_unsent = false;
    m_.exit_req_at = now_ms;
    SendSmall(wire::Kind::result, m_.xfer, U8(wire::ResultStatus::exiting), 0);
    m_.guest = GuestPhase::exiting;
    SetToken(Token::exiting, 0);
    if (config_.rejoin) Emit(ActionKind::write_rejoin);
    // post_exit follows in GuestTick (P4: the host must learn about the restart first).
    m_.exit_posted = false;
    m_.exit_at = now_ms;
}

void Core::GuestMessage(const wire::Message& msg, std::uint64_t now_ms) noexcept {
    switch (msg.kind) {
    case wire::Kind::offer: GuestOffer(msg, now_ms); return;
    case wire::Kind::chunk: GuestChunk(msg, now_ms); return;
    case wire::Kind::end: GuestEnd(msg, now_ms); return;
    case wire::Kind::abort:
        if (msg.xfer != m_.xfer || m_.guest != GuestPhase::receiving) {
            ++stats_.stale;  // a verified stage outlives the host's later ABORT
            return;
        }
        Log("save_handoff=abort_rx; why=%s; xfer=%08X", wire::AbortName(msg.abort.reason), m_.xfer);
        GuestFail(FailFor(msg.abort.reason), wire::Kind::state, now_ms);
        return;
    default: return;
    }
}

void Core::GuestDecline(wire::DeclineReason reason, std::uint32_t xfer) noexcept {
    SendSmall(wire::Kind::decline, xfer, U8(reason), 0);
    Log("save_handoff=decide; result=decline; reason=%s", wire::DeclineName(reason));
}

void Core::GuestOffer(const wire::Message& msg, std::uint64_t now_ms) noexcept {
    const wire::Offer& o = msg.offer;
    if (m_.guest == GuestPhase::exiting || m_.guest == GuestPhase::finishing) {
        ++stats_.stale;
        return;
    }
    if (m_.guest == GuestPhase::receiving) GuestDropPartial("replaced");
    Sha computed;
    const bool consistent = wire::SetSha256(o.files, o.n, computed) && SameSet(computed, o.set);
    const bool title = Fresh(now_ms, kPhaseFreshMs) && m_.report.title && !m_.report.ingame;
    wire::DeclineReason reason = wire::DeclineReason::none;
    if (config_.mode == wire::Mode::off) reason = wire::DeclineReason::off;
    else if (m_.declined) reason = wire::DeclineReason::user;
    else if (!consistent) reason = wire::DeclineReason::bad_offer;
    else if (m_.set_known && SameSet(m_.set.set, o.set)) reason = wire::DeclineReason::same;
    else if (m_.stage_ok && SameSet(manifest_.set, o.set)) {
        // No resume, but a verified stage of this very set is reused.
        m_.xfer = msg.xfer;
        Log("save_handoff=decide; result=staged; reason=reuse");
        SendSmall(wire::Kind::result, m_.xfer, U8(wire::ResultStatus::staged), 0);
        return;
    } else if (!title) reason = wire::DeclineReason::not_title;
    else if (config_.mode != wire::Mode::always && m_.need_known && !m_.need) reason = wire::DeclineReason::same_scene;
    else if (OffersFor(o.set) >= kMaxOffersPerSet) reason = wire::DeclineReason::budget;
    if (reason != wire::DeclineReason::none) {
        GuestDecline(reason, msg.xfer);
        if (reason == wire::DeclineReason::bad_offer) {
            Log("save_handoff=failed; why=%s", wire::FailName(wire::Fail::bad_offer));
            SetToken(Token::failed, U8(wire::Fail::bad_offer));
            m_.sticky = true;
            PostToast(Toast::g7_failed, U8(wire::Fail::bad_offer), 0);
        }
        return;
    }
    if (m_.stage_ok) {
        // An older stage of another set: dropped (request.json first) before the new transfer.
        if (Action* d = Emit(ActionKind::delete_stage)) d->xfer = m_.stage_xfer;
        m_.stage_ok = m_.request_written = m_.result_sent = m_.result_retry = false;
        m_.guest = GuestPhase::idle;
    }
    CountOffer(o.set);
    m_.xfer = msg.xfer;
    m_.offer = o;
    m_.guest = GuestPhase::receiving;
    m_.file = 0;
    m_.file_off = 0;
    link::Sha256Init(m_.file_hash);
    m_.recv_bytes = 0;
    m_.recv_at = now_ms;
    m_.last_chunk = now_ms;
    m_.progress_logged = 0;
    m_.sticky = false;
    m_.applied = false;
    std::uint8_t mask = 0;
    for (unsigned i = 0; i < o.n; ++i) mask = static_cast<std::uint8_t>(mask | (1u << o.files[i].slot));
    SendSmall(wire::Kind::want, m_.xfer, mask, 0);
    Log("save_handoff=decide; result=want; reason=%s", NeedWhyName(m_.need_known ? m_.need_why : NeedWhy::always));
    SetToken(Token::receiving, 0);
    view_.pct = 0;
    view_.bytes = o.total;
    view_.host_mission = o.bound_mission;
    PostToast(Toast::g1_receiving, (o.total + 1023u) / 1024u, 0);
}

void Core::GuestChunk(const wire::Message& msg, std::uint64_t now_ms) noexcept {
    if (m_.guest != GuestPhase::receiving || msg.xfer != m_.xfer) {
        ++stats_.stale;
        return;
    }
    const wire::Chunk& c = msg.chunk;
    if (m_.file >= m_.offer.n) {
        GuestFail(wire::Fail::protocol, wire::Kind::abort, now_ms);
        return;
    }
    const wire::FileEntry& f = m_.offer.files[m_.file];
    if (c.slot != f.slot || c.offset != m_.file_off || static_cast<std::uint64_t>(c.offset) + c.len > f.size) {
        GuestFail(wire::Fail::protocol, wire::Kind::abort, now_ms);
        return;
    }
    Action* w = Emit(ActionKind::stage_write);
    if (!w) {
        GuestFail(wire::Fail::disk, wire::Kind::result, now_ms);
        return;
    }
    w->xfer = m_.xfer;
    w->slot = c.slot;
    w->offset = c.offset;
    w->size = c.len;
    std::memcpy(w->data, c.payload, c.len);
    link::Sha256Update(m_.file_hash, c.payload, c.len);
    m_.file_off += c.len;
    m_.recv_bytes += c.len;
    m_.last_chunk = now_ms;
    ++stats_.chunks_received;
    if (m_.file_off == f.size) {
        Sha digest;
        link::Sha256Final(m_.file_hash, digest);
        if (!SameSet(digest, f.sha256)) {
            GuestFail(wire::Fail::hash_mismatch, wire::Kind::result, now_ms);
            return;
        }
        ++m_.file;
        m_.file_off = 0;
        link::Sha256Init(m_.file_hash);
    }
    const auto pct = static_cast<unsigned>(m_.offer.total ? m_.recv_bytes * 100 / m_.offer.total : 0);
    view_.pct = static_cast<std::uint8_t>(pct);
    while (m_.progress_logged < 4 && pct >= (m_.progress_logged + 1) * 25) {
        ++m_.progress_logged;
        Log("save_handoff=progress; xfer=%08X; pct=%u", m_.xfer, m_.progress_logged * 25);
    }
}

void Core::GuestEnd(const wire::Message& msg, std::uint64_t now_ms) noexcept {
    if (m_.guest != GuestPhase::receiving || msg.xfer != m_.xfer) {
        ++stats_.stale;
        return;
    }
    if (m_.file != m_.offer.n || msg.end.total != m_.offer.total || m_.recv_bytes != m_.offer.total) {
        GuestFail(wire::Fail::protocol, wire::Kind::abort, now_ms);
        return;
    }
    manifest_ = Manifest{};
    manifest_.xfer = m_.xfer;
    UtcNow(manifest_.created_utc);
    CopyText(manifest_.host_build, sizeof(manifest_.host_build), m_.remote_build);
    manifest_.host_mission = m_.offer.bound_mission;
    CopySet(manifest_.set, m_.offer.set);
    manifest_.n = m_.offer.n;
    for (unsigned i = 0; i < m_.offer.n; ++i) manifest_.files[i] = m_.offer.files[i];
    m_.guest = GuestPhase::finishing;
    if (Action* a = Emit(ActionKind::write_manifest)) a->xfer = m_.xfer;
}

void Core::StageCommitted(std::uint64_t now_ms) noexcept {
    m_.stage_ok = true;
    m_.stage_xfer = m_.xfer;
    m_.request_written = m_.result_sent = m_.result_retry = false;
    Log("save_handoff=staged; xfer=%08X; files=%u; bytes=%u; ms=%u", m_.xfer, unsigned{m_.offer.n}, m_.offer.total,
        static_cast<unsigned>(now_ms - m_.recv_at));
    SendSmall(wire::Kind::result, m_.xfer, U8(wire::ResultStatus::staged), 0);
    view_.pct = 100;
    if (config_.mode == wire::Mode::scene || config_.mode == wire::Mode::always) {
        request_ = Request{};
        request_.pid = config_.pid;
        CopyText(request_.process_start_utc, sizeof(request_.process_start_utc), config_.process_start_utc);
        request_.xfer = m_.xfer;
        CopySet(request_.set, m_.offer.set);
        request_.mode = config_.mode;
        request_.rejoin = config_.rejoin ? 1 : 0;
        UtcNow(request_.created_utc);
        if (Action* a = Emit(ActionKind::write_request)) {
            a->xfer = m_.xfer;
            a->rejoin = request_.rejoin;
        }
        return;  // countdown once request.json is written
    }
    m_.guest = GuestPhase::staged;  // stage mode: never request.json, never an exit
    SetToken(Token::staged, 0);
    PostToast(Toast::g3_stage_only, 0, 0);
}

void Core::GuestFail(wire::Fail fail, wire::Kind tell, std::uint64_t now_ms) noexcept {
    if (tell == wire::Kind::result) {
        SendSmall(wire::Kind::result, m_.xfer, U8(wire::ResultStatus::failed), U8(fail));
    } else if (tell == wire::Kind::abort) {
        const wire::AbortReason reason = AbortFor(fail);
        SendSmall(wire::Kind::abort, m_.xfer, U8(reason), 0);
        Log("save_handoff=abort; why=%s; xfer=%08X", wire::AbortName(reason), m_.xfer);
    }
    Log("save_handoff=failed; why=%s", wire::FailName(fail));
    if (Action* d = Emit(ActionKind::delete_stage)) d->xfer = m_.xfer;
    m_.guest = GuestPhase::idle;
    m_.stage_ok = m_.request_written = m_.result_sent = m_.result_retry = false;
    m_.sticky = true;
    view_.pct = 0;
    SetToken(Token::failed, U8(fail));
    PostToast(Toast::g7_failed, U8(fail), 0);
    (void)now_ms;
}

void Core::GuestDropPartial(const char* why) noexcept {
    Log("save_handoff=abort; why=%s; xfer=%08X", why, m_.xfer);
    if (Action* d = Emit(ActionKind::delete_stage)) d->xfer = m_.xfer;
    m_.guest = GuestPhase::idle;
    view_.pct = 0;
}

void Core::GuestAbortStage(wire::AbortReason reason, std::uint64_t now_ms) noexcept {
    SendSmall(wire::Kind::abort, m_.xfer, U8(reason), 0);
    Log("save_handoff=abort; why=%s; xfer=%08X", wire::AbortName(reason), m_.xfer);
    // delete_stage removes request.json first, then the staged files.
    if (Action* d = Emit(ActionKind::delete_stage)) d->xfer = m_.stage_xfer;
    m_.guest = GuestPhase::idle;
    m_.stage_ok = m_.request_written = m_.result_sent = m_.result_retry = false;
    m_.sticky = true;
    view_.pct = 0;
    if (reason == wire::AbortReason::user) {
        SetToken(Token::kept, U8(wire::DeclineReason::user));
        PostToast(Toast::g8_kept, 0, 0);
    } else {
        const wire::Fail fail = FailFor(reason);
        Log("save_handoff=failed; why=%s", wire::FailName(fail));
        SetToken(Token::failed, U8(fail));
        PostToast(Toast::g7_failed, U8(fail), 0);
    }
    (void)now_ms;
}

// Exiting, yet the game still runs: a veto (user) or the guest's own Continue
// (not_title) drops the request and the stage like the countdown does. The
// host's partner_restarting (done) accepts the ABORT. Staleness never cancels
// here: reports stop during a legitimate slow WM_CLOSE teardown.
void Core::GuestCancelExit(wire::AbortReason reason, std::uint64_t now_ms) noexcept {
    GuestAbortStage(reason, now_ms);
    m_.exit_posted = m_.exit_window = m_.exit_slow = false;
    m_.exit_pending = m_.exit_unsent = false;  // a WM_CLOSE still waiting for the flush never goes out
}

}  // namespace mgscoop::save_handoff_core
