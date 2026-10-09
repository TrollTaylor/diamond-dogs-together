// Original MGS Co-op SteamTransport: link v2 datagrams over Steam networking.
#include "mgscoop/steam_transport.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>

#include "mgscoop/link_protocol.hpp"
#include "mgscoop/steam_join_code.hpp"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace mgscoop::lab {
namespace {
using steam_net::Identity;
using steam_net::ISteamNetworkingMessages002Abi;
using steam_net::ISteamNetworkingSockets012Abi;

std::uint64_t NowMs() noexcept {
    LARGE_INTEGER frequency{}, counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart <= 0) return GetTickCount64();
    const auto f = static_cast<std::uint64_t>(frequency.QuadPart);
    const auto c = static_cast<std::uint64_t>(counter.QuadPart);
    return (c / f) * 1000 + ((c % f) * 1000) / f;
}

// ---- C-style SEH thunks: no object with a destructor in these frames. A
// fault sets *fault; the caller latches and makes no further Steam call.
std::int32_t TrySendUser(ISteamNetworkingMessages002Abi* m, const Identity* id, const void* data, std::uint32_t size,
                         std::int32_t flags, std::int32_t channel, bool* fault) {
    __try {
        return m->SendMessageToUser(*id, data, size, flags, channel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return 0;
    }
}
std::int32_t TryReceiveChannel(ISteamNetworkingMessages002Abi* m, std::int32_t channel, void** out, std::int32_t max, bool* fault) {
    __try {
        return m->ReceiveMessagesOnChannel(channel, out, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return 0;
    }
}
bool TryAccept(ISteamNetworkingMessages002Abi* m, const Identity* id, bool* fault) {
    __try {
        return m->AcceptSessionWithUser(*id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return false;
    }
}
bool TryCloseSession(ISteamNetworkingMessages002Abi* m, const Identity* id, bool* fault) {
    __try {
        return m->CloseSessionWithUser(*id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return false;
    }
}
std::int32_t TryInfo(ISteamNetworkingMessages002Abi* m, const Identity* id, void* info, bool* fault) {
    __try {
        return m->GetSessionConnectionInfo(*id, info, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return 0;
    }
}
std::int32_t TrySendConnection(ISteamNetworkingSockets012Abi* s, std::uint32_t conn, const void* data, std::uint32_t size,
                               std::int32_t flags, bool* fault) {
    __try {
        return s->SendMessageToConnection(conn, data, size, flags, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return 0;
    }
}
std::int32_t TryReceiveConnection(ISteamNetworkingSockets012Abi* s, std::uint32_t conn, void** out, std::int32_t max, bool* fault) {
    __try {
        return s->ReceiveMessagesOnConnection(conn, out, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return 0;
    }
}
// The public prefix and a copy of the identity (the data stays in place until Release).
struct MessageCopy {
    steam_net::MessageView view;
    Identity identity;
};
bool TryReadMessage(void* message, MessageCopy* out, bool* fault) {
    __try {
        out->view = steam_net::ReadMessage(message);
        const auto* from = reinterpret_cast<const std::uint8_t*>(out->view.identity);
        auto* to = reinterpret_cast<std::uint8_t*>(&out->identity);
        for (std::size_t i = 0; i < sizeof(Identity); ++i) to[i] = from[i];
        out->view.identity = &out->identity;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return false;
    }
}
void TryRelease(void* message, bool* fault) {
    __try {
        const steam_net::MessageReleaseFn release = steam_net::MessageRelease(message);
        if (release) release(message);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
    }
}

bool TryCheckPing(steam_net::ISteamNetworkingUtils004Abi* u, float max_age, bool* fault) {
    __try {
        return u->CheckPingDataUpToDate(max_age);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return false;
    }
}

SRWLOCK* AsLock(void*& storage) noexcept {
    static_assert(sizeof(SRWLOCK) == sizeof(void*), "SRWLOCK is pointer-sized");
    return reinterpret_cast<SRWLOCK*>(&storage);
}
std::uint64_t Load64(volatile long long& v) noexcept { return static_cast<std::uint64_t>(InterlockedCompareExchange64(&v, 0, 0)); }
}  // namespace

// ---------------------------------------------------------------- send ring
bool SteamSendRing::Push(const Endpoint& to, const WireChunk* chunks, unsigned count, std::size_t total) noexcept {
    const auto tail = static_cast<unsigned long>(tail_);
    const auto head = static_cast<unsigned long>(InterlockedCompareExchange(&head_, 0, 0));
    if (tail - head >= kSlots) return false;
    if (total == 0 || total > steam_net::kMaxMessage) return false;
    Slot& slot = slots_[tail % kSlots];
    std::size_t at = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto* bytes = static_cast<const std::uint8_t*>(chunks[i].data);
        for (std::size_t k = 0; k < chunks[i].size; ++k) slot.bytes[at++] = bytes[k];
    }
    slot.to = to;
    slot.size = static_cast<std::uint32_t>(total);
    InterlockedExchange(&tail_, static_cast<long>(tail + 1));  // full barrier: the slot is published
    return true;
}

bool SteamSendRing::Peek(Endpoint& to, const std::uint8_t*& data, std::size_t& size) const noexcept {
    const auto head = static_cast<unsigned long>(head_);
    const auto tail = static_cast<unsigned long>(InterlockedCompareExchange(const_cast<volatile long*>(&tail_), 0, 0));
    if (head == tail) return false;
    const Slot& slot = slots_[head % kSlots];
    to = slot.to;
    data = slot.bytes;
    size = slot.size;
    return true;
}

void SteamSendRing::Pop() noexcept { InterlockedIncrement(&head_); }

unsigned SteamSendRing::Depth() const noexcept {
    const auto head = static_cast<unsigned long>(InterlockedCompareExchange(const_cast<volatile long*>(&head_), 0, 0));
    const auto tail = static_cast<unsigned long>(InterlockedCompareExchange(const_cast<volatile long*>(&tail_), 0, 0));
    return static_cast<unsigned>(tail - head);
}

void SteamSendRing::Clear() noexcept { InterlockedExchange(&head_, InterlockedCompareExchange(&tail_, 0, 0)); }

// ------------------------------------------------------------ accept policy
const char* AcceptViaName(AcceptVia via) noexcept {
    switch (via) {
    case AcceptVia::poll: return "poll";
    case AcceptVia::beacon: return "beacon";
    default: return "callback";
    }
}

const char* SteamAcceptPolicy::DecisionName(Decision d) noexcept {
    switch (d) {
    case Decision::accept: return "accept";
    case Decision::not_accepting: return "not_accepting";
    case Decision::self: return "self";
    case Decision::denied: return "denied";
    case Decision::rate: return "rate";
    case Decision::cap: return "cap";
    default: return "known_peer";
    }
}

void SteamAcceptPolicy::Reset(std::uint64_t self) noexcept {
    *this = SteamAcceptPolicy{};
    self_ = self;
}

SteamAcceptPolicy::Entry* SteamAcceptPolicy::Find(std::uint64_t id) noexcept {
    for (auto& e : entries_) if (e.used && e.id == id) return &e;
    return nullptr;
}
const SteamAcceptPolicy::Entry* SteamAcceptPolicy::Find(std::uint64_t id) const noexcept {
    for (const auto& e : entries_) if (e.used && e.id == id) return &e;
    return nullptr;
}

unsigned SteamAcceptPolicy::non_peer() const noexcept {
    unsigned n = 0;
    for (const auto& e : entries_) n += e.used ? 1u : 0u;
    return n;
}

bool SteamAcceptPolicy::Denied(std::uint64_t id, std::uint64_t now_ms) const noexcept {
    for (const auto& d : deny_) if (d.id == id && now_ms < d.until_ms) return true;
    return false;
}

bool SteamAcceptPolicy::Accepted(std::uint64_t id) const noexcept { return Find(id) != nullptr || (id && (id == peer_ || id == last_peer_)); }

void SteamAcceptPolicy::DenyFor(std::uint64_t id, std::uint64_t now_ms) noexcept {
    for (auto& d : deny_)
        if (d.id == id) { d.until_ms = now_ms + kDenyMs; return; }
    deny_[deny_next_] = Deny{id, now_ms + kDenyMs};
    deny_next_ = (deny_next_ + 1) % kDenyEntries;
}

SteamAcceptPolicy::Decision SteamAcceptPolicy::OnRequest(std::uint64_t id, bool accepting, std::uint64_t now_ms) noexcept {
    if (!id || id == self_) return Decision::self;
    if (id == peer_ || id == last_peer_) return Decision::known_peer;
    if (Denied(id, now_ms)) return Decision::denied;
    if (!accepting) return Decision::not_accepting;
    if (Find(id)) return Decision::accept;  // a repeated request of an accepted identity
    if (now_ms - rate_window_ms_ >= 1000 || now_ms < rate_window_ms_) {
        rate_window_ms_ = now_ms;
        rate_count_ = 0;
    }
    if (rate_count_ >= kRatePerSec) return Decision::rate;
    if (non_peer() >= kMaxNonPeer) return Decision::cap;
    ++rate_count_;
    return Decision::accept;
}

SteamAcceptPolicy::Decision SteamAcceptPolicy::CheckKnown(std::uint64_t id, bool accepting, std::uint64_t now_ms) const noexcept {
    if (!id || id == self_) return Decision::self;
    if (id == peer_ || id == last_peer_) return Decision::known_peer;
    if (Denied(id, now_ms)) return Decision::denied;
    if (!accepting) return Decision::not_accepting;
    if (Find(id)) return Decision::accept;
    if (non_peer() >= kMaxNonPeer) return Decision::cap;
    return Decision::accept;
}

void SteamAcceptPolicy::OnAccepted(std::uint64_t id, std::uint64_t now_ms) noexcept {
    if (!id || id == self_ || id == peer_ || id == last_peer_ || Find(id)) return;
    for (auto& e : entries_)
        if (!e.used) {
            e = Entry{true, id, now_ms, now_ms, false};
            return;
        }
}

void SteamAcceptPolicy::OnMessage(std::uint64_t id, bool link, std::uint64_t now_ms) noexcept {
    if (id && id == peer_) peer_rx_ms_ = now_ms;
    if (id && id == last_peer_) last_peer_rx_ms_ = now_ms;
    if (Entry* e = Find(id)) {
        e->last_rx_ms = now_ms;
        e->link = e->link || link;
    }
}

void SteamAcceptPolicy::SetPeer(std::uint64_t id, std::uint64_t now_ms) noexcept {
    if (!id || id == self_) return;
    if (peer_ && peer_ != id) {
        last_peer_ = peer_;
        last_peer_rx_ms_ = peer_rx_ms_;
    }
    if (last_peer_ == id) last_peer_ = 0;
    peer_ = id;
    peer_rx_ms_ = now_ms;
    if (Entry* e = Find(id)) *e = Entry{};
}

void SteamAcceptPolicy::AddKnown(std::uint64_t id, KnownKind kind, std::uint64_t now_ms) noexcept {
    if (!id || id == self_) return;
    const std::uint64_t keep = kind == KnownKind::invited ? kInvitedKeepMs : kind == KnownKind::beacon ? kBeaconKeepMs : kFormerPeerIdleMs;
    for (auto& k : known_)
        if (k.id == id) {
            k.expires_ms = now_ms + keep;
            if (kind == KnownKind::beacon || k.kind != KnownKind::beacon) k.kind = kind;
            return;
        }
    unsigned invited = 0;
    Known* oldest_invited = nullptr;
    Known* free_slot = nullptr;
    for (auto& k : known_) {
        if (!k.id || now_ms >= k.expires_ms) {
            if (!free_slot) free_slot = &k;
            continue;
        }
        if (k.kind == KnownKind::invited) {
            ++invited;
            if (!oldest_invited || k.expires_ms < oldest_invited->expires_ms) oldest_invited = &k;
        }
    }
    Known* slot = free_slot;
    if (kind == KnownKind::invited && invited >= kMaxInvited) slot = oldest_invited;
    if (!slot) {
        for (auto& k : known_)
            if (!slot || k.expires_ms < slot->expires_ms) slot = &k;
    }
    *slot = Known{id, now_ms + keep, kind};
}

unsigned SteamAcceptPolicy::DuePolls(std::uint64_t now_ms, bool accepting, std::uint64_t* out, KnownKind* kinds, unsigned max) noexcept {
    if (now_ms < next_poll_ms_ || !out || !max) return 0;
    next_poll_ms_ = now_ms + kPollIntervalMs;
    unsigned n = 0;
    const auto add = [&](std::uint64_t id, KnownKind kind) noexcept {
        if (!id || id == self_ || n >= max) return;
        for (unsigned i = 0; i < n; ++i) if (out[i] == id) return;
        if (Denied(id, now_ms)) return;
        out[n] = id;
        if (kinds) kinds[n] = kind;
        ++n;
    };
    add(peer_, KnownKind::guest);
    add(last_peer_, KnownKind::guest);
    for (auto& k : known_) {
        if (!k.id) continue;
        if (now_ms >= k.expires_ms) { k = Known{}; continue; }
        if (accepting) add(k.id, k.kind);
    }
    return n;
}

bool SteamAcceptPolicy::NextExpired(std::uint64_t now_ms, std::uint64_t& id) noexcept {
    id = 0;
    for (auto& e : entries_) {
        if (!e.used) continue;
        if (!e.link && now_ms - e.accepted_ms >= kNoHeaderCloseMs) {
            id = e.id;
            DenyFor(id, now_ms);
            e = Entry{};
            return true;
        }
        if (e.link && now_ms - e.last_rx_ms >= kFormerPeerIdleMs) {
            id = e.id;
            e = Entry{};
            return true;
        }
    }
    if (last_peer_ && now_ms - last_peer_rx_ms_ >= kFormerPeerIdleMs) {
        id = last_peer_;
        last_peer_ = 0;
        return true;
    }
    return false;
}

// ------------------------------------------------------------- guest filter
bool SteamGuestFilter::OnForeign(std::uint64_t id, std::uint64_t now_ms) noexcept {
    Entry* entry = nullptr;
    for (auto& e : entries_) if (e.id == id) { entry = &e; break; }
    if (!entry) {
        entry = &entries_[next_];
        next_ = (next_ + 1) % kEntries;
        *entry = Entry{id, now_ms, 0};
    }
    if (now_ms - entry->first_ms >= kWindowMs) {
        entry->first_ms = now_ms;
        entry->count = 0;
    }
    if (++entry->count < kCloseAfter) return false;
    entry->count = 0;
    entry->first_ms = now_ms;
    return true;
}

void SteamGuestFilter::Reset() noexcept { *this = SteamGuestFilter{}; }

// ---------------------------------------------------------------- transport
bool SteamTransport::start(const SteamTransportConfig& config, const SteamSink& sink) noexcept {
    if (running_ || thread_) { error_ = TransportError::bad_config; return false; }
    if (!config.resolve || !sink.on_datagram || (config.leg == SteamLeg::connection && !config.conn)) {
        error_ = TransportError::bad_config;
        return false;
    }
    config_ = config;
    if (!config_.idle_interval_ms) config_.idle_interval_ms = 20;
    sink_ = sink;
    api_ = SteamNetApi{};
    ring_.Clear();
    host_id_ = config.role == SteamRole::guest ? config.host_id : 0;
    peer_id_ = static_cast<long long>(host_id_);
    accepting_ = config.accepting ? 1 : 0;
    stop_ = shutdown_ = faulted_ = ready_ = parked_ = 0;
    route_ = static_cast<long>(steam_net::Route::none);
    steam_state_ = 0;
    known_count_ = 0;
    for (auto& id : live_) id = 0;
    live_next_ = 0;
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) { error_ = TransportError::thread; return false; }
    timer_ = config_.high_res_timer ? CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)
                                    : nullptr;
    running_ = 1;
    thread_ = CreateThread(nullptr, 0, &SteamTransport::thread_entry, this, 0, nullptr);
    if (!thread_) {
        running_ = 0;
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
        if (timer_) { CloseHandle(timer_); timer_ = nullptr; }
        error_ = TransportError::thread;
        return false;
    }
    error_ = TransportError::none;
    return true;
}

bool SteamTransport::transmit_to(Endpoint to, const WireChunk* chunks, unsigned count, bool) noexcept {
    if (!running_ || stop_ || faulted_ || !chunks || !count || count > 4) return false;
    std::size_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!chunks[i].data && chunks[i].size) return false;
        total += chunks[i].size;
    }
    if (total == 0 || total > steam_net::kMaxMessage) {
        InterlockedIncrement64(&too_large_);
        return false;
    }
    if (config_.leg == SteamLeg::messages && (to.port != steam_net::kSteamPortTag || !to.address)) return false;
    if (shutdown_ && parked_) return false;
    if (!ring_.Push(to, chunks, count, total)) InterlockedIncrement64(&ring_full_);  // lost; link v2 recovers
    return true;
}

bool SteamTransport::transmit(const WireChunk* chunks, unsigned count, bool droppable) noexcept {
    const std::uint64_t peer = Load64(peer_id_);
    const Endpoint to = config_.leg == SteamLeg::connection ? kConnectionEndpoint : SteamEndpoint(peer);
    return transmit_to(to, chunks, count, droppable);
}

void SteamTransport::stop() noexcept {
    if (!thread_) return;
    InterlockedExchange(&stop_, 1);
    if (stop_event_) SetEvent(stop_event_);
    WaitForSingleObject(thread_, INFINITE);
    CloseHandle(thread_);
    thread_ = nullptr;
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
    if (timer_) { CloseHandle(timer_); timer_ = nullptr; }
    ring_.Clear();
    running_ = 0;
    ready_ = 0;
}

void SteamTransport::request_shutdown() noexcept {
    InterlockedExchange(&shutdown_, 1);
    // No wake-up call: the thread checks the flag at least every few ms.
}

TransportStats SteamTransport::stats() const noexcept {
    auto& self = const_cast<SteamTransport&>(*this);
    TransportStats s{};
    s.tx = Load64(self.tx_);
    s.rx = Load64(self.rx_);
    s.dropped = Load64(self.ring_full_) + Load64(self.send_fail_) + Load64(self.send_invalid_);
    s.rejected = Load64(self.foreign_) + Load64(self.bad_identity_) + Load64(self.denied_) + Load64(self.bad_size_);
    s.duplicated = 0;
    return s;
}

SteamTransportStats SteamTransport::steam_stats() const noexcept {
    auto& self = const_cast<SteamTransport&>(*this);
    SteamTransportStats s{};
    s.tx = Load64(self.tx_);
    s.rx = Load64(self.rx_);
    s.ring_full = Load64(self.ring_full_);
    s.send_fail = Load64(self.send_fail_);
    s.send_invalid = Load64(self.send_invalid_);
    s.too_large = Load64(self.too_large_);
    s.foreign = Load64(self.foreign_);
    s.bad_identity = Load64(self.bad_identity_);
    s.bad_size = Load64(self.bad_size_);
    s.denied = Load64(self.denied_);
    s.accepted = Load64(self.accepted_);
    s.accept_refused = Load64(self.accept_refused_);
    s.closed_idle = Load64(self.closed_idle_);
    s.closed_foreign = Load64(self.closed_foreign_);
    s.release = Load64(self.release_);
    s.received = Load64(self.received_);
    s.faults = Load64(self.faults_);
    s.non_peer = static_cast<std::uint32_t>(self.non_peer_);
    s.ring_depth = ring_.Depth();
    return s;
}

void SteamTransport::set_host(std::uint64_t id) noexcept {
    host_id_ = id;
    InterlockedExchange64(&peer_id_, static_cast<long long>(id));
    filter_.Reset();
}

void SteamTransport::set_peer(std::uint64_t id) noexcept {
    if (config_.role == SteamRole::guest || !steam::is_individual_steamid(id)) return;
    AcquireSRWLockExclusive(AsLock(known_lock_));
    pending_peer_ = id;
    ReleaseSRWLockExclusive(AsLock(known_lock_));
    InterlockedExchange64(&peer_id_, static_cast<long long>(id));
}

void SteamTransport::add_known(std::uint64_t id, KnownKind kind) noexcept {
    if (!steam::is_individual_steamid(id)) return;
    AcquireSRWLockExclusive(AsLock(known_lock_));
    bool merged = false;
    for (unsigned i = 0; i < known_count_; ++i)
        if (known_pending_[i].id == id) { known_pending_[i].kind = kind; merged = true; }
    if (!merged && known_count_ < sizeof(known_pending_) / sizeof(known_pending_[0])) known_pending_[known_count_++] = PendingKnown{id, kind};
    ReleaseSRWLockExclusive(AsLock(known_lock_));
}

unsigned long __stdcall SteamTransport::thread_entry(void* self) noexcept {
    static_cast<SteamTransport*>(self)->run();
    return 0;
}

void SteamTransport::log_line(const char* text) noexcept {
    if (config_.log) config_.log(text);
}

void SteamTransport::fault(const char* where) noexcept {
    if (InterlockedExchange(&faulted_, 1)) return;
    InterlockedIncrement64(&faults_);
    if (api_.on_fault) api_.on_fault(api_.fault_ctx, where);
    if (sink_.on_failed) sink_.on_failed(sink_.ctx, "fault");
}

void SteamTransport::park() noexcept {
    InterlockedExchange(&parked_, 1);
    // No further Steam call from this thread. A stop() (tests, self-test)
    // ends the wait; at game exit nothing signals it.
    WaitForSingleObject(stop_event_, INFINITE);
}

bool SteamTransport::resolve() noexcept {
    const std::uint64_t deadline = NowMs() + config_.resolve_timeout_ms;
    for (;;) {
        if (stop_ || shutdown_) return false;
        SteamNetApi api{};
        const char* reason = nullptr;
        int r = config_.resolve(config_.resolve_ctx, api, reason);
        if (r > 0) {
            const bool usable = config_.leg == SteamLeg::messages ? api.messages != nullptr : api.sockets != nullptr;
            if (usable) {
                api_ = api;
                return true;
            }
            r = -1;
            reason = "iface_missing";
        }
        if (r < 0 || NowMs() >= deadline) {
            if (sink_.on_failed) sink_.on_failed(sink_.ctx, reason ? reason : "acquire_timeout");
            return false;
        }
        WaitForSingleObject(stop_event_, kResolvePollMs);
    }
}

bool SteamTransport::accept_session(std::uint64_t id) noexcept {
    Identity identity{};
    steam_net::MakeSteamIdentity(id, identity);
    bool f = false;
    const bool ok = TryAccept(api_.messages, &identity, &f);
    if (f) { fault("accept"); return false; }
    return ok;
}

bool SteamTransport::mark_live(std::uint64_t id, bool live) noexcept {
    if (!id) return false;
    for (auto& entry : live_)
        if (entry == id) {
            if (!live) entry = 0;
            return true;
        }
    if (!live) return false;
    for (auto& entry : live_)
        if (!entry) {
            entry = id;
            return false;
        }
    live_[live_next_] = id;
    live_next_ = (live_next_ + 1) % kLiveEntries;
    return false;
}

bool SteamTransport::close_session(std::uint64_t id) noexcept {
    Identity identity{};
    steam_net::MakeSteamIdentity(id, identity);
    bool f = false;
    const bool ok = TryCloseSession(api_.messages, &identity, &f);
    if (f) { fault("close"); return false; }
    return ok;
}

void SteamTransport::drain_ring() noexcept {
    Endpoint to{};
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    while (!faulted_ && ring_.Peek(to, data, size)) {
        bool f = false;
        std::int32_t result = 0;
        if (config_.leg == SteamLeg::messages) {
            Identity identity{};
            steam_net::MakeSteamIdentity(SteamIdOf(to), identity);
            result = TrySendUser(api_.messages, &identity, data, static_cast<std::uint32_t>(size), steam_net::kMsgFlags,
                                 steam_net::kLinkChannel, &f);
        } else {
            result = TrySendConnection(api_.sockets, config_.conn, data, static_cast<std::uint32_t>(size), steam_net::kConnFlags, &f);
        }
        ring_.Pop();
        if (f) { fault("send"); return; }
        if (result == steam_net::kResultOk) {
            InterlockedIncrement64(&tx_);
            continue;
        }
        if (result == steam_net::kResultInvalidParam) InterlockedIncrement64(&send_invalid_);
        else InterlockedIncrement64(&send_fail_);
        const std::uint64_t now = NowMs();
        if (!send_fail_log_ms_ || now - send_fail_log_ms_ >= kSendFailLogMs) {
            send_fail_log_ms_ = now;
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=send_fail; result=%d; count=%llu\r\n", result,
                                           static_cast<unsigned long long>(Load64(send_fail_) + Load64(send_invalid_)))))
                log_line(line);
        }
    }
}

