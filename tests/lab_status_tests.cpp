// Pure lab-status formatting checks: no filesystem, game or Steam access.
#include "mgscoop/lab_status.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
std::string Json(const mgscoop::lab::Status& status) {
    char buffer[mgscoop::lab::kStatusJsonCapacity]{};
    std::size_t length = 0;
    Check(mgscoop::lab::FormatStatusJson(status, buffer, sizeof(buffer), length), "format status json");
    Check(length == std::strlen(buffer), "json length excludes NUL");
    return std::string(buffer, length);
}
std::string JsonString(const char* text) {
    char buffer[256]{};
    std::size_t length = 0;
    Check(mgscoop::lab::AppendJsonString(buffer, sizeof(buffer), length, text), "append json string");
    return std::string(buffer, length);
}

// A recording AtomicWriteOps backend: models tmp create/write/close/replace.
struct FakeFs {
    std::string committed;       // content under the final path
    std::string pending;         // content under <path>.tmp
    bool tmp_open = false;
    bool tmp_present = false;
    int fail_create = 0, fail_write = 0, fail_close = 0, fail_replace = 0;
    std::vector<std::string> events;
};
void* FsCreate(void* ctx, const wchar_t*) noexcept {
    auto* fs = static_cast<FakeFs*>(ctx);
    fs->events.push_back("create");
    if (fs->fail_create) return nullptr;
    fs->tmp_open = true;
    fs->tmp_present = true;
    fs->pending.clear();
    return fs;  // non-null handle sentinel
}
bool FsWrite(void* ctx, void*, const void* data, std::size_t size) noexcept {
    auto* fs = static_cast<FakeFs*>(ctx);
    fs->events.push_back("write");
    if (fs->fail_write) return false;
    fs->pending.assign(static_cast<const char*>(data), size);
    return true;
}
bool FsClose(void* ctx, void*) noexcept {
    auto* fs = static_cast<FakeFs*>(ctx);
    fs->events.push_back("close");
    fs->tmp_open = false;
    return fs->fail_close == 0;
}
bool FsReplace(void* ctx, const wchar_t*, const wchar_t*) noexcept {
    auto* fs = static_cast<FakeFs*>(ctx);
    fs->events.push_back("replace");
    if (fs->fail_replace) return false;
    fs->committed = fs->pending;
    fs->tmp_present = false;
    return true;
}
bool FsRemove(void* ctx, const wchar_t*) noexcept {
    auto* fs = static_cast<FakeFs*>(ctx);
    fs->events.push_back("remove");
    fs->tmp_present = false;
    return true;
}
mgscoop::lab::AtomicWriteOps FakeOps(FakeFs& fs) {
    mgscoop::lab::AtomicWriteOps ops{};
    ops.context = &fs;
    ops.create = &FsCreate;
    ops.write = &FsWrite;
    ops.close = &FsClose;
    ops.replace = &FsReplace;
    ops.remove = &FsRemove;
    return ops;
}
} // namespace

