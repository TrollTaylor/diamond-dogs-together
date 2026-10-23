// Diamond Dogs Together launcher core: the co-op session (see core_session.hpp).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core_session.hpp"

#include <algorithm>

#include "core_log.hpp"
#include "core_saves.hpp"
#include "launcher_join_code.hpp"

namespace ddt::core {
namespace {

constexpr char kMarkerName[] = "script-observe.enabled";

std::int64_t ParseTimeOr0(const std::string& text) {
    std::int64_t ms = 0;
    return !text.empty() && ParseUtc(text, ms) ? ms : 0;
}

bool AliveFromRecord(Env& env, std::int64_t pid, const std::string& start_text) {
    if (pid <= 0) return false;
    std::int64_t start = 0;
    if (start_text.empty() || !ParseUtc(start_text, start)) return env.ProcessAlive(static_cast<std::uint32_t>(pid));
    return ProcessAliveSince(env, static_cast<std::uint32_t>(pid), start);
}

bool SleepCancel(Env& env, std::uint32_t ms, const std::atomic<bool>* cancel) {
    while (ms > 0) {
        if (cancel && cancel->load()) return false;
        const std::uint32_t step = std::min<std::uint32_t>(ms, 100);
        env.SleepMs(step);
        ms -= step;
    }
    return !(cancel && cancel->load());
}

bool ReadBytes(const std::wstring& path, std::string& out, std::uint64_t max = 1u << 20) { return ReadTextShared(path, out, max); }

std::string ProcessStartText(Env& env, std::uint32_t pid) {
    std::int64_t ms = 0;
    return pid && env.ProcessStartMs(pid, ms) ? FormatUtcRoundTrip(ms) : std::string();
}

bool RemoveLinkRejoin(const InstancePaths& p) {
    if (!IsFile(p.link_rejoin)) return false;
    if (HasReparsePoint(p.link_rejoin, p.game)) return false;
    return DeleteFileChecked(p.link_rejoin);
}

}  // namespace

// ---- PlayMutex --------------------------------------------------------------------------
PlayMutex::PlayMutex(const std::wstring& game_root) {
    handle_ = CreateMutexW(nullptr, FALSE, PlayMutexName(game_root).c_str());
}

PlayMutex::~PlayMutex() {
    if (locked_) ReleaseMutex(static_cast<HANDLE>(handle_));
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
}

bool PlayMutex::Lock(unsigned long timeout_ms) {
    if (!handle_) return false;
    const DWORD r = WaitForSingleObject(static_cast<HANDLE>(handle_), timeout_ms);
    locked_ = r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
    return locked_;
}

// ---- play-session.json ------------------------------------------------------------
bool ReadPlaySession(const InstancePaths& paths, json::Value& out) {
    std::string text;
    if (!IsFile(paths.play_session) || !ReadTextShared(paths.play_session, text, 4u << 20)) return false;
    json::Value v;
    if (!json::Parse(text, v) || !v.is_object() || v.Int("v", 0) != 2) return false;
    out = std::move(v);
    return true;
}

bool SavePlaySession(const InstancePaths& paths, const json::Value& session, std::string& error) {
    if (HasReparsePoint(paths.play_session, paths.game)) {
        error = "Filesystem links are not supported here.";
        return false;
    }
    return WriteAtomic(paths.play_session, json::Dump(session) + "\n", true, error);
}

bool SessionIsActive(const json::Value& session) { return session.is_object() && session.Str("state") == "active"; }

json::Value HandoffOf(const json::Value& session) {
    json::Value t = json::Value::Object();
    const json::Value* h = session.Find("handoff");
    const json::Value empty = json::Value::Object();
    if (!h || !h->is_object()) h = &empty;
    t["state"] = h->Str("state");
    t["claimed_by_pid"] = h->Int("claimed_by_pid", 0);
    for (const char* k : {"claimed_start_utc", "claimed_utc"}) {
        const std::int64_t ms = ParseTimeOr0(h->Str(k));
        t[k] = ms ? FormatUtcRoundTrip(ms) : std::string();
    }
    t["xfer"] = h->Str("xfer");
    t["set_sha256"] = h->Str("set_sha256");
    t["count"] = h->Int("count", 0);
    t["aside"] = h->Str("aside");
    t["message"] = h->Str("message");
    return t;
}

bool PlaySessionLive(Env& env, const InstancePaths& paths) {
    json::Value s;
    if (!ReadPlaySession(paths, s) || !SessionIsActive(s)) return false;
    const std::int64_t self = env.CurrentPid();
    const std::int64_t launcher = s.Int("launcher_pid", 0);
    if (launcher != self && AliveFromRecord(env, launcher, s.Str("launcher_start_utc"))) return true;
    const std::int64_t watchdog = s.Int("watchdog_pid", 0);
    return watchdog != self && AliveFromRecord(env, watchdog, s.Str("watchdog_start_utc"));
}

bool HandoffClaimLive(Env& env, const json::Value& session) {
    const json::Value h = HandoffOf(session);
    const std::string state = h.Str("state");
    if (state != "applying" && state != "relaunching") return false;
    const std::int64_t claimed = ParseTimeOr0(h.Str("claimed_utc"));
    if (!claimed || env.NowMs() - claimed >= 300000) return false;
    return AliveFromRecord(env, h.Int("claimed_by_pid", 0), h.Str("claimed_start_utc"));
}

bool HandoffPending(Env& env, const json::Value& session) {
    const json::Value h = HandoffOf(session);
    const std::int64_t claimed = ParseTimeOr0(h.Str("claimed_utc"));
    if (!claimed || env.NowMs() - claimed >= 300000) return false;
    if (h.Str("state") == "relaunching") return true;
    return h.Str("state") == "applying" && AliveFromRecord(env, h.Int("claimed_by_pid", 0), h.Str("claimed_start_utc"));
}

bool UpdatePlaySession(Env& /*env*/, const InstancePaths& paths, const std::string& session_id,
                       const std::function<void(json::Value&)>& update, json::Value* out) {
    PlayMutex mutex(paths.game);
    if (!mutex.Lock()) return false;
    json::Value s;
    if (!ReadPlaySession(paths, s) || !SessionIsActive(s)) return false;
    if (!session_id.empty() && s.Str("session_id") != session_id) return false;
    update(s);
    std::string error;
    if (!SavePlaySession(paths, s, error)) return false;
    if (out) *out = s;
    return true;
}

bool SetSessionGame(Env& env, const InstancePaths& paths, const std::string& session_id, std::uint32_t pid, std::int64_t start_ms,
                    bool* relaunched) {
    bool changed = false;
    const bool ok = UpdatePlaySession(env, paths, session_id, [&](json::Value& t) {
        t["game_pid"] = static_cast<std::int64_t>(pid);
        t["game_start_utc"] = start_ms ? FormatUtcRoundTrip(start_ms) : std::string();
        json::Value h = HandoffOf(t);
        if (h.Str("state") == "relaunching") {
            h["state"] = "relaunched";
            t["handoff"] = h;
            changed = true;
        }
    });
    if (relaunched) *relaunched = changed;
    return ok;
}

// ---- Activation -----------------------------------------------------------------------
bool SessionFileTexts(const ActivationRequest& r, std::vector<std::pair<std::string, std::string>>& files, std::string& error) {
    files.clear();
    std::string lab, coop;
    if (!MakeLabIniText(r.lab, lab, error)) return false;
    if (!MakeCoopIniText(r.character, r.fallback, r.hud, coop, error)) return false;
    const bool wait = r.lab.role == "guest" && r.lab.steam_wait;
    if (!wait && !IsCanonicalSecret(r.secret)) {
        error = "Invalid link secret";
        return false;
    }
    files.emplace_back("lab.ini", lab);
    if (!wait) files.emplace_back("link-secret.txt", r.secret + "\r\n");
    files.emplace_back("coop.ini", coop);
    files.emplace_back(kMarkerName, kMarkerText);
    return true;
}

bool ActivateSession(Env& env, const InstancePaths& paths, const ActivationRequest& request,
                     const std::function<bool(std::string&)>& verify_before_marker, std::string& session_id_out, std::string& error) {
    session_id_out.clear();
    std::vector<std::pair<std::string, std::string>> files;
    if (!SessionFileTexts(request, files, error)) return false;
    PlayMutex mutex(paths.game);
    if (!mutex.Lock()) {
        error = "Another Diamond Dogs Together window is changing this game folder right now; try again in a moment.";
        return false;
    }
    if (HasReparsePoint(paths.mod, paths.game)) {
        error = "Filesystem links are not supported in the game folder.";
        return false;
    }
    json::Value s = json::Value::Object();
    s["v"] = 2;
    s["state"] = "active";
    s["role"] = request.lab.role;
    s["port"] = 47830;
    s["started_utc"] = FormatUtcRoundTrip(env.NowMs());
    s["launcher_pid"] = static_cast<std::int64_t>(env.CurrentPid());
    const std::int64_t me = env.CurrentStartMs();
    s["launcher_start_utc"] = me ? FormatUtcRoundTrip(me) : std::string();
    s["watchdog_pid"] = 0;
    s["watchdog_start_utc"] = "";
    s["session_id"] = RandomHex(16);
    s["transport"] = "steam";
    s["files"] = json::Value::Array();
    json::Value upnp = json::Value::Object();
    upnp["added"] = false;
    upnp["port"] = 0;
    s["upnp"] = upnp;
    s["clip_sha256"] = request.clip_sha256;
    s["lab"] = LabRecord(request.lab);
    s["launcher"] = request.launcher_note;
    s["note"] = "Written by Diamond Dogs Together; no secrets. The listed files are deleted when the game exits, only while their "
                "SHA-256 matches.";
    for (const auto& f : files) {
        const std::wstring path = JoinPath2(paths.mod, Wide(f.first));
        if (PathExists(path)) {
            error = "mgs-coop\\" + f.first + " appeared while co-op was being switched on; nothing more was written.";
            return false;
        }
        if (f.first == kMarkerName && verify_before_marker && !verify_before_marker(error)) return false;
        // Recorded first: a crash right after the write still leaves an owned file.
        json::Value row = json::Value::Object();
        row["name"] = f.first;
        row["sha256"] = Sha256Hex(f.second);
        s["files"].Push(std::move(row));
        if (!SavePlaySession(paths, s, error)) {
            error = "Could not record the co-op session: " + error;
            return false;
        }
        if (!WriteAtomic(path, f.second, false, error)) {
            error = "Could not write mgs-coop\\" + f.first + ": " + error;
            return false;
        }
    }
    session_id_out = s.Str("session_id");
    return true;
}

// ---- Cleanup ----------------------------------------------------------------------------
CleanupResult CleanupSession(Env& env, const InstancePaths& paths, bool force) {
    CleanupResult r;
    if (!IsFile(paths.play_session)) return r;
    if (AnyGameRunningIn(env, paths.game)) {
        r.messages.push_back("MGSV is running from this folder; co-op is switched off after it exits.");
        return r;
    }
    PlayMutex mutex(paths.game);
    if (!mutex.Lock()) {
        r.messages.push_back("Another Diamond Dogs Together window is changing this game folder right now.");
        return r;
    }
    json::Value s;
    if (!ReadPlaySession(paths, s) || !SessionIsActive(s)) return r;
    if (!force && HandoffClaimLive(env, s)) {
        r.messages.push_back("a save handoff restart is in progress");
        return r;
    }
    r.ran = true;
    if (HasReparsePoint(paths.mod, paths.game)) {
        r.messages.push_back("Filesystem links are not supported in the game folder; nothing was removed.");
        return r;
    }
    std::vector<std::pair<std::string, std::vector<std::string>>> records;
    if (const json::Value* files = s.Find("files")) {
        std::vector<const json::Value*> rows;
        if (files->is_object()) rows.push_back(files);
        else
            for (const json::Value& f : files->Elements()) rows.push_back(&f);
        for (const json::Value* f : rows) {
            std::vector<std::string> hashes = {UpperAscii(f->Str("sha256"))};
            const std::string prev = UpperAscii(f->Str("prev_sha256"));
            if (!prev.empty()) hashes.push_back(prev);
            records.emplace_back(f->Str("name"), hashes);
        }
    }
    for (const char* name : {kMarkerName, "coop.ini", "lab.ini", "link-secret.txt"}) {
        const auto it = std::find_if(records.begin(), records.end(), [&](const auto& kv) { return kv.first == name; });
        if (it == records.end()) continue;
        const std::wstring path = JoinPath2(paths.mod, Wide(name));
        if (!PathExists(path)) continue;
        std::string bytes;
        bool ours = !HasReparsePoint(path, paths.game) && ReadBytes(path, bytes) &&
                    std::find(it->second.begin(), it->second.end(), Sha256Hex(bytes)) != it->second.end();
        if (std::string(name) == kMarkerName && bytes != kMarkerText) ours = false;
        if (ours && DeleteFileChecked(path)) {
            r.removed.push_back(name);
        } else {
            r.kept.push_back(name);
            r.messages.push_back(std::string(name) + " was changed after Diamond Dogs Together wrote it; it was left in place (delete it "
                                                     "yourself if you do not need it).");
        }
    }
    if (RemoveLinkRejoin(paths)) r.removed.push_back("link-rejoin.txt");
    const std::string clip = UpperAscii(s.Str("clip_sha256"));
    if (!clip.empty()) {
        std::string current;
        if (env.GetClipboardText(current)) {
            while (!current.empty() && (current.back() == '\r' || current.back() == '\n')) current.pop_back();
            if (!current.empty() && Sha256Hex(current) == clip && env.ClearClipboard())
                r.messages.push_back("The join code was cleared from the clipboard.");
        }
    }
    s["state"] = "off";
    s["ended_utc"] = FormatUtcRoundTrip(env.NowMs());
    std::string error;
    SavePlaySession(paths, s, error);
    LauncherLog(env, paths, "cleanup; removed=" + std::to_string(r.removed.size()) + "; kept=" + std::to_string(r.kept.size()) +
                                "; force=" + (force ? std::string("1") : std::string("0")));
    return r;
}

StaleResult RemoveStaleHandoff(Env& env, const InstancePaths& paths) {
    StaleResult r;
    if (!IsDir(paths.mod)) return r;
    if (PlaySessionLive(env, paths) || AnyGameRunningIn(env, paths.game)) return r;
    PlayMutex mutex(paths.game);
    if (!mutex.Lock()) return r;
    json::Value s;
    if (ReadPlaySession(paths, s) && SessionIsActive(s) && HandoffClaimLive(env, s)) return r;
    if (HasReparsePoint(paths.mod, paths.game)) return r;
    bool not_applied = false;
    if (IsFile(paths.link_rejoin) && !HasReparsePoint(paths.link_rejoin, paths.game) && DeleteFileChecked(paths.link_rejoin))
        r.removed.push_back("link-rejoin.txt");
    const std::wstring dir = paths.coop_saves_handoff;
    if (IsDir(dir) && !HasReparsePoint(dir, paths.game)) {
        const std::wstring request = JoinPath2(dir, L"request.json");
        if (IsFile(request) && DeleteFileChecked(request)) {
            r.removed.push_back("coop-saves-handoff/request.json");
            not_applied = true;
        }
        for (const DirEntry& e : ListDirectory(dir)) {
            if (!e.directory || e.reparse) continue;
            const std::string name = Utf8(e.name);
            const bool partial = EndsWith(name, ".partial");
            const std::string base = partial ? name.substr(0, name.size() - 8) : name;
            if (!IsHexText(base, 8)) continue;
            if (RemoveProgressStage(JoinPath2(dir, e.name))) {
                r.removed.push_back("coop-saves-handoff/" + name);
                if (!partial) not_applied = true;
            }
        }
    }
    if (not_applied) r.messages.push_back("A host save from an earlier session was not applied and was removed.");
    if (!r.removed.empty()) LauncherLog(env, paths, "stale_handoff; removed=" + std::to_string(r.removed.size()));
    return r;
}

// ---- Steam's running flag and the relaunch ------------------------------------------------
SteamRunning GetSteamAppRunning(Env& env) {
    SteamRunning out;
    std::uint32_t running = 0, app = 0;
    const bool have_running = env.RegDword(false, L"Software\\Valve\\Steam\\Apps\\287700", L"Running", running);
    const bool have_app = env.RegDword(false, L"Software\\Valve\\Steam", L"RunningAppID", app);
    if (!have_running && !have_app) return out;
    out.known = true;
    out.running = (have_running && running != 0) || (have_app && app == 287700);
    out.text = "running=" + (have_running ? std::to_string(running) : std::string("none")) +
               ";running_app=" + (have_app ? std::to_string(app) : std::string("none"));
    return out;
}

SteamWait WaitSteamAppStopped(Env& env, int timeout_sec, const std::atomic<bool>* cancel) {
    SteamWait w;
    const std::int64_t start = env.NowMs();
    const std::int64_t deadline = start + timeout_sec * 1000ll;
    SteamRunning s = GetSteamAppRunning(env);
    while (s.known && s.running && env.NowMs() < deadline) {
        if (!SleepCancel(env, 500, cancel)) break;
        s = GetSteamAppRunning(env);
    }
    w.stopped = !(s.known && s.running);
    w.known = s.known;
    w.waited_ms = env.NowMs() - start;
    w.last = s.text;
    return w;
}

RelaunchResult StartRelaunch(Env& env, const InstancePaths& paths, bool check_only) {
    RelaunchResult out;
    {
        PlayMutex mutex(paths.game);
        if (!mutex.Lock()) {
            out.problem = "the game folder is busy";
        } else {
            json::Value s;
            do {
                if (!ReadPlaySession(paths, s) || !SessionIsActive(s)) {
                    out.problem = "co-op was switched off in the meantime";
                    break;
                }
                if (HandoffOf(s).Str("state") != "relaunching") {
                    out.problem = "no restart for the host's save is pending";
                    break;
                }
                if (AnyGameRunningIn(env, paths.game)) {
                    out.ok = true;
                    break;
                }
                std::vector<std::pair<std::string, std::string>> records;
                if (const json::Value* files = s.Find("files")) {
                    if (files->is_object()) records.emplace_back(files->Str("name"), UpperAscii(files->Str("sha256")));
                    else
                        for (const json::Value& f : files->Elements()) records.emplace_back(f.Str("name"), UpperAscii(f.Str("sha256")));
                }
                bool fine = true;
                for (const char* need : {"lab.ini", "coop.ini", kMarkerName}) {
                    if (std::none_of(records.begin(), records.end(), [&](const auto& kv) { return kv.first == need; })) {
                        out.problem = std::string("the session does not own ") + need;
                        fine = false;
                        break;
                    }
                }
                for (const auto& kv : records) {
                    if (!fine) break;
                    const std::wstring path = JoinPath2(paths.mod, Wide(kv.first));
                    std::string bytes;
                    if (!IsFile(path)) {
                        out.problem = kv.first + " is missing";
                        fine = false;
                    } else if (HasReparsePoint(path, paths.game) || !ReadBytes(path, bytes) || Sha256Hex(bytes) != kv.second) {
                        out.problem = kv.first + " changed";
                        fine = false;
                    } else if (kv.first == kMarkerName && bytes != kMarkerText) {
                        out.problem = "the co-op marker changed";
                        fine = false;
                    }
                }
                if (!fine) break;
                if (!check_only) {
                    if (!env.OpenUrl(Wide(kSteamUrl))) {
                        out.problem = "Steam did not accept the request to start MGSV";
                        break;
                    }
                    out.started = true;
                }
                out.ok = true;
            } while (false);
        }
    }
    const SteamRunning steam = GetSteamAppRunning(env);
    if (check_only) LauncherLog(env, paths, "relaunch=checked; ok=" + std::string(out.ok ? "1" : "0") + "; problem=" + out.problem + "; steam=" + steam.text);
    else if (out.ok && !out.started) LauncherLog(env, paths, "relaunch=skipped; reason=game_running; steam=" + steam.text);
    else if (out.ok) LauncherLog(env, paths, "relaunch=requested; started=1; steam=" + steam.text);
    else LauncherLog(env, paths, "relaunch=refused; problem=" + out.problem);
    return out;
}

// ---- The save handoff after the guest's game exited --------------------------------------
HandoffOutcome HandoffAfterExit(Env& env, const InstancePaths& paths, std::uint32_t exited_pid, std::int64_t exited_start_ms,
                                bool owner_watchdog, bool launcher_alive, std::int64_t exit_seen_ms,
                                const std::function<void(const HandoffOutcome&)>& on_applied, const std::atomic<bool>* cancel) {
    HandoffOutcome result;
    if (!IsFile(paths.play_session)) return result;
    json::Value first;
    if (!ReadPlaySession(paths, first) || !SessionIsActive(first) || first.Str("role") != "guest") return result;
    if (exited_pid == 0) return result;
    const std::int64_t wait_until = env.NowMs() + 30000;
    while (AnyGameRunningIn(env, paths.game)) {
        if (env.NowMs() >= wait_until) {
            result.action = "deferred";
            result.messages = {"MGSV is still running from this folder."};
            return result;
        }
        if (!SleepCancel(env, 500, cancel)) {
            result.action = "cancelled";
            return result;
        }
    }
    const std::int64_t seen = exit_seen_ms ? exit_seen_ms : env.NowMs();
    const std::string owner = owner_watchdog ? "watchdog" : "launcher";
    const std::uint32_t self = env.CurrentPid();
    bool launch = false, imported = false;
    {
        PlayMutex mutex(paths.game);
        if (!mutex.Lock()) {
            result.action = "deferred";
            return result;
        }
        json::Value session;
        if (!ReadPlaySession(paths, session) || !SessionIsActive(session) || session.Str("role") != "guest") return result;
        json::Value handoff = HandoffOf(session);
        const std::string hstate = handoff.Str("state");
        if ((hstate == "applying" || hstate == "relaunching") && handoff.Int("claimed_by_pid", 0) != static_cast<std::int64_t>(self) &&
            HandoffClaimLive(env, session)) {
            result.action = "claimed_by_other";
            result.messages = {"The background helper is restarting MGSV with the host's save..."};
            return result;
        }
        if (owner_watchdog && launcher_alive && env.NowMs() - seen < 20000) {
            result.action = "deferred";
            return result;
        }
        const HandoffRequest req = GetHandoffRequest(paths.coop_saves_handoff, exited_pid, exited_start_ms,
                                                     ParseTimeOr0(session.Str("started_utc")), env.NowMs());
        if (req.status == "none") return result;
        std::string error;
        if (req.status != "ok") {
            std::string stale_xfer;
            {
                std::string text;
                json::Value rj;
                if (ReadTextShared(req.request_path, text, 65536) && json::Parse(text, rj)) stale_xfer = rj.Str("xfer");
            }
            DeleteFileChecked(req.request_path);
            if (IsHexText(stale_xfer, 8)) RemoveProgressStage(JoinPath2(paths.coop_saves_handoff, Wide(stale_xfer)));
            RemoveLinkRejoin(paths);
            result.action = "stale";
            result.messages = {"A host save that does not belong to this game start was not applied and was removed (" + req.problem + ")."};
            LauncherLog(env, paths, "handoff=stale");
            return result;
        }
        if (handoff.Int("count", 0) >= 1) {
            DeleteFileChecked(req.request_path);
            RemoveProgressStage(req.stage_dir);
            RemoveLinkRejoin(paths);
            handoff["state"] = "limit";
            handoff["message"] = "a second host save in one session was not applied";
            session["handoff"] = handoff;
            SavePlaySession(paths, session, error);
            result.action = "limit";
            result.messages = {"The host's save was already applied once this session; the newer one was not used."};
            LauncherLog(env, paths, "handoff=limit");
            return result;
        }
        json::Value claim = json::Value::Object();
        claim["state"] = "applying";
        claim["claimed_by_pid"] = static_cast<std::int64_t>(self);
        const std::int64_t my_start = env.CurrentStartMs();
        claim["claimed_start_utc"] = my_start ? FormatUtcRoundTrip(my_start) : std::string();
        claim["claimed_utc"] = FormatUtcRoundTrip(env.NowMs());
        claim["xfer"] = req.xfer;
        claim["set_sha256"] = req.set_sha256;
        claim["count"] = 0;
        claim["aside"] = "";
        claim["message"] = "";
        session["handoff"] = claim;
        SavePlaySession(paths, session, error);
        LauncherLog(env, paths, "handoff=claimed; owner=" + owner + "; exited_pid=" + std::to_string(exited_pid) + "; xfer=" + req.xfer);
        std::string failure;
        do {
            if (HasReparsePoint(paths.mod, paths.game)) {
                failure = "filesystem links are not supported in the game folder";
                break;
            }
            json::Value files = session.Find("files") ? *session.Find("files") : json::Value::Array();
            if (files.is_object()) {
                json::Value a = json::Value::Array();
                a.Push(files);
                files = a;
            }
            int lab_rows = 0;
            std::string lab_row_sha;
            for (const json::Value& f : files.Elements())
                if (f.Str("name") == "lab.ini") {
                    ++lab_rows;
                    lab_row_sha = UpperAscii(f.Str("sha256"));
                }
            if (lab_rows != 1) {
                failure = "the session does not own lab.ini";
                break;
            }
            LabIniParams lab;
            const json::Value* lab_record = session.Find("lab");
            if (!lab_record || !LabParamsFromRecord(*lab_record, lab, failure)) {
                if (failure.empty()) failure = "the session does not record its lab.ini settings";
                break;
            }
            std::string old_text;
            if (!MakeLabIniText(lab, old_text, failure)) break;
            const std::string old_hash = Sha256Hex(old_text);
            std::string current;
            if (HasReparsePoint(paths.lab_ini, paths.game) || !IsFile(paths.lab_ini) || !ReadBytes(paths.lab_ini, current) ||
                Sha256Hex(current) != old_hash || lab_row_sha != old_hash) {
                failure = "lab.ini changed after Diamond Dogs Together wrote it";
                break;
            }
            json::Value manifest = json::Value::Array();
            for (const StagedFile& f : req.files) {
                json::Value row = json::Value::Object();
                row["entry"] = f.entry;
                row["bytes"] = static_cast<std::int64_t>(f.bytes);
                row["sha256"] = f.sha256;
                manifest.Push(std::move(row));
            }
            std::wstring aside;
            if (!ImportProgressFiles(env, req.stage_dir, paths.coop_saves, paths.coop_saves_replaced, "-before-handoff", "handoff",
                                     req.created_utc, &manifest, false, aside, failure))
                break;
            imported = true;
            result.aside = Utf8(LeafOf(aside));
            result.files = req.files.size();
            result.bytes = req.bytes;
            claim["aside"] = result.aside;
            json::Value applied = json::Value::Object();
            applied["format"] = "mgs-coop-save-handoff-applied";
            applied["v"] = 1;
            applied["set_sha256"] = req.set_sha256;
            applied["applied_utc"] = FormatUtcSeconds(env.NowMs());
            applied["aside"] = result.aside;
            WriteAtomic(JoinPath2(paths.coop_saves_handoff, L"applied.json"), json::Dump(applied) + "\n", true, error);
            // lab.ini again: no second handoff this session; a waiting guest
            // rejoins the host it was armed with (link-rejoin.txt).
            LabIniParams next = lab;
            next.save_handoff = "off";
            std::string secret_bytes;
            if (next.steam_wait && IsFile(paths.link_rejoin) && !HasReparsePoint(paths.link_rejoin, paths.game) &&
                FileSizeOrZero(paths.link_rejoin) <= 512) {
                std::string rejoin_text, secret;
                std::uint64_t host = 0;
                if (ReadBytes(paths.link_rejoin, rejoin_text, 512) && ParseJoinCode(rejoin_text, secret, host) &&
                    Decimal(host) != next.steam_self && !PathExists(paths.link_secret)) {
                    next.steam_wait = false;
                    next.steam_peer = Decimal(host);
                    secret_bytes = secret + "\r\n";
                }
                std::fill(rejoin_text.begin(), rejoin_text.end(), '\0');
                std::fill(secret.begin(), secret.end(), '\0');
            }
            std::string new_text;
            if (!MakeLabIniText(next, new_text, failure)) break;
            json::Value rows = json::Value::Array();
            for (const json::Value& f : files.Elements()) {
                if (f.Str("name") == "lab.ini") {
                    json::Value row = json::Value::Object();
                    row["name"] = "lab.ini";
                    row["sha256"] = Sha256Hex(new_text);
                    row["prev_sha256"] = old_hash;
                    rows.Push(std::move(row));
                } else {
                    rows.Push(f);
                }
            }
            if (!secret_bytes.empty()) {
                json::Value row = json::Value::Object();
                row["name"] = "link-secret.txt";
                row["sha256"] = Sha256Hex(secret_bytes);
                rows.Push(std::move(row));
            }
            session["files"] = rows;
            json::Value lab_out = *lab_record;
            lab_out["save_handoff"] = "off";
            if (!secret_bytes.empty()) {
                lab_out["steam_wait"] = false;
                lab_out["steam_peer"] = next.steam_peer;
            }
            session["lab"] = lab_out;
            // Recorded before the writes: a crash in between still leaves owned files.
            if (!SavePlaySession(paths, session, failure)) break;
            if (!secret_bytes.empty() && !WriteAtomic(paths.link_secret, secret_bytes, false, failure)) {
                std::fill(secret_bytes.begin(), secret_bytes.end(), '\0');
                break;
            }
            std::fill(secret_bytes.begin(), secret_bytes.end(), '\0');
            if (!WriteAtomic(paths.lab_ini, new_text, true, failure)) break;
            RemoveLinkRejoin(paths);
            DeleteFileChecked(req.request_path);
            RemoveProgressStage(req.stage_dir);
            claim["state"] = "relaunching";
            claim["count"] = 1;
            session["handoff"] = claim;
            SavePlaySession(paths, session, error);
            launch = true;
            LauncherLog(env, paths, "handoff=applied; files=" + std::to_string(result.files) + "; bytes=" + std::to_string(result.bytes));
        } while (false);
        if (!launch) {
            claim["state"] = "failed";
            claim["message"] = failure;
            session["handoff"] = claim;
            SavePlaySession(paths, session, error);
            DeleteFileChecked(req.request_path);
            RemoveLinkRejoin(paths);
            result.action = "failed";
            LauncherLog(env, paths, std::string("handoff=failed; imported=") + (imported ? "1" : "0"));
            if (imported)
                result.messages = {"The host's save was applied, but MGSV could not be started again: " + failure +
                                   ". Host or join again in Diamond Dogs Together; your previous co-op save is kept (Tools > Share save > "
                                   "Restore previous brings it back)."};
            else
                result.messages = {"Could not apply the host's save: " + failure + ". Your co-op save is unchanged."};
            return result;
        }
    }
    if (on_applied) on_applied(result);
    // Steam ignores a launch request while it still marks MGSV as running: wait for its flag, then a moment more.
    const SteamWait steam = WaitSteamAppStopped(env, 30, cancel);
    LauncherLog(env, paths, "steam_wait; stopped=" + std::string(steam.stopped ? "1" : "0") + "; known=" + (steam.known ? "1" : "0") +
                                "; ms=" + std::to_string(steam.waited_ms) + "; last=" + steam.last);
    if (!SleepCancel(env, steam.known ? 2000 : 5000, cancel)) {
        // The window is closing: the helper retries the start (the state stays relaunching).
        result.action = "cancelled";
        return result;
    }
    const RelaunchResult launched = StartRelaunch(env, paths, false);
    if (launched.ok) {
        result.action = "relaunched";
        return result;
    }
    UpdatePlaySession(env, paths, "", [&](json::Value& t) {
        json::Value h = HandoffOf(t);
        if (h.Str("state") == "relaunching" && h.Int("claimed_by_pid", 0) == static_cast<std::int64_t>(self)) {
            h["state"] = "failed";
            h["message"] = launched.problem;
            t["handoff"] = h;
        }
    });
    result.action = "failed";
    result.messages = {"The host's save was applied, but MGSV could not be started again: " + launched.problem +
                       ". Host or join again in Diamond Dogs Together; your previous co-op save is kept (Tools > Share save > Restore "
                       "previous brings it back)."};
    return result;
}

RelaunchWait GetRelaunchWait(Env& env, const InstancePaths& paths, const std::string& session_id) {
    RelaunchWait out;
    json::Value s;
    bool have = ReadPlaySession(paths, s);
    for (int retry = 0; !have && retry < 5; ++retry) {
        env.SleepMs(200);
        have = ReadPlaySession(paths, s);
    }
    if (!have || !SessionIsActive(s) || (!session_id.empty() && s.Str("session_id") != session_id)) {
        out.stop = true;
        out.off = true;
        out.lines = {"Co-op was switched off while MGSV was restarting. Do not start MGSV from Steam now; host or join again in Diamond "
                     "Dogs Together."};
        return out;
    }
    const json::Value h = HandoffOf(s);
    out.claimed_ms = ParseTimeOr0(h.Str("claimed_utc"));
    const std::string state = h.Str("state");
    if (state == "relaunching" || state == "relaunched" || HandoffClaimLive(env, s)) return out;
    out.stop = true;
    const std::string message = h.Str("message");
    out.lines.push_back("MGSV could not be started again" + (message.empty() ? std::string() : ": " + message) +
                        ". Do not start MGSV from Steam now; host or join again in Diamond Dogs Together.");
    if (!h.Str("aside").empty()) out.lines.push_back("Your previous co-op save is kept (Tools > Share save > Restore previous brings it back).");
    return out;
}

// ---- The background helper ---------------------------------------------------------------------
bool StartWatchdogFor(Env& env, const InstancePaths& paths, const std::string& session_id) {
    std::uint32_t pid = 0;
    if (!env.StartWatchdog(paths.game, session_id, pid) || pid == 0) {
        LauncherLog(env, paths, "watchdog=failed");
        return false;
    }
    const std::string start = ProcessStartText(env, pid);
    UpdatePlaySession(env, paths, session_id, [&](json::Value& t) {
        // The helper claims the role itself; keep a live claim of another helper.
        const std::int64_t current = t.Int("watchdog_pid", 0);
        if (current && current != static_cast<std::int64_t>(pid) && AliveFromRecord(env, current, t.Str("watchdog_start_utc"))) return;
        t["watchdog_pid"] = static_cast<std::int64_t>(pid);
        t["watchdog_start_utc"] = start;
    });
    LauncherLog(env, paths, "watchdog=started; pid=" + std::to_string(pid));
    return true;
}

int RunWatchdog(Env& env, const std::wstring& game_dir, const std::string& session_id) {
    const InstancePaths paths = GetInstancePaths(game_dir);
    const std::uint32_t self = env.CurrentPid();
    {
        // Never two helpers: claim the role under the mutex.
        PlayMutex mutex(paths.game);
        if (!mutex.Lock()) return 0;
        json::Value s;
        if (!ReadPlaySession(paths, s) || !SessionIsActive(s) || s.Str("session_id") != session_id) return 0;
        const std::int64_t current = s.Int("watchdog_pid", 0);
        if (current && current != static_cast<std::int64_t>(self) && AliveFromRecord(env, current, s.Str("watchdog_start_utc"))) {
            LauncherLog(env, paths, "watch=second_helper_exits");
            return 0;
        }
        s["watchdog_pid"] = static_cast<std::int64_t>(self);
        const std::int64_t start = env.CurrentStartMs();
        s["watchdog_start_utc"] = start ? FormatUtcRoundTrip(start) : std::string();
        std::string error;
        SavePlaySession(paths, s, error);
    }
    LauncherLog(env, paths, "watch=start");
    bool seen = false;
    std::uint32_t last_pid = 0;
    std::int64_t last_start = 0, exit_seen = 0, restart_until = 0, retry_at = 0;
    const std::int64_t restart_wait_ms = 180000;
    for (;;) {
        json::Value session;
        if (!ReadPlaySession(paths, session) || !SessionIsActive(session) || session.Str("session_id") != session_id) {
            LauncherLog(env, paths, "watch=session_over");
            return 0;
        }
        const bool launcher_alive = AliveFromRecord(env, session.Int("launcher_pid", 0), session.Str("launcher_start_utc"));
        const std::vector<ProcessRow> games = GameProcessesIn(env, paths.game);
        if (!games.empty()) {
            for (const ProcessRow& g : games) {
                if (g.unknown) continue;
                if (g.pid != last_pid) {
                    last_pid = g.pid;
                    last_start = 0;
                    env.ProcessStartMs(last_pid, last_start);
                    SetSessionGame(env, paths, session_id, last_pid, last_start);
                }
                break;
            }
            seen = true;
            exit_seen = 0;
            restart_until = 0;
        } else if (seen) {
            if (!exit_seen) {
                exit_seen = env.NowMs();
                LauncherLog(env, paths, "watch=game_exited; game_pid=" + std::to_string(last_pid) + "; launcher_alive=" +
                                            (launcher_alive ? "1" : "0"));
            }
            const HandoffOutcome h = HandoffAfterExit(env, paths, last_pid, last_start, true, launcher_alive, exit_seen, {}, nullptr);
            if (h.action == "deferred") {
                env.SleepMs(2000);
                continue;
            }
            if (h.action != "none") LauncherLog(env, paths, "handoff_after_exit; owner=watchdog; action=" + h.action);
            json::Value now_session;
            const bool pending = ReadPlaySession(paths, now_session) && HandoffPending(env, now_session);
            if (h.action == "relaunched" || h.action == "claimed_by_other" || pending) {
                seen = false;
                exit_seen = 0;
                restart_until = env.NowMs() + restart_wait_ms;
                retry_at = env.NowMs() + 20000;
                env.SleepMs(2000);
                continue;
            }
            break;
        } else if (restart_until) {
            const json::Value h = HandoffOf(session);
            const std::int64_t claimed = ParseTimeOr0(h.Str("claimed_utc"));
            const bool console_waits = launcher_alive && claimed && env.NowMs() - claimed < 330000;
            if (env.NowMs() >= restart_until && !console_waits) {
                LauncherLog(env, paths, "watch=restart_limit");
                CleanupSession(env, paths, true);
                return 0;
            }
            if (!launcher_alive && env.NowMs() >= retry_at && env.NowMs() + 30000 < restart_until && h.Str("state") == "relaunching") {
                retry_at = env.NowMs() + 20000;
                const SteamRunning steam = GetSteamAppRunning(env);
                if (!(steam.known && steam.running)) {
                    LauncherLog(env, paths, "watch=relaunch_retry");
                    StartRelaunch(env, paths, false);
                }
            }
        } else if (!launcher_alive) {
            // The window closed before the game started; a starting game gets a moment.
            env.SleepMs(3000);
            if (AnyGameRunningIn(env, paths.game)) {
                seen = true;
                continue;
            }
            break;
        }
        env.SleepMs(2000);
    }
    CleanupSession(env, paths, false);
    LauncherLog(env, paths, "watch=done");
    return 0;
}

}  // namespace ddt::core
