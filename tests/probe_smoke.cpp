// Original proxy-forwarding smoke test. No game process or COM registration.
// API reference: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416756(v=vs.85)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#if !defined(_M_X64) && !defined(__x86_64__)
#error The proxy smoke test must be built for Windows x64.
#endif

namespace {
constexpr DWORD kPathCapacity = 32768;
using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using JoystickFn = const void*(WINAPI*)();

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path module_path(HMODULE module) {
    std::array<wchar_t, kPathCapacity> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), kPathCapacity);
    require(length != 0 && length < kPathCapacity, "Cannot resolve loaded module path");
    return std::filesystem::path(std::wstring(path.data(), length));
}

class Module {
public:
    Module(const std::filesystem::path& path, DWORD flags)
        : handle_(LoadLibraryExW(path.c_str(), nullptr, flags)) {
        if (!handle_) {
            throw std::runtime_error("LoadLibraryExW failed with error " + std::to_string(GetLastError()));
        }
    }
    ~Module() { if (handle_) FreeLibrary(handle_); }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    HMODULE get() const { return handle_; }
private:
    HMODULE handle_ = nullptr;
};

class InputObject {
public:
    ~InputObject() {
        if (value_) static_cast<IDirectInput8W*>(value_)->Release();
    }
    InputObject() = default;
    InputObject(const InputObject&) = delete;
    InputObject& operator=(const InputObject&) = delete;
    void** output() { return &value_; }
    bool present() const { return value_ != nullptr; }
private:
    void* value_ = nullptr;
};

template <typename Function>
Function function(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    require(address != nullptr, std::string("Missing export: ") + name);
    return reinterpret_cast<Function>(address);
}

void compare_create(CreateFn system_create, CreateFn proxy_create, DWORD version, bool expect_success) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    require(instance != nullptr, "Cannot obtain smoke-test executable instance");
    InputObject system_object;
    InputObject proxy_object;
    const HRESULT system_result = system_create(instance, version, IID_IDirectInput8W,
                                                system_object.output(), nullptr);
    const HRESULT proxy_result = proxy_create(instance, version, IID_IDirectInput8W,
                                              proxy_object.output(), nullptr);
    std::cout << "DirectInput8Create version=0x" << std::hex << version
              << " system=0x" << static_cast<unsigned long>(system_result)
              << " proxy=0x" << static_cast<unsigned long>(proxy_result) << std::dec << '\n';
    require(system_result == proxy_result, "Proxy changed DirectInput8Create HRESULT");
    require(system_object.present() == proxy_object.present(), "Proxy changed output-pointer success state");
    if (expect_success) {
        require(SUCCEEDED(system_result) && system_object.present() && proxy_object.present(),
                "DirectInput8 creation did not succeed for both system and proxy");
    } else {
        require(FAILED(system_result) && !system_object.present() && !proxy_object.present(),
                "Invalid DirectInput version was not rejected consistently");
    }
    // Both returned interfaces release before either module can unload.
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "Usage: probe_smoke.exe <absolute path to proxy dinput8.dll>");
        const auto executable_path = module_path(nullptr);
        require(_wcsicmp(executable_path.filename().c_str(), L"mgsvtpp.exe") != 0,
                "Run this test as probe_smoke.exe, never as the game's executable name");

        std::filesystem::path proxy_path(argv[1]);
        require(proxy_path.is_absolute(), "The proxy DLL path must be absolute");
        proxy_path = proxy_path.lexically_normal();
        proxy_path.make_preferred();
        require(std::filesystem::is_regular_file(proxy_path), "Proxy DLL does not exist at the supplied path");

        std::array<wchar_t, kPathCapacity> system_directory{};
        const UINT length = GetSystemDirectoryW(system_directory.data(), kPathCapacity);
        require(length != 0 && length < kPathCapacity, "Cannot resolve the native System32 directory");
        const auto system_path = std::filesystem::path(std::wstring(system_directory.data(), length)) / L"dinput8.dll";
        require(!std::filesystem::equivalent(proxy_path, system_path), "Proxy argument points to the real system DLL");

        // Load the real DLL first by full path. Never depend on a bare DLL-name lookup.
        const Module system(system_path, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(std::filesystem::equivalent(module_path(system.get()), system_path),
                "System DirectInput loaded from an unexpected location");
        const Module proxy(proxy_path, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(proxy.get() != system.get(), "The system and proxy handles identify the same loaded module");
        require(std::filesystem::equivalent(module_path(proxy.get()), proxy_path),
                "The explicitly requested proxy did not load from its supplied path");

        constexpr std::array<const char*, 6> export_names{
            "DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
            "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"
        };
        for (const auto* name : export_names) {
            require(GetProcAddress(system.get(), name) != nullptr, std::string("System missing export: ") + name);
            require(GetProcAddress(proxy.get(), name) != nullptr, std::string("Proxy missing export: ") + name);
        }
        std::cout << "PASS: six named exports exist in the distinct system and proxy modules.\n";

        const auto system_joystick = function<JoystickFn>(system.get(), "GetdfDIJoystick");
        const auto proxy_joystick = function<JoystickFn>(proxy.get(), "GetdfDIJoystick");
        const void* real_format = system_joystick();
        require(real_format != nullptr && proxy_joystick() == real_format,
                "Proxy did not return the system joystick-format pointer");
        std::cout << "PASS: GetdfDIJoystick forwards the exact system pointer.\n";

        const auto system_create = function<CreateFn>(system.get(), "DirectInput8Create");
        const auto proxy_create = function<CreateFn>(proxy.get(), "DirectInput8Create");
        compare_create(system_create, proxy_create, DIRECTINPUT_VERSION, true);
        compare_create(system_create, proxy_create, 0, false);
        std::cout << "PASS: valid and invalid DirectInput8Create calls match; interfaces released.\n"
                  << "SCOPE: standalone proxy forwarding only; in-game loading and gameplay remain unverified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
