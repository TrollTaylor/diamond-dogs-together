#include "mgscoop/steam_getter_observation.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace mgscoop;
thread_local std::uint32_t error_value = 0;
thread_local unsigned calls = 0;
thread_local unsigned depth = 0;
thread_local unsigned mode = 0;
thread_local int opaque_storage = 0;
std::atomic<unsigned> checks{0};
void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("getter forwarding check failed"); }
std::uint32_t read_error() noexcept { return error_value; }
void write_error(std::uint32_t value) noexcept { error_value = value; }
struct Failure { int token; };
struct Receipt {
    unsigned count = 0;
    SteamGetterResult result = SteamGetterResult::Threw;
};
void sink(void* context, SteamGetterResult result) noexcept {
    auto& receipt = *static_cast<Receipt*>(context);
    ++receipt.count;
    receipt.result = result;
    error_value = 0xdead;
}
ISteamNetworking* MGSCOOP_STEAM_CALL original() {
    ++calls;
    check(error_value == 0x1234);
    error_value = 0xabcd;
    if (mode == 2) throw Failure{73};
    if (mode == 3 && depth == 0) {
        ++depth;
        Receipt inner;
        const auto result = forward_observed_networking_getter(original,
            {0x1234,read_error,write_error,sink,&inner});
        --depth;
        check(inner.count == 1 && inner.result == SteamGetterResult::ReturnedNonNull);
        check(result == reinterpret_cast<ISteamNetworking*>(&opaque_storage));
        check(error_value == 0xabcd);
        error_value = 0xbeef;
    }
    return mode == 0 ? nullptr : reinterpret_cast<ISteamNetworking*>(&opaque_storage);
}
void scenarios() {
    for (mode = 0; mode < 4; ++mode) {
        calls = 0;
        error_value = 0;
        Receipt receipt;
        bool threw = false;
        try {
            const auto result = forward_observed_networking_getter(original,
                {0x1234,read_error,write_error,sink,&receipt});
            check(mode != 2);
            check(result == (mode == 0 ? nullptr : reinterpret_cast<ISteamNetworking*>(&opaque_storage)));
        } catch (const Failure& failure) {
            threw = true;
            check(mode == 2 && failure.token == 73);
        }
        check(threw == (mode == 2));
        check(calls == (mode == 3 ? 2u : 1u));
        check(receipt.count == 1);
        check(receipt.result == (mode == 0 ? SteamGetterResult::ReturnedNull :
            mode == 2 ? SteamGetterResult::Threw : SteamGetterResult::ReturnedNonNull));
        check(error_value == (mode == 3 ? 0xbeefu : 0xabcdu));
    }
    mode = 1;
    calls = 0;
    check(forward_observed_networking_getter(original,
        {0x1234,read_error,write_error,nullptr,nullptr}) == reinterpret_cast<ISteamNetworking*>(&opaque_storage));
    check(calls == 1 && error_value == 0xabcd);
}
}
int main() {
    try {
        scenarios();
        std::array<std::exception_ptr,4> failures{};
        std::vector<std::thread> workers;
        for (std::size_t n = 0; n < failures.size(); ++n) workers.emplace_back([&, n] {
            try { for (unsigned i = 0; i < 100; ++i) scenarios(); }
            catch (...) { failures[n] = std::current_exception(); }
        });
        for (auto& worker : workers) worker.join();
        for (auto failure : failures) if (failure) std::rethrow_exception(failure);
        std::cout << checks << " Steam getter forwarding checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