void SteamTransport::deliver(void* message) noexcept {
    InterlockedIncrement64(&received_);
    bool f = false;
    MessageCopy copy{};
    if (!TryReadMessage(message, &copy, &f) || f) { fault("message"); return; }
    const steam_net::MessageView& v = copy.view;
    bool ok = v.data != nullptr && v.size >= 1 && static_cast<std::size_t>(v.size) <= steam_net::kMaxMessage;
    if (!ok) InterlockedIncrement64(&bad_size_);
    Endpoint from{};
    if (ok && config_.leg == SteamLeg::connection) {
        ok = steam_net::IdentityIsIp(*v.identity) && v.conn == config_.conn;
        if (!ok) InterlockedIncrement64(&bad_identity_);
        from = kConnectionEndpoint;
    } else if (ok) {
        std::uint64_t id = 0;
        ok = steam_net::IdentitySteamId(*v.identity, id) && id != config_.self_id;
        if (!ok) {
            InterlockedIncrement64(&bad_identity_);
        } else if (config_.role == SteamRole::guest) {
            if (!host_id_ || id != host_id_) {
                ok = false;
                InterlockedIncrement64(&foreign_);
                if (filter_.OnForeign(id, NowMs())) {
                    // Release first: no message of this identity is held while closing.
                    TryRelease(message, &f);
                    InterlockedIncrement64(&release_);
                    if (f) { fault("release"); return; }
                    if (close_session(id)) InterlockedIncrement64(&closed_foreign_);
                    return;
                }
            }
        } else if (policy_.Denied(id, NowMs())) {
            ok = false;
            InterlockedIncrement64(&denied_);
        } else {
            link::Header header{};
            const bool is_link = link::DecodeHeader(v.data, static_cast<std::size_t>(v.size), header) == link::DecodeResult::ok;
            policy_.OnMessage(id, is_link, NowMs());
        }
        from = SteamEndpoint(id);
    }
    if (ok) {
        InterlockedIncrement64(&rx_);
        sink_.on_datagram(sink_.ctx, v.data, static_cast<std::size_t>(v.size), from);
    }
    TryRelease(message, &f);  // exactly once, delivered or not
    InterlockedIncrement64(&release_);
    if (f) fault("release");
}