int main() {
    using namespace mgscoop::lab;

    // Enum spellings, including out-of-range values mapping to the first word.
    Check(std::strcmp(RoleName(Role::host), "host") == 0, "role host");
    Check(std::strcmp(RoleName(Role::guest), "guest") == 0, "role guest");
    Check(std::strcmp(RoleName(static_cast<Role>(99)), "none") == 0, "role unknown -> none");
    Check(std::strcmp(ModeName(Mode::host_v3), "host_v3") == 0, "mode host_v3");
    Check(std::strcmp(ModeName(Mode::client_v1), "client_v1") == 0, "mode client_v1");
    Check(std::strcmp(ModeName(Mode::client_check_v1), "client_check_v1") == 0, "mode client_check_v1");
    Check(std::strcmp(ModeName(static_cast<Mode>(99)), "off") == 0, "mode unknown -> off");
    const char* session_words[] = {"none", "host_ready", "host_peer_present", "client_requested",
                                    "client_established", "session_error", "closed"};
    for (unsigned i = 0; i < 7; ++i)
        Check(std::strcmp(SessionStatusName(static_cast<SessionStatus>(i)), session_words[i]) == 0, "session word");
    Check(std::strcmp(SessionStatusName(static_cast<SessionStatus>(99)), "none") == 0, "session unknown -> none");
    const char* join_words[] = {"idle", "armed", "requested", "refused", "failed", "established"};
    for (unsigned i = 0; i < 6; ++i)
        Check(std::strcmp(JoinStatusName(static_cast<JoinStatus>(i)), join_words[i]) == 0, "join word");
    Check(std::strcmp(JoinStatusName(static_cast<JoinStatus>(99)), "idle") == 0, "join unknown -> idle");

    // JSON string escaping.
    Check(JsonString("plain") == "\"plain\"", "plain escape");
    Check(JsonString("a\"b\\c") == "\"a\\\"b\\\\c\"", "quote/backslash escape");
    Check(JsonString("x\ny\tz\r") == "\"x\\ny\\tz\\r\"", "control escape");
    { char one[2]{}; one[0] = 0x01; Check(JsonString(one) == "\"\\u0001\"", "low control hex escape"); }
    { char hi[2]{}; hi[0] = static_cast<char>(0xC3); Check(JsonString(hi) == "\"?\"", "high byte replaced"); }
    Check(JsonString("") == "\"\"", "empty string");
    // Overflow leaves length unchanged.
    { char tiny[4]{}; std::size_t len = 0; Check(!AppendJsonString(tiny, sizeof(tiny), len, "toolong"), "overflow fails");
      Check(len == 0, "overflow preserves length"); }

    // Default document.
    Status status{};
    status.pid = 1234;
    const std::string def = Json(status);
    Check(def ==
        "{\"version\":1,\"pid\":1234,\"role\":\"none\",\"mode\":\"off\",\"mission\":0,"
        "\"session_status\":\"none\",\"members\":0,\"local_index\":-1,\"join\":\"idle\","
        "\"join_attempts\":0,\"last_refusal\":\"\",\"updated_tick_ms\":0}\n", "default json");

    // Populated document with a negative index and an escaped refusal.
    Status guest{};
    guest.pid = 52824;
    guest.role = Role::guest;
    guest.mode = Mode::client_v1;
    guest.mission = 30050;
    guest.session = SessionStatus::client_established;
    guest.members = 2;
    guest.local_index = 1;
    guest.join = JoinStatus::established;
    guest.join_attempts = 3;
    SetRefusal(guest, "placeholder_host_id");
    guest.updated_tick_ms = 123456789ULL;
    const std::string populated = Json(guest);
    Check(populated.find("\"role\":\"guest\"") != std::string::npos, "guest role in json");
    Check(populated.find("\"session_status\":\"client_established\"") != std::string::npos, "established in json");
    Check(populated.find("\"local_index\":1") != std::string::npos, "local index 1");
    Check(populated.find("\"join_attempts\":3") != std::string::npos, "attempts in json");
    Check(populated.find("\"last_refusal\":\"placeholder_host_id\"") != std::string::npos, "refusal in json");
    Check(populated.find("\"updated_tick_ms\":123456789") != std::string::npos, "tick in json");

    // SameContent ignores updated_tick_ms only.
    Status a = guest, b = guest;
    Check(SameContent(a, b), "identical same");
    b.updated_tick_ms = 999;
    Check(SameContent(a, b), "tick ignored by SameContent");
    b.members = 1;
    Check(!SameContent(a, b), "member change differs");
    b = guest;
    SetRefusal(b, "not_ready");
    Check(!SameContent(a, b), "refusal change differs");

    // SetRefusal truncates and null clears.
    Status t{};
    std::string long_reason(200, 'x');
    SetRefusal(t, long_reason.c_str());
    Check(std::strlen(t.last_refusal) == kRefusalCapacity - 1, "refusal truncated");
    SetRefusal(t, nullptr);
    Check(t.last_refusal[0] == 0, "null refusal clears");

    // Literal mapping (bare word, receipt form, keep-previous cases).
    SessionStatus mapped = SessionStatus::none;
    Check(SessionStatusFromLiteral("host_ready", mapped) && mapped == SessionStatus::host_ready, "bare host_ready");
    Check(SessionStatusFromLiteral("return 'host_peer_present'", mapped) && mapped == SessionStatus::host_peer_present, "receipt peer");
    Check(SessionStatusFromLiteral("return 'client_connecting'", mapped) && mapped == SessionStatus::client_requested, "connecting -> requested");
    Check(SessionStatusFromLiteral("return 'client_established'", mapped) && mapped == SessionStatus::client_established, "established literal");
    Check(SessionStatusFromLiteral("return 'session_error'", mapped) && mapped == SessionStatus::session_error, "error literal");
    Check(SessionStatusFromLiteral("return 'host_not_accepting'", mapped) && mapped == SessionStatus::none, "not_accepting -> none");
    mapped = SessionStatus::host_ready;
    Check(!SessionStatusFromLiteral("return 'unavailable'", mapped) && mapped == SessionStatus::host_ready, "unavailable keeps previous");
    Check(!SessionStatusFromLiteral("return 'bogus'", mapped), "unknown keeps previous");
    Check(!SessionStatusFromLiteral(nullptr, mapped), "null literal");

    // session_status log line.
    char line[64]{};
    Check(FormatSessionStatusLine(SessionStatus::host_ready, line, sizeof(line)) &&
          std::strcmp(line, "session_status=host_ready\r\n") == 0, "status line host_ready");
    Check(FormatSessionStatusLine(SessionStatus::client_established, line, sizeof(line)) &&
          std::strcmp(line, "session_status=client_established\r\n") == 0, "status line established");
    { char small[4]{}; Check(!FormatSessionStatusLine(SessionStatus::host_ready, small, sizeof(small)), "status line overflow"); }

    // SteamID line.
    char id_line[32]{};
    std::size_t id_len = 0;
    Check(FormatSteamIdLine(76561198000000001ULL, id_line, sizeof(id_line), id_len) &&
          std::strcmp(id_line, "76561198000000001\n") == 0, "steamid line");
    Check(id_len == 18, "steamid line length");
    { char small[4]{}; std::size_t l = 0; Check(!FormatSteamIdLine(76561198000000001ULL, small, sizeof(small), l), "steamid overflow"); }

    // Temporary path derivation.
    wchar_t tmp[64]{};
    Check(MakeTemporaryPath(L"C:\\a\\b.json", tmp, 64) && std::wcscmp(tmp, L"C:\\a\\b.json.tmp") == 0, "tmp path");
    { wchar_t small[4]{}; Check(!MakeTemporaryPath(L"longname.json", small, 4), "tmp path overflow"); }

    // Atomic write happy path: tmp written, closed, then replaced; no leftover tmp.
    {
        FakeFs fs;
        const char payload[] = "hello";
        Check(WriteFileAtomic(FakeOps(fs), L"out.json", payload, 5), "atomic write ok");
        Check(fs.committed == "hello", "committed content");
        Check(!fs.tmp_present, "no leftover tmp");
        Check(fs.events.size() == 4 && fs.events[0] == "create" && fs.events[1] == "write" &&
              fs.events[2] == "close" && fs.events[3] == "replace", "happy path order");
    }
    // Create failure: nothing written.
    { FakeFs fs; fs.fail_create = 1; Check(!WriteFileAtomic(FakeOps(fs), L"out.json", "x", 1), "create fail");
      Check(fs.committed.empty() && !fs.tmp_present, "create fail leaves nothing"); }
    // Write failure: tmp removed, previous content intact.
    { FakeFs fs; fs.committed = "old"; fs.fail_write = 1;
      Check(!WriteFileAtomic(FakeOps(fs), L"out.json", "new", 3), "write fail");
      Check(fs.committed == "old" && !fs.tmp_present, "write fail keeps old, removes tmp");
      Check(fs.events.back() == "remove", "write fail removes tmp"); }
    // Replace failure: tmp removed, previous content intact.
    { FakeFs fs; fs.committed = "old"; fs.fail_replace = 1;
      Check(!WriteFileAtomic(FakeOps(fs), L"out.json", "new", 3), "replace fail");
      Check(fs.committed == "old" && !fs.tmp_present, "replace fail keeps old, removes tmp");
      Check(fs.events.back() == "remove", "replace fail removes tmp"); }
    // Zero-length write is valid.
    { FakeFs fs; Check(WriteFileAtomic(FakeOps(fs), L"empty.json", "", 0), "empty write ok");
      Check(fs.committed.empty(), "empty committed"); }
    // Null data with nonzero size is rejected.
    { FakeFs fs; Check(!WriteFileAtomic(FakeOps(fs), L"x.json", nullptr, 4), "null data rejected"); }

    std::printf("%u lab-status checks: enum spellings, JSON escaping/document, SameContent, refusal truncation, "
                "literal mapping, status/steamid lines, atomic write happy/failure paths; no filesystem or game calls\n", checks);
    return 0;
}
