#pragma once
#include "mgscoop/steam_runtime.hpp"

namespace mgscoop {
enum class SteamGetterResult { ReturnedNull, ReturnedNonNull, Threw };
struct SteamGetterObservationContext {
    std::uint32_t incoming_error;
    std::uint32_t (*read_error)() noexcept;
    void (*write_error)(std::uint32_t) noexcept;
    void (*sink)(void*, SteamGetterResult) noexcept;
    void* sink_context;
};

// Called only to forward a game-initiated getter. Original and error callbacks
// must be non-null. The sink receives no pointer and grants no runtime access.
// No locks or shared mutable state; reentrant/concurrent calls keep local state.
inline ISteamNetworking* forward_observed_networking_getter(
    SteamNetworkingGetter original, const SteamGetterObservationContext& context) {
    context.write_error(context.incoming_error);
    ISteamNetworking* result;
    try { result = original(); }
    catch (...) {
        const auto outgoing = context.read_error();
        if (context.sink) context.sink(context.sink_context, SteamGetterResult::Threw);
        context.write_error(outgoing);
        throw;
    }
    const auto outgoing = context.read_error();
    if (context.sink) context.sink(context.sink_context,
        result ? SteamGetterResult::ReturnedNonNull : SteamGetterResult::ReturnedNull);
    context.write_error(outgoing);
    return result;
}
} // namespace mgscoop