unsigned SteamTransport::receive() noexcept {
    unsigned total = 0;
    for (unsigned pass = 0; pass < kReceivePasses && !faulted_; ++pass) {
        void* messages[kReceiveBatch]{};
        bool f = false;
        std::int32_t n = config_.leg == SteamLeg::messages
                             ? TryReceiveChannel(api_.messages, steam_net::kLinkChannel, messages, kReceiveBatch, &f)
                             : TryReceiveConnection(api_.sockets, config_.conn, messages, kReceiveBatch, &f);
        if (f) { fault("receive"); return total; }
        if (n <= 0) break;
        if (n > kReceiveBatch) n = kReceiveBatch;
        for (std::int32_t i = 0; i < n && !faulted_; ++i) deliver(messages[i]);
        total += static_cast<unsigned>(n);
        if (n < kReceiveBatch) break;
    }
    return total;
}

void SteamTransport::accept_work(std::uint64_t now) noexcept {
    // Known ids posted from other threads (invites, beacon hits).
    PendingKnown pending[16]{};
    unsigned count = 0;
    AcquireSRWLockExclusive(AsLock(known_lock_));
    for (unsigned i = 0; i < known_count_; ++i) pending[i] = known_pending_[i];
    count = known_count_;
    known_count_ = 0;
    const std::uint64_t new_peer = pending_peer_;
    pending_peer_ = 0;
    ReleaseSRWLockExclusive(AsLock(known_lock_));
    if (new_peer) {
        policy_.SetPeer(new_peer, now);
        logged_state_ = -1;  // log the new peer's route/state
    }
    for (unsigned i = 0; i < count; ++i) policy_.AddKnown(pending[i].id, pending[i].kind, now);

    const bool accepting = accepting_ != 0;
    char line[128]{}, peer[24]{};
    // 1. The 1251 queue (via=callback).
    std::uint64_t id = 0;
    for (unsigned budget = 0; config_.pop_request && budget < 16 && !faulted_ && config_.pop_request(id); ++budget) {
        const auto decision = policy_.OnRequest(id, accepting, now);
        if (decision != SteamAcceptPolicy::Decision::accept && decision != SteamAcceptPolicy::Decision::known_peer) {
            InterlockedIncrement64(&accept_refused_);
            continue;
        }
        if (!accept_session(id)) continue;
        policy_.OnAccepted(id, now);
        mark_live(id, true);  // a 1251 is a pending session: always a real accept
        InterlockedIncrement64(&accepted_);
        steam_code::FormatPeer(id, peer);
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=accept; peer=%s; via=callback\r\n", peer))) log_line(line);
    }
    // 2. Poll-accept known ids at 4 Hz (false = no session). These are ids the
    // host chose (peer, last peer, invited friends, beacon hits): no rate token.
    std::uint64_t ids[SteamAcceptPolicy::kKnownEntries + 2]{};
    KnownKind kinds[SteamAcceptPolicy::kKnownEntries + 2]{};
    const unsigned n = policy_.DuePolls(now, accepting, ids, kinds, SteamAcceptPolicy::kKnownEntries + 2);
    for (unsigned i = 0; i < n && !faulted_; ++i) {
        const auto decision = policy_.CheckKnown(ids[i], accepting, now);
        if (decision != SteamAcceptPolicy::Decision::accept && decision != SteamAcceptPolicy::Decision::known_peer) continue;
        const bool ok = accept_session(ids[i]);
        if (faulted_) break;
        // True for an established session too: only a new one is an accept.
        const bool was_live = mark_live(ids[i], ok);
        if (!ok) continue;
        if (decision != SteamAcceptPolicy::Decision::known_peer) policy_.OnAccepted(ids[i], now);
        if (was_live) continue;
        InterlockedIncrement64(&accepted_);
        steam_code::FormatPeer(ids[i], peer);
        const AcceptVia via = kinds[i] == KnownKind::beacon ? AcceptVia::beacon : AcceptVia::poll;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=accept; peer=%s; via=%s\r\n", peer, AcceptViaName(via))))
            log_line(line);
    }
}

