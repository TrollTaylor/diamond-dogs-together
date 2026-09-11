// Original bounded client for an existing, dedicated CDB session. Never attaches
// to a target process and never calls DebugBreakProcess or creates a target thread.
// Public contracts: Microsoft DebugConnect, IDebugControl::SetInterrupt,
// GetExecutionStatus, and .detach /h documentation on learn.microsoft.com.
// Event contract and exact SDK record:
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dbgeng/nf-dbgeng-idebugcontrol-getlasteventinformation
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbgeng.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>

namespace {
template<class T> struct Interface {
    T* value = nullptr;
    ~Interface() { if (value) value->Release(); }
    T* operator->() const { return value; }
};
bool token(const char* value, std::size_t minimum, std::size_t maximum) {
    const auto length = std::strlen(value);
    if (length < minimum || length > maximum) return false;
    for (std::size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9'))) return false;
    }
    return true;
}
int failure(const char* stage, HRESULT status) {
    std::fprintf(stderr, "MGS_STOP_FAILED stage=%s hresult=0x%08lx\n", stage,
                 static_cast<unsigned long>(status));
    return 1;
}
struct StopEvent {
    ULONG type = 0;
    ULONG process = DEBUG_ANY_ID;
    ULONG thread = DEBUG_ANY_ID;
    ULONG used = 0;
    DEBUG_LAST_EVENT_INFO_EXCEPTION exception{};
};
HRESULT read_stop(IDebugControl* control, StopEvent& event) {
    return control->GetLastEventInformation(&event.type, &event.process, &event.thread,
        &event.exception, static_cast<ULONG>(sizeof(event.exception)), &event.used,
        nullptr, 0, nullptr);
}
bool expected_interrupt(const StopEvent& event, ULONG process) {
    return event.type == DEBUG_EVENT_EXCEPTION && event.process == process &&
        event.thread != DEBUG_ANY_ID && event.used == sizeof(event.exception) &&
        event.exception.FirstChance == 1 &&
        event.exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT &&
        event.exception.ExceptionRecord.ExceptionFlags == 0;
}
bool same_stop(const StopEvent& a, const StopEvent& b) {
    return a.type == b.type && a.process == b.process && a.thread == b.thread &&
        a.used == b.used && a.exception.FirstChance == b.exception.FirstChance &&
        a.exception.ExceptionRecord.ExceptionCode == b.exception.ExceptionRecord.ExceptionCode &&
        a.exception.ExceptionRecord.ExceptionFlags == b.exception.ExceptionRecord.ExceptionFlags &&
        a.exception.ExceptionRecord.ExceptionAddress == b.exception.ExceptionRecord.ExceptionAddress;
}
}

