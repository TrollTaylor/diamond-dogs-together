#pragma once
// Original foregroundless worker wire protocol. Pure parsers: no OS/game calls.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include <utility>

namespace mgsworker {
inline constexpr char kRetailExeSha[] = "085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45";
inline constexpr char kRetailSteamSha[] = "08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A";
inline constexpr char kBaselineArchiveSha[] = "87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96";
inline constexpr std::size_t kMaxManifest = 16384;
inline constexpr std::uint64_t kQualificationAgeMs = 3600000;

inline bool RunId(const std::string& s) {
    return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
inline bool Sha256(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}
inline char Fold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
inline bool EqualFold(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return Fold(x) == Fold(y); });
}
inline bool Decimal(const std::string& s, std::uint64_t limit, std::uint64_t& out) {
    if (s.empty() || (s.size() > 1 && s[0] == '0')) return false;
    std::uint64_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (digit > limit || n > (limit - digit) / 10) return false;
        n = n * 10 + digit;
    }
    out = n; return true;
}
inline bool Utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto lead = static_cast<unsigned char>(s[i++]);
        if (lead < 0x80) continue;
        unsigned more = 0; std::uint32_t cp = 0, minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { more = 1; cp = lead & 31; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { more = 2; cp = lead & 15; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { more = 3; cp = lead & 7; minimum = 0x10000; }
        else return false;
        if (more > s.size() - i) return false;
        for (unsigned j = 0; j < more; ++j) {
            const auto c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
// Drive-absolute paths only. Reject device/UNC/ADS, aliases and relative segments.
// Final OS path, reparse and file identities are separately checked by supervisor.
inline bool AbsolutePath(const std::string& s) {
    if (s.size() < 4 || s.size() >= 240 || !Utf8(s) ||
        !((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) || s[1] != ':' || s[2] != '\\') return false;
    std::size_t first = 3;
    for (std::size_t i = 3; i <= s.size(); ++i) {
        if (i != s.size()) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c < 32 || c == 127 || s[i] == '/' || s[i] == ':' || s[i] == '"' || s[i] == '<' || s[i] == '>' || s[i] == '|' || s[i] == '?' || s[i] == '*') return false;
        }
        if (i == s.size() || s[i] == '\\') {
            const std::string part = s.substr(first, i - first);
            if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
            if (EqualFold(part, "userdata")) return false;
            first = i + 1;
        }
    }
    return true;
}
inline bool Inside(const std::string& child, const std::string& parent) {
    return child.size() > parent.size() && child[parent.size()] == '\\' && EqualFold(child.substr(0, parent.size()), parent);
}
inline std::string Parent(const std::string& path) { const auto p = path.find_last_of('\\'); return p == std::string::npos ? std::string{} : path.substr(0, p); }
inline std::string Name(const std::string& path) { const auto p = path.find_last_of('\\'); return path.substr(p == std::string::npos ? 0 : p + 1); }

struct Manifest {
    std::map<std::string, std::string> values;
    std::string mode, run, directory, desktop;
    std::uint32_t timeout_ms = 0, ready_timeout_ms = 0, offline_port = 0;
    const std::string& At(const char* key) const { return values.at(key); }
};
inline bool ParseManifest(const std::string& text, Manifest& out, std::string& reason) {
    auto fail = [&](const char* why) { reason = why; return false; };
    if (text.empty() || text.size() > kMaxManifest || !Utf8(text)) return fail("manifest_size_or_utf8");
    Manifest m;
    for (std::size_t first = 0; first < text.size();) {
        const auto end = text.find('\n', first); if (end == std::string::npos) return fail("manifest_final_newline");
        auto line = text.substr(first, end - first); first = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) return fail("manifest_empty_line");
        const auto eq = line.find('=');
        if (eq == 0 || eq == std::string::npos || eq + 1 == line.size()) return fail("manifest_pair");
        const auto key = line.substr(0, eq), value = line.substr(eq + 1);
        if (!std::all_of(key.begin(), key.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; })) return fail("manifest_key");
        for (unsigned char c : value) if (c < 32 || c == 127) return fail("manifest_control");
        if (!m.values.emplace(key, value).second) return fail("manifest_duplicate_key");
    }
    auto get = [&](const char* key) -> std::string { const auto i = m.values.find(key); return i == m.values.end() ? std::string{} : i->second; };
    if (get("schema") != "1") return fail("manifest_schema");
    m.mode = get("mode"); if (m.mode != "probe" && m.mode != "mgsv") return fail("manifest_mode");
    m.run = get("run_id"); m.directory = get("run_dir"); m.desktop = get("desktop");
    if (!RunId(m.run) || m.desktop != "MGSWorker_" + m.run) return fail("manifest_run_or_desktop");
    if (!AbsolutePath(m.directory)) return fail("manifest_run_directory");
    std::uint64_t number = 0;
    if (!Decimal(get("timeout_ms"), 600000, number) || number < 2000) return fail("manifest_timeout");
    m.timeout_ms = static_cast<std::uint32_t>(number);
    if (!Decimal(get("ready_timeout_ms"), 180000, number) || number < 1000 || number > m.timeout_ms) return fail("manifest_ready_timeout");
    m.ready_timeout_ms = static_cast<std::uint32_t>(number);
    std::vector<std::string> keys{"schema", "mode", "run_id", "run_dir", "desktop", "timeout_ms", "ready_timeout_ms"};
    auto path = [&](const char* key) { keys.emplace_back(key); return AbsolutePath(get(key)); };
    auto hash = [&](const char* key) { keys.emplace_back(key); return Sha256(get(key)); };
    if (m.mode == "probe") {
        if (!path("probe_exe") || !hash("probe_sha256")) return fail("manifest_probe_pin");
    } else {
        if (!hash("exe_sha256") || !EqualFold(get("exe_sha256"), kRetailExeSha) ||
            !hash("steam_sha256") || !EqualFold(get("steam_sha256"), kRetailSteamSha) || !hash("loader_sha256")) return fail("manifest_retail_pin");
        if (!hash("runtime_archive_sha256") || !EqualFold(get("runtime_archive_sha256"), kBaselineArchiveSha) || get("runtime_motion_count") != "21") return fail("manifest_baseline_catalog_pin");
        keys.emplace_back("runtime_motion_count");
        if (!path("probe_receipt") || !hash("probe_receipt_sha256")) return fail("manifest_qualification_pin");
        if (!Inside(get("probe_receipt"), Parent(m.directory)) || Inside(get("probe_receipt"), m.directory)) return fail("manifest_qualification_location");
        if (!Decimal(get("offline_master_port"), 65535, number) || number < 1024) return fail("manifest_offline_port");
        m.offline_port = static_cast<std::uint32_t>(number); keys.emplace_back("offline_master_port");
        for (const char* role : {"host", "guest"}) {
            const std::string prefix = role;
            const auto exe = prefix + "_exe", saves = prefix + "_save_dir";
            if (!path(exe.c_str()) || !EqualFold(Name(get(exe.c_str())), "mgsvtpp.exe") || !path(saves.c_str()) || !Inside(get(saves.c_str()), m.directory)) return fail("manifest_game_or_save_path");
            for (const char* kind : {"lab", "coop", "graphics"}) if (!hash((prefix + "_" + kind + "_sha256").c_str())) return fail("manifest_configuration_pin");
        }
        const auto host = Parent(get("host_exe")), guest = Parent(get("guest_exe"));
        if (EqualFold(host, guest) || Inside(host, guest) || Inside(guest, host) || Inside(m.directory, host) || Inside(m.directory, guest)) return fail("manifest_game_roots_overlap");
        const auto hs = get("host_save_dir"), gs = get("guest_save_dir");
        if (EqualFold(hs, gs) || Inside(hs, gs) || Inside(gs, hs)) return fail("manifest_save_roots_overlap");
    }
    for (const auto& pair : m.values) if (std::find(keys.begin(), keys.end(), pair.first) == keys.end()) return fail("manifest_unknown_key");
    if (m.values.size() != keys.size()) return fail("manifest_missing_key");
    out = std::move(m); reason.clear(); return true;
}