void SteamTransport::hygiene(std::uint64_t now) noexcept {
    std::uint64_t id = 0;
    for (unsigned i = 0; i < 4 && !faulted_ && policy_.NextExpired(now, id); ++i) {
        close_session(id);
        mark_live(id, false);
        InterlockedIncrement64(&closed_idle_);
    }
}

void SteamTransport::route_poll(std::uint64_t now) noexcept {
    if (now < next_route_ms_) return;
    next_route_ms_ = now + kRouteMs;
    const std::uint64_t peer = Load64(peer_id_);
    if (!peer) {
        InterlockedExchange(&route_, static_cast<long>(steam_net::Route::none));
        return;
    }
    Identity identity{};
    steam_net::MakeSteamIdentity(peer, identity);
    for (auto& b : info_) b = 0;
    bool f = false;
    const std::int32_t state = TryInfo(api_.messages, &identity, info_, &f);
    if (f) { fault("info"); return; }
    const steam_net::RouteInfo route = steam_net::DecodeConnectionInfo(state, info_, peer);
    InterlockedExchange(&route_, static_cast<long>(route.route));
    InterlockedExchange(&steam_state_, state);
    if (state != logged_state_ || static_cast<std::uint8_t>(route.route) != logged_route_) {
        logged_state_ = state;
        logged_route_ = static_cast<std::uint8_t>(route.route);
        char line[160]{}, text[24]{};
        steam_code::FormatPeer(peer, text);
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=session; peer=%s; state=%s; route=%s\r\n", text,
                                       steam_net::ConnectionStateName(state), steam_net::RouteName(route.route))))
            log_line(line);
    }
}

