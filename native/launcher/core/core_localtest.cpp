// Diamond Dogs Together launcher core: the developer's local two-window test
// (see core_localtest.hpp). Ported from tools/dev/Play-LocalCoop.ps1,
// LocalCoopSession.psm1 and LocalCoopWindows.psm1; the journal files and their
// format are the PowerShell module's, so either tool restores the other's runs.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core_localtest.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <regex>

#include "core_install.hpp"
#include "core_log.hpp"
#include "core_session.hpp"
#include "core_text.hpp"
#include "core_util.hpp"
#include "launcher/hash.hpp"

namespace ddt::core {
namespace {

const char* const kActivationNames[4] = {"lab.ini", "coop.ini", "link-secret.txt", "script-observe.enabled"};
const char* const kDeveloperMarkers[6] = {"partner.trigger", "world.trigger", "join.trigger", "reset.trigger", "worker.ini", "worker.enabled"};
const char* const kLogNames[3] = {"early.log", "steam-observer.log", "script-observer.log"};
const char* const kRoles[2] = {"host", "guest"};
constexpr std::size_t kMaxActivationBytes = 262144;
constexpr std::uint64_t kMaxJournalBytes = 2097152;
constexpr std::int64_t kEvidenceDeadlineMs = 120000;

bool IsActivationName(const std::string& name) {
    for (const char* n : kActivationNames)
        if (name == n) return true;
    return false;
}

// ---- Base64 (the journal keeps the original bytes as text) ------------------------------------
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64(const std::string& bytes) {
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                           static_cast<unsigned char>(bytes[i + 2]);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
    }
    if (i + 1 == bytes.size()) {
        const unsigned v = static_cast<unsigned char>(bytes[i]) << 16;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

bool FromBase64(const std::string& text, std::string& out) {
    out.clear();
    if (text.size() % 4) return false;
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=' && i + 4 == text.size() && k >= 2) {
                v[k] = 0;
                ++pad;
                continue;
            }
            if (pad) return false;
            v[k] = value(c);
            if (v[k] < 0) return false;
        }
        const unsigned n = (static_cast<unsigned>(v[0]) << 18) | (static_cast<unsigned>(v[1]) << 12) | (static_cast<unsigned>(v[2]) << 6) |
                           static_cast<unsigned>(v[3]);
        out += static_cast<char>((n >> 16) & 0xFF);
        if (pad < 2) out += static_cast<char>((n >> 8) & 0xFF);
        if (pad < 1) out += static_cast<char>(n & 0xFF);
    }
    return true;
}