inline bool Tokens(const std::string& text, std::size_t expected, std::vector<std::string>& out) {
    if (text.empty() || text.size() > 1024 || text.back() != '\n') return false;
    const auto line = text.substr(0, text.size() - 1);
    std::vector<std::string> words;
    for (std::size_t first = 0; first < line.size();) {
        const auto end = line.find(' ', first); const auto n = end == std::string::npos ? line.size() : end;
        if (n == first) return false;
        auto word = line.substr(first, n - first);
        for (unsigned char c : word) if (c < 33 || c > 126) return false;
        words.push_back(std::move(word)); first = n + 1;
        if (words.size() > expected) return false;
        if (end != std::string::npos && first == line.size()) return false;
    }
    if (words.size() != expected) return false; out = std::move(words); return true;
}
struct Control { std::uint64_t sequence = 0; };
inline bool ParseControl(const std::string& text, const std::string& run, std::uint64_t previous, Control& out) {
    std::vector<std::string> words; std::uint64_t seq = 0;
    if (!Tokens(text, 4, words) || words[0] != "MGSWORKERCTL1" || words[1] != run || words[3] != "shutdown" ||
        !Decimal(words[2], UINT64_MAX, seq) || seq == 0 || seq <= previous) return false;
    out.sequence = seq; return true;
}
inline bool Admission(const std::string& text, const std::string& magic, const std::string& run,
                      const std::string& role, std::uint32_t pid, std::uint64_t creation, const std::string& desktop) {
    std::vector<std::string> words; std::uint64_t id = 0, time = 0;
    return Tokens(text, 7, words) && words[0] == magic && words[1] == run && words[2] == role &&
        Decimal(words[3], UINT32_MAX, id) && id == pid && Decimal(words[4], UINT64_MAX, time) && time == creation &&
        words[5] == desktop && words[6] == "admitted";
}
struct Qualification { std::string run, supervisor_sha, probe_sha, bmp_sha; std::uint64_t tick = 0; };
inline bool ParseQualification(const std::string& text, const std::string& supervisor_sha, std::uint64_t now, Qualification& out) {
    std::vector<std::string> w; std::uint64_t tick = 0;
    if (!Tokens(text, 7, w) || w[0] != "MGSWORKERQUAL1" || !RunId(w[1]) || !Sha256(w[2]) || !EqualFold(w[2], supervisor_sha) ||
        !Sha256(w[3]) || !Sha256(w[4]) || !Decimal(w[5], UINT64_MAX, tick) || tick > now || now - tick > kQualificationAgeMs || w[6] != "qualified") return false;
    out = {w[1], w[2], w[3], w[4], tick}; return true;
}
inline std::string Json(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { constexpr char h[] = "0123456789abcdef"; out += "\\u00"; out += h[c >> 4]; out += h[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
} // namespace mgsworker