void SteamTransport::log_stats(std::uint64_t now, bool force) noexcept {
    if (!force && now < next_stats_ms_) return;
    next_stats_ms_ = now + kStatsMs;
    const SteamTransportStats s = steam_stats();
    const std::uint64_t current[8] = {s.tx, s.rx, s.ring_full, s.send_fail + s.send_invalid, s.foreign, s.bad_identity, s.accepted,
                                      s.closed_idle};
    bool changed = !stats_logged_;
    for (unsigned i = 0; i < 8; ++i) changed = changed || current[i] != logged_[i];
    if (!changed && !force) return;
    stats_logged_ = true;
    for (unsigned i = 0; i < 8; ++i) logged_[i] = current[i];
    char line[320]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "steam=stats; tx=%llu; rx=%llu; ring_full=%llu; send_fail=%llu; foreign=%llu; bad_identity=%llu; accepted=%llu; "
            "closed_idle=%llu; release=%llu; received=%llu; denied=%llu\r\n",
            static_cast<unsigned long long>(s.tx), static_cast<unsigned long long>(s.rx), static_cast<unsigned long long>(s.ring_full),
            static_cast<unsigned long long>(s.send_fail + s.send_invalid), static_cast<unsigned long long>(s.foreign),
            static_cast<unsigned long long>(s.bad_identity), static_cast<unsigned long long>(s.accepted),
            static_cast<unsigned long long>(s.closed_idle), static_cast<unsigned long long>(s.release),
            static_cast<unsigned long long>(s.received), static_cast<unsigned long long>(s.denied))))
        log_line(line);
}