int main(int argc, char** argv) {
    // Only localhost is supported. The caller creates and retains a unique pipe,
    // password and target PID for its own debugger session.
    if (argc != 4 || !token(argv[1], 16, 96) || !token(argv[2], 12, 12)) {
        std::fprintf(stderr, "Usage: stop_debugger <owned-local-pipe> <password12> <expected-target-pid>\n");
        return 2;
    }
    char* end = nullptr;
    if (argv[3][0] < '0' || argv[3][0] > '9') return 2;
    errno = 0;
    const auto expected_pid = std::strtoul(argv[3], &end, 10);
    if (!end || *end || expected_pid == 0 || errno == ERANGE) return 2;
    const auto module = LoadLibraryExW(
        L"C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\dbgeng.dll",
        nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return failure("load_installed_dbgeng", HRESULT_FROM_WIN32(GetLastError()));
    // The process retains the module until exit; interfaces are released first.
    const auto connect = reinterpret_cast<decltype(&DebugConnect)>(GetProcAddress(module, "DebugConnect"));
    if (!connect) return failure("resolve_DebugConnect", HRESULT_FROM_WIN32(GetLastError()));
    const std::string options = std::string("npipe:server=.,pipe=") + argv[1] + ",password=" + argv[2];
    Interface<IDebugClient> client;
    auto hr = connect(options.c_str(), __uuidof(IDebugClient), reinterpret_cast<void**>(&client.value));
    if (FAILED(hr)) return failure("connect_owned_debugger", hr);
    Interface<IDebugControl> control;
    hr = client->QueryInterface(__uuidof(IDebugControl), reinterpret_cast<void**>(&control.value));
    if (FAILED(hr)) return failure("query_control", hr);
    Interface<IDebugSystemObjects> objects;
    hr = client->QueryInterface(__uuidof(IDebugSystemObjects), reinterpret_cast<void**>(&objects.value));
    if (FAILED(hr)) return failure("query_system_objects", hr);
    ULONG count = 0, observed_pid = 0;
    hr = objects->GetNumberProcesses(&count);
    if (FAILED(hr) || count != 1) return failure("require_one_target", FAILED(hr) ? hr : E_UNEXPECTED);
    // Current register/process context is unavailable while a remote target
    // runs. Enumerate the already known system ID instead of selecting context.
    ULONG engine_pid = 0;
    hr = objects->GetProcessIdsByIndex(0, 1, &engine_pid, &observed_pid);
    if (FAILED(hr) || observed_pid != expected_pid) {
        std::fprintf(stderr, "MGS_STOP_PID expected=%lu observed=%lu query=0x%08lx\n", expected_pid, observed_pid, static_cast<unsigned long>(hr));
        return failure("verify_target_pid", FAILED(hr) ? hr : E_UNEXPECTED);
    }
    ULONG status = 0;
    hr = control->GetExecutionStatus(&status);
    if (FAILED(hr)) return failure("read_status", hr);
    // Even a legitimate initial-loader break or a previous helper's interrupt
    // is not owned by this invocation. Leave it stopped for its controller.
    // Normal registrar-hit detachment is handled by the generator itself.
    if (status == DEBUG_STATUS_BREAK) return failure("already_stopped_not_owned", E_ACCESSDENIED);
    if (status != DEBUG_STATUS_GO) return failure("target_not_running_normally", E_UNEXPECTED);
    hr = control->SetInterruptTimeout(2);
    if (FAILED(hr)) return failure("set_interrupt_timeout", hr);
    hr = control->SetInterrupt(DEBUG_INTERRUPT_ACTIVE);
    if (FAILED(hr)) return failure("interrupt_existing_debugger", hr);
    const auto deadline = GetTickCount64() + 5000;
    do {
        Sleep(20);
        hr = control->GetExecutionStatus(&status);
        if (FAILED(hr)) return failure("wait_for_break", hr);
    } while (status != DEBUG_STATUS_BREAK && GetTickCount64() < deadline);
    if (status != DEBUG_STATUS_BREAK) return failure("break_not_confirmed", E_PENDING);

    StopEvent event;
    hr = read_stop(control.value, event);
    if (hr != S_OK || !expected_interrupt(event, engine_pid)) {
        std::fprintf(stderr, "MGS_STOP_UNEXPECTED event_type=0x%lx engine_pid=%lu thread=%lu exception=0x%08lx first_chance=%lu\n",
            event.type, event.process, event.thread, event.exception.ExceptionRecord.ExceptionCode,
            event.exception.FirstChance);
        return failure("unexpected_stop_event", hr != S_OK ? hr : E_ACCESSDENIED);
    }
    // A matching exception code alone could also be a game's own DebugBreak.
    // Require the observed Windows interrupt site, resolved in the target.
    Interface<IDebugSymbols> symbols;
    hr = client->QueryInterface(__uuidof(IDebugSymbols), reinterpret_cast<void**>(&symbols.value));
    if (FAILED(hr)) return failure("query_symbols", hr);
    ULONG64 interrupt_address = 0;
    hr = symbols->GetOffsetByName("ntdll!DbgBreakPoint", &interrupt_address);
    if (hr != S_OK || !interrupt_address || event.exception.ExceptionRecord.ExceptionAddress != interrupt_address)
        return failure("unrecognized_interrupt_address", hr != S_OK ? hr : E_ACCESSDENIED);

    // DbgEng exposes no causal interrupt token and no atomic check-and-detach.
    // This requires the dedicated session's exclusive controller: another
    // client must not resume/change events during recovery. A same-site natural
    // DebugBreak racing this request is not distinguishable by these APIs.
    // Recheck immediately before detaching to reject observable event changes.
    StopEvent final_event;
    hr = control->GetExecutionStatus(&status);
    if (FAILED(hr) || status != DEBUG_STATUS_BREAK)
        return failure("stop_changed_before_detach", FAILED(hr) ? hr : E_ACCESSDENIED);
    hr = read_stop(control.value, final_event);
    if (hr != S_OK || !same_stop(event, final_event))
        return failure("event_changed_before_detach", hr != S_OK ? hr : E_ACCESSDENIED);
    // The dedicated observer owns only these IDs. Detach removes every remaining
    // debugger breakpoint and restores software bytes as part of the engine's
    // detach operation; no register writes or broad resume loop are performed.
    hr = control->Execute(DEBUG_OUTCTL_ALL_CLIENTS, ".detach /h", DEBUG_EXECUTE_ECHO | DEBUG_EXECUTE_NO_REPEAT);
    if (FAILED(hr)) return failure("detach_handled", hr);
    hr = control->GetExecutionStatus(&status);
    if (FAILED(hr) || status != DEBUG_STATUS_NO_DEBUGGEE)
        return failure("verify_detached", FAILED(hr) ? hr : E_UNEXPECTED);
    std::printf("MGS_STOP_DETACHED target_pid=%lu handled=1 interrupt_verified=1\n", observed_pid);
    return 0;
}