bool IsUpperHex64(const std::string& text) {
    if (text.size() != 64) return false;
    for (char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

bool IsLowerHex32(const std::string& text) {
    if (text.size() != 32) return false;
    for (char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::wstring DriveRootOf(const std::wstring& full) {
    if (full.size() >= 3 && full[1] == L':' && (full[2] == L'\\' || full[2] == L'/')) return full.substr(0, 3);
    return full;
}

// Assert-NoReparsePoint without -StopAt: the path and every existing ancestor.
bool AnyReparse(const std::wstring& path) { return HasReparsePoint(FullPath(path), DriveRootOf(FullPath(path))); }

// Two folders that are not the same and do not contain each other.
bool Distinct(const std::wstring& a, const std::wstring& b) { return !PathInside(a, b) && !PathInside(b, a); }

std::string Join(const std::vector<std::string>& parts, const std::string& with) {
    std::string out;
    for (const std::string& p : parts) {
        if (!out.empty()) out += with;
        out += p;
    }
    return out;
}

// Both folders' play mutexes (the same names the PowerShell tools use), in a fixed order.
class RootLocks {
public:
    RootLocks(const std::wstring& a, const std::wstring& b) {
        std::wstring first = FullPath(a), second = FullPath(b);
        if (UpperInvariant(second) < UpperInvariant(first)) std::swap(first, second);
        first_ = std::make_unique<PlayMutex>(first);
        second_ = std::make_unique<PlayMutex>(second);
        ok_ = first_->Lock() && second_->Lock();
    }
    bool ok() const { return ok_; }

private:
    std::unique_ptr<PlayMutex> first_, second_;
    bool ok_ = false;
};

// Get-FriendSaveDirProblem: "" when acceptable as lab.ini save_dir.
std::string SaveDirProblem(const std::string& path) {
    if (path.empty()) return "save_dir is empty.";
    const bool drive = path.size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
                       path[2] == '\\';
    if (!drive && !StartsWith(path, "\\\\")) return "save_dir must be an absolute path.";
    for (unsigned char c : path)
        if (c < 0x20 || c == 0x7F) return "save_dir contains control characters.";
    if (path != Trim(path)) return "save_dir has leading or trailing blanks.";
    std::string t = path;
    while (!t.empty() && (t.back() == '\\' || t.back() == '/')) t.pop_back();
    if (t.size() > kMaxSaveDirBytes)
        return "The game folder path is too long for the save copy (" + std::to_string(t.size()) + " bytes; the limit is " +
               std::to_string(kMaxSaveDirBytes) + ").";
    return {};
}

// Get-LocalCoopRoot: "" when `root` is an existing game folder with mgsvtpp.exe and an mgs-coop folder.
std::string LocalRootProblem(const std::wstring& root) {
    if (root.size() < 3 || !(root[1] == L':' || StartsWith(Utf8(root), "\\\\"))) return "A local game root must be an absolute path.";
    const std::wstring full = FullPath(root);
    if (IsDriveRoot(full) || !IsDir(full)) return "Not an existing game folder: " + Utf8(full);
    if (AnyReparse(full)) return "Filesystem links are not supported here: " + Utf8(full);
    if (!IsFile(JoinPath2(full, L"mgsvtpp.exe"))) return "No mgsvtpp.exe in " + Utf8(full);
    const std::wstring mod = JoinPath2(full, L"mgs-coop");
    if (HasReparsePoint(mod, full)) return "Filesystem links are not supported here: " + Utf8(mod);
    if (!IsDir(mod)) return "No owned mod folder in " + Utf8(full);
    return {};
}

bool ReadSmallFile(const std::wstring& path, std::string& out, std::uint64_t limit) {
    out.clear();
    Bytes bytes;
    std::string error;
    if (!ReadFileShared(path, bytes, limit, error)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

bool AliveAt(Env& env, std::int64_t pid, const std::string& start) {
    if (pid <= 0) return false;
    std::int64_t ms = 0;
    if (start.empty() || !ParseUtc(start, ms)) return env.ProcessAlive(static_cast<std::uint32_t>(pid));
    return ProcessAliveSince(env, static_cast<std::uint32_t>(pid), ms);
}

bool AnyGameIn(Env& env, const json::Value& session) {
    return AnyGameRunningIn(env, Wide(session.Str("host_root"))) || AnyGameRunningIn(env, Wide(session.Str("guest_root")));
}

std::wstring RootOf(const json::Value& session, const std::string& role) {
    return Wide(session.Str(role == "host" ? "host_root" : "guest_root"));
}

std::wstring ActivationPath(const json::Value& session, const std::string& role, const std::string& name) {
    return JoinPath2(JoinPath2(RootOf(session, role), L"mgs-coop"), Wide(name));
}

// "" or the sentence why an activation target cannot be used.
std::string ActivationTargetProblem(const json::Value& session, const std::string& role, const std::string& name) {
    const std::wstring path = ActivationPath(session, role, name);
    if (HasReparsePoint(path, RootOf(session, role))) return "Filesystem links are not supported here: " + Utf8(path);
    if (IsDir(path)) return "A directory occupies the target file: " + Utf8(path);
    return {};
}

std::string FileHash(const std::wstring& path) {
    std::string hex;
    return Sha256FileHex(path, hex) ? hex : std::string();
}

json::Value* FilesOf(json::Value& session) {
    json::Value& files = session["files"];
    if (!files.is_array()) files = json::Value::Array();
    return &files;
}

// Token `tok` in a log line: at the start or after "; ", followed by ';' or the end.
bool HasToken(const std::string& line, const std::string& tok) {
    for (std::size_t at = line.find(tok); at != std::string::npos; at = line.find(tok, at + 1)) {
        const bool start = at == 0 || (at >= 2 && line.compare(at - 2, 2, "; ") == 0);
        const std::size_t end = at + tok.size();
        if (start && (end == line.size() || line[end] == ';')) return true;
    }
    return false;
}

bool ImageBelongs(const std::wstring& image, const std::wstring& root) {
    if (image.empty() || root.empty()) return false;
    std::wstring p = image;
    if (StartsWith(Utf8(p), "\\\\?\\UNC\\")) p = L"\\\\" + p.substr(8);
    else if (StartsWith(Utf8(p), "\\\\?\\")) p = p.substr(4);
    return IEquals(FullPath(p), JoinPath2(FullPath(root), L"mgsvtpp.exe"));
}

bool Intersects(const LocalRect& a, const LocalRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

std::string RectKey(const LocalRect& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + "," + std::to_string(r.height);
}

// jsoncpp FastWriter pieces.
bool FastString(const std::string& s, std::string& out) {
    static const char kHex[] = "0123456789abcdef";
    out += '"';
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c > 0x7E) return false;
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += kHex[c >> 4];
                out += kHex[c & 15];
            } else {
                out += ch;
            }
        }
    }
    out += '"';
    return true;
}

bool FastTo(const json::Value& v, std::string& out) {
    switch (v.type()) {
    case json::Type::null: out += "null"; return true;
    case json::Type::boolean: out += v.AsBool() ? "true" : "false"; return true;
    case json::Type::number:
        if (!v.is_integer()) return false;   // the game's config holds no floating-point values
        out += std::to_string(v.AsInt());
        return true;
    case json::Type::string: return FastString(v.StringRef(), out);
    case json::Type::array: {
        out += '[';
        bool first = true;
        for (const json::Value& e : v.Elements()) {
            if (!first) out += ',';
            first = false;
            if (!FastTo(e, out)) return false;
        }
        out += ']';
        return true;
    }
    case json::Type::object: {
        std::vector<const std::pair<std::string, json::Value>*> items;
        for (const auto& kv : v.Items()) {
            for (char c : kv.first)
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
            items.push_back(&kv);
        }
        std::sort(items.begin(), items.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
        out += '{';
        bool first = true;
        for (const auto* kv : items) {
            if (!first) out += ',';
            first = false;
            if (!FastString(kv->first, out)) return false;
            out += ':';
            if (!FastTo(kv->second, out)) return false;
        }
        out += '}';
        return true;
    }
    }
    return false;
}

// Exactly one match of `re` in `text`, replaced by group 1 + value + group 2.
bool ReplaceOnce(std::string& text, const std::regex& re, const std::string& value, std::string& error, const char* what) {
    const auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    const std::ptrdiff_t count = std::distance(begin, std::sregex_iterator());
    if (count != 1) {
        error = "TPP_GRAPHICS_CONFIG has an unexpected layout (" + std::to_string(count) + " matches for " + what + ").";
        return false;
    }
    const std::smatch m = *begin;
    text = m.prefix().str() + m[1].str() + value + m[2].str() + m.suffix().str();
    return true;
}

}  // namespace

// ---- Pure text -------------------------------------------------------------------------------------
bool MakeLocalLabIniText(const LocalLabParams& p, std::string& text, std::string& error) {
    text.clear();
    error.clear();
    if (p.role != "host" && p.role != "guest") {
        error = "The local role must be host or guest.";
        return false;
    }
    if (p.partner != "native" && p.partner != "clips") {
        error = "The partner must be native or clips.";
        return false;
    }
    if (p.world < 0 || p.world > 3) {
        error = "world must be 0..3.";
        return false;
    }
    error = SaveDirProblem(p.save_dir);
    if (!error.empty()) return false;
    auto bad_port = [](int port) { return port < 1024 || port > 65535 || port == 47810 || port == 47811 || port == 47899; };
    if (p.host_port == p.guest_port || bad_port(p.host_port) || bad_port(p.guest_port)) {
        error = "Local partner ports must differ and must not reuse the local lab or closed master ports.";
        return false;
    }
    if (p.host_steam != "learn" && !IsIndividualSteamId64(p.host_steam)) {
        error = "Invalid host Steam identity for the local guest.";
        return false;
    }
    if (p.host_steam == kLocalGuestSteamId) {
        error = "The real host and local guest must have different identities.";
        return false;
    }
    const bool guest = p.role == "guest";
    std::vector<std::string> lines = {kLocalLabHeader,
                                      "enabled=1",
                                      "mode=lab",
                                      "role=" + p.role,
                                      "loopback=1",
                                      std::string("self_steamid=") + (guest ? kLocalGuestSteamId : "real"),
                                      "peer_steamid=" + (guest ? p.host_steam : std::string(kLocalGuestSteamId)),
                                      std::string("listen_port=") + (guest ? "47811" : "47810"),
                                      std::string("peer_port=") + (guest ? "47810" : "47811"),
                                      "save_dir=" + p.save_dir,
                                      std::string("xinput=") + (guest ? "0" : "none"),
                                      "join_trigger=0",
                                      "net=1",
                                      "net_bind=loopback",
                                      "net_listen_port=" + std::to_string(guest ? p.guest_port : p.host_port),
                                      "net_peer_port=" + std::to_string(guest ? p.host_port : p.guest_port),
                                      "partner_interp=auto"};
    if (guest) {
        lines.push_back("self_name=Guest");
        lines.push_back("mutex_suffix=-guest");
    }
    for (const auto& kv : PartnerDefaults()) lines.push_back(std::string(kv.first) + "=" + kv.second);
    lines.push_back("partner_input_send=1");
    if (p.partner == "native") lines.push_back("partner_native_profile=gun");
    if (p.world > 0) lines.push_back("world=" + std::to_string(p.world));
    for (const std::string& l : lines) text += l + "\n";
    return true;
}

bool MakeLocalCoopIniText(const std::string& character, const std::string& hud, std::string& text, std::string& error) {
    text.clear();
    static const char* const kCharacters[] = {"default", "quiet", "dd_male", "dd_female", "snake", "avatar", "ocelot", "story"};
    if (std::none_of(std::begin(kCharacters), std::end(kCharacters), [&](const char* c) { return character == c; })) {
        error = "Invalid character: " + character;
        return false;
    }
    const std::string h = LowerAscii(hud);
    if (!h.empty() && h != "compact" && h != "full" && h != "off") {
        error = "Invalid hud: " + hud;
        return false;
    }
    text = std::string(kOldPlayHeader) + "\nmaster_url=" + kMasterUrl + "\ncharacter=" + character + "\ncharacter_fallback=dd_male\n";
    if (!h.empty()) text += "hud=" + h + "\n";
    return true;
}

// ---- TPP_GRAPHICS_CONFIG ------------------------------------------------------------------------------
bool JsonCppFastText(const json::Value& value, std::string& out, bool document) {
    out.clear();
    if (!FastTo(value, out)) {
        out.clear();
        return false;
    }
    if (document) out += "\n";
    return true;
}

bool GraphicsTag(const json::Value& config, std::string& tag) {
    tag.clear();
    if (!config.is_object()) return false;
    json::Value copy = json::Value::Object();
    for (const auto& kv : config.Items())
        if (kv.first != "as") copy[kv.first] = kv.second;
    std::string serial;
    if (!JsonCppFastText(copy, serial, true)) return false;
    char hex[32];
    std::snprintf(hex, sizeof(hex), "%zx", serial.size());
    const std::string salted = serial + "-" + hex;
    Md5Digest digest{};
    if (!Md5(salted.data(), salted.size(), digest)) return false;
    tag = Base64(std::string(reinterpret_cast<const char*>(digest.data()), 8));
    return true;
}

GraphicsInfo ReadGraphicsInfo(const std::string& text) {
    GraphicsInfo info;
    json::Value config;
    std::string error;
    if (!json::Parse(text, config, &error) || !config.is_object()) {
        info.error = "TPP_GRAPHICS_CONFIG is not valid JSON. " + error;
        return info;
    }
    info.stored_tag = config.Str("as");
    if (!GraphicsTag(config, info.computed_tag)) {
        info.error = "TPP_GRAPHICS_CONFIG holds values the tag cannot cover.";
        return info;
    }
    const json::Value* graphics = config.Find("graphics");
    const json::Value* video = graphics ? graphics->Find("videoout_setting") : nullptr;
    if (video) {
        info.window_mode = video->Str("window_mode");
        info.width = static_cast<int>(video->Int("width", 0));
        info.height = static_cast<int>(video->Int("height", 0));
    }
    info.valid = !info.stored_tag.empty() && info.stored_tag == info.computed_tag;
    return info;
}

bool SetGraphicsWindowed(const std::string& text, int width, int height, std::string& out, std::string& error) {
    out.clear();
    if (width < 640 || width > 7680 || height < 480 || height > 4320) {
        error = "Unsupported window size.";
        return false;
    }
    const GraphicsInfo before = ReadGraphicsInfo(text);
    if (!before.valid) {
        error = "TPP_GRAPHICS_CONFIG tag does not verify; refusing to edit. " + before.error;
        return false;
    }
    std::string result = text;
    static const std::regex kMode(R"re(("window_mode"\s*:\s*")[A-Za-z]+("))re");
    static const std::regex kWidth(R"re(("width"\s*:\s*)[0-9]+())re");
    static const std::regex kHeight(R"re(("height"\s*:\s*)[0-9]+())re");
    static const std::regex kAs(R"re(("as"\s*:\s*")[A-Za-z0-9+/=]+("))re");
    if (!ReplaceOnce(result, kMode, "Windowed", error, "window_mode") || !ReplaceOnce(result, kWidth, std::to_string(width), error, "width") ||
        !ReplaceOnce(result, kHeight, std::to_string(height), error, "height"))
        return false;
    json::Value config;
    std::string tag;
    if (!json::Parse(result, config) || !GraphicsTag(config, tag)) {
        error = "Internal error: edited TPP_GRAPHICS_CONFIG does not parse.";
        return false;
    }
    if (!ReplaceOnce(result, kAs, tag, error, "as")) return false;
    const GraphicsInfo after = ReadGraphicsInfo(result);
    if (!after.valid || after.window_mode != "Windowed" || after.width != width || after.height != height) {
        error = "Internal error: edited TPP_GRAPHICS_CONFIG does not verify.";
        return false;
    }
    out = result;
    return true;
}

// ---- Layout and placement ---------------------------------------------------------------------------
bool ComputeLocalLayout(const LocalRect& area, int dpi, LocalLayout& out, std::string& error) {
    out = LocalLayout{};
    const long long x = area.x, y = area.y, width = area.width, height = area.height;
    if (width < 2 || height < 1 || width > 32768 || height > 32768 || x + width > 2147483647LL || y + height > 2147483647LL) {
        error = "The monitor working area has an invalid rectangle.";
        return false;
    }
    if (dpi < 48 || dpi > 768) {
        error = "The monitor DPI is invalid.";
        return false;
    }
    const long long gap = static_cast<long long>(std::nearbyint(8.0 * dpi / 96.0));   // banker's rounding, like [Math]::Round
    const long long frame = static_cast<long long>(std::ceil(8.0 * dpi / 96.0));
    const long long caption = static_cast<long long>(std::ceil(23.0 * dpi / 96.0));
    const long long extra_w = 2 * frame, extra_h = 2 * frame + caption;
    const long long min_w = 640 + extra_w, min_h = 480 + extra_h;
    out.dpi = dpi;
    if (width >= 2 * min_w + gap && height >= min_h) {
        const long long host_w = static_cast<long long>(std::floor((width - gap) / 2.0));
        const long long guest_w = width - gap - host_w;
        const long long client_w = std::min<long long>(7680, host_w - extra_w);
        const long long client_h = static_cast<long long>(std::min<double>(
            static_cast<double>(std::min<long long>(4320, height - extra_h)), std::max(480.0, std::ceil(client_w * 9.0 / 16.0))));
        const long long outer_h = client_h + extra_h;
        const long long top = y + static_cast<long long>(std::floor((height - outer_h) / 2.0));
        out.host = LocalRect{static_cast<int>(x), static_cast<int>(top), static_cast<int>(host_w), static_cast<int>(outer_h)};
        out.guest = LocalRect{static_cast<int>(x + host_w + gap), static_cast<int>(top), static_cast<int>(guest_w), static_cast<int>(outer_h)};
        out.host_client_width = static_cast<int>(client_w);
        out.host_client_height = static_cast<int>(client_h);
        out.guest_client_width = static_cast<int>(std::min<long long>(7680, guest_w - extra_w));
        out.guest_client_height = static_cast<int>(client_h);
        out.vertical = false;
        return true;
    }
    if (width >= min_w && height >= 2 * min_h + gap) {
        const long long slot_h = static_cast<long long>(std::floor((height - gap) / 2.0));
        const long long client_h = std::min<long long>(4320, slot_h - extra_h);
        const long long client_w = static_cast<long long>(std::min<double>(
            static_cast<double>(std::min<long long>(7680, width - extra_w)), std::max(640.0, std::floor(client_h * 16.0 / 9.0))));
        const long long outer_w = client_w + extra_w, outer_h = client_h + extra_h;
        const long long left = x + static_cast<long long>(std::floor((width - outer_w) / 2.0));
        out.host = LocalRect{static_cast<int>(left), static_cast<int>(y), static_cast<int>(outer_w), static_cast<int>(outer_h)};
        out.guest = LocalRect{static_cast<int>(left), static_cast<int>(y + outer_h + gap), static_cast<int>(outer_w), static_cast<int>(outer_h)};
        out.host_client_width = out.guest_client_width = static_cast<int>(client_w);
        out.host_client_height = out.guest_client_height = static_cast<int>(client_h);
        out.vertical = true;
        return true;
    }
    error = "This display is too small for two MGSV windows at 640x480 with their window borders. Use a larger display or reduce "
            "Windows display scaling.";
    return false;
}

PlacementDecision DecidePlacement(const PlacementEntry* previous, std::uint64_t handle, const LocalRect& window, const LocalRect& target,
                                  std::int64_t now, bool retile, bool moving, bool overlapping) {
    PlacementDecision d;
    const std::string key = RectKey(window);
    PlacementEntry e;
    if (!previous || previous->handle != handle) {
        e.handle = handle;
        e.seen_ms = e.stable_since_ms = now;
        e.last_key = key;
    } else {
        e = *previous;
        if (e.last_key != key) {
            e.last_key = key;
            e.stable_since_ms = now;
        }
    }
    d.reason = "settled";
    auto off = [](int a, int b) { return std::abs(a - b) > 2; };
    if (moving) {
        d.reason = "user_moving";
        e.user_arranged = true;
    } else if (retile) {
        d.place = true;
        d.reason = "retile";
        e.placed = true;
        e.placed_ms = now;
        e.deadline_ms = now + 6000;
        e.corrections = 0;
        e.user_arranged = false;
    } else if (e.user_arranged) {
        d.reason = "user_arranged";
    } else if (!e.placed) {
        if (now - e.seen_ms >= 300 && now - e.stable_since_ms >= 300) {
            d.place = true;
            d.reason = "initial";
            e.placed = true;
            e.placed_ms = now;
            e.deadline_ms = now + 6000;
        } else {
            d.reason = "window_settling";
        }
    } else if (now < e.deadline_ms && e.corrections < 2 &&
               (off(window.width, target.width) || off(window.height, target.height) ||
                (overlapping && (off(window.x, target.x) || off(window.y, target.y))))) {
        if (now - e.placed_ms >= 750 && now - e.stable_since_ms >= 500) {
            d.place = true;
            d.reason = "startup_reset";
            e.placed_ms = now;
            ++e.corrections;
        } else {
            d.reason = "reset_debounce";
        }
    }
    d.entry = e;
    return d;
}

// ---- Start-up evidence ---------------------------------------------------------------------------------
LocalEvidence GetLocalLaunchEvidence(const std::string& role, const std::string& early_log, const std::string& steam_log,
                                     const std::string& observer_log, const std::string& save_dir, bool final_check) {
    LocalEvidence out;
    const Evidence base = GetLaunchEvidence(early_log, steam_log, observer_log, save_dir, false, false);
    std::string reason;
    if (base.saves != "ok") {
        if (base.saves == "missing") out.missing.push_back("saves=redirected to the local save copy");
        else reason = "saves_" + base.saves;
    }
    if (base.master != "ok") {
        if (base.master == "missing") out.missing.push_back("master_server=redirected to the closed local port");
        else reason = "master_redirect_failed";
    }
    const std::vector<std::string> lines = SplitLines(early_log);
    bool started = false;
    for (const std::string& line : lines) {
        if (!StartsWith(line, "early=start;")) continue;
        started = true;
        if (!HasToken(line, "lab=1") || !HasToken(line, "role=" + role)) reason = "lab_role_mismatch";
    }
    if (!started) out.missing.push_back("early=start; lab=1; role=" + role);
    if (role == "guest") {
        bool mutex = false, identity = false;
        for (const std::string& line : lines) {
            if (StartsWith(line, "mutex=renamed") || StartsWith(line, "mutex=unchanged")) {
                mutex = true;
                const std::string want = "mutex=renamed; suffix=-guest";
                if (!(StartsWith(line, want) && (line.size() == want.size() || line[want.size()] == ';'))) reason = "guest_mutex_mismatch";
            }
            if (StartsWith(line, "identity=fake") || StartsWith(line, "identity=real")) {
                identity = true;
                const std::string want = std::string("identity=fake; steamid=") + kLocalGuestSteamId;
                if (!(StartsWith(line, want) && (line.size() == want.size() || line[want.size()] == ';'))) reason = "guest_identity_mismatch";
            }
        }
        if (!mutex) out.missing.push_back("mutex=renamed; suffix=-guest");
        if (!identity) out.missing.push_back(std::string("identity=fake; steamid=") + kLocalGuestSteamId);
    }
    if (final_check && !out.missing.empty() && reason.empty()) reason = "startup_evidence_missing";
    out.reason = reason;
    out.state = !reason.empty() ? "unsafe" : !out.missing.empty() ? "pending" : "ok";
    return out;
}

// ---- The second game folder ------------------------------------------------------------------------------
namespace {
// The guest's loader is an earlier Diamond Dogs Together's: its own package.json lists exactly that hash.
bool GuestHasOlderPackageLoader(const InstancePaths& guest, const std::string& loader_hash) {
    std::string text;
    json::Value v;
    if (!ReadSmallFile(guest.package_json, text, 1u << 20) || !json::Parse(text, v) || v.Str("product") != kPackageProduct) return false;
    if (const json::Value* files = v.Find("files"); files && files->is_array())
        for (const json::Value& row : files->Elements())
            if (row.Str("target") == "dinput8.dll" && UpperAscii(row.Str("sha256")) == loader_hash) return true;
    return false;
}

// The SHA-256 a package.json lists for `target` ("" when it lists none, or not exactly one).
std::string PackageRowSha256(const InstancePaths& paths, const std::string& target) {
    std::string text, sha;
    json::Value v;
    if (!ReadSmallFile(paths.package_json, text, 1u << 20) || !json::Parse(text, v)) return {};
    int rows = 0;
    if (const json::Value* files = v.Find("files"); files && files->is_array())
        for (const json::Value& row : files->Elements())
            if (row.Str("target") == target) {
                ++rows;
                sha = UpperAscii(row.Str("sha256"));
            }
    return rows == 1 && IsUpperHex64(sha) ? sha : std::string();
}

// The second copy's mgsvtpp.exe is 166 MB: its hash is kept for this process while the file's size, last write
// time and identity (volume and file index) stay the same; a rewritten or replaced file is hashed again.
bool CachedLargeFileSha256(const std::wstring& path, std::string& hex) {
    struct Entry {
        std::uint64_t size = 0, write = 0, index = 0;
        std::uint32_t volume = 0;
        std::string hex;
    };
    static std::mutex mutex;
    static std::map<std::string, Entry> cache;
    hex.clear();
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool got = GetFileInformationByHandle(h, &info) != 0;
    CloseHandle(h);
    if (!got || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    Entry e;
    e.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    e.write = (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    e.index = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    e.volume = info.dwVolumeSerialNumber;
    const std::string key = LowerAscii(Utf8(FullPath(path)));
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = cache.find(key);
        if (it != cache.end() && it->second.size == e.size && it->second.write == e.write && it->second.index == e.index &&
            it->second.volume == e.volume) {
            hex = it->second.hex;
            return true;
        }
    }
    std::uint64_t hashed = 0;
    if (!Sha256FileHex(path, hex, &hashed)) return false;
    if (hashed != e.size) return true;   // changed while it was read: right now, but not kept
    e.hex = hex;
    std::lock_guard<std::mutex> lock(mutex);
    cache[key] = e;
    return true;
}

struct GuestPlan {
    bool copy_loader = false, update_loader = false, copy_runtime = false;
    bool update_launcher = false;   // its own DiamondDogsTogether.exe would not match the package.json copied in
};

// "" when the second folder can be used; `plan` says what the start copies in.
std::string GuestProblem(Env& env, const std::wstring& host_in, const std::wstring& guest_in, GuestPlan& plan) {
    plan = GuestPlan{};
    if (Trim(Utf8(guest_in)).empty()) return "Choose the second MGSV game folder (a separate copy of the game) in Settings > Debug.";
    const std::wstring guest = FullPath(guest_in), host = FullPath(host_in);
    if (IsDriveRoot(guest)) return "Choose a game folder, not the top of a drive.";
    if (!IsDir(guest)) return "That folder does not exist: " + Utf8(guest);
    if (!IsFile(JoinPath2(guest, L"mgsvtpp.exe")))
        return "There is no mgsvtpp.exe in that folder. Choose a separate MGSV game folder (a full copy of the game).";
    if (!Distinct(host, guest))
        return "Choose a second, separate MGSV folder: not this game folder, and neither folder inside the other.";
    const InstancePaths g = GetInstancePaths(guest);
    if (AnyReparse(guest) || (PathExists(g.mod) && HasReparsePoint(g.mod, guest)))
        return "That folder (or its mgs-coop folder) is a link (junction or symbolic link). Choose the real folder.";
    const std::string path_problem = GamePathProblem(guest);
    if (!path_problem.empty()) return "That folder cannot be used: " + path_problem;
    std::string hex;
    if (!CachedLargeFileSha256(g.exe, hex) || hex != UpperAscii(Hooks().exe_sha256))
        return "mgsvtpp.exe in that folder is not the supported Steam version 1.0.15.4 (unmodified). Verify that copy's game files.";
    if (!IsFile(g.steam_api) || !Sha256FileHex(g.steam_api, hex) || hex != UpperAscii(Hooks().steam_api_sha256))
        return "steam_api64.dll in that folder is not the original file shipped with the game. Verify that copy's game files.";
    if (AnyGameRunningIn(env, guest)) return "MGSV is running from that folder. Quit it normally first.";
    for (const char* name : kDeveloperMarkers)
        if (PathExists(JoinPath2(g.mod, Wide(name))))
            return std::string(name) + " in that folder's mgs-coop belongs to a developer test. Finish that test first.";
    const InstancePaths h = GetInstancePaths(host);
    const PackageInfo package = ReadPackage(h);
    if (!package.ok) return package.problem;
    if (!IsFile(g.loader)) {
        plan.copy_loader = true;
    } else {
        if (HasReparsePoint(g.loader, guest) || !Sha256FileHex(g.loader, hex)) return "dinput8.dll in that folder cannot be read.";
        if (hex != package.loader_sha256) {
            // An earlier Diamond Dogs Together's loader (that folder's own package.json lists it, as after extracting an
            // older zip there) is updated, also when the developer tools recorded it in probe-install.json: that record
            // stays exactly as it is, as after any drag-and-drop update. A loader only those tools own is not replaced.
            if (!GuestHasOlderPackageLoader(g, hex)) {
                if (IsFile(g.record))
                    return "That folder's dinput8.dll is a co-op loader managed by the developer tools (probe-install.json). Update it with "
                           "tools\\package-install.ps1 (or Play-Offline-Coop.bat) first.";
                return "That folder has a dinput8.dll that is not from Diamond Dogs Together (another mod?). It is never replaced: "
                       "remove it from that folder yourself, then try again.";
            }
            plan.update_loader = true;
        }
    }
    // That folder's own launcher must match the package.json copied in with the loader (else it would refuse to
    // start there): it is updated too, from this folder's launcher when that is the one this package.json lists.
    const std::wstring guest_launcher = JoinPath2(guest, L"DiamondDogsTogether.exe");
    if ((plan.copy_loader || plan.update_loader) && IsFile(guest_launcher)) {
        if (HasReparsePoint(guest_launcher, guest) || !Sha256FileHex(guest_launcher, hex))
            return "DiamondDogsTogether.exe in that folder cannot be read.";
        const std::string want = PackageRowSha256(h, "DiamondDogsTogether.exe");
        std::string own;
        if (!want.empty() && hex != want && Sha256FileHex(JoinPath2(host, L"DiamondDogsTogether.exe"), own) && own == want)
            plan.update_launcher = true;
    }
    std::string dat_error;
    if (!IsFile(g.archive) || !RuntimeArchiveValid(g, nullptr, dat_error)) plan.copy_runtime = true;
    return {};
}
}  // namespace

LocalFolderCheck CheckLocalTestFolder(Env& env, const std::wstring& host, const std::wstring& guest) {
    LocalFolderCheck c;
    GuestPlan plan;
    c.message = GuestProblem(env, host, guest, plan);
    if (!c.message.empty()) return c;
    c.ok = true;
    std::vector<std::string> copied;
    if (plan.copy_loader) copied.push_back("co-op loader");
    if (plan.copy_runtime) copied.push_back("runtime file");
    c.message = "Ready.";
    if (!copied.empty())
        c.message += " This version's " + Join(copied, " and ") + (copied.size() > 1 ? " are" : " is") + " copied into it when the test starts.";
    if (plan.update_loader || plan.update_launcher)
        c.message += " Its older Diamond Dogs Together files are updated to this version (the old ones are kept in mgs-coop\\install-backups).";
    return c;
}

// ---- The journal ----------------------------------------------------------------------------------------------
std::wstring LocalRunsRoot(const std::wstring& host_root) { return JoinPath2(JoinPath2(FullPath(host_root), L"mgs-coop"), L"local-test-runs"); }

bool CheckLocalSession(const json::Value& s, std::string& error) {
    auto refuse = [&](const std::string& why) {
        error = why;
        return false;
    };
    if (!s.is_object() || s.Int("v", 0) != 1 || s.Str("product") != kLocalSessionProduct) return refuse("Invalid local test session.");
    const std::string id = s.Str("id");
    if (!IsLowerHex32(id)) return refuse("Invalid local test session identity.");
    const std::wstring host = Wide(s.Str("host_root")), guest = Wide(s.Str("guest_root"));
    std::string problem = LocalRootProblem(host);
    if (problem.empty()) problem = LocalRootProblem(guest);
    if (!problem.empty()) return refuse(problem);
    if (!Distinct(FullPath(host), FullPath(guest))) return refuse("Local game folders must be distinct and must not contain each other.");
    const std::wstring run = FullPath(Wide(s.Str("run_directory")));
    if (!IEquals(run, JoinPath2(LocalRunsRoot(host), Wide(id)))) return refuse("Local test run directory escaped its owned location.");
    if (HasReparsePoint(run, FullPath(host))) return refuse("Filesystem links are not supported here: " + Utf8(run));
    std::string marker;
    const std::wstring marker_path = JoinPath2(run, L".local-test-owner");
    if (!IsFile(marker_path) || !ReadSmallFile(marker_path, marker, 4096) || marker != id)
        return refuse("Local test run ownership marker is missing or changed.");
    std::vector<std::string> roles;
    if (const json::Value* pointers = s.Find("pointers"); pointers && pointers->is_array()) {
        for (const json::Value& p : pointers->Elements()) {
            const std::string role = p.Str("role");
            if ((role != "host" && role != "guest") || std::find(roles.begin(), roles.end(), role) != roles.end() || !IsUpperHex64(p.Str("sha256")))
                return refuse("Invalid local ownership pointer record.");
            roles.push_back(role);
        }
    }
    std::vector<std::string> names;
    if (const json::Value* files = s.Find("files"); files && files->is_array()) {
        for (const json::Value& e : files->Elements()) {
            const std::string role = e.Str("role"), name = e.Str("name");
            if ((role != "host" && role != "guest") || !IsActivationName(name) ||
                std::find(names.begin(), names.end(), role + "/" + name) != names.end())
                return refuse("Invalid or repeated local activation record.");
            names.push_back(role + "/" + name);
            if (!IsUpperHex64(e.Str("written_sha256"))) return refuse("Invalid local activation hash.");
            if (e.Bool("original_exists")) {
                std::string original;
                if (!FromBase64(e.Str("original_base64"), original) || original.size() > kMaxActivationBytes ||
                    Sha256Hex(original) != e.Str("original_sha256"))
                    return refuse("Local activation backup hash does not match.");
            }
        }
    }
    return true;
}

bool ReadLocalSession(const std::wstring& run_dir, json::Value& session, std::string& error) {
    error.clear();
    session = json::Value();
    const std::wstring run = FullPath(run_dir);
    if (AnyReparse(run)) {
        error = "Filesystem links are not supported here: " + Utf8(run);
        return false;
    }
    const std::wstring path = JoinPath2(run, L"session.json");
    if (!IsFile(path)) return false;
    if (FileSizeOrZero(path) > kMaxJournalBytes) {
        error = "Local session journal is too large.";
        return false;
    }
    std::string text;
    json::Value v;
    if (!ReadSmallFile(path, text, kMaxJournalBytes) || !json::Parse(text, v)) {
        error = "Local session journal is not readable: " + Utf8(path);
        return false;
    }
    if (!CheckLocalSession(v, error)) return false;
    if (!IEquals(run, FullPath(Wide(v.Str("run_directory"))))) {
        error = "Local journal names another run.";
        return false;
    }
    session = v;
    return true;
}

bool SaveLocalSession(const json::Value& session, std::string& error) {
    if (!CheckLocalSession(session, error)) return false;
    const std::string text = json::Dump(session) + "\n";
    if (text.size() > kMaxJournalBytes) {
        error = "Local session journal is too large.";
        return false;
    }
    const std::wstring path = JoinPath2(FullPath(Wide(session.Str("run_directory"))), L"session.json");
    if (HasReparsePoint(path, FullPath(Wide(session.Str("host_root"))))) {
        error = "Filesystem links are not supported here: " + Utf8(path);
        return false;
    }
    return WriteAtomic(path, text, true, error);
}

bool NewLocalSession(Env& env, const std::wstring& host_in, const std::wstring& guest_in, const std::wstring& run_in, json::Value& session,
                     std::string& error) {
    session = json::Value();
    std::string problem = LocalRootProblem(host_in);
    if (problem.empty()) problem = LocalRootProblem(guest_in);
    if (!problem.empty()) {
        error = problem;
        return false;
    }
    const std::wstring host = FullPath(host_in), guest = FullPath(guest_in);
    if (!Distinct(host, guest)) {
        error = "Local game folders must be distinct and must not contain each other.";
        return false;
    }
    RootLocks locks(host, guest);
    if (!locks.ok()) {
        error = "Another co-op tool is changing these game folders right now; try again in a moment.";
        return false;
    }
    const std::wstring run = FullPath(run_in);
    const std::string id = Utf8(LeafOf(run));
    if (!IsLowerHex32(id) || !IEquals(run, JoinPath2(LocalRunsRoot(host), Wide(id)))) {
        error = "Use an owned GUID directory under host mgs-coop\\local-test-runs.";
        return false;
    }
    if (HasReparsePoint(run, host)) {
        error = "Filesystem links are not supported here: " + Utf8(run);
        return false;
    }
    if (PathExists(run)) {
        error = "The local run directory already exists.";
        return false;
    }
    if (AnyGameRunningIn(env, host) || AnyGameRunningIn(env, guest)) {
        error = "Quit both game copies before changing their local test settings.";
        return false;
    }
    const std::int64_t started = env.CurrentStartMs();
    if (!started) {
        error = "Cannot record the local launcher process identity.";
        return false;
    }
    for (const std::wstring& root : {host, guest}) {
        const std::wstring pointer = JoinPath2(JoinPath2(root, L"mgs-coop"), L"local-test-session.json");
        if (HasReparsePoint(pointer, root)) {
            error = "Filesystem links are not supported here: " + Utf8(pointer);
            return false;
        }
        if (PathExists(pointer)) {
            error = "A local-test ownership pointer already exists. Recover that session before starting another.";
            return false;
        }
    }
    if (!CreateDirectories(run) || !WriteAtomic(JoinPath2(run, L".local-test-owner"), id, false, error)) {
        if (error.empty()) error = "The local run directory could not be created.";
        return false;
    }
    json::Value s = json::Value::Object();
    s["v"] = 1;
    s["product"] = kLocalSessionProduct;
    s["id"] = id;
    s["state"] = "prepared";
    s["host_root"] = Utf8(host);
    s["guest_root"] = Utf8(guest);
    s["run_directory"] = Utf8(run);
    s["launcher_pid"] = static_cast<std::int64_t>(env.CurrentPid());
    s["launcher_start_utc"] = FormatUtcRoundTrip(started);
    s["created_utc"] = FormatUtcRoundTrip(env.NowMs());
    s["watchdog_pid"] = 0;
    s["watchdog_start_utc"] = "";
    s["files"] = json::Value::Array();
    s["pointers"] = json::Value::Array();
    if (!SaveLocalSession(s, error)) return false;
    for (const char* role : kRoles) {
        const std::wstring root = std::string(role) == "host" ? host : guest;
        const std::wstring pointer = JoinPath2(JoinPath2(root, L"mgs-coop"), L"local-test-session.json");
        json::Value p = json::Value::Object();
        p["v"] = 1;
        p["product"] = kLocalPointerProduct;
        p["id"] = id;
        p["host_root"] = Utf8(host);
        p["guest_root"] = Utf8(guest);
        p["run_directory"] = Utf8(run);
        const std::string bytes = json::Dump(p) + "\n";
        json::Value row = json::Value::Object();
        row["role"] = role;
        row["sha256"] = Sha256Hex(bytes);
        s["pointers"].Push(row);
        if (!SaveLocalSession(s, error)) return false;
        if (PathExists(pointer)) {
            error = "A local-test ownership pointer appeared during preparation; it was left alone.";
            return false;
        }
        if (!WriteAtomic(pointer, bytes, false, error)) return false;
    }
    session = s;
    return true;
}

bool SetLocalSessionFile(Env& env, json::Value& session, const std::string& role, const std::string& name, const std::string& bytes,
                         std::string& error) {
    if (!CheckLocalSession(session, error)) return false;
    if (role != "host" && role != "guest") {
        error = "The local role must be host or guest.";
        return false;
    }
    if (!IsActivationName(name)) {
        error = "Not a local activation file: " + name;
        return false;
    }
    RootLocks locks(RootOf(session, "host"), RootOf(session, "guest"));
    if (!locks.ok()) {
        error = "Another co-op tool is changing these game folders right now; try again in a moment.";
        return false;
    }
    const std::string state = session.Str("state");
    if (state != "prepared" && state != "active") {
        error = "The local session is not open for activation.";
        return false;
    }
    if (bytes.size() > kMaxActivationBytes) {
        error = "Local activation file is too large.";
        return false;
    }
    if (AnyGameIn(env, session)) {
        error = "Quit both game copies before changing their local test settings.";
        return false;
    }
    for (const json::Value& e : FilesOf(session)->Elements())
        if (e.Str("role") == role && e.Str("name") == name) {
            error = "This local activation file is already journaled.";
            return false;
        }
    error = ActivationTargetProblem(session, role, name);
    if (!error.empty()) return false;
    const std::wstring path = ActivationPath(session, role, name);
    const bool exists = IsFile(path);
    std::string original;
    if (exists) {
        if (FileSizeOrZero(path) > kMaxActivationBytes || !ReadSmallFile(path, original, kMaxActivationBytes)) {
            error = "Existing activation file is too large to back up: " + Utf8(path);
            return false;
        }
    }
    json::Value entry = json::Value::Object();
    entry["role"] = role;
    entry["name"] = name;
    entry["original_exists"] = exists;
    entry["original_base64"] = Base64(original);
    entry["original_sha256"] = Sha256Hex(original);
    entry["written_sha256"] = Sha256Hex(bytes);
    entry["phase"] = "planned";
    FilesOf(session)->Push(entry);
    session["state"] = "active";
    // This durable journal precedes every activation write, the marker included.
    if (!SaveLocalSession(session, error)) return false;
    const bool still = IsFile(path);
    if (still != exists || (still && FileHash(path) != Sha256Hex(original))) {
        error = "An activation file changed before it could be replaced; it was left alone.";
        return false;
    }
    if (!WriteAtomic(path, bytes, true, error)) return false;
    if (FileHash(path) != Sha256Hex(bytes)) {
        error = "Local activation file did not match its recorded hash.";
        return false;
    }
    FilesOf(session)->Elements().back()["phase"] = "written";
    return SaveLocalSession(session, error);
}

bool RestoreLocalSession(Env& env, json::Value& session, LocalRestoreResult& result, std::string& error) {
    result = LocalRestoreResult{};
    if (!CheckLocalSession(session, error)) return false;
    RootLocks locks(RootOf(session, "host"), RootOf(session, "guest"));
    if (!locks.ok()) {
        error = "Another co-op tool is changing these game folders right now; try again in a moment.";
        return false;
    }
    if (AnyGameIn(env, session)) {
        error = "Local settings stay active until both game copies have exited.";
        return false;
    }
    std::vector<json::Value>& files = FilesOf(session)->Elements();
    // Validate every restoration target before the first change.
    for (const json::Value& e : files) {
        error = ActivationTargetProblem(session, e.Str("role"), e.Str("name"));
        if (!error.empty()) return false;
    }
    for (const char* role : kRoles)
        if (HasReparsePoint(ActivationPath(session, role, "local-test-session.json"), RootOf(session, role))) {
            error = "Filesystem links are not supported here.";
            return false;
        }
    // The role markers first, then the remaining settings.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < files.size(); ++i)
        if (files[i].Str("name") == "script-observe.enabled") order.push_back(i);
    for (std::size_t i = 0; i < files.size(); ++i)
        if (files[i].Str("name") != "script-observe.enabled") order.push_back(i);
    for (std::size_t i : order) {
        json::Value& e = files[i];
        const std::string role = e.Str("role"), name = e.Str("name"), key = role + "/" + name;
        const std::wstring path = ActivationPath(session, role, name);
        const bool exists = IsFile(path);
        const std::string current = exists ? FileHash(path) : std::string();
        const bool original_exists = e.Bool("original_exists");
        if ((original_exists && exists && current == e.Str("original_sha256")) || (!original_exists && !exists)) {
            e["phase"] = "restored";
            continue;
        }
        if (!exists || current != e.Str("written_sha256")) {
            result.kept.push_back(key);
            e["phase"] = "kept";
            continue;
        }
        if (original_exists) {
            std::string original;
            FromBase64(e.Str("original_base64"), original);
            if (!WriteAtomic(path, original, true, error)) return false;
            if (FileHash(path) != e.Str("original_sha256")) {
                error = "Restored local activation backup did not match its hash.";
                return false;
            }
            result.restored.push_back(key);
        } else {
            if (!DeleteFileChecked(path)) {
                error = "Could not remove " + Utf8(path);
                return false;
            }
            result.removed.push_back(key);
        }
        e["phase"] = "restored";
        if (!SaveLocalSession(session, error)) return false;
    }
    if (result.kept.empty()) {
        if (const json::Value* pointers = session.Find("pointers"); pointers && pointers->is_array()) {
            for (const json::Value& p : pointers->Elements()) {
                const std::string role = p.Str("role");
                const std::wstring path = ActivationPath(session, role, "local-test-session.json");
                if (!PathExists(path)) continue;
                if (!IsFile(path) || FileHash(path) != p.Str("sha256")) {
                    result.kept.push_back(role + "/local-test-session.json");
                    continue;
                }
                if (!DeleteFileChecked(path)) {
                    error = "Could not remove " + Utf8(path);
                    return false;
                }
                result.removed.push_back(role + "/local-test-session.json");
            }
        }
    }
    session["state"] = result.kept.empty() ? "off" : "partial";
    result.state = session.Str("state");
    return SaveLocalSession(session, error);
}

bool LocalSessionOwnerLive(Env& env, const json::Value& session) {
    const std::int64_t pid = session.Int("launcher_pid", 0);
    const std::string started = session.Str("launcher_start_utc");
    if (pid <= 0 || started.empty()) return true;
    // This process never counts: it calls this only while it runs no local test.
    std::int64_t start_ms = 0;
    const bool self = static_cast<std::uint32_t>(pid) == env.CurrentPid() && ParseUtc(started, start_ms) &&
                      std::llabs(start_ms - env.CurrentStartMs()) < 1000;
    if (!self && AliveAt(env, pid, started)) return true;
    const std::int64_t helper = session.Int("watchdog_pid", 0);
    if (static_cast<std::uint32_t>(helper) == env.CurrentPid()) return false;
    return AliveAt(env, helper, session.Str("watchdog_start_utc"));
}

namespace {
// The run's journal is this run's and says it is finished (state "off"); nothing else is checked.
bool LocalJournalFinished(const std::wstring& run_dir, const std::string& id) {
    const std::wstring path = JoinPath2(run_dir, L"session.json");
    if (AnyReparse(run_dir) || !IsFile(path) || FileSizeOrZero(path) > kMaxJournalBytes) return false;
    std::string text;
    json::Value v;
    return ReadSmallFile(path, text, kMaxJournalBytes) && json::Parse(text, v) && v.is_object() && v.Int("v", 0) == 1 &&
           v.Str("product") == kLocalSessionProduct && v.Str("id") == id && v.Str("state") == "off";
}
}  // namespace

bool RecoverLocalSessions(Env& env, const std::wstring& root_in, std::vector<std::string>& recovered, std::string& error) {
    recovered.clear();
    error.clear();
    const std::wstring root = FullPath(root_in);
    const std::wstring mod = JoinPath2(root, L"mgs-coop");
    if (!IsDir(mod)) return true;   // nothing of a local test can be there
    if (HasReparsePoint(mod, root)) {
        error = "Filesystem links are not supported here: " + Utf8(mod);
        return false;
    }
    std::vector<json::Value> sessions;
    auto add = [&](const json::Value& s) {
        for (const json::Value& have : sessions)
            if (have.Str("id") == s.Str("id")) return;
        sessions.push_back(s);
    };
    const std::wstring pointer_path = JoinPath2(mod, L"local-test-session.json");
    if (PathExists(pointer_path)) {
        std::string text;
        json::Value p;
        if (!IsFile(pointer_path) || FileSizeOrZero(pointer_path) > 16384 || !ReadSmallFile(pointer_path, text, 16384) || !json::Parse(text, p) ||
            p.Int("v", 0) != 1 || p.Str("product") != kLocalPointerProduct) {
            error = "Unknown local-test ownership pointer (" + Utf8(pointer_path) + "); it was left alone.";
            return false;
        }
        json::Value s;
        if (!ReadLocalSession(Wide(p.Str("run_directory")), s, error) || p.Str("id") != s.Str("id") || p.Str("host_root") != s.Str("host_root") ||
            p.Str("guest_root") != s.Str("guest_root")) {
            if (error.empty()) error = "Local ownership pointer does not match its journal.";
            return false;
        }
        std::string role;
        if (IEquals(root, FullPath(Wide(s.Str("host_root"))))) role = "host";
        else if (IEquals(root, FullPath(Wide(s.Str("guest_root"))))) role = "guest";
        else {
            error = "Local pointer names unrelated game folders.";
            return false;
        }
        std::string want;
        if (const json::Value* pointers = s.Find("pointers"))
            for (const json::Value& row : pointers->Elements())
                if (row.Str("role") == role) want = row.Str("sha256");
        if (want.empty() || FileHash(pointer_path) != want) {
            error = "Local ownership pointer changed; it was left alone.";
            return false;
        }
        add(s);
    }
    const std::wstring runs = JoinPath2(mod, L"local-test-runs");
    if (HasReparsePoint(runs, root)) {
        error = "Filesystem links are not supported here: " + Utf8(runs);
        return false;
    }
    if (IsDir(runs)) {
        for (const DirEntry& d : ListDirectory(runs)) {
            if (!d.directory || !IsLowerHex32(Utf8(d.name))) continue;
            if (d.reparse) {
                error = "Filesystem links are not supported here.";
                return false;
            }
            // A finished run has nothing to restore: it never blocks a later test, also when its second
            // folder was moved or deleted since (the strict check below requires both folders).
            if (LocalJournalFinished(JoinPath2(runs, d.name), Utf8(d.name))) continue;
            json::Value s;
            if (!ReadLocalSession(JoinPath2(runs, d.name), s, error)) {
                if (error.empty()) continue;   // no journal there
                return false;
            }
            if (!IEquals(root, FullPath(Wide(s.Str("host_root"))))) {
                error = "Local run journal names another host.";
                return false;
            }
            if (s.Str("state") != "off") add(s);
        }
    }
    for (const json::Value& s : sessions) {
        if (s.Str("state") == "off") continue;
        if (LocalSessionOwnerLive(env, s)) {
            error = "Another local two-window test (or its cleanup helper) is still running. Close it first.";
            return false;
        }
        if (AnyGameIn(env, s)) {
            error = "A local two-window test game is still running. Quit both game copies first.";
            return false;
        }
    }
    for (json::Value& s : sessions) {
        if (s.Str("state") == "off") continue;
        LocalRestoreResult r;
        if (!RestoreLocalSession(env, s, r, error)) return false;
        if (!r.kept.empty()) {
            error = "Local test settings changed after activation and were left alone: " + Join(r.kept, ", ") +
                    ". Restore those files before starting another test.";
            return false;
        }
        recovered.push_back(s.Str("id"));
    }
    return true;
}

// ---- A run ---------------------------------------------------------------------------------------------
LocalTestRun::LocalTestRun(std::shared_ptr<Env> env, std::shared_ptr<LocalHost> host) : env_(std::move(env)), host_(std::move(host)) {}
LocalTestRun::~LocalTestRun() = default;

bool LocalTestRun::Fail(const std::string& sentence, std::string& error) {
    error = sentence;
    return false;
}

bool LocalTestRun::OwnedAlive(const Instance& game, const std::wstring& root) {
    if (!game.pid) return false;
    std::int64_t start = 0;
    if (!env_->ProcessStartMs(game.pid, start)) return false;
    if (std::llabs(start - game.start_ms) >= 1000) return false;
    return ImageBelongs(host_->ProcessImage(game.pid), root);
}

void LocalTestRun::StopOwnedGames() {
    // A safety failure closes only the exact processes this run started.
    for (int i = 0; i < 2; ++i)
        if (OwnedAlive(games_[i], paths_[i].game)) host_->CloseGame(games_[i].pid, 2000);
}

namespace {
// Copies every file of `from` into the new folder `to` (no links), checking each copy.
bool CopySaveTree(const std::wstring& from, const std::wstring& to, std::string& error) {
    for (const DirEntry& e : ListDirectory(from)) {
        const std::wstring src = JoinPath2(from, e.name), dst = JoinPath2(to, e.name);
        if (e.reparse) {
            error = "Filesystem links are not supported here: " + Utf8(src);
            return false;
        }
        if (e.directory) {
            if (!CopySaveTree(src, dst, error)) return false;
            continue;
        }
        if (!CreateDirectories(to) || !CopyFileChecked(src, dst, false, error)) {
            if (error.empty()) error = "Could not copy " + Utf8(src);
            return false;
        }
        if (FileHash(dst) != FileHash(src)) {
            error = "The co-op save copy changed during local test setup; no game was launched.";
            return false;
        }
    }
    return true;
}

bool TreeHasReparse(const std::wstring& dir) {
    for (const DirEntry& e : ListDirectory(dir)) {
        if (e.reparse) return true;
        if (e.directory && TreeHasReparse(JoinPath2(dir, e.name))) return true;
    }
    return false;
}

bool InstallIntoGuest(Env& env, const InstancePaths& host, const InstancePaths& guest, std::string& error) {
    GuestPlan plan;
    error = GuestProblem(env, host.game, guest.game, plan);
    if (!error.empty()) return false;
    const PackageInfo package = ReadPackage(host);
    if (!package.ok) {
        error = package.problem;
        return false;
    }
    if (!CreateDirectories(guest.mod) || HasReparsePoint(guest.mod, guest.game)) {
        error = "The second folder's mgs-coop folder could not be created.";
        return false;
    }
    if (plan.copy_loader || plan.update_loader) {
        if (!LoaderIsOurs(host, package)) {
            error = "This game folder's dinput8.dll is not this package's. Extract the whole zip into the game folder again.";
            return false;
        }
        // This version's launcher (when it is updated), loader and package.json, in that order (the launcher is
        // the one most likely to be in use). Whatever they replace is kept first, in the second folder's
        // mgs-coop\install-backups\local-test-<time>; a failed copy puts back what was already replaced.
        std::vector<std::pair<std::wstring, std::wstring>> copies;   // (this folder's file, the second folder's)
        if (plan.update_launcher)
            copies.push_back({JoinPath2(host.game, L"DiamondDogsTogether.exe"), JoinPath2(guest.game, L"DiamondDogsTogether.exe")});
        copies.push_back({host.loader, guest.loader});
        copies.push_back({host.package_json, guest.package_json});
        std::wstring aside;
        std::vector<std::pair<std::wstring, std::wstring>> kept;   // (the second folder's file, its backup)
        for (const auto& c : copies) {
            if (!IsFile(c.second) || FileHash(c.second) == FileHash(c.first)) continue;
            if (aside.empty()) {
                aside = UniquePath(JoinPath2(JoinPath2(guest.mod, L"install-backups"), L"local-test-" + Wide(StampNow(env))), L"");
                if (!CreateDirectories(aside) || HasReparsePoint(aside, guest.game)) {
                    error = "The second folder's mgs-coop\\install-backups folder could not be created.";
                    return false;
                }
            }
            const std::wstring backup = JoinPath2(aside, LeafOf(c.second));
            if (!CopyFileChecked(c.second, backup, false, error) || FileHash(backup) != FileHash(c.second)) {
                if (error.empty()) error = "Could not keep a copy of " + Utf8(c.second) + "; nothing was replaced.";
                return false;
            }
            kept.push_back({c.second, backup});
        }
        std::vector<std::wstring> replaced;
        for (const auto& c : copies) {
            if (IsFile(c.second) && FileHash(c.second) == FileHash(c.first)) continue;
            std::string copy_error;
            if (!CopyFileChecked(c.first, c.second, true, copy_error) || FileHash(c.second) != FileHash(c.first)) {
                for (const auto& k : kept)
                    if (std::find(replaced.begin(), replaced.end(), k.first) != replaced.end()) {
                        std::string ignored;
                        CopyFileChecked(k.second, k.first, true, ignored);
                    }
                error = "Could not copy " + Utf8(LeafOf(c.first)) + " into the second folder" + (copy_error.empty() ? "" : " (" + copy_error + ")") +
                        ". Is a program from that folder running? Its earlier files are back in place.";
                return false;
            }
            replaced.push_back(c.second);
        }
        std::string hex;
        if (!Sha256FileHex(guest.loader, hex) || hex != package.loader_sha256) {
            error = "The co-op loader copied into the second folder does not match this package.";
            return false;
        }
        LauncherLog(env, host, std::string("local_test=guest_loader; action=") + (plan.update_loader ? "updated" : "copied") +
                                   "; launcher=" + (plan.update_launcher ? "updated" : "kept") + "; kept=" + std::to_string(kept.size()));
    }
    if (plan.copy_runtime) {
        std::string dat_error;
        if (!RuntimeArchiveValid(host, nullptr, dat_error)) {
            error = "This game folder's co-op runtime file is missing or changed. Let co-op get ready first.";
            return false;
        }
        if (!CopyFileChecked(host.archive, guest.archive, true, error)) return false;
        if (!RuntimeArchiveValid(guest, nullptr, dat_error)) {
            error = "The runtime file copied into the second folder does not verify.";
            return false;
        }
        LauncherLog(env, host, "local_test=guest_runtime; action=copied");
    }
    return true;
}
}  // namespace

bool LocalTestRun::Start(const LocalTestRequest& request, std::string& error) {
    error.clear();
    if (running_) return Fail("A local test is already running.", error);
    Env& env = *env_;
    request_ = request;
    status_ = LocalStatus{};
    const std::wstring host = FullPath(request.host_root), guest = FullPath(request.guest_root);
    paths_[0] = GetInstancePaths(host);
    paths_[1] = GetInstancePaths(guest);
    const LocalFolderCheck check = CheckLocalTestFolder(env, host, guest);
    if (!check.ok) return Fail(check.message, error);
    for (const InstancePaths& p : paths_)
        if (AnyGameRunningIn(env, p.game)) return Fail("Quit MGSV in " + Utf8(p.game) + " normally before starting a local test.", error);
    for (const InstancePaths& p : paths_) {
        std::vector<std::string> recovered;
        if (!RecoverLocalSessions(env, p.game, recovered, error)) return false;
        if (!recovered.empty()) LauncherLog(env, paths_[0], "local_test=recovered; runs=" + std::to_string(recovered.size()));
    }
    const PackageInfo package = ReadPackage(paths_[0]);
    if (!package.ok) return Fail(package.problem, error);
    if (!LoaderIsOurs(paths_[0], package))
        return Fail("This game folder's dinput8.dll is not this package's. Extract the whole zip into the game folder again.", error);
    std::string dat_error;
    if (!RuntimeArchiveValid(paths_[0], nullptr, dat_error))
        return Fail("This game folder's co-op runtime file is missing or changed. Let co-op get ready first.", error);
    json::Value state;
    if (!ReadInstallState(paths_[0], state) || state.Str("status") != "installed" || !IsDir(paths_[0].coop_saves))
        return Fail("Co-op is not set up in this game folder yet (it makes the co-op save copy that seeds the local test). Let it get ready first.",
                    error);
    for (int i = 0; i < 2; ++i) {
        const std::string role = kRoles[i];
        if (HasReparsePoint(paths_[i].game, paths_[i].game) || (PathExists(paths_[i].mod) && HasReparsePoint(paths_[i].mod, paths_[i].game)))
            return Fail("[" + role + "] Filesystem links are not supported here.", error);
        if (PlaySessionLive(env, paths_[i]))
            return Fail("[" + role + "] Co-op is on for that game folder (another Diamond Dogs Together window or its helper). Quit that game first.",
                        error);
        if (IsDir(paths_[i].mod)) CleanupSession(env, paths_[i], false);   // hash-checked leftovers of an online session
    }
    std::string host_steam = "learn";
    if (!request.fixture) {
        const std::uint64_t self = OwnSteamId(env);
        if (!self) return Fail("Start Steam and sign in first. The local link itself stays on this PC.", error);
        host_steam = Decimal(self);
        // The offline contract: nothing at the closed master port, and free local link ports, all on loopback.
        static const struct {
            int port;
            bool tcp;
        } kPorts[] = {{47899, true}, {47810, false}, {47811, false}, {kLocalHostPort, false}, {kLocalGuestPort, false}};
        for (const auto& p : kPorts)
            if (!host_->PortFree(p.port, p.tcp))
                return Fail("Local test port " + std::to_string(p.port) + " is in use. Close the earlier local or development test first.", error);
        if (!host_->WorkArea(area_, dpi_)) return Fail("Windows did not report the screen's working area.", error);
    } else {
        area_ = LocalRect{0, 0, 1920, 1080};
        dpi_ = 96;
    }
    if (!ComputeLocalLayout(area_, dpi_, layout_, error)) return false;
    // The mod in the second copy, from this package.
    if (!InstallIntoGuest(env, paths_[0], paths_[1], error)) return false;
    // Separate local-test save copies, seeded from this folder's co-op copy only.
    for (int i = 0; i < 2; ++i) {
        const std::string role = kRoles[i];
        const std::wstring dest = JoinPath2(paths_[i].mod, L"local-test-saves");
        if (HasReparsePoint(dest, paths_[i].game)) return Fail("[" + role + "] Filesystem links are not supported here.", error);
        const std::string problem = SaveDirProblem(Utf8(dest));
        if (!problem.empty()) return Fail(problem, error);
        const std::wstring local = JoinPath2(dest, L"287700\\local");
        if (!IsFile(JoinPath2(local, L"TPP_GAME_DATA0")) && !IsFile(JoinPath2(local, L"TPP_GAME_DATA1"))) {
            if (PathExists(dest))
                return Fail("[" + role + "] The local test save copy " + Utf8(dest) +
                                " is incomplete. It was kept for recovery: move it out of the mgs-coop folder, then try again. No save "
                                "files were replaced.",
                            error);
            const std::wstring temp = dest + L".seed-" + Wide(RandomHex(16));
            if (!CreateDirectories(temp)) return Fail("[" + role + "] The local test save copy could not be created.", error);
            if (TreeHasReparse(paths_[0].coop_saves) || !CopySaveTree(paths_[0].coop_saves, temp, error)) {
                RemoveTree(temp);
                if (error.empty()) error = "Filesystem links are not supported here.";
                return false;
            }
            if (!MovePath(temp, dest, error)) {
                RemoveTree(temp);
                return false;
            }
            LauncherLog(env, paths_[0], "local_test=saves_seeded; role=" + role);
        }
        if (TreeHasReparse(dest)) return Fail("[" + role + "] Filesystem links are not supported here.", error);
        const std::wstring graphics = JoinPath2(local, L"TPP_GRAPHICS_CONFIG");
        if (!IsFile(graphics)) return Fail("[" + role + "] The copied save has no graphics configuration. Start normal co-op once, then retry.", error);
        std::string text, edited;
        if (!ReadSmallFile(graphics, text, 1u << 20)) return Fail("[" + role + "] The graphics configuration cannot be read.", error);
        const int w = std::max(640, i == 0 ? layout_.host_client_width : layout_.guest_client_width);
        const int h = std::max(480, i == 0 ? layout_.host_client_height : layout_.guest_client_height);
        if (!SetGraphicsWindowed(text, w, h, edited, error)) return false;
        if (!WriteAtomic(graphics, edited, true, error)) return false;
        save_dirs_[i] = dest;
    }
    // Activation under both folders' locks: the journal first, then each file.
    json::Value session;
    {
        RootLocks locks(host, guest);
        if (!locks.ok()) return Fail("Another co-op tool is changing these game folders right now; try again in a moment.", error);
        for (const InstancePaths& p : paths_) {
            if (AnyGameRunningIn(env, p.game)) return Fail("Quit both game copies before starting a local test.", error);
            if (PlaySessionLive(env, p)) return Fail("Another co-op session started during preparation; no local activation files were written.", error);
        }
        run_id_ = RandomHex(16);
        run_dir_ = JoinPath2(LocalRunsRoot(host), Wide(run_id_));
        if (!NewLocalSession(env, host, guest, run_dir_, session, error)) return false;
        const std::string secret = NewSecret();
        std::string written_error;
        bool ok = !secret.empty();
        if (!ok) written_error = "Windows could not make a link secret (no random numbers).";
        for (int i = 0; ok && i < 2; ++i) {
            const std::string role = kRoles[i];
            LocalLabParams lab;
            lab.role = role;
            lab.save_dir = Utf8(save_dirs_[i]);
            lab.partner = request.partner;
            lab.world = request.world;
            lab.host_steam = host_steam;
            std::string lab_text, coop_text;
            ok = MakeLocalLabIniText(lab, lab_text, written_error) &&
                 MakeLocalCoopIniText(i == 0 ? "snake" : "quiet", request.hud, coop_text, written_error) &&
                 SetLocalSessionFile(env, session, role, "lab.ini", lab_text, written_error) &&
                 SetLocalSessionFile(env, session, role, "link-secret.txt", secret + "\r\n", written_error) &&
                 SetLocalSessionFile(env, session, role, "coop.ini", coop_text, written_error) &&
                 SetLocalSessionFile(env, session, role, "script-observe.enabled", kMarkerText, written_error);
        }
        if (!ok) {
            LocalRestoreResult r;
            std::string restore_error;
            json::Value back;
            if (ReadLocalSession(run_dir_, back, restore_error)) RestoreLocalSession(env, back, r, restore_error);
            return Fail(written_error, error);
        }
    }
    LauncherLog(env, paths_[0], "local_test=activated; run=" + run_id_ + "; fixture=" + (request.fixture ? "1" : "0"));
    if (request.fixture) return true;   // the PowerShell test's fixture mode: activation only
    auto undo = [&](const std::string& why) {
        json::Value back;
        LocalRestoreResult r;
        std::string e;
        if (ReadLocalSession(run_dir_, back, e)) RestoreLocalSession(env, back, r, e);
        return Fail(why, error);
    };
    std::uint32_t helper_pid = 0;
    std::int64_t helper_start = 0;
    if (!host_->StartHelper(host, run_id_, helper_pid, helper_start))
        return undo("The background cleanup helper could not start; no games were launched.");
    session["watchdog_pid"] = static_cast<std::int64_t>(helper_pid);
    session["watchdog_start_utc"] = helper_start ? FormatUtcRoundTrip(helper_start) : std::string();
    if (!SaveLocalSession(session, error)) return undo(error);
    for (int i = 0; i < 2; ++i) {
        const std::string role = kRoles[i];
        Instance& g = games_[i];
        g = Instance{};
        for (int k = 0; k < 3; ++k) g.offsets[k] = static_cast<std::int64_t>(FileSizeOrZero(JoinPath2(paths_[i].mod, Wide(kLogNames[k]))));
        std::uint32_t pid = 0;
        std::int64_t start = 0;
        std::string launch_error;
        bool ok = host_->StartGame(paths_[i].exe, paths_[i].game, pid, start, launch_error);
        bool verified = false;
        for (int attempt = 0; ok && attempt < 20 && !verified; ++attempt) {
            verified = ImageBelongs(host_->ProcessImage(pid), paths_[i].game);
            if (!verified) env.SleepMs(50);
        }
        if (!ok || !verified) {
            StopOwnedGames();
            for (int wait = 0; wait < 10 && (AnyGameRunningIn(env, paths_[0].game) || AnyGameRunningIn(env, paths_[1].game)); ++wait) env.SleepMs(300);
            const std::string why = !ok ? "[" + role + "] MGSV could not be started: " + launch_error
                                        : "[" + role + "] The launched process did not match this game copy.";
            if (!AnyGameRunningIn(env, paths_[0].game) && !AnyGameRunningIn(env, paths_[1].game)) return undo(why);
            // A game is still running (one this test started and could not close, or the unverified one): the run
            // stays, stopped, and puts both folders back itself once both games have exited (Poll); the helper does
            // it if this window is closed first.
            running_ = true;
            next_poll_ms_ = 0;
            status_.active = true;
            status_.failed = true;
            stop_reason_ = why;
            status_.headline = "Local test stopped";
            status_.detail = why + " Quit both games; the test settings in both folders are put back after both have exited.";
            LauncherLog(env, paths_[0], std::string("local_test=stopped; reason=start_failed; role=") + role);
            return Fail(status_.detail, error);
        }
        g.pid = pid;
        g.start_ms = start;
        g.exe = paths_[i].exe;
        g.started_ms = env.NowMs();
        LauncherLog(env, paths_[0], "local_test=game_started; role=" + role + "; pid=" + std::to_string(pid));
    }
    running_ = true;
    next_poll_ms_ = 0;
    status_.active = true;
    status_.headline = "Starting both games...";
    status_.detail = layout_.vertical ? "Host above, guest below." : "Host on the left, guest on the right.";
    return true;
}

void LocalTestRun::PlaceWindows(bool retile) {
    const std::int64_t now = env_->NowMs();
    for (int i = 0; i < 2; ++i) {
        Instance& g = games_[i];
        if (!OwnedAlive(g, paths_[i].game)) continue;
        const std::vector<LocalWindow> windows = host_->Windows(g.pid);
        if (windows.empty()) continue;
        const LocalWindow& w = windows.front();
        const LocalRect& target = i == 0 ? layout_.host : layout_.guest;
        bool overlap = false;
        const Instance& other = games_[1 - i];
        if (OwnedAlive(other, paths_[1 - i].game))
            for (const LocalWindow& o : host_->Windows(other.pid))
                if (Intersects(w.rect, o.rect)) overlap = true;
        const PlacementDecision d = DecidePlacement(g.has_placement ? &g.placement : nullptr, w.handle, w.rect, target, now, retile, w.moving, overlap);
        g.placement = d.entry;
        g.has_placement = true;
        if (!d.place) continue;
        if (!OwnedAlive(g, paths_[i].game)) continue;
        bool matched = false;
        if (host_->PlaceWindow(w.handle, target)) {
            for (const LocalWindow& after : host_->Windows(g.pid))
                if (after.handle == w.handle)
                    matched = std::abs(after.rect.x - target.x) <= 2 && std::abs(after.rect.y - target.y) <= 2 &&
                              std::abs(after.rect.width - target.width) <= 2 && std::abs(after.rect.height - target.height) <= 2;
        }
        if (!matched && notice_.empty())
            notice_ = std::string("The ") + kRoles[i] + " game did not take its window position: press Arrange windows, or move it yourself.";
    }
}

void LocalTestRun::Arrange() {
    if (!running_) return;
    if (!request_.fixture) {
        LocalRect area;
        int dpi = 96;
        LocalLayout layout;
        std::string error;
        if (host_->WorkArea(area, dpi) && ComputeLocalLayout(area, dpi, layout, error)) {
            area_ = area;
            dpi_ = dpi;
            layout_ = layout;
        }
    }
    notice_.clear();
    PlaceWindows(true);
}

void LocalTestRun::Finish() {
    json::Value session;
    std::string error;
    LocalRestoreResult r;
    if (!ReadLocalSession(run_dir_, session, error) || !RestoreLocalSession(*env_, session, r, error)) {
        status_.detail = "Both games have closed, but the folders could not be restored yet: " + error;
        return;   // tried again on the next poll
    }
    running_ = false;
    status_.active = false;
    LauncherLog(*env_, paths_[0], "local_test=ended; state=" + r.state + "; kept=" + std::to_string(r.kept.size()));
    // Only the test's settings files come back; the co-op files copied into the second folder at the start stay.
    const std::string ended = r.kept.empty()
                                  ? "Local test ended. The test settings in both folders were put back; the second folder keeps this version's "
                                    "co-op files."
                                  : "Local test ended. Files changed during the test were kept for review: " + Join(r.kept, ", ") + ".";
    status_.headline = status_.failed ? "Local test stopped" : "Local test ended";
    status_.detail = status_.failed ? stop_reason_ + " " + ended : ended;
}

bool LocalTestRun::Poll() {
    if (!running_) return false;
    Env& env = *env_;
    const std::int64_t now = env.NowMs();
    if (now < next_poll_ms_) return true;
    next_poll_ms_ = now + 500;
    const bool host_running = AnyGameRunningIn(env, paths_[0].game), guest_running = AnyGameRunningIn(env, paths_[1].game);
    if (!host_running && !guest_running) {
        Finish();
        return running_;
    }
    // Only the processes this run started may run from the two folders.
    if (!status_.failed) {
        for (int i = 0; i < 2 && !status_.failed; ++i) {
            for (const ProcessRow& row : GameProcessesIn(env, paths_[i].game)) {
                std::int64_t start = 0;
                if (!env.ProcessStartMs(row.pid, start)) continue;   // exited between the polls
                if (row.pid == games_[i].pid && OwnedAlive(games_[i], paths_[i].game)) continue;
                StopOwnedGames();
                status_.failed = true;
                stop_reason_ = std::string("[") + kRoles[i] + "] An unexpected game process appeared, so only this test's own games were closed.";
                LauncherLog(env, paths_[0], std::string("local_test=stopped; reason=unexpected_process; role=") + kRoles[i]);
                break;
            }
        }
    }
    // Start-up evidence: both copies must use their local-test saves.
    for (int i = 0; i < 2 && !status_.failed; ++i) {
        Instance& g = games_[i];
        if (g.safe || !g.pid) continue;
        for (int k = 0; k < 3; ++k) {
            std::string delta;
            ReadNewLines(JoinPath2(paths_[i].mod, Wide(kLogNames[k])), g.offsets[k], delta);
            if (g.texts[k].size() < (8u << 20)) g.texts[k] += delta;
        }
        const bool final_check = now - g.started_ms >= kEvidenceDeadlineMs;
        const LocalEvidence ev = GetLocalLaunchEvidence(kRoles[i], g.texts[0], g.texts[1], g.texts[2], Utf8(save_dirs_[i]), final_check);
        if (ev.state == "unsafe") {
            StopOwnedGames();
            status_.failed = true;
            stop_reason_ = std::string("[") + kRoles[i] + "] Startup did not confirm that the separate local-test saves are in use (" +
                           ev.reason + "), so only this test's games were closed.";
            LauncherLog(env, paths_[0], std::string("local_test=stopped; reason=") + ev.reason + "; role=" + kRoles[i]);
        } else if (ev.state == "ok") {
            g.safe = true;
            LauncherLog(env, paths_[0], std::string("local_test=startup_ok; role=") + kRoles[i]);
        }
    }
    if (!status_.failed) PlaceWindows(false);
    // Each game's status line.
    std::string texts[2];
    for (int i = 0; i < 2; ++i) {
        ReadTextShared(paths_[i].link_status, texts[i], 65536);
        const json::Value status = ParseStatusText(texts[i], games_[i].pid);
        const bool alive = OwnedAlive(games_[i], paths_[i].game);
        const std::string line = alive ? FormatStatusLine(status, i == 0, now, CoreBuildId()) : std::string("closed");
        (i == 0 ? status_.host_line : status_.guest_line) = line;
    }
    status_.host_link = texts[0];
    if (status_.failed) {
        status_.headline = "Local test stopped";
        status_.detail = stop_reason_ + " The test settings in both folders are put back after both games have exited.";
    } else {
        status_.headline = games_[0].safe && games_[1].safe ? "Local test running" : "Starting both games...";
        status_.detail = "Host: " + status_.host_line + ". Guest: " + status_.guest_line + ".";
        if (!notice_.empty()) status_.detail += " " + notice_;
    }
    return true;
}

// ---- The hidden helper -----------------------------------------------------------------------------------
int RunLocalWatch(Env& env, const std::wstring& host_root, const std::string& run_id) {
    if (!IsLowerHex32(run_id)) return 2;
    const std::wstring host = FullPath(host_root);
    const std::wstring run = JoinPath2(LocalRunsRoot(host), Wide(run_id));
    json::Value s;
    std::string error;
    if (!ReadLocalSession(run, s, error) || !IEquals(FullPath(Wide(s.Str("host_root"))), host)) return 1;
    const InstancePaths paths = GetInstancePaths(host);
    LauncherLog(env, paths, "local_watch=start; run=" + run_id);
    for (;;) {
        if (!ReadLocalSession(run, s, error)) return error.empty() ? 0 : 1;
        if (s.Str("state") == "off") return 0;
        if (!AnyGameIn(env, s) && !AliveAt(env, s.Int("launcher_pid", 0), s.Str("launcher_start_utc"))) {
            env.SleepMs(3000);
            if (!AnyGameIn(env, s)) {
                LocalRestoreResult r;
                if (!ReadLocalSession(run, s, error) || !RestoreLocalSession(env, s, r, error)) {
                    LauncherLog(env, paths, "local_watch=restore_failed");
                    return 1;
                }
                LauncherLog(env, paths, "local_watch=restored; state=" + r.state + "; kept=" + std::to_string(r.kept.size()));
                return 0;
            }
        }
        env.SleepMs(2000);
    }
}

}  // namespace ddt::core