void SteamTransport::wait_idle() noexcept {
    if (timer_) {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(kWaitMs) * 10000;  // relative, 100 ns units
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            HANDLE handles[2] = {timer_, stop_event_};
            WaitForMultipleObjects(2, handles, FALSE, 50);
            return;
        }
    }
    Sleep(1);
}

void SteamTransport::shutdown_sequence() noexcept {
    // C11: one CLOSE (queued by the owner), the queued datagrams, one
    // CloseSessionWithUser for the peer, then no further Steam call.
    if (sink_.on_shutdown) sink_.on_shutdown(sink_.ctx);
    drain_ring();
    const std::uint64_t peer = Load64(peer_id_);
    if (!faulted_ && config_.leg == SteamLeg::messages && peer) {
        // CloseSessionWithUser closes without linger: give Steam's service
        // thread a moment to put the queued CLOSE on the wire first (no
        // Steam call of ours meanwhile; stop() ends the wait in tests).
        if (stop_event_) WaitForSingleObject(stop_event_, kCloseGraceMs);
        close_session(peer);
    }
    log_stats(NowMs(), true);
    log_line("steam=parked; reason=shutdown\r\n");
}

void SteamTransport::run() noexcept {
    if (!resolve()) {
        if (!stop_) park();
        return;
    }
    policy_.Reset(config_.self_id);
    filter_.Reset();
    InterlockedExchange(&ready_, 1);
    if (sink_.on_ready) sink_.on_ready(sink_.ctx);
    if (config_.warm_relay && api_.utils) {
        bool f = false;
        const bool fresh = TryCheckPing(api_.utils, steam_net::kWarmRelayMaxAge, &f);
        if (f) {
            fault("utils");
        } else {
            log_line(fresh ? "steam=relay; warm=fresh\r\n" : "steam=relay; warm=started\r\n");
        }
    }
    std::uint64_t now = NowMs();
    next_idle_ms_ = now + config_.idle_interval_ms;
    next_route_ms_ = now + kRouteMs;
    next_stats_ms_ = now + kStatsMs;
    while (!stop_) {
        if (api_.halt && *api_.halt && !faulted_) {
            InterlockedExchange(&faulted_, 1);
            if (sink_.on_failed) sink_.on_failed(sink_.ctx, "fault");
        }
        if (faulted_) { park(); return; }
        if (shutdown_) {
            shutdown_sequence();
            park();
            return;
        }
        drain_ring();
        const unsigned received = faulted_ ? 0 : receive();
        if (faulted_) continue;
        now = NowMs();
        if (config_.leg == SteamLeg::messages && config_.role == SteamRole::host) {
            accept_work(now);
            if (!faulted_) hygiene(now);
            InterlockedExchange(&non_peer_, static_cast<long>(policy_.non_peer()));
        }
        if (faulted_) continue;
        if (now >= next_idle_ms_) {
            next_idle_ms_ = now + config_.idle_interval_ms;
            if (sink_.on_idle) sink_.on_idle(sink_.ctx);
        }
        if (config_.leg == SteamLeg::messages) route_poll(now);
        log_stats(now, false);
        if (!received && !stop_ && !shutdown_ && ring_.Depth() == 0) wait_idle();
    }
}

}  // namespace mgscoop::lab
