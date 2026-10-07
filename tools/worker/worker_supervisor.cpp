// Original private-desktop worker owner. GUI subsystem; no console or UI.
// CLI accepts ONLY --launch <pinned manifest>. No arbitrary program arguments.
#include "worker_win32.hpp"
#include "worker_game_gate.hpp"
#include <iphlpapi.h>
#include <imm.h>
#include <shellapi.h>
#include <sstream>
#include <thread>
#include <exception>

namespace {
using namespace mgsworker;
static_assert(sizeof(void*) == 8, "Build the worker for x64");
constexpr DWORD kUi = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS |
                      JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_WRITECLIPBOARD | JOB_OBJECT_UILIMIT_EXITWINDOWS;
struct Child {
    std::string role, path, sha, suspended_desktop_observation; DWORD pid = 0, tid = 0, exit = STILL_ACTIVE;
    std::uint64_t creation = 0; bool resumed = false, admitted = false;
    Handle process, thread;
};
struct Worker {
    Manifest manifest; std::string stage = "preflight", reason = "starting", initial_input, last_input, own_sha;
    DWORD error = 0, pid = GetCurrentProcessId(); std::uint64_t creation = 0, deadline = 0, control_sequence = 0;
    bool ready = false, forced = false, job_verified = false, desktop_created = false, violation = false, probe_ready = false;
    bool private_enum_result = false, private_callback_bad = false, private_offending_visible = false;
    DWORD private_enum_error = 0, private_callback_error = 0, private_offending_pid = 0, private_offending_tid = 0;
    std::uint64_t private_offending_hwnd = 0; std::string private_offending_class;
    HWND sentinel_hwnd = nullptr; DWORD sentinel_tid = 0, private_callback_count = 0;
    std::string sentinel_class; bool sentinel_created = false, sentinel_destroyed = false, private_sentinel_seen = false;
    bool sentinel_ime_disabled = false; DWORD sentinel_ime_thread = 0;
    std::uint64_t offending_desktop_handle = 0; DWORD offending_desktop_error = 0, offending_name_error = 0;
    std::string offending_desktop_name, offending_reason;
    DWORD cross_desktop_available = 0, cross_desktop_unavailable = 0, cross_desktop_mismatch = 0;
    DWORD window_thread_leases = 0, owned_thread_leases = 0;
    Handle job, mutex, run_lock; std::vector<Child> children; std::vector<Pin> pins; std::vector<Handle> paths;
    HDESK desktop = nullptr;
    std::wstring At(const std::string& name) const { return Wide(manifest.directory + "\\" + name); }
    std::string JsonText() {
        std::ostringstream o;
        o << "{\"schema\":1,\"mode\":" << Json(manifest.mode) << ",\"run_id\":" << Json(manifest.run) << ",\"run_dir\":" << Json(manifest.directory)
          << ",\"desktop\":" << Json(manifest.desktop) << ",\"supervisor_pid\":" << pid << ",\"supervisor_start_filetime\":" << Json(std::to_string(creation))
          << ",\"supervisor_sha256\":" << Json(own_sha) << ",\"stage\":" << Json(stage) << ",\"reason\":" << Json(reason) << ",\"win32_error\":" << error
          << ",\"ready\":" << (ready ? "true" : "false") << ",\"probe_ready\":" << (probe_ready ? "true" : "false") << ",\"deadline_tick64\":" << deadline
          << ",\"control_sequence\":" << control_sequence << ",\"job_policy_verified\":" << (job_verified ? "true" : "false") << ",\"job_kill_on_close\":true"
          << ",\"job_ui_restrictions\":" << kUi << ",\"handle_inheritance\":false,\"switch_desktop_called\":false,\"system_input_called\":false,\"focus_called\":false"
          << ",\"desktop_created\":" << (desktop_created ? "true" : "false") << ",\"input_desktop_initial\":" << Json(initial_input) << ",\"input_desktop_last\":" << Json(last_input)
          << ",\"input_desktop_private\":" << (!last_input.empty() && last_input == manifest.desktop ? "true" : "false")
          << ",\"visible_desktop_unchanged_at_observations\":" << (!initial_input.empty() && last_input == initial_input ? "true" : "false")
          << ",\"desktop_ownership_violation\":" << (violation ? "true" : "false") << ",\"forced_job_termination\":" << (forced ? "true" : "false")
          << ",\"private_sentinel\":{\"hwnd\":" << Json(std::to_string(reinterpret_cast<std::uint64_t>(sentinel_hwnd)))
          << ",\"pid\":" << pid << ",\"tid\":" << sentinel_tid << ",\"class\":" << Json(sentinel_class)
          << ",\"created\":" << (sentinel_created ? "true" : "false") << ",\"destroyed\":" << (sentinel_destroyed ? "true" : "false")
          << ",\"show_window_called\":false,\"focus_called\":false,\"ime_disabled\":" << (sentinel_ime_disabled ? "true" : "false")
          << ",\"ime_disabled_thread\":" << sentinel_ime_thread << ",\"ime_process_wide\":false}"
          << ",\"private_window_enumeration\":{\"returned\":" << (private_enum_result ? "true" : "false")
          << ",\"callback_count\":" << private_callback_count << ",\"sentinel_seen\":" << (private_sentinel_seen ? "true" : "false")
          << ",\"last_error\":" << private_enum_error << ",\"callback_bad\":" << (private_callback_bad ? "true" : "false")
          << ",\"callback_error\":" << private_callback_error << ",\"offending_hwnd\":" << Json(std::to_string(private_offending_hwnd))
          << ",\"offending_reason\":" << Json(offending_reason) << ",\"thread_desktop_handle\":" << Json(std::to_string(offending_desktop_handle))
          << ",\"thread_desktop_last_error\":" << offending_desktop_error << ",\"queried_desktop_name\":" << Json(offending_desktop_name)
          << ",\"queried_desktop_name_error\":" << offending_name_error
          << ",\"window_thread_leases\":" << window_thread_leases << ",\"owned_thread_leases\":" << owned_thread_leases
          << ",\"cross_desktop_query_available\":" << cross_desktop_available << ",\"cross_desktop_query_unavailable\":" << cross_desktop_unavailable
          << ",\"cross_desktop_query_mismatch\":" << cross_desktop_mismatch << ",\"cross_desktop_unavailable_is_mismatch\":false"
          << ",\"offending_pid\":" << private_offending_pid << ",\"offending_tid\":" << private_offending_tid
          << ",\"offending_class\":" << Json(private_offending_class) << ",\"offending_visible\":" << (private_offending_visible ? "true" : "false") << '}'
          << ",\"gameplay_validated\":false,\"security_sandbox\":false,\"gpu_timeout_settings_changed\":false,\"owned_processes\":[";
        bool first = true;
        for (auto& child : children) {
            if (!first) o << ','; first = false;
            if (child.process.Valid()) GetExitCodeProcess(child.process.value, &child.exit);
            o << "{\"role\":" << Json(child.role) << ",\"pid\":" << child.pid << ",\"primary_tid\":" << child.tid << ",\"creation_filetime\":" << Json(std::to_string(child.creation))
              << ",\"exe\":" << Json(child.path) << ",\"sha256\":" << Json(child.sha) << ",\"resumed\":" << (child.resumed ? "true" : "false")
              << ",\"admitted\":" << (child.admitted ? "true" : "false")
              << ",\"suspended_desktop_observation\":" << Json(child.suspended_desktop_observation)
              << ",\"suspended_desktop_is_admission_proof\":false,\"exit_code\":" << child.exit << '}';
        }
        o << "],\"file_pins\":["; first = true;
        for (const auto& pin : pins) { if (!first) o << ','; first = false;
            o << "{\"path\":" << Json(Narrow(pin.path)) << ",\"sha256\":" << Json(pin.digest) << ",\"read_handle_held\":" << (pin.file.Valid() ? "true" : "false") << '}'; }
        o << "]}\n"; return o.str();
    }
    void Publish() { if (run_lock.Valid()) Atomic(At("supervisor.json"), JsonText()); }
    void LockPath(const std::string& path, bool directory) {
        auto added = LockAncestors(path, directory); for (auto& handle : added) paths.push_back(std::move(handle));
    }
    std::size_t AddPin(const std::string& path, const std::string& sha) {
        LockPath(path, false); pins.push_back(Pinned(path, sha)); return pins.size() - 1;
    }
    bool Alive(const Child& child) const { return child.process.Valid() && WaitForSingleObject(child.process.value, 0) == WAIT_TIMEOUT; }
    const Child* Owned(DWORD child_pid) const {
        for (const auto& child : children) if (child.pid == child_pid && Alive(child)) return &child; return nullptr;
    }
    void Lease(const Child& child) const {
        Require(Alive(child) && CreationTime(child.process.value) == child.creation && SameWide(ExePath(child.process.value), Wide(child.path)), "owned_process_lease");
        BOOL member = FALSE; Require(IsProcessInJob(child.process.value, job.value, &member) != FALSE && member, "owned_process_job");
    }
};
struct Sentinel {
    Worker* owner = nullptr; HWND hwnd = nullptr; HINSTANCE instance = GetModuleHandleW(nullptr); ATOM klass = 0; std::wstring name;
    void Create(Worker& worker) {
        owner = &worker; PrivateThread(worker.manifest.desktop); name = Wide("MGSWorkerSentinel_" + worker.manifest.run);
        // Prevent a default IME auxiliary window for ONLY this fresh private
        // helper thread, before its first top-level WM_CREATE. No process-wide
        // or user input/IME setting is changed; the helper ends with this run.
        // https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immdisableime
        const DWORD helper_tid = GetCurrentThreadId();
        Require(ImmDisableIME(helper_tid) != FALSE, "sentinel_thread_disable_ime");
        worker.sentinel_ime_disabled = true; worker.sentinel_ime_thread = helper_tid;
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = instance; wc.lpszClassName = name.c_str();
        klass = RegisterClassW(&wc); Require(klass != 0, "sentinel_register_class");
        // Exact private top-level sentinel keeps enumeration nonempty before a
        // child connects to USER/GDI. Never WS_VISIBLE, ShowWindow or focus.
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdesktopwindows
        hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, name.c_str(), L"", WS_POPUP,
                               0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        Require(hwnd != nullptr, "sentinel_create"); DWORD pid = 0;
        const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        Require(pid == worker.pid && tid == GetCurrentThreadId() && !IsWindowVisible(hwnd), "sentinel_initial_ownership");
        PrivateThread(worker.manifest.desktop); worker.sentinel_hwnd = hwnd; worker.sentinel_tid = tid;
        worker.sentinel_class = Narrow(name); worker.sentinel_created = true;
    }
    void Destroy(bool strict) {
        bool success = true;
        if (hwnd) { if (DestroyWindow(hwnd)) hwnd = nullptr; else success = false; }
        if (klass && !hwnd) { if (UnregisterClassW(name.c_str(), instance)) klass = 0; else success = false; }
        if (owner) owner->sentinel_destroyed = hwnd == nullptr && klass == 0;
        if (strict) Require(success && hwnd == nullptr && klass == 0, "sentinel_destroy_before_desktop_restore");
    }
    ~Sentinel() { Destroy(false); }
};
void VerifySentinel(const Worker& worker) {
    Require(worker.sentinel_created && !worker.sentinel_destroyed && worker.sentinel_hwnd && IsWindow(worker.sentinel_hwnd), "sentinel_missing");
    DWORD pid = 0; const DWORD tid = GetWindowThreadProcessId(worker.sentinel_hwnd, &pid);
    Require(pid == worker.pid && tid == worker.sentinel_tid && tid == GetCurrentThreadId() && !IsWindowVisible(worker.sentinel_hwnd), "sentinel_ownership_changed");
    wchar_t klass[128]{}; const int count = GetClassNameW(worker.sentinel_hwnd, klass, 128);
    Require(count > 0 && Narrow(std::wstring(klass, static_cast<std::size_t>(count))) == worker.sentinel_class, "sentinel_class_changed");
    const auto desktop = GetThreadDesktop(tid); Require(desktop && ObjectName(desktop) == worker.manifest.desktop, "sentinel_desktop_changed");
}
bool NoListener(std::uint32_t port) {
    DWORD bytes = 0; const DWORD first = GetTcpTable(nullptr, &bytes, FALSE);
    Require(first == ERROR_INSUFFICIENT_BUFFER && bytes >= offsetof(MIB_TCPTABLE, table) && bytes <= 1048576, "offline_port_table_size");
    std::vector<unsigned char> buffer(bytes); const DWORD result = GetTcpTable(reinterpret_cast<MIB_TCPTABLE*>(buffer.data()), &bytes, FALSE);
    Require(result == NO_ERROR && bytes <= buffer.size(), "offline_port_table"); const auto* table = reinterpret_cast<const MIB_TCPTABLE*>(buffer.data());
    Require(table->dwNumEntries <= (bytes - offsetof(MIB_TCPTABLE, table)) / sizeof(MIB_TCPROW), "offline_port_table_bounds");
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i]; const auto p = ((row.dwLocalPort & 255) << 8) | ((row.dwLocalPort >> 8) & 255);
        if (row.dwState == MIB_TCP_STATE_LISTEN && p == port) return false;
    }
    return true;
}
bool WindowedJson(const std::string& text) {
    // The controller separately verifies MGSV's tag. Here require one unescaped
    // string property, pinned by the manifest; no loose substring acceptance.
    const std::string key = "\"window_mode\""; const auto p = text.find(key);
    if (p == std::string::npos || text.find(key, p + key.size()) != std::string::npos) return false;
    auto at = p + key.size(); auto blank = [&] { while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at; };
    blank(); if (at >= text.size() || text[at++] != ':') return false; blank();
    return text.compare(at, 10, "\"Windowed\"") == 0 && at + 10 < text.size() &&
           (text[at + 10] == ',' || text[at + 10] == '}' || text[at + 10] == ' ' || text[at + 10] == '\r' || text[at + 10] == '\n' || text[at + 10] == '\t');
}
void GateGames(Worker& worker) {
    auto& m = worker.manifest; Require(NoListener(m.offline_port), "offline_master_port_listening");
    const auto qualification = worker.AddPin(m.At("probe_receipt"), m.At("probe_receipt_sha256")); Qualification seal;
    Require(ParseQualification(Read(worker.pins[qualification].file.value, 1024), worker.own_sha, GetTickCount64(), seal) && seal.run != m.run, "probe_qualification_not_current");
    std::string host_mutex;
    for (const char* role : {"host", "guest"}) {
        const std::string prefix = role; const auto root = Parent(m.At((prefix + "_exe").c_str()));
        worker.LockPath(root, true); worker.LockPath(m.At((prefix + "_save_dir").c_str()), true);
        worker.LockPath(m.At((prefix + "_save_dir").c_str()) + "\\287700\\local", true);
        worker.LockPath(m.At((prefix + "_save_dir").c_str()) + "\\remote", true);
        worker.AddPin(m.At((prefix + "_exe").c_str()), m.At("exe_sha256"));
        worker.AddPin(root + "\\steam_api64.dll", m.At("steam_sha256")); worker.AddPin(root + "\\dinput8.dll", m.At("loader_sha256"));
        const auto archive = worker.AddPin(root + "\\mgs-coop\\mgs-coop-player-runtime.dat", m.At("runtime_archive_sha256"));
        Require(Size(worker.pins[archive].file.value, 16333824) == 16333824, "baseline_archive_size");
        const auto lab = worker.AddPin(root + "\\mgs-coop\\lab.ini", m.At((prefix + "_lab_sha256").c_str()));
        const auto coop = worker.AddPin(root + "\\mgs-coop\\coop.ini", m.At((prefix + "_coop_sha256").c_str()));
        const auto graphics_path = m.At((prefix + "_save_dir").c_str()) + "\\287700\\local\\TPP_GRAPHICS_CONFIG";
        const auto graphics = worker.AddPin(graphics_path, m.At((prefix + "_graphics_sha256").c_str()));
        const auto lab_text = Read(worker.pins[lab].file.value, 65536); const auto coop_text = Read(worker.pins[coop].file.value, 65536);
        mgscoop::LabConfig lc{}; mgscoop::CoopConfig cc{};
        Require(mgscoop::ParseLabConfig(lab_text.data(), lab_text.size(), lc) && OfflineLab(lc, prefix, m.At((prefix + "_save_dir").c_str())) &&
                SameWide(Wide(lc.save_dir), Wide(m.At((prefix + "_save_dir").c_str()))), "copied_save_lab_configuration");
        if (prefix == "host") host_mutex = lc.mutex_suffix; else Require(host_mutex != lc.mutex_suffix && lc.self_steamid != 0, "guest_identity_or_mutex");
        const std::string url = "http://127.0.0.1:" + std::to_string(m.offline_port) + "/tppstm/gate";
        Require(mgscoop::ParseCoopConfig(coop_text.data(), coop_text.size(), cc) && cc.present && cc.invalid_values == 0 && cc.master_url == url, "offline_master_configuration");
        const auto marker_path = root + "\\mgs-coop\\script-observe.enabled"; worker.LockPath(marker_path, false);
        Pin marker; marker.path = Wide(marker_path); marker.file = OpenRead(marker.path); marker.digest = Hash(marker.file.value);
        const auto marker_text = Read(marker.file.value, 64);
        Require(marker_text == "offline-v1\n", "offline_marker"); worker.pins.push_back(std::move(marker));
        Require(WindowedJson(Read(worker.pins[graphics].file.value, 65536)), "copied_graphics_not_windowed");
        // Graphics config is a save-copy file that the game may update. Release
        // its read pin only after all prelaunch observations; other pins remain.
        worker.pins[graphics].file.Reset();
    }
    Require(NoListener(m.offline_port), "offline_master_port_changed");
}
struct AttributeList {
    std::vector<unsigned char> storage; LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr; bool initialized = false; DWORD child_policy = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
    AttributeList() {
        SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        Require(bytes > 0 && bytes <= 65536, "process_attribute_size"); storage.resize(bytes); list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        Require(InitializeProcThreadAttributeList(list, 1, 0, &bytes) != FALSE, "process_attribute_list");
        initialized = true;
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &child_policy, sizeof(child_policy), nullptr, nullptr)) {
            const DWORD error = GetLastError(); DeleteProcThreadAttributeList(list); initialized = false; throw Failure("child_process_restriction", error);
        }
    }
    ~AttributeList() { if (initialized) DeleteProcThreadAttributeList(list); }
};
void Launch(Worker& worker, const std::string& role, const std::string& path, const std::string& sha) {
    PrivateThread(worker.manifest.desktop); worker.last_input = InputDesktop(); Require(worker.last_input == worker.initial_input, "launch_input_desktop_changed");
    auto exe = Wide(path), cwd = Wide(Parent(path)); auto environment = Environment(worker.manifest, role, Parent(path));
    std::wstring command = Quote(exe); if (role == "probe") command += L" --probe";
    auto desktop = Wide("WinSta0\\" + worker.manifest.desktop); AttributeList attributes; STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup); startup.StartupInfo.lpDesktop = desktop.data(); startup.StartupInfo.dwFlags = STARTF_FORCEOFFFEEDBACK;
    startup.lpAttributeList = attributes.list; PROCESS_INFORMATION created{};
    Require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, environment.data(), cwd.c_str(), &startup.StartupInfo, &created) != FALSE, "owned_child_create");
    Child child; child.role = role; child.path = path; child.sha = sha; child.pid = created.dwProcessId; child.tid = created.dwThreadId;
    child.process.Reset(created.hProcess); child.thread.Reset(created.hThread);
    try {
        child.creation = CreationTime(child.process.value); Require(SameWide(ExePath(child.process.value), exe), "suspended_child_image");
        Require(AssignProcessToJobObject(worker.job.value, child.process.value) != FALSE, "suspended_child_job_assignment");
        worker.Lease(child);
        // Desktop connection is made when a thread connects to USER/GDI; an
        // unstarted suspended thread is not desktop-admission evidence. Keep
        // this observation diagnostic only. lpDesktop, noninheritance and the
        // restrictive owned job are established before the first instruction.
        // https://learn.microsoft.com/en-us/windows/win32/winstation/thread-connection-to-a-desktop
        child.suspended_desktop_observation = "unavailable";
        try { const auto thread_desktop = GetThreadDesktop(child.tid);
            if (thread_desktop) child.suspended_desktop_observation = ObjectName(thread_desktop);
        } catch (const Failure&) { /* diagnostic query may be unavailable */ }
        worker.children.push_back(std::move(child)); worker.reason = role + "_suspended_owned"; worker.Publish();
        auto& owned = worker.children.back(); Require(ResumeThread(owned.thread.value) == 1, "owned_child_resume"); owned.resumed = true; worker.Publish();
    } catch (...) {
        // Before assignment/resume failure, kill only the exact suspended child
        // returned by CreateProcess, never an executable-name lookup.
        if (child.process.Valid()) { TerminateProcess(child.process.value, ERROR_CANCELLED); WaitForSingleObject(child.process.value, 2000); }
        throw;
    }
}
void Job(Worker& worker) {
    worker.job.Reset(CreateJobObjectW(nullptr, nullptr)); Require(worker.job.Valid(), "owned_job_create");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = worker.manifest.mode == "probe" ? 1 : 2;
    Require(SetInformationJobObject(worker.job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, "owned_job_limits");
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{}; ui.UIRestrictionsClass = kUi;
    Require(SetInformationJobObject(worker.job.value, JobObjectBasicUIRestrictions, &ui, sizeof(ui)) != FALSE, "owned_job_ui_limits");
    JOBOBJECT_BASIC_UI_RESTRICTIONS read_ui{}; JOBOBJECT_EXTENDED_LIMIT_INFORMATION read_limits{};
    Require(QueryInformationJobObject(worker.job.value, JobObjectBasicUIRestrictions, &read_ui, sizeof(read_ui), nullptr) != FALSE &&
            QueryInformationJobObject(worker.job.value, JobObjectExtendedLimitInformation, &read_limits, sizeof(read_limits), nullptr) != FALSE &&
            read_ui.UIRestrictionsClass == kUi && read_limits.BasicLimitInformation.LimitFlags == limits.BasicLimitInformation.LimitFlags &&
            read_limits.BasicLimitInformation.ActiveProcessLimit == limits.BasicLimitInformation.ActiveProcessLimit, "owned_job_policy_readback"); worker.job_verified = true;
}
struct Enumerate {
    Worker* worker; bool private_desktop; bool close = false; DWORD error = 0; bool bad = false;
    std::uint64_t offending_hwnd = 0; DWORD offending_pid = 0, offending_tid = 0; std::string offending_class; bool offending_visible = false;
    DWORD callback_count = 0; bool sentinel_seen = false;
    std::uint64_t desktop_handle = 0; DWORD desktop_error = 0, name_error = 0; std::string desktop_name, reason;
};
Handle LeaseThread(const Worker& worker, const Child& child, DWORD tid) {
    worker.Lease(child); Handle thread(OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid));
    Require(thread.Valid(), "owned_thread_open_query");
    Require(GetProcessIdOfThread(thread.value) == child.pid, "owned_thread_process_mismatch");
    DWORD exit = 0; Require(GetExitCodeThread(thread.value, &exit) != FALSE && exit == STILL_ACTIVE, "owned_thread_not_alive");
    worker.Lease(child); return thread;
}
struct ForeignDesktop {
    HDESK handle = nullptr; DWORD desktop_error = 0, name_error = 0; std::string name; bool available = false;
};
ForeignDesktop ObserveForeignDesktop(Worker& worker, DWORD tid) {
    ForeignDesktop result; SetLastError(ERROR_SUCCESS); result.handle = GetThreadDesktop(tid); result.desktop_error = GetLastError();
    if (result.handle) {
        try { result.name = ObjectName(result.handle); result.available = true; }
        catch (const Failure& e) { result.name_error = e.code; }
    }
    // Borrowed handle: never close. Foreign GetThreadDesktop can be denied by
    // the OS. Unavailability is diagnostic; a successful different name still
    // refuses. Window proof comes from association with our explicit HDESK in
    // EnumDesktopWindows and exact OS thread/process/job leases. The probe's
    // SELF PrivateThread check precedes window creation; MGSV's SELF check is
    // mandatory for context admission and every scenario/capture callback.
    if (result.available) { ++worker.cross_desktop_available;
        if (result.name != worker.manifest.desktop) ++worker.cross_desktop_mismatch;
    } else ++worker.cross_desktop_unavailable;
    return result;
}
void BadWindow(Enumerate& query, HWND hwnd, DWORD pid, DWORD tid, DWORD error) {
    query.bad = true; query.error = error;
    if (!query.private_desktop) { SetLastError(error); return; } // no visible metadata
    query.offending_hwnd = reinterpret_cast<std::uint64_t>(hwnd); query.offending_pid = pid; query.offending_tid = tid;
    query.offending_visible = IsWindowVisible(hwnd) != FALSE; wchar_t klass[256]{};
    const int count = GetClassNameW(hwnd, klass, static_cast<int>(std::size(klass)));
    try { query.offending_class = count > 0 ? Narrow(std::wstring(klass, static_cast<std::size_t>(count))) : "unavailable"; }
    catch (...) { query.offending_class = "unavailable"; }
    SetLastError(error); // EnumDesktopWindows requires callback failure errors.
}
BOOL CALLBACK Windows(HWND hwnd, LPARAM parameter) {
    auto& query = *reinterpret_cast<Enumerate*>(parameter); DWORD pid = 0; const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    query.desktop_handle = 0; query.desktop_error = 0; query.name_error = 0; query.desktop_name.clear(); query.reason.clear();
    ++query.callback_count;
    if (!tid || !pid) { query.reason = "GetWindowThreadProcessId"; BadWindow(query, hwnd, pid, tid, ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
    const auto* child = query.worker->Owned(pid);
    if (query.private_desktop) {
        if (hwnd == query.worker->sentinel_hwnd) {
            try { VerifySentinel(*query.worker); query.sentinel_seen = true; return TRUE; }
            catch (const Failure& e) { query.reason = std::string("sentinel:") + e.what(); BadWindow(query, hwnd, pid, tid, e.code); return FALSE; }
            catch (...) { BadWindow(query, hwnd, pid, tid, ERROR_UNHANDLED_EXCEPTION); return FALSE; }
        }
        if (!child) { query.reason = "unknown_private_window_owner"; BadWindow(query, hwnd, pid, tid, ERROR_INVALID_OWNER); return FALSE; }
        try { query.reason = "owned_window_thread_lease"; auto thread = LeaseThread(*query.worker, *child, tid); (void)thread;
            ++query.worker->window_thread_leases;
            const auto desktop = ObserveForeignDesktop(*query.worker, tid);
            query.desktop_handle = reinterpret_cast<std::uint64_t>(desktop.handle); query.desktop_error = desktop.desktop_error;
            query.desktop_name = desktop.name; query.name_error = desktop.name_error;
            if (desktop.available && desktop.name != query.worker->manifest.desktop) { query.reason = "positive_desktop_name_mismatch"; BadWindow(query, hwnd, pid, tid, ERROR_ACCESS_DENIED); return FALSE; }
            query.reason = "owned_window_thread_final_recheck"; DWORD final_pid = 0;
            Require(IsWindow(hwnd) && GetWindowThreadProcessId(hwnd, &final_pid) == tid && final_pid == child->pid, "owned_window_thread_changed");
            query.worker->Lease(*child);
            if (query.close && GetWindowThreadProcessId(hwnd, &pid) == tid && pid == child->pid) {
                // Graceful OS close of a leased private owned window, not input.
                if (!PostMessageW(hwnd, WM_CLOSE, 0, 0)) { query.reason = "private_owned_WM_CLOSE"; BadWindow(query, hwnd, pid, tid, GetLastError()); return FALSE; }
            }
        } catch (const Failure& e) { BadWindow(query, hwnd, pid, tid, e.code); return FALSE; }
          catch (...) { BadWindow(query, hwnd, pid, tid, ERROR_UNHANDLED_EXCEPTION); return FALSE; }
    } else if (child || pid == query.worker->pid || hwnd == query.worker->sentinel_hwnd) { query.reason = "owned_window_on_input_desktop"; BadWindow(query, hwnd, pid, tid, ERROR_ACCESS_DENIED); return FALSE; }
    return TRUE;
}
void Inspect(Worker& worker) {
    PrivateThread(worker.manifest.desktop); worker.last_input = InputDesktop();
    Require(worker.last_input == worker.initial_input && worker.last_input != worker.manifest.desktop, "visible_input_desktop_changed");
    VerifySentinel(worker);
    worker.cross_desktop_available = worker.cross_desktop_unavailable = worker.cross_desktop_mismatch = 0;
    worker.window_thread_leases = worker.owned_thread_leases = 0;
    // No unowned children/broker adoption. The child policy is defense in depth;
    // Windows documents bypass limits outside a stronger security sandbox.
    alignas(JOBOBJECT_BASIC_PROCESS_ID_LIST) unsigned char bytes[sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 8 * sizeof(ULONG_PTR)]{};
    auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(bytes);
    Require(QueryInformationJobObject(worker.job.value, JobObjectBasicProcessIdList, list, sizeof(bytes), nullptr) != FALSE &&
            list->NumberOfAssignedProcesses == list->NumberOfProcessIdsInList && list->NumberOfProcessIdsInList <= worker.children.size(), "owned_job_process_bounds");
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) Require(list->ProcessIdList[i] <= UINT32_MAX && worker.Owned(static_cast<DWORD>(list->ProcessIdList[i])) != nullptr, "unowned_job_child");
    Enumerate private_query{&worker, true}; SetLastError(ERROR_SUCCESS);
    const BOOL enumerated = EnumDesktopWindows(worker.desktop, Windows, reinterpret_cast<LPARAM>(&private_query));
    const DWORD enumeration_error = GetLastError();
    worker.private_enum_result = enumerated != FALSE; worker.private_enum_error = enumeration_error;
    worker.private_callback_count = private_query.callback_count; worker.private_sentinel_seen = private_query.sentinel_seen;
    worker.private_callback_bad = private_query.bad; worker.private_callback_error = private_query.error;
    worker.private_offending_hwnd = private_query.offending_hwnd; worker.private_offending_pid = private_query.offending_pid;
    worker.private_offending_tid = private_query.offending_tid; worker.private_offending_class = private_query.offending_class;
    worker.private_offending_visible = private_query.offending_visible;
    worker.offending_reason = private_query.bad ? private_query.reason : "none";
    worker.offending_desktop_handle = private_query.bad ? private_query.desktop_handle : 0;
    worker.offending_desktop_error = private_query.bad ? private_query.desktop_error : 0;
    worker.offending_desktop_name = private_query.bad ? private_query.desktop_name : "";
    worker.offending_name_error = private_query.bad ? private_query.name_error : 0;
    if (!enumerated || private_query.bad) throw Failure("private_window_ownership", private_query.bad ? private_query.error : enumeration_error);
    Require(private_query.callback_count > 0 && private_query.sentinel_seen, "private_sentinel_not_enumerated");
    OwnedDesktop input(OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS)); Require(input.value != nullptr, "input_window_enumeration_open");
    Enumerate input_query{&worker, false}; SetLastError(ERROR_SUCCESS);
    Require(EnumDesktopWindows(input.value, Windows, reinterpret_cast<LPARAM>(&input_query)) != FALSE && !input_query.bad, "owned_window_on_visible_desktop");
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)); Require(snapshot.Valid(), "thread_ownership_snapshot"); THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    Require(Thread32First(snapshot.value, &entry) != FALSE, "thread_ownership_first"); unsigned traversed = 0, owned_threads = 0;
    do {
        Require(++traversed <= 65536, "thread_snapshot_bound"); const auto* child = worker.Owned(entry.th32OwnerProcessID);
        if (child) { Require(++owned_threads <= 512, "owned_thread_count_bound");
            auto thread = LeaseThread(worker, *child, entry.th32ThreadID); (void)thread; ++worker.owned_thread_leases;
            const auto desktop = ObserveForeignDesktop(worker, entry.th32ThreadID);
            Require(!desktop.available || desktop.name == worker.manifest.desktop, "owned_thread_positive_desktop_mismatch"); }
        entry.dwSize = sizeof(entry);
    } while (Thread32Next(snapshot.value, &entry));
    Require(GetLastError() == ERROR_NO_MORE_FILES, "thread_ownership_iteration");
    for (const auto& child : worker.children) if (worker.Alive(child)) worker.Lease(child);
}
bool ControlRequested(Worker& worker) {
    std::string text; if (!TryText(worker.At("supervisor.control"), 1024, text)) return false;
    Control control;
    if (!ParseControl(text, worker.manifest.run, worker.control_sequence, control)) {
        // Duplicate shutdown commands are inert; malformed/stale commands never
        // authorize actions. Do not remove files owned by the controller.
        return false;
    }
    worker.control_sequence = control.sequence; return true;
}
bool AllExited(Worker& worker) { return std::none_of(worker.children.begin(), worker.children.end(), [&](const Child& c) { return worker.Alive(c); }); }
void CloseOwned(Worker& worker, bool gentle) {
    worker.ready = false;
    if (gentle && worker.desktop) {
        Enumerate query{&worker, true, true}; EnumDesktopWindows(worker.desktop, Windows, reinterpret_cast<LPARAM>(&query));
        const auto end = GetTickCount64() + 5000;
        while (!AllExited(worker) && GetTickCount64() < end) Sleep(50);
    }
    if (!AllExited(worker) && worker.job.Valid()) {
        worker.forced = true; Require(TerminateJobObject(worker.job.value, ERROR_CANCELLED) != FALSE, "owned_job_termination");
        for (const auto& child : worker.children) if (child.process.Valid()) WaitForSingleObject(child.process.value, 2000);
    }
}
void WaitAdmission(Worker& worker, Child& child) {
    const auto end = std::min<std::uint64_t>(worker.deadline, GetTickCount64() + worker.manifest.ready_timeout_ms);
    for (;;) {
        Inspect(worker);
        if (ControlRequested(worker)) throw Failure("shutdown_during_admission", ERROR_CANCELLED);
        std::string text; const auto path = worker.At(child.role + ".context");
        if (TryText(path, 1024, text)) {
            const auto magic = child.role == "probe" ? "MGSWORKERPROBE1" : "MGSWORKERCTX1";
            Require(Admission(text, magic, worker.manifest.run, child.role, child.pid, child.creation, worker.manifest.desktop), "child_admission_ownership_mismatch");
            // A successful probe may already have exited after publishing its result.
            if (child.role != "probe") worker.Lease(child);
            child.admitted = true; Inspect(worker);
            worker.reason = child.role + "_actual_context_admitted"; worker.Publish(); return;
        }
        if (!worker.Alive(child)) throw Failure("child_exited_before_admission", ERROR_PROCESS_ABORTED);
        if (GetTickCount64() >= end) throw Failure("child_admission_timeout", WAIT_TIMEOUT);
        worker.Publish(); Sleep(100);
    }
}
void PrepareRun(Worker& worker, const std::string& manifest_path) {
    Require(AbsolutePath(manifest_path), "manifest_absolute_path"); auto manifest_ancestors = LockAncestors(manifest_path, false);
    auto manifest = OpenRead(Wide(manifest_path)); std::string reason;
    const bool valid = ParseManifest(Read(manifest.value, kMaxManifest), worker.manifest, reason);
    Require(valid, reason.empty() ? "manifest_invalid" : reason.c_str());
    Require(Inside(manifest_path, worker.manifest.directory), "manifest_not_in_run_directory");
    worker.paths = LockAncestors(worker.manifest.directory, true);
    worker.mutex.Reset(CreateMutexW(nullptr, FALSE, Wide("Local\\MGSWorkerOwner_" + worker.manifest.run).c_str()));
    Require(worker.mutex.Valid() && GetLastError() != ERROR_ALREADY_EXISTS, "run_id_already_owned");
    worker.run_lock.Reset(CreateFileW(worker.At("supervisor.lock").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Require(worker.run_lock.Valid(), "run_directory_already_owned"); worker.creation = CreationTime(GetCurrentProcess());
    WriteAll(worker.run_lock.value, "MGSWORKEROWNER1 " + worker.manifest.run + " " + std::to_string(worker.pid) + " " + std::to_string(worker.creation) + "\n");
    for (const char* name : {"supervisor.json", "probe.context", "probe.json", "qualification.txt", "host.context", "guest.context", "probe-backbuffer.bmp"})
        Require(GetFileAttributesW(worker.At(name).c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND, "run_directory_stale_receipt");
    auto self = OpenRead(OwnExe()); worker.own_sha = Hash(self.value);
    Pin own; own.path = OwnExe(); own.file = std::move(self); own.digest = worker.own_sha; worker.pins.push_back(std::move(own));
    Pin launch; launch.path = Wide(manifest_path); launch.digest = Hash(manifest.value); launch.file = std::move(manifest); worker.pins.push_back(std::move(launch));
    worker.initial_input = InputDesktop(); Require(worker.initial_input != worker.manifest.desktop, "requested_desktop_is_input"); worker.last_input = worker.initial_input;
    worker.deadline = GetTickCount64() + worker.manifest.timeout_ms;
    if (worker.manifest.mode == "probe") worker.AddPin(worker.manifest.At("probe_exe"), worker.manifest.At("probe_sha256")); else GateGames(worker);
    worker.Publish();
}
bool NameUnused(const std::wstring& name) {
    SetLastError(ERROR_SUCCESS); OwnedDesktop existing(OpenDesktopW(name.c_str(), 0, FALSE, DESKTOP_READOBJECTS));
    if (existing.value) return false;
    const DWORD error = GetLastError(); Require(error == ERROR_FILE_NOT_FOUND || error == ERROR_INVALID_NAME, "desktop_name_unavailable"); return true;
}
void Execute(Worker& worker) {
    const auto name = Wide(worker.manifest.desktop); Require(NameUnused(name), "private_desktop_name_exists");
    // This function runs on a fresh window-free supervisor thread, not a Codex/
    // PowerShell thread. CreateDesktop/SetThreadDesktop never display a desktop.
    const HDESK original = GetThreadDesktop(GetCurrentThreadId()); Require(original != nullptr, "supervisor_original_desktop");
    OwnedDesktop desktop(CreateDesktopW(name.c_str(), nullptr, nullptr, 0, DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_ENUMERATE, nullptr));
    Require(desktop.value != nullptr, "private_desktop_create"); worker.desktop = desktop.value; worker.desktop_created = true;
    Sentinel sentinel;
    try {
        Require(SetThreadDesktop(desktop.value) != FALSE, "supervisor_private_thread_assignment"); PrivateThread(worker.manifest.desktop); Job(worker);
        sentinel.Create(worker); Inspect(worker);
        worker.stage = "starting"; worker.reason = "private_desktop_job_qualified"; worker.Publish();
        if (worker.manifest.mode == "probe") {
            Launch(worker, "probe", worker.manifest.At("probe_exe"), worker.manifest.At("probe_sha256")); WaitAdmission(worker, worker.children.back());
            while (!AllExited(worker) && GetTickCount64() < worker.deadline) { Inspect(worker); if (ControlRequested(worker)) throw Failure("probe_shutdown_requested", ERROR_CANCELLED); Sleep(50); }
            Require(AllExited(worker), "probe_exit_timeout"); Require(GetExitCodeProcess(worker.children[0].process.value, &worker.children[0].exit) != FALSE && worker.children[0].exit == 0, "probe_exit_not_success");
            const auto bitmap = worker.At("probe-backbuffer.bmp"); auto image = OpenRead(bitmap); Require(Size(image.value, 16438) == 16438, "probe_bitmap_size");
            const auto bmp_sha = Hash(image.value); const auto tick = GetTickCount64();
            Atomic(worker.At("qualification.txt"), "MGSWORKERQUAL1 " + worker.manifest.run + " " + worker.own_sha + " " + worker.manifest.At("probe_sha256") + " " + bmp_sha + " " + std::to_string(tick) + " qualified\n", false);
            worker.probe_ready = true; worker.stage = "completed"; worker.reason = "private_hardware_probe_qualified";
        } else {
            for (const char* role : {"host", "guest"}) {
                Require(NoListener(worker.manifest.offline_port), "offline_master_port_before_launch"); const auto key = std::string(role) + "_exe";
                Launch(worker, role, worker.manifest.At(key.c_str()), worker.manifest.At("exe_sha256")); WaitAdmission(worker, worker.children.back());
            }
            worker.ready = true; worker.stage = "running"; worker.reason = "both_actual_worker_contexts_admitted"; worker.Publish();
            bool requested = false;
            while (!AllExited(worker) && GetTickCount64() < worker.deadline) {
                Inspect(worker); if (ControlRequested(worker)) { requested = true; break; } worker.Publish(); Sleep(250);
            }
            worker.stage = "closing"; worker.reason = requested ? "owned_shutdown_requested" : (AllExited(worker) ? "owned_processes_exited" : "worker_timeout"); worker.Publish();
            CloseOwned(worker, true); worker.stage = worker.forced ? "terminated" : "closed";
        }
        worker.last_input = InputDesktop(); Require(worker.last_input == worker.initial_input && worker.last_input != worker.manifest.desktop, "final_input_desktop_changed");
        worker.job.Reset(); sentinel.Destroy(true); Require(SetThreadDesktop(original) != FALSE, "supervisor_restore_thread_desktop"); worker.desktop = nullptr;
    } catch (...) {
        worker.violation = true; worker.ready = false;
        try { CloseOwned(worker, false); } catch (...) { worker.forced = true; }
        worker.job.Reset(); sentinel.Destroy(false); SetThreadDesktop(original); worker.desktop = nullptr; throw;
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Process-local and inherited by our children; no global Windows setting.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    Worker worker;
    try {
        int count = 0; auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
        const bool valid = args && count == 3 && std::wstring(args[1]) == L"--launch"; std::string path;
        if (valid) path = Narrow(args[2]); if (args) LocalFree(args); Require(valid, "supervisor_arguments");
        PrepareRun(worker, path);
        // All desktop-affecting work lives on a fresh helper thread. Main never
        // creates a window, activates a window or changes its desktop binding.
        std::exception_ptr problem;
        std::thread owner([&] { try { Execute(worker); } catch (...) { problem = std::current_exception(); } }); owner.join();
        if (problem) std::rethrow_exception(problem);
        worker.Publish(); return worker.manifest.mode == "probe" && worker.probe_ready ? 0 : (worker.forced ? 4 : 0);
    } catch (const Failure& e) { worker.stage = "refused"; worker.reason = e.what(); worker.error = e.code; worker.ready = false; }
      catch (...) { worker.stage = "refused"; worker.reason = "supervisor_exception"; worker.error = ERROR_UNHANDLED_EXCEPTION; worker.ready = false; }
    try { worker.Publish(); } catch (...) {}
    return 2;
}
