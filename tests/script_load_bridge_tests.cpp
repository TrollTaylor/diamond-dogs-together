#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include "mgscoop/script_load_bridge.hpp"

using namespace mgscoop::script_bridge;
namespace {
std::atomic<unsigned> failures{0}, selectors{0}, observations{0}, replacements{0}, calls{0};
int context_value = 100;
const char original_bytes[] = "original";
const char replacement_bytes[] = "mod";
void Check(bool value) { if (!value) failures.fetch_add(1); }

__declspec(noinline) int Fixture(void* state, const char* bytes, std::size_t size, const char* name) {
    Check(state == &context_value);
    Check(GetLastError() == 0x1234);
    calls.fetch_add(1);
    if (name && std::strcmp(name, "throw") == 0) throw 42;
    if (name && std::strcmp(name, "replace") == 0) {
        Check(bytes == replacement_bytes && size == 3);
    } else Check(bytes == original_bytes && size == 8);
    SetLastError(0x5678);
    return *static_cast<int*>(state) + static_cast<int>(size) + static_cast<unsigned char>(bytes[0]);
}

bool SelectChunk(void* context, const char* bytes, std::size_t size, const char* name, Replacement& out) noexcept {
    Check(context == &context_value);
    Check(bytes == original_bytes && size == 8);
    selectors.fetch_add(1);
    if (name && std::strcmp(name,"selector_reentry") == 0) {
        SetLastError(0x1234);
        Check(Fixture(&context_value, original_bytes, 8, "inner") == 219);
    }
    SetLastError(0x9999); // Bridge must restore the error passed into the original.
    if (name && std::strcmp(name,"replace") == 0) { out = {replacement_bytes,3}; return true; }
    if (name && std::strcmp(name,"oversize") == 0) { out = {replacement_bytes,max_replacement_bytes+1}; return true; }
    if (name && std::strcmp(name,"null") == 0) { out = {nullptr,3}; return true; }
    return false;
}

void ObserveResult(void* context, int result, bool replaced) noexcept {
    Check(context == &context_value);
    Check(result == (replaced ? 212 : 219));
    const unsigned observation = observations.fetch_add(1);
    if (replaced) replacements.fetch_add(1);
    if (observation == 0) {
        // Reentrant observer calls must neither select nor observe recursively.
        SetLastError(0x1234);
        Check(Fixture(&context_value, original_bytes, 8, "inner") == 219);
    }
    SetLastError(0xaaaa);
}

void Invoke(const char* name, int expected) {
    SetLastError(0x1234);
    Check(Fixture(&context_value,original_bytes,8,name) == expected);
    Check(GetLastError() == 0x5678);
}
}

int main() {
    std::array<std::uint8_t,32> bytes{};
    std::memcpy(bytes.data(),reinterpret_cast<void*>(&Fixture),bytes.size());
    auto wrong = bytes;
    wrong[0] ^= 1;
    Policy policy{&context_value,SelectChunk,ObserveResult};
    Check(Install(&Fixture,wrong.data(),wrong.size(),policy) == InstallResult::bytes_differ);
    Check(std::memcmp(bytes.data(),reinterpret_cast<void*>(&Fixture),bytes.size()) == 0);
    Check(Install(&Fixture,bytes.data(),bytes.size(),policy) == InstallResult::installed);
    Check(Install(&Fixture,bytes.data(),bytes.size(),policy) == InstallResult::already_installed);
    Invoke("normal",219);
    Invoke("replace",212);
    Invoke("oversize",219);
    Invoke("null",219);
    Invoke("selector_reentry",219);
    const auto selectors_before = selectors.load();
    Check(selectors_before == 5); // Both forms of callback reentry were bypassed.
    try { Invoke("throw",0); Check(false); } catch (int code) { Check(code == 42); }
    std::vector<std::thread> workers;
    for (unsigned i=0;i<4;++i) workers.emplace_back([] { for (unsigned n=0;n<500;++n) Invoke("replace",212); });
    for (auto& worker:workers) worker.join();
    Check(selectors.load() == 2006);
    Check(observations.load() == 2005);
    Check(replacements.load() == 2001);
    Check(calls.load() == 2008);
    // All callers have now joined. This is the required removal boundary.
    Check(RemoveAfterCallersStopped());
    Check(std::memcmp(bytes.data(),reinterpret_cast<void*>(&Fixture),bytes.size()) == 0);
    Invoke("normal",219);
    Check(selectors.load() == 2006 && observations.load() == 2005);
    Check(Install(&Fixture,bytes.data(),bytes.size(),{}) == InstallResult::installed);
    Invoke("normal",219);
    Check(RemoveAfterCallersStopped());
    Check(std::memcmp(bytes.data(),reinterpret_cast<void*>(&Fixture),bytes.size()) == 0);
    std::cout << "script bridge failures=" << failures.load() << " fixture_calls=" << calls.load()
              << "; no game functions called\n";
    return failures.load() ? 1 : 0;
}
