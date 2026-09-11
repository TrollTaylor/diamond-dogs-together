#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include "mgscoop/import_table.hpp"
#include "mgscoop/win_import_patch.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
extern "C" __declspec(dllimport) void __cdecl MgsFixtureCallback();
extern "C" __declspec(dllimport) LONG __cdecl MgsFixtureCount();
extern "C" __declspec(dllimport) DWORD __cdecl MgsFixtureIncomingError();
static void(__cdecl* original)() = nullptr;
static volatile LONG observations = 0;
static void __cdecl replacement() {
    const auto incoming = GetLastError();
    SetLastError(incoming);
    original();
    const auto outgoing = GetLastError();
    InterlockedIncrement(&observations);
    SetLastError(999); // Deliberately disturb LastError during observation.
    SetLastError(outgoing);
}
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Failed at line " + std::to_string(__LINE__)); } while(false)
int main() {
    try {
        MODULEINFO info{};
        CHECK(K32GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &info, sizeof(info)));
        const auto slot = mgscoop::find_import_slot(static_cast<const std::uint8_t*>(info.lpBaseOfDll), info.SizeOfImage, "mgs_import_fixture.dll", "MgsFixtureCallback");
        CHECK(slot.status == mgscoop::ImportStatus::found);
        original = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(GetModuleHandleW(L"mgs_import_fixture.dll"), "MgsFixtureCallback"));
        CHECK(original);
        auto* const address = static_cast<unsigned char*>(info.lpBaseOfDll) + slot.offset;
        MEMORY_BASIC_INFORMATION before{}, after{};
        CHECK(VirtualQuery(address, &before, sizeof(before)));
        const auto changed = mgscoop::exchange_import(info.lpBaseOfDll, info.SizeOfImage, slot.offset, reinterpret_cast<void*>(original), reinterpret_cast<void*>(&replacement));
        CHECK(changed.status == mgscoop::PatchStatus::installed && changed.replacement_installed && changed.protection_restored);
        CHECK(VirtualQuery(address, &after, sizeof(after)) && after.Protect == before.Protect);
        SetLastError(567);
        MgsFixtureCallback();
        CHECK(GetLastError() == 0x1234u);
        CHECK(MgsFixtureCount() == 1 && observations == 1 && MgsFixtureIncomingError() == 567);
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) threads.emplace_back([] { for (int j = 0; j < 1000; ++j) MgsFixtureCallback(); });
        for (auto& t : threads) t.join();
        CHECK(MgsFixtureCount() == 4001 && observations == 4001);
        auto mismatch = mgscoop::exchange_import(info.lpBaseOfDll, info.SizeOfImage, slot.offset, reinterpret_cast<void*>(original), reinterpret_cast<void*>(&replacement));
        CHECK(mismatch.status == mgscoop::PatchStatus::unexpected_target && !mismatch.replacement_installed);
        const auto restored = mgscoop::exchange_import(info.lpBaseOfDll, info.SizeOfImage, slot.offset, reinterpret_cast<void*>(&replacement), reinterpret_cast<void*>(original));
        CHECK(restored.status == mgscoop::PatchStatus::installed && restored.protection_restored);
        MgsFixtureCallback(); CHECK(MgsFixtureCount() == 4002 && observations == 4001);
        CHECK(VirtualQuery(address, &after, sizeof(after)) && after.Protect == before.Protect);
        CHECK(mgscoop::exchange_import(info.lpBaseOfDll, info.SizeOfImage, slot.offset + 1, reinterpret_cast<void*>(original), reinterpret_cast<void*>(&replacement)).status == mgscoop::PatchStatus::invalid_argument);
        auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        CHECK(memory);
        const mgscoop::MappedImageSpan guarded{memory, 8192};
        DWORD protection = 0;
        CHECK(mgscoop::readable_image_range(&guarded, 0, 8192));
        CHECK(VirtualProtect(memory + 4096, 4096, PAGE_NOACCESS, &protection));
        CHECK(!mgscoop::readable_image_range(&guarded, 4090, 20));
        CHECK(VirtualProtect(memory, 4096, PAGE_READWRITE | PAGE_GUARD, &protection));
        CHECK(mgscoop::find_import_slot(memory, 8192, "x", "y", mgscoop::readable_image_range, &guarded).status == mgscoop::ImportStatus::invalid_image);
        CHECK(VirtualQuery(memory, &after, sizeof(after)) && (after.Protect & PAGE_GUARD));
        CHECK(VirtualProtect(memory, 4096, PAGE_READWRITE, &protection));
        *reinterpret_cast<void**>(memory) = reinterpret_cast<void*>(original);
        CHECK(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &protection));
        CHECK(mgscoop::exchange_import(memory, 8192, 0, reinterpret_cast<void*>(original), reinterpret_cast<void*>(&replacement)).status == mgscoop::PatchStatus::inaccessible);
        CHECK(VirtualQuery(memory, &after, sizeof(after)) && after.Protect == PAGE_EXECUTE_READ);
        CHECK(VirtualFree(memory, 0, MEM_RELEASE));
        std::cout << "Real import forwarding/restoration, original error states, 4001 calls across threads, and page protections passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
