// Fixture tests of the Steam online path (docs/steam-online.md section 13):
// the ABI, the MGS2 / 337 / command-line parsers, the structural gates on
// synthetic images, the callback objects and their dispatch, the send ring,
// the accept policy and guest filter, route decoding, Release-exactly-once,
// fault latching, shutdown, and two link v2 sessions end to end over two
// SteamTransports on fake ISteamNetworkingMessages002 / Sockets012 objects.
// Section 20: the function-pointer dispatch (config 204/205 queued for
// Sockets012::RunCallbacks, the sources of a session request, 1252, the
// disp32 forwarder and owner gates), the plain-language reasons against the
// shared vectors (argv[1] = tests/fixtures/link-reasons.json) and the in-game
// status line model. No Steam call is made and steamclient64 is never loaded.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mgscoop/coop_hud.hpp"
#include "mgscoop/partner_slots.hpp"
#include "mgscoop/coop_reason_text.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/link_session.hpp"
#include "mgscoop/steam_join_code.hpp"
#include "mgscoop/steam_link_harness.hpp"
#include "mgscoop/steam_net_abi.hpp"
#include "mgscoop/steam_net_callbacks.hpp"
#include "mgscoop/steam_net_dispatch.hpp"
#include "mgscoop/steam_net_gates.hpp"
#include "mgscoop/steam_online_state.hpp"
#include "mgscoop/steam_transport.hpp"

namespace {
namespace sn = mgscoop::steam_net;
namespace lab = mgscoop::lab;
namespace link = mgscoop::link;
namespace code = mgscoop::steam_code;
namespace gate = mgscoop::steam_gate;

unsigned g_checks = 0;
void Check(bool value, const char* what) {
    ++g_checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", g_checks, what);
        std::exit(1);
    }
}

std::uint64_t Now() {
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(c.QuadPart) * 1000 / static_cast<std::uint64_t>(f.QuadPart);
}
template <typename F>
bool WaitFor(F condition, std::uint32_t ms) {
    const std::uint64_t end = Now() + ms;
    while (Now() < end) {
        if (condition()) return true;
        Sleep(2);
    }
    return condition();
}

constexpr std::uint64_t kHost = 0x0110000100001001ull;
constexpr std::uint64_t kGuest = 0x0110000100002002ull;
constexpr std::uint64_t kStranger = 0x0110000100003003ull;

// ------------------------------------------------------------ fake messages
volatile LONG g_released = 0, g_allocated = 0;
struct FakeMessage {
    alignas(16) std::uint8_t header[sn::kMsgObjectSize];
    std::uint8_t payload[1300];
};
void ReleaseFake(void* message) {
    InterlockedIncrement(&g_released);
    delete static_cast<FakeMessage*>(message);
}
FakeMessage* MakeMessage(const sn::Identity& from, const void* data, int size, std::uint32_t conn = 0, bool null_data = false) {
    auto* m = new FakeMessage{};
    InterlockedIncrement(&g_allocated);
    if (size > 0) std::memcpy(m->payload, data, static_cast<std::size_t>(size < 1300 ? size : 1300));
    *reinterpret_cast<void**>(m->header + sn::kMsgData) = null_data ? nullptr : m->payload;
    *reinterpret_cast<std::int32_t*>(m->header + sn::kMsgSize) = size;
    *reinterpret_cast<std::uint32_t*>(m->header + sn::kMsgConn) = conn;
    std::memcpy(m->header + sn::kMsgIdentity, &from, sizeof(from));
    *reinterpret_cast<sn::MessageReleaseFn*>(m->header + sn::kMsgRelease) = &ReleaseFake;
    *reinterpret_cast<std::int32_t*>(m->header + sn::kMsgChannel) = sn::kLinkChannel;
    return m;
}
sn::Identity Id(std::uint64_t id) {
    sn::Identity identity{};
    sn::MakeSteamIdentity(id, identity);
    return identity;
}

class FakeDispatchHub {
public:
    struct SetCall { std::int32_t value, scope; std::intptr_t object; std::int32_t type; sn::ConfigCallbackFn fn; };
    bool Set(std::int32_t value, std::int32_t scope, std::intptr_t object, std::int32_t type, const void* arg) {
        if (fault_set) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        AcquireSRWLockExclusive(&lock_);
        const sn::ConfigCallbackFn fn = arg ? *static_cast<const sn::ConfigCallbackFn*>(arg) : nullptr;
        calls.push_back(SetCall{value, scope, object, type, fn});
        bool ok = value != fail_value && (!arg || type == sn::kConfigTypePtr);
        if (ok) global_[value] = fn;
        ReleaseSRWLockExclusive(&lock_);
        return ok;
    }
    // steamclient's queue: only when a pointer is set (InternalQueueCallback).
    bool Queue(std::int32_t value, const void* payload, std::size_t size) {
        AcquireSRWLockExclusive(&lock_);
        const sn::ConfigCallbackFn fn = global_.count(value) ? global_[value] : nullptr;
        if (fn) queue_.push_back(std::make_pair(fn, std::vector<std::uint8_t>(static_cast<const std::uint8_t*>(payload),
                                                                               static_cast<const std::uint8_t*>(payload) + size)));
        ReleaseSRWLockExclusive(&lock_);
        return fn != nullptr;
    }
    // RunCallbacks: swap the queue under the lock, dispatch outside it.
    void Run() {
        if (fault_run) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        std::deque<std::pair<sn::ConfigCallbackFn, std::vector<std::uint8_t>>> work;
        AcquireSRWLockExclusive(&lock_);
        work.swap(queue_);
        ++runs;
        run_thread = GetCurrentThreadId();
        ReleaseSRWLockExclusive(&lock_);
        for (auto& item : work) item.first(item.second.data());
    }
    sn::ConfigCallbackFn Global(std::int32_t value) {
        AcquireSRWLockExclusive(&lock_);
        const sn::ConfigCallbackFn fn = global_.count(value) ? global_[value] : nullptr;
        ReleaseSRWLockExclusive(&lock_);
        return fn;
    }
    bool AnySet() { return Global(sn::kConfigMessagesSessionRequest) || Global(sn::kConfigMessagesSessionFailed); }
    unsigned Calls() {
        AcquireSRWLockExclusive(&lock_);
        const auto n = static_cast<unsigned>(calls.size());
        ReleaseSRWLockExclusive(&lock_);
        return n;
    }
    std::vector<SetCall> calls;
    volatile LONG runs = 0;
    volatile DWORD run_thread = 0;
    std::int32_t fail_value = 0;
    bool fault_set = false, fault_run = false;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::map<std::int32_t, sn::ConfigCallbackFn> global_;
    std::deque<std::pair<sn::ConfigCallbackFn, std::vector<std::uint8_t>>> queue_;
};

// ------------------------------------------------- FakeNet (Messages002 hub)
// Steam semantics the transport relies on: the first message to a peer
// that has not accepted is held and posts a session request (1251); a send
// implicitly accepts the destination; a broken session fails sends with
// k_EResultNoConnection (3) unless AutoRestartBrokenSession (0x20) restarts
// it, which needs a new accept.
class FakeNet {
public:
    struct Stats { unsigned sends, bad_flags, bad_channel, accepts_true, accepts_false, accepts_existing, closes, infos; };
    std::uint32_t loss_permille = 0, dup_permille = 0, reorder_permille = 0, latency_ms = 0;
    std::int32_t info_state = sn::kConnConnected;
    std::int32_t info_flags = sn::kConnFlagRelayed;

    void Add(std::uint64_t id) {
        Lock l(this);
        users_[id];
    }
    // Section 20: this user's function-pointer queue (config 204/205), and
    // whether its Init-wrapper 1251 reaches steam_cb directly (both sources).
    void SetHub(std::uint64_t id, FakeDispatchHub* hub, bool callback_to_queue) {
        Lock l(this);
        users_[id].hub = hub;
        users_[id].callback_to_queue = callback_to_queue;
    }
    // A failed session (1252) for `self` with `remote`.
    void FailSession(std::uint64_t self, std::uint64_t remote, std::int32_t end, const char* debug) {
        Lock l(this);
        User& u = users_[self];
        if (!u.hub) return;
        std::uint8_t info[sn::kSessionFailedSize]{};
        const sn::Identity id = Id(remote);
        std::memcpy(info + sn::kConnInfoIdentity, &id, sizeof(id));
        const std::int32_t state = sn::kConnProblemDetectedLocally;
        std::memcpy(info + sn::kConnInfoState, &state, 4);
        std::memcpy(info + sn::kConnInfoEndReason, &end, 4);
        std::memcpy(info + sn::kConnInfoEndDebug, debug, std::strlen(debug));
        u.hub->Queue(sn::kConfigMessagesSessionFailed, info, sizeof(info));
    }
    unsigned ClosesWithPointer() {
        Lock l(this);
        return closes_with_pointer_;
    }
    std::int32_t Send(std::uint64_t from, std::uint64_t to, const void* data, std::uint32_t size, std::int32_t flags, std::int32_t channel) {
        Lock l(this);
        ++stats_.sends;
        if (flags != sn::kMsgFlags) ++stats_.bad_flags;
        if (channel != sn::kLinkChannel) ++stats_.bad_channel;
        last_flags_ = flags;
        if (!users_.count(to)) return sn::kResultNoConnection;
        const auto pair = std::make_pair(from, to);
        if (broken_.count(pair)) {
            if (!(flags & sn::kSendAutoRestartBrokenSession)) return sn::kResultNoConnection;
            broken_.erase(pair);
            users_[to].accepted.erase(from);  // the restarted session needs a new accept
            ++restarts_;
        }
        if (size > 0x80000) return sn::kResultInvalidParam;
        users_[from].accepted.insert(to);  // sending implicitly accepts the destination
        User& dest = users_[to];
        if (Rand() % 1000 < loss_permille) return sn::kResultOk;
        const unsigned copies = Rand() % 1000 < dup_permille ? 2u : 1u;
        for (unsigned c = 0; c < copies; ++c) {
            FakeMessage* m = MakeMessage(Id(from), data, static_cast<int>(size));
            if (dest.accepted.count(from)) {
                Queue(dest, m);
            } else {
                dest.held.push_back(std::make_pair(from, m));
                if (!dest.request_posted.count(from)) {
                    dest.request_posted.insert(from);
                    dest.requests.push_back(from);
                    const sn::Identity who = Id(from);
                    if (dest.hub) dest.hub->Queue(sn::kConfigMessagesSessionRequest, &who, sizeof(who));
                    if (dest.callback_to_queue) mgscoop::steam_cb::OnSessionRequest(&who);
                }
            }
        }
        return sn::kResultOk;
    }
    std::int32_t Receive(std::uint64_t self, void** out, std::int32_t max) {
        Lock l(this);
        User& u = users_[self];
        std::int32_t n = 0;
        const std::uint64_t now = Now();
        while (n < max && !u.inbox.empty() && u.inbox.front().ready_ms <= now) {
            out[n++] = u.inbox.front().message;
            u.inbox.pop_front();
        }
        return n;
    }
    bool Accept(std::uint64_t self, std::uint64_t remote) {
        Lock l(this);
        User& u = users_[self];
        bool pending = u.request_posted.count(remote) != 0;
        // As steamclient's CSteamNetworkingMessages::AcceptSessionWithUser
        // (0xA7AEF0 in caba4826...): true for ANY existing session (it only
        // accepts a connecting one), false when there is none.
        if (!pending && u.accepted.count(remote)) {
            ++stats_.accepts_existing;
            return true;
        }
        if (!pending) {
            ++stats_.accepts_false;
            return false;
        }
        ++stats_.accepts_true;
        u.accepted.insert(remote);
        u.request_posted.erase(remote);
        for (auto it = u.held.begin(); it != u.held.end();) {
            if (it->first == remote) {
                Queue(u, it->second);
                it = u.held.erase(it);
            } else {
                ++it;
            }
        }
        return true;
    }
    bool Close(std::uint64_t self, std::uint64_t remote) {
        Lock l(this);
        ++stats_.closes;
        ++closes_[std::make_pair(self, remote)];
        if (users_[self].hub && users_[self].hub->AnySet()) ++closes_with_pointer_;
        users_[self].accepted.erase(remote);
        users_[self].request_posted.erase(remote);
        return true;
    }
    std::int32_t Info(std::uint64_t self, std::uint64_t remote, void* info) {
        Lock l(this);
        ++stats_.infos;
        (void)self;
        if (!info) return info_state;
        auto* bytes = static_cast<std::uint8_t*>(info);
        std::memset(bytes, 0, sn::kConnInfoSize);
        const sn::Identity id = Id(remote);
        std::memcpy(bytes + sn::kConnInfoIdentity, &id, sizeof(id));
        std::memcpy(bytes + sn::kConnInfoState, &info_state, 4);
        std::memcpy(bytes + sn::kConnInfoFlags, &info_flags, 4);
        return info_state;
    }
    // The 1251 queue of `self` (what steam_api would dispatch to our callback).
    bool PopRequest(std::uint64_t self, std::uint64_t& id) {
        Lock l(this);
        User& u = users_[self];
        if (u.requests.empty()) return false;
        id = u.requests.front();
        u.requests.pop_front();
        return true;
    }
    void Inject(std::uint64_t to, FakeMessage* m) {
        Lock l(this);
        users_[to].inbox.push_back(Item{m, 0});
    }
    void Break(std::uint64_t from, std::uint64_t to) {
        Lock l(this);
        broken_.insert(std::make_pair(from, to));
    }
    Stats GetStats() {
        Lock l(this);
        return stats_;
    }
    unsigned Closes(std::uint64_t self, std::uint64_t remote) {
        Lock l(this);
        return closes_[std::make_pair(self, remote)];
    }
    unsigned Restarts() {
        Lock l(this);
        return restarts_;
    }
    std::int32_t LastFlags() {
        Lock l(this);
        return last_flags_;
    }
    void Drain() {
        Lock l(this);
        for (auto& [id, u] : users_) {
            for (auto& item : u.inbox) ReleaseFake(item.message);
            for (auto& h : u.held) ReleaseFake(h.second);
            u.inbox.clear();
            u.held.clear();
        }
    }

private:
    struct Item { FakeMessage* message; std::uint64_t ready_ms; };
    struct User {
        std::deque<Item> inbox;
        std::set<std::uint64_t> accepted, request_posted;
        std::vector<std::pair<std::uint64_t, FakeMessage*>> held;
        std::deque<std::uint64_t> requests;
        FakeDispatchHub* hub = nullptr;
        bool callback_to_queue = false;
    };
    struct Lock {
        explicit Lock(FakeNet* n) : net(n) { AcquireSRWLockExclusive(&net->lock_); }
        ~Lock() { ReleaseSRWLockExclusive(&net->lock_); }
        FakeNet* net;
    };
    void Queue(User& u, FakeMessage* m) {
        const std::uint64_t ready = Now() + latency_ms;
        if (Rand() % 1000 < reorder_permille && !u.inbox.empty()) u.inbox.insert(u.inbox.end() - 1, Item{m, ready});
        else u.inbox.push_back(Item{m, ready});
    }
    unsigned Rand() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::map<std::uint64_t, User> users_;
    std::set<std::pair<std::uint64_t, std::uint64_t>> broken_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, unsigned> closes_;
    Stats stats_{};
    unsigned restarts_ = 0;
    unsigned closes_with_pointer_ = 0;
    std::int32_t last_flags_ = 0;
    unsigned rng_ = 0x12345678u;
};

class FakeMessages002 final : public sn::ISteamNetworkingMessages002Abi {
public:
    FakeMessages002(FakeNet* net, std::uint64_t self) : net_(net), self_(self) {}
    std::int32_t SendMessageToUser(const sn::Identity& remote, const void* data, std::uint32_t size, std::int32_t flags,
                                   std::int32_t channel) override {
        Call();
        std::uint64_t to = 0;
        if (remote.type == sn::kIdentityInvalid) return sn::kResultInvalidParam;
        if (!sn::IdentitySteamId(remote, to)) return sn::kResultInvalidParam;
        return net_->Send(self_, to, data, size, flags, channel);
    }
    std::int32_t ReceiveMessagesOnChannel(std::int32_t channel, void** out, std::int32_t max) override {
        Call();
        if (channel != sn::kLinkChannel) return 0;
        return net_->Receive(self_, out, max);
    }
    bool AcceptSessionWithUser(const sn::Identity& remote) override {
        Call();
        std::uint64_t id = 0;
        return sn::IdentitySteamId(remote, id) && net_->Accept(self_, id);
    }
    bool CloseSessionWithUser(const sn::Identity& remote) override {
        Call();
        std::uint64_t id = 0;
        return sn::IdentitySteamId(remote, id) && net_->Close(self_, id);
    }
    bool CloseChannelWithUser(const sn::Identity&, std::int32_t) override {
        Call();
        return false;
    }
    std::int32_t GetSessionConnectionInfo(const sn::Identity& remote, void* info, void* quick) override {
        Call();
        quick_seen_ = quick_seen_ || quick != nullptr;
        std::uint64_t id = 0;
        sn::IdentitySteamId(remote, id);
        return net_->Info(self_, id, info);
    }
    LONG calls() const { return calls_; }
    void FaultAfter(LONG n) { fault_after_ = n; }
    bool quick_seen() const { return quick_seen_; }

private:
    void Call() {
        const LONG n = InterlockedIncrement(&calls_);
        if (fault_after_ > 0 && n >= fault_after_) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
    FakeNet* net_;
    std::uint64_t self_;
    volatile LONG calls_ = 0;
    volatile LONG fault_after_ = 0;
    bool quick_seen_ = false;
};

// Sockets012: an in-process pair whose messages carry IP-localhost identities.
class FakeSockets012 final : public sn::ISteamNetworkingSockets012Abi {
public:
    bool CloseConnection(std::uint32_t conn, std::int32_t, const char*, bool) override {
        AcquireSRWLockExclusive(&lock_);
        ++closed_;
        inbox_.erase(conn);
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }
    std::int32_t SendMessageToConnection(std::uint32_t conn, const void* data, std::uint32_t size, std::int32_t flags,
                                         std::int64_t*) override {
        AcquireSRWLockExclusive(&lock_);
        last_flags_ = flags;
        const std::uint32_t other = conn == 11 ? 12 : 11;
        sn::Identity ip{};
        ip.type = ip_type_;
        ip.size = sn::kIdentityIpSize;
        inbox_[other].push_back(MakeMessage(ip, data, static_cast<int>(size), other));
        ReleaseSRWLockExclusive(&lock_);
        return sn::kResultOk;
    }
    std::int32_t ReceiveMessagesOnConnection(std::uint32_t conn, void** out, std::int32_t max) override {
        AcquireSRWLockExclusive(&lock_);
        std::int32_t n = 0;
        auto& q = inbox_[conn];
        while (n < max && !q.empty()) {
            out[n++] = q.front();
            q.pop_front();
        }
        ReleaseSRWLockExclusive(&lock_);
        return n;
    }
    bool CreateSocketPair(std::uint32_t* a, std::uint32_t* b, bool loopback, const sn::Identity* i1, const sn::Identity* i2) override {
        loopback_ = loopback;
        nulls_ = !i1 && !i2;
        *a = 11;
        *b = 12;
        return true;
    }
    void RunCallbacks() override {
        InterlockedIncrement(&run_calls_);
        if (hub) hub->Run();
    }
    FakeDispatchHub* hub = nullptr;
    volatile LONG run_calls_ = 0;
    void Clear() {
        AcquireSRWLockExclusive(&lock_);
        for (auto& [conn, q] : inbox_) {
            for (FakeMessage* m : q) ReleaseFake(m);
            q.clear();
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    std::int32_t ip_type_ = sn::kIdentityIp;
    std::int32_t last_flags_ = 0;
    bool loopback_ = true, nulls_ = false;
    unsigned closed_ = 0;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::map<std::uint32_t, std::deque<FakeMessage*>> inbox_;
};

class FakeUtils004 final : public sn::ISteamNetworkingUtils004Abi {
public:
    std::int32_t GetRelayNetworkStatus(void*) override { return 100; }
    bool CheckPingDataUpToDate(float age) override {
        ++pings;
        last_age = age;
        return false;
    }
    bool SetConfigValue(std::int32_t value, std::int32_t scope, std::intptr_t object, std::int32_t type, const void* arg) override {
        return hub && hub->Set(value, scope, object, type, arg);
    }
    FakeDispatchHub* hub = nullptr;
    unsigned pings = 0;
    float last_age = 0;
};

// ----------------------------------------------------- transport test rigs
struct ApiBox {
    lab::SteamNetApi api{};
    int result = 1;
    const char* reason = nullptr;
    volatile LONG faults = 0;
};
int Resolve(void* ctx, lab::SteamNetApi& out, const char*& reason) noexcept {
    auto* box = static_cast<ApiBox*>(ctx);
    out = box->api;
    reason = box->reason;
    return box->result;
}
void OnFault(void* ctx, const char*) noexcept { InterlockedIncrement(&static_cast<ApiBox*>(ctx)->faults); }

FakeNet* g_net = nullptr;
// The Init wrapper's 1251 as steam_api would dispatch it (source callback).
bool PopHostRequest(std::uint64_t& id, std::uint8_t& sources) noexcept {
    sources = mgscoop::steam_cb::kSourceCallback;
    return g_net && g_net->PopRequest(kHost, id);
}

// Transport log capture: counts the "steam=accept;" lines.
volatile LONG g_accept_lines = 0;
void CountLog(const char* line) noexcept {
    if (line && std::strncmp(line, "steam=accept;", 13) == 0) InterlockedIncrement(&g_accept_lines);
}

// A sink that records datagrams.
struct Recorder {
    volatile LONG datagrams = 0, idles = 0, readies = 0, shutdowns = 0;
    char failed[32]{};
    std::vector<std::vector<std::uint8_t>> seen;
    std::vector<lab::Endpoint> from;
    SRWLOCK lock = SRWLOCK_INIT;
    lab::SteamTransport* transport = nullptr;
    bool close_on_shutdown = false;
};
void RecDatagram(void* ctx, const std::uint8_t* d, std::size_t n, const lab::Endpoint& f) noexcept {
    auto* r = static_cast<Recorder*>(ctx);
    AcquireSRWLockExclusive(&r->lock);
    r->seen.emplace_back(d, d + n);
    r->from.push_back(f);
    ReleaseSRWLockExclusive(&r->lock);
    InterlockedIncrement(&r->datagrams);
}
void RecIdle(void* ctx) noexcept { InterlockedIncrement(&static_cast<Recorder*>(ctx)->idles); }
void RecReady(void* ctx) noexcept { InterlockedIncrement(&static_cast<Recorder*>(ctx)->readies); }
void RecFailed(void* ctx, const char* reason) noexcept {
    auto* r = static_cast<Recorder*>(ctx);
    std::snprintf(r->failed, sizeof(r->failed), "%s", reason ? reason : "?");
}
void RecShutdown(void* ctx) noexcept {
    auto* r = static_cast<Recorder*>(ctx);
    InterlockedIncrement(&r->shutdowns);
    if (r->close_on_shutdown && r->transport) {
        const std::uint8_t close[60] = {'C', 'L', 'O', 'S', 'E'};
        const lab::WireChunk chunk{close, sizeof(close)};
        r->transport->transmit(&chunk, 1);
    }
}
lab::SteamSink Sink(Recorder& r) {
    lab::SteamSink s{};
    s.on_datagram = &RecDatagram;
    s.on_idle = &RecIdle;
    s.on_ready = &RecReady;
    s.on_failed = &RecFailed;
    s.on_shutdown = &RecShutdown;
    s.ctx = &r;
    return s;
}

// ----------------------------------------------------------------- 1. ABI
void TestIdentity() {
    sn::Identity id{};
    std::memset(&id, 0xAB, sizeof(id));
    sn::MakeSteamIdentity(kHost, id);
    Check(id.type == 16 && id.size == 8, "identity type 16, cbSize 8");
    bool zero = true;
    for (std::size_t i = 8 + 8; i < sizeof(id.data) + 8; ++i) zero = zero && reinterpret_cast<const std::uint8_t*>(&id)[i] == 0;
    Check(zero, "identity zero-filled after the SteamID");
    std::uint64_t out = 0;
    Check(sn::IdentitySteamId(id, out) && out == kHost, "identity round trip");
    for (std::int32_t type : {0, 1, 17}) {
        sn::Identity bad = id;
        bad.type = type;
        Check(!sn::IdentitySteamId(bad, out) && out == 0, "identity type 0/1/17 refused");
    }
    sn::Identity size = id;
    size.size = 9;
    Check(!sn::IdentitySteamId(size, out), "identity cbSize != 8 refused");
    sn::Identity group = Id(0x0170000100000001ull);
    Check(!sn::IdentitySteamId(group, out), "non-individual SteamID refused");
    sn::Identity zero_account = Id(0x0110000100000000ull);
    Check(!sn::IdentitySteamId(zero_account, out), "account 0 refused");
    sn::Identity ip{};
    ip.type = sn::kIdentityIp;
    ip.size = sn::kIdentityIpSize;
    Check(sn::IdentityIsIp(ip) && !sn::IdentityIsIp(id), "IP-localhost identity (type 1, cbSize 18)");
    Check(lab::SteamIdOf(lab::SteamEndpoint(kGuest)) == kGuest && lab::SteamEndpoint(kGuest).port == 0x5354, "endpoint mapping");
    Check(sn::kMsgFlags == 0x21 && sn::kConnFlags == 1 && sn::kLinkChannel == 0x4D47, "send flags and channel");
}

// ------------------------------------------------------- 10. join codes
void TestJoinCodes() {
    struct Vector { const char* secret; std::uint64_t host; const char* cc; };
    // Independently computed (Python hashlib); tests/friend_package_tests.ps1 uses the same.
    const Vector vectors[] = {{"ABCDE12345FGHJK67890", 76561198058195100ull, "6Z"},
                              {"C00PC00PC00PC00PC00P", 76561197960265729ull, "D1"},
                              {"ZZZZZYYYYYXXXXXWWWWW", 76561202255233023ull, "5D"},
                              {"0123456789ABCDEFGHJK", 76561198057664236ull, "2R"}};
    for (const auto& v : vectors) {
        char secret[link::kSecretChars];
        std::memcpy(secret, v.secret, link::kSecretChars);
        char check[3]{};
        code::Check(secret, v.host, check);
        Check(std::strcmp(check, v.cc) == 0, "MGS2 check characters");
        char text[code::kCodeBuffer]{};
        Check(code::Format(secret, v.host, text, sizeof(text)) && std::strlen(text) == code::kCodeChars, "MGS2 format");
        code::Code parsed{};
        Check(code::Parse(text, std::strlen(text), parsed) == code::Status::ok && parsed.host == v.host &&
                  std::memcmp(parsed.secret, v.secret, 20) == 0,
              "MGS2 round trip");
    }
    code::Code c{};
    const char* good = "MGS2-ABCDE-12345-FGHJK-67890-6Z@76561198058195100";
    Check(code::Parse(good, std::strlen(good), c) == code::Status::ok, "canonical code");
    const char* loose = " mgs2 abcde 12345 fghjk 67890 6z @ 76561198058195100\r\n";
    Check(code::Parse(loose, std::strlen(loose), c) == code::Status::ok && c.host == 76561198058195100ull, "blanks and case tolerated");
    const char* confusable = "MGS2-ABCDE-I2345-FGHJK-67890-6Z@76561198058195100";  // I -> 1
    Check(code::Parse(confusable, std::strlen(confusable), c) == code::Status::ok, "I/L -> 1, O -> 0");
    struct Bad { const char* text; code::Status status; };
    const Bad bad[] = {{"", code::Status::empty},
                       {"   ", code::Status::empty},
                       {"MGS1-ABCDE-12345-FGHJK-67890-6Z@203.0.113.7", code::Status::bad_prefix},
                       {"MGS2-ABCDE-12345-FGHJK-67890-6Z", code::Status::bad_steamid},
                       {"MGS2-ABCDU-12345-FGHJK-67890-6Z@76561198058195100", code::Status::bad_char},
                       {"MGS2-ABCDE-12345-FGHJK-6789-6Z@76561198058195100", code::Status::bad_length},
                       {"MGS2-ABCDE-12345-FGHJK-67890-6Y@76561198058195100", code::Status::bad_check},
                       {"MGS2-ABCDE-12345-FGHJK-67890-6Z@76561198058195101", code::Status::bad_check},
                       {"MGS2-ABCDE-12345-FGHJK-67890-6Z@076561198058195100", code::Status::bad_steamid},
                       {"MGS2-ABCDE-12345-FGHJK-67890-6Z@103582791429521408", code::Status::bad_steamid},
                       {"MGS2-ABCDE-12345-FGHJK-67890-6Z@76561197960265728", code::Status::bad_steamid}};
    for (const auto& b : bad) Check(code::Parse(b.text, std::strlen(b.text), c) == b.status && c.host == 0 && c.secret[0] == 0, b.text);
    // The invite / 337 connect string.
    char secret[link::kSecretChars];
    std::memcpy(secret, "ABCDE12345FGHJK67890", 20);
    char invite[code::kInviteBuffer]{};
    Check(code::FormatInvite(secret, 76561198058195100ull, invite, sizeof(invite)) &&
              std::strcmp(invite, "+mgscoop_join MGS2-ABCDE-12345-FGHJK-67890-6Z@76561198058195100") == 0,
          "invite string");
    sn::JoinRequested req{};
    req.friend_id = 76561198058195100ull;
    std::snprintf(req.connect, sizeof(req.connect), "%s", invite);
    Check(code::ParseJoinRequest(req.friend_id, req.connect, sizeof(req.connect), c).status == code::JoinStatus::ok && c.host == req.friend_id,
          "337 accepted");
    Check(code::ParseJoinRequest(kStranger, req.connect, sizeof(req.connect), c).status == code::JoinStatus::host_mismatch, "337 host != friend");
    std::snprintf(req.connect, sizeof(req.connect), "+connect_lobby 109775241234567890");
    Check(code::ParseJoinRequest(req.friend_id, req.connect, sizeof(req.connect), c).status == code::JoinStatus::not_mgscoop, "337 other prefix");
    std::snprintf(req.connect, sizeof(req.connect), "%s extra", invite);
    Check(code::ParseJoinRequest(req.friend_id, req.connect, sizeof(req.connect), c).status == code::JoinStatus::extra_text, "337 trailing text");
    std::snprintf(req.connect, sizeof(req.connect), "+mgscoop_join MGS2-ABCDE-12345-FGHJK-67890-6Y@76561198058195100");
    const auto wrong = code::ParseJoinRequest(req.friend_id, req.connect, sizeof(req.connect), c);
    Check(wrong.status == code::JoinStatus::code && wrong.code == code::Status::bad_check &&
              std::strcmp(code::JoinReason(wrong), "bad_check") == 0,
          "337 bad check");
    std::memset(req.connect, 'A', sizeof(req.connect));
    Check(code::ParseJoinRequest(req.friend_id, req.connect, sizeof(req.connect), c).status == code::JoinStatus::not_terminated, "337 no NUL");
    std::snprintf(req.connect, sizeof(req.connect), "%s", invite);
    req.connect[20] = '\x07';
    Check(code::ParseJoinRequest(req.friend_id, req.connect, sizeof(req.connect), c).status == code::JoinStatus::not_printable, "337 control");
    // S3 extractor (not wired until S3 is approved).
    code::Status cs{};
    Check(code::ExtractCommandLineJoin(L"mgsvtpp.exe -foo", c, cs) == code::CommandLineStatus::none, "S3 none");
    Check(code::ExtractCommandLineJoin(L"\"D:\\g\\mgsvtpp.exe\" +mgscoop_join MGS2-ABCDE-12345-FGHJK-67890-6Z@76561198058195100", c, cs) ==
                  code::CommandLineStatus::ok && c.host == 76561198058195100ull,
          "S3 ok");
    Check(code::ExtractCommandLineJoin(L"x +mgscoop_join A +mgscoop_join B", c, cs) == code::CommandLineStatus::multiple, "S3 multiple");
    Check(code::ExtractCommandLineJoin(L"x +mgscoop_join", c, cs) == code::CommandLineStatus::missing_code, "S3 missing code");
    Check(code::ExtractCommandLineJoin(L"x +mgscoop_join MGS2-ABCDE-12345-FGHJK-67890-6Y@76561198058195100", c, cs) ==
                  code::CommandLineStatus::bad_code && cs == code::Status::bad_check,
          "S3 bad code");
    char peer[24]{};
    code::FormatPeer(76561198058195100ull, peer);
    Check(std::strcmp(peer, "steam:...5100") == 0, "masked peer");
    code::FormatPeer(0, peer);
    Check(std::strcmp(peer, "steam:none") == 0, "masked none");
}

// -------------------------------------------------- 8. gates on synthetic images
struct Image {
    std::vector<std::uint8_t> bytes;
    std::uintptr_t base() const { return reinterpret_cast<std::uintptr_t>(bytes.data()); }
    void Put32(std::size_t at, std::uint32_t v) { std::memcpy(&bytes[at], &v, 4); }
    void Put64(std::size_t at, std::uint64_t v) { std::memcpy(&bytes[at], &v, 8); }
};
constexpr std::uint32_t kText = 0x1000, kRdata = 0x3000, kData = 0x5000, kImage = 0x7000;
// .text: forwarder stubs; .rdata: COL, TypeDescriptor name, vtable; .data: writable.
Image BuildImage(const char* rtti, bool good_forwarders = true) {
    Image img;
    img.bytes.assign(kImage, 0xCC);
    std::memset(img.bytes.data(), 0, 0x400);
    img.bytes[0] = 'M';
    img.bytes[1] = 'Z';
    img.Put32(0x3C, 0x80);
    img.Put32(0x80, 0x00004550);
    img.bytes[0x84] = 0x64;
    img.bytes[0x85] = 0x86;  // x64
    img.bytes[0x86] = 3;     // sections
    img.bytes[0x94] = 0xF0;  // SizeOfOptionalHeader
    img.bytes[0x98] = 0x0B;
    img.bytes[0x99] = 0x02;  // PE32+
    img.Put32(0x98 + 56, kImage);
    const std::size_t table = 0x98 + 0xF0;
    const struct { const char* name; std::uint32_t va, chars; } sections[] = {
        {".text", kText, 0x60000020}, {".rdata", kRdata, 0x40000040}, {".data", kData, 0xC0000040}};
    for (unsigned i = 0; i < 3; ++i) {
        const std::size_t s = table + 40 * i;
        std::memcpy(&img.bytes[s], sections[i].name, std::strlen(sections[i].name));
        img.Put32(s + 8, 0x2000);
        img.Put32(s + 12, sections[i].va);
        img.Put32(s + 16, 0x2000);
        img.Put32(s + 36, sections[i].chars);
    }
    // Forwarders for slots 0..5 at .text + 0x40 * N, inner functions after them.
    for (unsigned n = 0; n < 6; ++n) {
        std::uint8_t* f = &img.bytes[kText + 0x40 * n];
        const std::uint8_t head[] = {0x48, 0x8B, 0x49, 0x08};
        std::memcpy(f, head, 4);
        if (n == 0) {
            const std::uint8_t r10[] = {0x4C, 0x8B, 0x11, 0x49, 0xFF, 0x22};
            std::memcpy(f + 4, r10, sizeof(r10));
        } else {
            const std::uint8_t rax[] = {0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, static_cast<std::uint8_t>(8 * n)};
            std::memcpy(f + 4, rax, sizeof(rax));
        }
        if (!good_forwarders && n == 3) f[10] = 0x20;  // jmp [rax+20h] in slot 3 (should be +18h)
    }
    // TypeDescriptor at .rdata + 0x100 (name at +0x10), COL at +0x200, vtable at +0x300 (COL ptr at +0x2F8).
    std::memcpy(&img.bytes[kRdata + 0x110], rtti, std::strlen(rtti) + 1);
    const std::size_t col = kRdata + 0x200;
    img.Put32(col + 0, 1);
    img.Put32(col + 4, 0);
    img.Put32(col + 8, 0);
    img.Put32(col + 12, kRdata + 0x100);
    img.Put32(col + 16, 0);
    img.Put32(col + 20, static_cast<std::uint32_t>(col));
    img.Put64(kRdata + 0x2F8, img.base() + col);
    for (unsigned n = 0; n < 24; ++n) img.Put64(kRdata + 0x300 + 8 * n, img.base() + kText + 0x40 * (n % 6));
    // The inner vtable at .rdata + 0x600 (6 code slots).
    for (unsigned n = 0; n < 6; ++n) img.Put64(kRdata + 0x600 + 8 * n, img.base() + kText + 0x800 + 0x10 * n);
    return img;
}
bool ReadPlain(void*, std::uintptr_t address, std::uint64_t& out) noexcept {
    std::memcpy(&out, reinterpret_cast<const void*>(address), 8);
    return true;
}

void TestGates() {
    Image img = BuildImage(sn::kRttiMessages);
    gate::ModuleView view{};
    Check(gate::ReadModuleView(img.bytes.data(), img.bytes.size(), view) && view.count == 3, "module view");
    Check(gate::Classify(view, img.base() + kText + 4, 8) == gate::Region::code, ".text is code");
    Check(gate::Classify(view, img.base() + kRdata + 0x300, 8) == gate::Region::rdata, ".rdata");
    Check(gate::Classify(view, img.base() + kData, 8) == gate::Region::other, ".data writable");
    Check(gate::Classify(view, img.base() + kImage + 16, 8) == gate::Region::outside, "outside the image");
    // The objects (heap, outside the module).
    std::uint64_t inner[1] = {img.base() + kRdata + 0x600};
    std::uint64_t adapter[2] = {img.base() + kRdata + 0x300, reinterpret_cast<std::uint64_t>(inner)};
    const gate::InterfaceRule messages{sn::kRttiMessages, 6, true};
    Check(gate::CheckInterface(view, reinterpret_cast<std::uintptr_t>(adapter), messages, &ReadPlain, nullptr) == nullptr, "Messages accepted");
    Check(std::strcmp(gate::CheckInterface(view, 0, messages, &ReadPlain, nullptr), "iface_missing") == 0, "null interface");
    std::uint64_t outside[2] = {reinterpret_cast<std::uint64_t>(inner), adapter[1]};
    Check(std::strcmp(gate::CheckInterface(view, reinterpret_cast<std::uintptr_t>(outside), messages, &ReadPlain, nullptr), "iface_module") == 0,
          "vptr outside the module");
    std::uint64_t writable[2] = {img.base() + kData + 0x10, adapter[1]};
    Check(std::strcmp(gate::CheckInterface(view, reinterpret_cast<std::uintptr_t>(writable), messages, &ReadPlain, nullptr), "iface_module") == 0,
          "vptr in a writable section");
    const gate::InterfaceRule sockets{sn::kRttiSockets, 20, false};
    Check(std::strcmp(gate::CheckInterface(view, reinterpret_cast<std::uintptr_t>(adapter), sockets, &ReadPlain, nullptr), "iface_rtti") == 0,
          "wrong RTTI name");
    Image bad = BuildImage(sn::kRttiMessages, false);
    gate::ModuleView bad_view{};
    gate::ReadModuleView(bad.bytes.data(), bad.bytes.size(), bad_view);
    std::uint64_t bad_inner[1] = {bad.base() + kRdata + 0x600};
    std::uint64_t bad_adapter[2] = {bad.base() + kRdata + 0x300, reinterpret_cast<std::uint64_t>(bad_inner)};
    Check(std::strcmp(gate::CheckInterface(bad_view, reinterpret_cast<std::uintptr_t>(bad_adapter), messages, &ReadPlain, nullptr), "iface_shape") == 0,
          "bad forwarder");
    std::uint64_t bad_inner_vptr[1] = {img.base() + kData};
    std::uint64_t inner_bad[2] = {adapter[0], reinterpret_cast<std::uint64_t>(bad_inner_vptr)};
    Check(std::strcmp(gate::CheckInterface(view, reinterpret_cast<std::uintptr_t>(inner_bad), messages, &ReadPlain, nullptr), "iface_shape") == 0,
          "inner vptr not read-only");
    // Forwarder shapes (exact runtime rule).
    const std::uint8_t f0[] = {0x48, 0x8B, 0x49, 0x08, 0x4C, 0x8B, 0x11, 0x49, 0xFF, 0x22, 0xCC};
    const std::uint8_t f5[] = {0x48, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x28};
    const std::uint8_t f2r10[] = {0x48, 0x8B, 0x49, 0x08, 0x4C, 0x8B, 0x11, 0x49, 0xFF, 0x62, 0x10};
    Check(gate::IsForwarder(f0, sizeof(f0), 0) && !gate::IsForwarder(f0, sizeof(f0), 1), "r10 slot 0 forwarder");
    Check(gate::IsForwarder(f5, sizeof(f5), 5) && !gate::IsForwarder(f5, sizeof(f5), 4), "rax disp8 forwarder");
    Check(gate::IsForwarder(f2r10, sizeof(f2r10), 2), "r10 disp8 forwarder");
    // The client object (gate 4).
    Image client = BuildImage(sn::kRttiClient);
    gate::ModuleView client_view{};
    gate::ReadModuleView(client.bytes.data(), client.bytes.size(), client_view);
    std::uint64_t client_object[1] = {client.base() + kRdata + 0x300};
    Check(gate::CheckClient(client_view, reinterpret_cast<std::uintptr_t>(client_object), sn::kRttiClient, 12, &ReadPlain, nullptr) == nullptr,
          "client accepted");
    Check(std::strcmp(gate::CheckClient(view, reinterpret_cast<std::uintptr_t>(adapter), sn::kRttiClient, 12, &ReadPlain, nullptr),
                      "client_iface") == 0,
          "client RTTI mismatch");
    // Malformed headers.
    Image broken = img;
    broken.bytes[0x85] = 0x01;
    gate::ModuleView v{};
    Check(!gate::ReadModuleView(broken.bytes.data(), broken.bytes.size(), v), "non-x64 image refused");
    // Gate 2 and gate 5 decisions.
    int dummy = 0;
    Check(gate::InitializedGate(&dummy, 1, 1, &dummy) == nullptr && std::strcmp(gate::InitializedGate(&dummy, 0, 1, &dummy), "not_initialized") == 0,
          "not_initialized");
    Check(std::strcmp(gate::IdentityGate(false, false, kHost, kHost, true, 0), "offline") == 0, "offline");
    Check(std::strcmp(gate::IdentityGate(false, true, kHost, kGuest, true, 0), "self_mismatch") == 0, "self_mismatch");
    Check(std::strcmp(gate::IdentityGate(false, true, 0x0170000100000001ull, 0x0170000100000001ull, true, 0), "self_mismatch") == 0,
          "self_mismatch: not individual");
    Check(std::strcmp(gate::IdentityGate(false, true, kGuest, kGuest, false, kGuest), "peer_is_self") == 0, "peer_is_self");
    Check(gate::IdentityGate(false, true, kGuest, kGuest, false, kHost) == nullptr, "guest accepted");
    Check(gate::IdentityGate(true, false, 0, 0, true, 0) == nullptr, "self-test records logged_on only");
    // client_module: the module must lie under SteamPath.
    Check(gate::PathUnder(L"C:\\Program Files (x86)\\Steam\\steamclient64.dll", L"c:/program files (x86)/steam"), "steamclient under SteamPath");
    Check(!gate::PathUnder(L"C:\\Program Files (x86)\\SteamX\\steamclient64.dll", L"c:/program files (x86)/steam"), "prefix is not enough");
    Check(!gate::PathUnder(L"D:\\Games\\steamclient64.dll", L"c:/program files (x86)/steam"), "other folder refused");
    Check(!gate::PathUnder(L"C:\\Steam\\..\\evil\\steamclient64.dll", L"C:\\Steam"), "dot-dot refused");
}

// ---------------------------------------------------- 7. route decoding
void TestRoute() {
    std::uint8_t info[sn::kConnInfoSize]{};
    const sn::Identity peer = Id(kGuest);
    std::memcpy(info, &peer, sizeof(peer));
    std::int32_t state = sn::kConnConnected, flags = sn::kConnFlagRelayed;
    std::memcpy(info + sn::kConnInfoState, &state, 4);
    std::memcpy(info + sn::kConnInfoFlags, &flags, 4);
    Check(sn::DecodeConnectionInfo(sn::kConnConnected, info, kGuest).route == sn::Route::relay, "relayed");
    flags = 0;
    std::memcpy(info + sn::kConnInfoFlags, &flags, 4);
    Check(sn::DecodeConnectionInfo(sn::kConnConnected, info, kGuest).route == sn::Route::direct, "direct");
    Check(sn::DecodeConnectionInfo(sn::kConnFindingRoute, info, kGuest).route == sn::Route::unknown, "state != info state");
    Check(sn::DecodeConnectionInfo(sn::kConnConnected, info, kHost).route == sn::Route::unknown, "identity != peer");
    Check(sn::DecodeConnectionInfo(sn::kConnConnected, nullptr, kGuest).route == sn::Route::unknown, "no info");
    state = sn::kConnConnecting;
    std::memcpy(info + sn::kConnInfoState, &state, 4);
    Check(sn::DecodeConnectionInfo(sn::kConnConnecting, info, kGuest).route == sn::Route::unknown, "not connected yet");
    Check(std::strcmp(sn::ConnectionStateName(sn::kConnFindingRoute), "finding_route") == 0, "state names");
}

// ----------------------------------------------- 9. callbacks and dispatch
// steam_api64 RegisterCallback (0x2B10): or byte [cb+8],1; mov [cb+0Ch],id.
void FakeRegister(void* cb, int id) {
    auto* bytes = static_cast<std::uint8_t*>(cb);
    bytes[mgscoop::steam::kCallbackFlagsOffset] |= 1;
    std::memcpy(bytes + mgscoop::steam::kCallbackIdOffset, &id, 4);
}
// steam_api64 dispatch (0x2DE0..0x2DF9): GameServer bit vs bpl, then call [vt+8](cb, payload).
void FakeDispatch(void* cb, void* payload, bool game_server) {
    auto* object = static_cast<sn::CallbackObject*>(cb);
    const bool server = ((object->flags >> 1) & 1) != 0;
    if (server != game_server) return;
    reinterpret_cast<sn::CallbackRunFn>(object->vptr[1])(cb, payload);
}
// Another thread takes the 337 mailbox and the 1251 queue (both under the handlers' lock).
DWORD WINAPI TakeBothProbe(void*) {
    mgscoop::steam_net::JoinRequested out{};
    std::uint64_t id = 0;
    std::uint8_t src = 0;
    mgscoop::steam_cb::TakeJoinRequest(out);
    mgscoop::steam_cb::PopSessionRequest(id, src);
    return 0;
}
void TestCallbacks() {
    namespace cb = mgscoop::steam_cb;
    cb::ResetForTest();
    sn::CallbackObject* req = cb::SessionRequestObject();
    sn::CallbackObject* join = cb::JoinRequestObject();
    Check(req->flags == 0 && join->flags == 0, "flags start clear (GameServer bit 0)");
    Check(req->vptr == cb::SessionRequestVtable() && join->vptr == cb::JoinRequestVtable(), "static vtables");
    Check(reinterpret_cast<sn::CallbackSizeFn>(req->vptr[2])(req) == 136 && reinterpret_cast<sn::CallbackSizeFn>(join->vptr[2])(join) == 264,
          "GetCallbackSizeBytes slot 2");
    FakeRegister(req, sn::kCallbackSessionRequest);
    FakeRegister(join, sn::kCallbackJoinRequested);
    Check(req->flags == 1 && req->id == 1251 && join->id == 337, "registered layout +8/+0xC");
    sn::Identity g = Id(kGuest);
    FakeDispatch(req, &g, true);
    std::uint64_t id = 0;
    std::uint8_t src = 0;
    Check(!cb::PopSessionRequest(id, src), "a GameServer dispatch is filtered");
    FakeDispatch(req, &g, false);
    FakeDispatch(req, &g, false);
    Check(cb::PopSessionRequest(id, src) && id == kGuest && src == cb::kSourceCallback && !cb::PopSessionRequest(id, src),
          "1251 enqueued once (duplicates coalesced), source callback");
    Check(cb::GetStats().coalesced == 1, "coalesced counted");
    sn::Identity ip{};
    ip.type = 1;
    ip.size = 18;
    FakeDispatch(req, &ip, false);
    Check(cb::GetStats().bad_identity == 1 && !cb::PopSessionRequest(id, src), "a non-SteamID identity is refused");
    for (std::uint64_t i = 1; i <= 17; ++i) {
        sn::Identity x = Id(0x0110000100010000ull + i);
        FakeDispatch(req, &x, false);
    }
    unsigned popped = 0;
    while (cb::PopSessionRequest(id, src)) ++popped;
    Check(popped == 16 && cb::GetStats().dropped == 1, "16-entry queue, the 17th dropped");
    // Slot 0 Run(p, io, call) forwards to Run(p).
    reinterpret_cast<sn::CallbackRunIoFn>(req->vptr[0])(req, &g, false, 0);
    Check(cb::PopSessionRequest(id, src) && id == kGuest, "slot 0 overload");
    // 337: newest within the rate, at most 1 per 2 s.
    sn::JoinRequested a{}, b{};
    a.friend_id = kHost;
    std::snprintf(a.connect, sizeof(a.connect), "+mgscoop_join A");
    b.friend_id = kStranger;
    std::snprintf(b.connect, sizeof(b.connect), "+mgscoop_join B");
    cb::OnJoinRequested(&a, 10000);
    cb::OnJoinRequested(&b, 10500);
    sn::JoinRequested out{};
    Check(cb::TakeJoinRequest(out) && out.friend_id == kHost && !cb::TakeJoinRequest(out), "337 mailbox; the second within 2 s throttled");
    cb::OnJoinRequested(&b, 12100);
    Check(cb::TakeJoinRequest(out) && out.friend_id == kStranger && cb::GetStats().join_throttled == 1, "337 after 2 s");
    FakeDispatch(join, &a, false);
    Check(cb::TakeJoinRequest(out) && out.friend_id == kHost, "337 through the dispatch");
    // A 337 payload that cannot be read faults inside the handler: the fault
    // is counted and no lock is left held (the transport thread's mailbox and
    // queue, and the 1251 handler on the game's callback thread, must not block).
    cb::ResetForTest();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    auto* pages = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 2 * si.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old_protect = 0;
    Check(pages && VirtualProtect(pages + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old_protect), "guard page");
    FakeDispatch(join, pages + si.dwPageSize - 100, false);  // 100 readable bytes of 264
    Check(cb::GetStats().faults == 1, "an unreadable 337 payload faults inside the handler (counted)");
    HANDLE probe = CreateThread(nullptr, 0, &TakeBothProbe, nullptr, 0, nullptr);
    Check(probe && WaitForSingleObject(probe, 3000) == WAIT_OBJECT_0, "no lock left held after a handler fault");
    if (probe) CloseHandle(probe);
    FakeDispatch(req, &g, false);
    Check(cb::PopSessionRequest(id, src) && id == kGuest, "1251 still enqueues after the fault");
    VirtualFree(pages, 0, MEM_RELEASE);
    cb::ResetForTest();
}

// ---------------------------------------------- 4. the send ring (pure)
void TestRing() {
    static lab::SteamSendRing ring;
    std::uint8_t data[1200];
    for (unsigned i = 0; i < 64; ++i) {
        data[0] = static_cast<std::uint8_t>(i);
        const lab::WireChunk chunk{data, 10 + i};
        Check(ring.Push(lab::SteamEndpoint(kGuest), &chunk, 1, 10 + i), "push");
    }
    const lab::WireChunk one{data, 1};
    Check(!ring.Push(lab::SteamEndpoint(kGuest), &one, 1, 1) && ring.Depth() == 64, "full ring refuses");
    lab::Endpoint to{};
    const std::uint8_t* d = nullptr;
    std::size_t n = 0;
    for (unsigned i = 0; i < 64; ++i) {
        Check(ring.Peek(to, d, n) && d[0] == i && n == 10 + i && to.address == sn::AccountOf(kGuest), "FIFO order");
        ring.Pop();
    }
    Check(!ring.Peek(to, d, n) && ring.Depth() == 0, "empty");
    const lab::WireChunk two[2] = {{"ab", 2}, {"cd", 2}};
    Check(ring.Push(lab::SteamEndpoint(kHost), two, 2, 4) && ring.Peek(to, d, n) && n == 4 && std::memcmp(d, "abcd", 4) == 0, "chunks gathered");
    ring.Pop();
    Check(!ring.Push(lab::SteamEndpoint(kHost), two, 2, 0) && !ring.Push(lab::SteamEndpoint(kHost), two, 2, 1201), "size 0 / >1200 refused");
}

// ------------------------------------------- 5. accept policy (pure)
void TestAcceptPolicy() {
    using P = lab::SteamAcceptPolicy;
    using D = P::Decision;
    static P p;
    p.Reset(kHost);
    std::uint64_t t = 100000;
    Check(p.OnRequest(kHost, true, t) == D::self, "self refused");
    Check(p.OnRequest(kGuest, false, t) == D::not_accepting, "accepting off");
    // Rate: 4 new identities per second.
    unsigned accepted = 0;
    for (std::uint64_t i = 1; i <= 6; ++i) {
        const std::uint64_t id = 0x0110000100020000ull + i;
        if (p.OnRequest(id, true, t) == D::accept) {
            p.OnAccepted(id, t);
            ++accepted;
        }
    }
    Check(accepted == 4, "4 new identities per second");
    Check(p.OnRequest(0x0110000100020005ull, true, t + 10) == D::rate, "the 5th within the second: rate");
    // Non-peer cap: 8.
    t += 1000;
    for (std::uint64_t i = 5; i <= 8; ++i) {
        const std::uint64_t id = 0x0110000100020000ull + i;
        Check(p.OnRequest(id, true, t) == D::accept, "next second");
        p.OnAccepted(id, t);
    }
    Check(p.non_peer() == 8, "8 non-peer identities");
    t += 1000;
    Check(p.OnRequest(0x0110000100020009ull, true, t) == D::cap, "cap of 8");
    // A session peer leaves the table and is always accepted.
    p.SetPeer(0x0110000100020001ull, t);
    Check(p.non_peer() == 7 && p.OnRequest(0x0110000100020001ull, false, t) == D::known_peer, "peer: known, even when not accepting");
    // Idle hygiene: no link header for 15 s -> close + deny 60 s.
    p.OnMessage(0x0110000100020002ull, true, t);  // this one speaks link v2
    std::uint64_t expired = 0;
    Check(!p.NextExpired(t + 1000, expired), "nothing expired yet");
    unsigned closed = 0;
    while (p.NextExpired(t + 15000, expired)) {
        ++closed;
        Check(expired != 0x0110000100020002ull, "a link speaker is not closed at 15 s");
    }
    Check(closed == 6 && p.non_peer() == 1, "six silent identities closed");
    Check(p.Denied(0x0110000100020003ull, t + 15000) && p.OnRequest(0x0110000100020003ull, true, t + 16000) == D::denied, "denied for 60 s");
    Check(!p.Denied(0x0110000100020003ull, t + 75001), "deny expires");
    // A link speaker idle for 60 s is closed (no deny).
    Check(p.NextExpired(t + 61000, expired) && expired == 0x0110000100020002ull && !p.Denied(expired, t + 61000), "idle link speaker closed");
    // Poll-accept: the peer always; invited/beacon ids only while accepting; 4 Hz.
    p.AddKnown(kStranger, lab::KnownKind::invited, t);
    std::uint64_t ids[32]{};
    lab::KnownKind kinds[32]{};
    unsigned n = p.DuePolls(t + 100000, true, ids, kinds, 32);
    Check(n == 2 && ids[0] == 0x0110000100020001ull && ids[1] == kStranger && kinds[1] == lab::KnownKind::invited, "peer and invited id polled");
    n = p.DuePolls(t + 700000, true, ids, kinds, 32);
    Check(n == 1 && ids[0] == 0x0110000100020001ull, "invited ids expire after 10 min; the peer is polled");
    p.AddKnown(kStranger, lab::KnownKind::invited, t + 800000);
    Check(p.DuePolls(t + 800000, true, ids, kinds, 32) == 2, "invited again");
    Check(p.DuePolls(t + 800100, true, ids, kinds, 32) == 0, "4 Hz");
    n = p.DuePolls(t + 800300, false, ids, kinds, 32);
    Check(n == 1, "not accepting: only the peer");
    // Former peers: the last peer is polled, then closed after 60 s idle.
    p.SetPeer(kGuest, t + 800300);
    n = p.DuePolls(t + 800600, true, ids, kinds, 32);
    Check(n >= 2 && ids[0] == kGuest && ids[1] == 0x0110000100020001ull, "peer and last peer polled");
    bool former = false;
    for (unsigned i = 0; i < 8 && p.NextExpired(t + 900000, expired); ++i) former = former || expired == 0x0110000100020001ull;
    Check(former && p.last_peer() == 0, "former peer closed after 60 s idle");
    // Poll candidates (ids the host chose) use no rate token: polling an
    // invited friend at 4 Hz must not starve a code join's 1251.
    static P q;
    q.Reset(kHost);
    for (unsigned i = 0; i < 12; ++i) Check(q.CheckKnown(0x0110000100030000ull + i, true, 5000) == D::accept, "poll candidate: no rate");
    Check(q.OnRequest(kGuest, true, 5000) == D::accept, "a 1251 still has its rate token after the polls");
    Check(q.CheckKnown(kStranger, false, 5000) == D::not_accepting && q.CheckKnown(kHost, true, 5000) == D::self, "poll: accepting off, self");
    q.SetPeer(kStranger, 5000);
    Check(q.CheckKnown(kStranger, false, 5000) == D::known_peer, "poll: the peer even when not accepting");
}

void TestGuestFilter() {
    static lab::SteamGuestFilter f;
    f.Reset();
    Check(!f.OnForeign(kStranger, 1000) && !f.OnForeign(kStranger, 2000), "1st and 2nd foreign message");
    Check(f.OnForeign(kStranger, 3000), "3rd within 10 s closes");
    Check(!f.OnForeign(kStranger, 4000), "counting restarts");
    Check(!f.OnForeign(kHost, 1000) && !f.OnForeign(kHost, 12000) && !f.OnForeign(kHost, 13000), "spread over more than 10 s");
}

// --------------------------------- 2./3. Release exactly once; guest filter
void TestReleaseAndFilter() {
    static FakeNet net;
    net.Add(kHost);
    net.Add(kGuest);
    static FakeMessages002 guest_messages(&net, kGuest);
    static ApiBox box;
    box.api.messages = &guest_messages;
    box.api.on_fault = &OnFault;
    box.api.fault_ctx = &box;
    static Recorder rec;
    static lab::SteamTransport t;
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::guest;
    c.self_id = kGuest;
    c.host_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &box;
    const LONG released0 = g_released;
    Check(t.start(c, Sink(rec)), "guest transport starts");
    Check(WaitFor([&] { return rec.readies == 1; }, 2000), "ready");
    std::uint8_t data[1201]{};
    data[0] = 'M';
    unsigned injected = 0;
    const auto inject = [&](FakeMessage* m) {
        net.Inject(kGuest, m);
        ++injected;
    };
    inject(MakeMessage(Id(kHost), data, 100));                // delivered
    inject(MakeMessage(Id(kHost), data, 1201));               // oversize
    inject(MakeMessage(Id(kHost), data, 0));                  // zero
    inject(MakeMessage(Id(kHost), data, 50, 0, true));        // null data
    sn::Identity ip{};
    ip.type = 1;
    ip.size = 18;
    inject(MakeMessage(ip, data, 50));                        // bad identity on the Messages leg
    inject(MakeMessage(Id(kGuest), data, 50));                // ourselves
    for (int i = 0; i < 3; ++i) inject(MakeMessage(Id(kStranger), data, 60));  // foreign x3 -> close
    inject(MakeMessage(Id(kHost), data, 1200));               // delivered (max)
    Check(WaitFor([&] { return g_released - released0 == static_cast<LONG>(injected); }, 3000), "every message released exactly once");
    Sleep(50);
    Check(g_released - released0 == static_cast<LONG>(injected), "no double release");
    const lab::SteamTransportStats s = t.steam_stats();
    Check(rec.datagrams == 2 && s.rx == 2, "only the configured host's valid messages are delivered");
    Check(s.release == s.received && s.received == injected, "release == received");
    Check(s.bad_size == 3 && s.bad_identity == 2 && s.foreign == 3, "drop counters");
    Check(s.closed_foreign == 1 && net.Closes(kGuest, kStranger) == 1, "3 foreign messages in 10 s close that session");
    AcquireSRWLockExclusive(&rec.lock);
    const bool from_ok = rec.from.size() == 2 && rec.from[0].address == sn::AccountOf(kHost) && rec.from[0].port == 0x5354 &&
                         rec.seen[1].size() == 1200;
    ReleaseSRWLockExclusive(&rec.lock);
    Check(from_ok, "from = Endpoint{account, 0x5354}");
    // Sends: flags 0x21 on channel 0x4D47, in order; size limits.
    for (std::uint8_t i = 0; i < 20; ++i) {
        std::uint8_t d[64]{};
        d[0] = i;
        const lab::WireChunk chunk{d, sizeof(d)};
        Check(t.transmit(&chunk, 1), "transmit to the host");
    }
    const lab::WireChunk empty{data, 0}, big{data, 1201};
    Check(!t.transmit(&empty, 1) && !t.transmit(&big, 1), "size 0 / 1201 refused");
    const lab::WireChunk ok{data, 10};
    Check(!t.transmit_to(lab::Endpoint{0x7F000001, 47830}, &ok, 1), "a UDP endpoint is refused on the Messages leg");
    Check(WaitFor([&] { return net.GetStats().sends >= 20; }, 2000), "sent");
    const FakeNet::Stats ns = net.GetStats();
    Check(ns.bad_flags == 0 && ns.bad_channel == 0 && net.LastFlags() == 0x21, "flags 0x21, channel 0x4D47");
    t.stop();
    net.Drain();
}

// -------------------------------------------------- 8. fault latch
void TestFault() {
    static FakeNet net;
    net.Add(kHost);
    static FakeMessages002 m(&net, kHost);
    static ApiBox box;
    box.api.messages = &m;
    box.api.on_fault = &OnFault;
    box.api.fault_ctx = &box;
    static Recorder rec;
    static lab::SteamTransport t;
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::host;
    c.self_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &box;
    m.FaultAfter(5);
    Check(t.start(c, Sink(rec)), "start");
    // faulted() is set before on_failed runs: wait for both.
    Check(WaitFor([&] { return t.faulted() && rec.failed[0] != 0; }, 2000), "an access violation in steamclient latches the transport");
    const LONG calls = m.calls();
    Sleep(100);
    Check(m.calls() == calls, "no further Steam call after the fault");
    Check(box.faults == 1 && std::strcmp(rec.failed, "fault") == 0, "fault reported once (steam=disabled; reason=fault)");
    t.stop();
    // Another of our threads latched a fault (the process-wide halt): park without another call.
    static FakeMessages002 m2(&net, kHost);
    static ApiBox box2;
    static volatile long halt = 0;
    box2.api.messages = &m2;
    box2.api.halt = &halt;
    static Recorder rec2;
    static lab::SteamTransport t2;
    c.resolve_ctx = &box2;
    Check(t2.start(c, Sink(rec2)) && WaitFor([&] { return m2.calls() > 3; }, 2000), "running");
    InterlockedExchange(&halt, 1);
    Check(WaitFor([&] { return t2.faulted() && rec2.failed[0] != 0; }, 2000) && std::strcmp(rec2.failed, "fault") == 0, "the halt latch parks the transport");
    const LONG halted = m2.calls();
    Sleep(100);
    Check(m2.calls() == halted, "no Steam call after the halt");
    t2.stop();
}

// Resolve: pending, then failed -> on_failed(reason); never a fallback.
void TestResolveFailure() {
    static ApiBox box;
    box.result = -1;
    box.reason = "offline";
    static Recorder rec;
    static lab::SteamTransport t;
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::guest;
    c.self_id = kGuest;
    c.host_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &box;
    Check(t.start(c, Sink(rec)), "start");
    Check(WaitFor([&] { return std::strcmp(rec.failed, "offline") == 0; }, 2000) && !t.ready() && rec.readies == 0,
          "a disabled API refuses (link=disabled; reason=steam_offline)");
    t.stop();
    // Missing Messages for the Messages leg.
    static ApiBox empty;
    static Recorder rec2;
    static lab::SteamTransport t2;
    c.resolve_ctx = &empty;
    Check(t2.start(c, Sink(rec2)) && WaitFor([&] { return std::strcmp(rec2.failed, "iface_missing") == 0; }, 2000), "iface_missing");
    t2.stop();
}

// -------------------------------------------------- 11. shutdown
void TestShutdown() {
    static FakeNet net;
    net.Add(kHost);
    net.Add(kGuest);
    static FakeMessages002 m(&net, kGuest);
    static ApiBox box;
    box.api.messages = &m;
    static Recorder rec;
    static lab::SteamTransport t;
    rec.transport = &t;
    rec.close_on_shutdown = true;
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::guest;
    c.self_id = kGuest;
    c.host_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &box;
    Check(t.start(c, Sink(rec)) && WaitFor([&] { return t.ready(); }, 2000), "ready");
    const unsigned sends = net.GetStats().sends;
    t.request_shutdown();
    Check(WaitFor([&] { return net.Closes(kGuest, kHost) == 1; }, 2000), "one CloseSessionWithUser");
    Sleep(20);
    const LONG calls = m.calls();
    Check(rec.shutdowns == 1 && net.GetStats().sends == sends + 1, "exactly one CLOSE sent");
    Sleep(150);
    Check(m.calls() == calls && net.Closes(kGuest, kHost) == 1, "then zero Steam calls");
    const std::uint8_t d[8]{};
    const lab::WireChunk chunk{d, sizeof(d)};
    t.transmit(&chunk, 1);
    Sleep(30);
    Check(m.calls() == calls, "nothing sent after parking");
    t.stop();
    net.Drain();
}

// ----------------------------------------- 6. two link v2 sessions end to end
struct End {
    lab::SteamTransport transport;
    lab::LinkHarnessEnd link;
};
End g_host_end, g_guest_end, g_other_end;
void EndDatagram(void* ctx, const std::uint8_t* d, std::size_t n, const lab::Endpoint& f) noexcept {
    static_cast<End*>(ctx)->link.OnDatagram(d, n, f, Now());
}
void EndIdle(void* ctx) noexcept { static_cast<End*>(ctx)->link.OnIdle(Now()); }
bool EndSend(void* ctx, const lab::Endpoint& to, const std::uint8_t* d, std::size_t n) noexcept {
    const lab::WireChunk chunk{d, n};
    return static_cast<End*>(ctx)->transport.transmit_to(to, &chunk, 1, true);
}
lab::SteamSink EndSink(End& e) {
    lab::SteamSink s{};
    s.on_datagram = &EndDatagram;
    s.on_idle = &EndIdle;
    s.ctx = &e;
    return s;
}

void TestEndToEnd() {
    static FakeNet net;
    g_net = &net;
    net.Add(kHost);
    net.Add(kGuest);
    net.Add(kStranger);
    net.latency_ms = 3;
    net.reorder_permille = 20;
    net.dup_permille = 10;
    static FakeMessages002 mh(&net, kHost), mg(&net, kGuest), mx(&net, kStranger);
    static FakeUtils004 utils;
    static ApiBox bh, bg, bx;
    bh.api.messages = &mh;
    bh.api.utils = &utils;
    bg.api.messages = &mg;
    bx.api.messages = &mx;
    char secret[link::kSecretChars];
    std::memcpy(secret, "ABCDE12345FGHJK67890", 20);
    const std::uint64_t t0 = Now();
    g_host_end.link.Start(link::Role::host, true, lab::Endpoint{}, secret, "0.12.0", &EndSend, &g_host_end, t0);
    g_guest_end.link.Start(link::Role::guest, true, lab::SteamEndpoint(kHost), secret, "0.12.0", &EndSend, &g_guest_end, t0);
    lab::SteamTransportConfig ch{}, cg{};
    ch.role = lab::SteamRole::host;
    ch.self_id = kHost;
    ch.resolve = &Resolve;
    ch.resolve_ctx = &bh;
    ch.pop_request = &PopHostRequest;
    ch.warm_relay = true;
    ch.log = &CountLog;
    g_accept_lines = 0;
    cg.role = lab::SteamRole::guest;
    cg.self_id = kGuest;
    cg.host_id = kHost;
    cg.resolve = &Resolve;
    cg.resolve_ctx = &bg;
    Check(g_host_end.transport.start(ch, EndSink(g_host_end)) && g_guest_end.transport.start(cg, EndSink(g_guest_end)), "transports");
    Check(WaitFor([&] { return g_host_end.link.Connected() && g_guest_end.link.Connected(); }, 3000), "handshake over Steam Messages");
    Check(utils.pings == 1 && utils.last_age == sn::kWarmRelayMaxAge, "relay warm-up once (CheckPingDataUpToDate(1e10f))");
    Check(g_host_end.transport.steam_stats().accepted >= 1, "the host accepted the guest's session (1251 -> AcceptSessionWithUser)");
    const lab::Endpoint peer = g_host_end.link.Peer();
    Check(peer.address == sn::AccountOf(kGuest) && peer.port == 0x5354, "host locked onto Endpoint{guest account, 0x5354}");
    g_host_end.transport.set_peer(kGuest);  // what partner_net does on the commit
    // 500 FRAMEs guest -> host.
    for (std::uint32_t i = 0; i < 500; ++i) {
        while (g_guest_end.transport.steam_stats().ring_depth > 32) Sleep(1);
        g_guest_end.link.SendFrame(i, Now());
    }
    Check(WaitFor([&] { return g_host_end.link.GetCounters().frames_rx >= 500; }, 5000), "500 FRAMEs delivered");
    Check(g_host_end.link.GetCounters().frames_bad == 0, "no corrupted FRAME");
    // 64 reliable messages on channel 2, host -> guest, in order.
    for (std::uint32_t i = 0; i < 64;) {
        const auto r = g_host_end.link.SendReliable(2, i);
        if (r == link::ReliableResult::queued) ++i;
        else Sleep(5);
    }
    Check(WaitFor([&] { return g_guest_end.link.GetCounters().rel_rx == 64; }, 8000) && g_guest_end.link.GetCounters().rel_bad == 0,
          "64 reliable messages in order");
    // A world DATAGRAM.
    Check(g_host_end.link.SendDatagram(1, 7, Now()), "datagram sent");
    Check(WaitFor([&] { return g_guest_end.link.GetCounters().datagrams_rx >= 1; }, 2000), "DATAGRAM delivered");
    // Route at 1 Hz from GetSessionConnectionInfo (relayed in the fake), on both ends.
    Check(WaitFor([&] { return g_host_end.transport.route() == sn::Route::relay && g_guest_end.transport.route() == sn::Route::relay; }, 2500),
          "route=relay");
    Check(!mh.quick_seen(), "quick status always null");
    // A broken Steam session: the fake fails sends without 0x21; with it the
    // session restarts and the host must accept again (poll or callback).
    const unsigned restarts = net.Restarts();
    net.Break(kGuest, kHost);
    const std::uint64_t frames_before = g_host_end.link.GetCounters().frames_rx;
    for (std::uint32_t i = 1000; i < 1100; ++i) {
        g_guest_end.link.SendFrame(i, Now());
        Sleep(1);
    }
    Check(WaitFor([&] { return g_host_end.link.GetCounters().frames_rx >= frames_before + 90; }, 5000), "frames flow again after the restart");
    Check(net.Restarts() == restarts + 1, "AutoRestartBrokenSession restarted the session");
    Check(WaitFor([&] { return g_host_end.link.Connected() && g_guest_end.link.Connected(); }, 3000), "the link is up after the restart");
    // Another SteamID replaying the guest's traffic is never rebound to.
    g_other_end.link.Start(link::Role::guest, true, lab::SteamEndpoint(kHost), secret, "0.12.0", &EndSend, &g_other_end, Now());
    lab::SteamTransportConfig cx{};
    cx.role = lab::SteamRole::guest;
    cx.self_id = kStranger;
    cx.host_id = kHost;
    cx.resolve = &Resolve;
    cx.resolve_ctx = &bx;
    Check(g_other_end.transport.start(cx, EndSink(g_other_end)), "second guest transport");
    // A second guest with the right code while the first is live: REJECT(busy), verified.
    Check(WaitFor([&] { return g_other_end.link.GetCounters().rejects >= 1; }, 5000) && g_other_end.link.GetCounters().reject_verified,
          "busy REJECT (verified) across Steam");
    Check(g_host_end.link.Peer().address == sn::AccountOf(kGuest), "no rebound to another SteamID while live");
    g_other_end.transport.stop();
    // A wrong code: an unauthenticated bad_code hint, never a session.
    char wrong[link::kSecretChars];
    std::memcpy(wrong, "ZZZZZYYYYYXXXXXWWWWW", 20);
    static End wrong_end;
    wrong_end.link.Start(link::Role::guest, true, lab::SteamEndpoint(kHost), wrong, "0.12.0", &EndSend, &wrong_end, Now());
    Check(wrong_end.transport.start(cx, EndSink(wrong_end)), "wrong-code transport");
    Check(WaitFor([&] { return wrong_end.link.GetCounters().rejects >= 1; }, 5000) && !wrong_end.link.GetCounters().reject_verified &&
              !wrong_end.link.Connected(),
          "wrong code: bad_code hint (verified=0), no session");
    Check(g_host_end.link.Peer().address == sn::AccountOf(kGuest) && g_host_end.link.Connected(), "host unchanged");
    wrong_end.transport.stop();
    const lab::SteamTransportStats hs = g_host_end.transport.steam_stats(), gs = g_guest_end.transport.steam_stats();
    g_host_end.transport.stop();
    g_guest_end.transport.stop();
    Check(hs.release == hs.received && gs.release == gs.received, "release == received on both ends");
    Check(hs.faults == 0 && gs.faults == 0 && hs.bad_identity == 0, "no fault, no bad identity");
    // Accepts: the guest, its restarted session and the second guest (1251
    // each); the 4 Hz polls of the live peer (true every time) add none.
    Check(g_accept_lines >= 2 && g_accept_lines <= 4 && net.GetStats().accepts_existing >= 2, "steam=accept logged per new session, not per poll");
    net.Drain();
    g_net = nullptr;
}

// Host accept through the transport: the 1251 queue, accepting=off, poll of an invited id.
void TestHostAccept() {
    static FakeNet net;
    g_net = &net;
    net.Add(kHost);
    net.Add(kGuest);
    net.Add(kStranger);
    static FakeMessages002 mh(&net, kHost), mx(&net, kStranger);
    static ApiBox bh;
    bh.api.messages = &mh;
    static Recorder rec;
    static lab::SteamTransport t;
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::host;
    c.self_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &bh;
    c.pop_request = &PopHostRequest;
    c.accepting = false;
    Check(t.start(c, Sink(rec)) && WaitFor([&] { return t.ready(); }, 2000), "host ready");
    std::uint8_t hello[64]{};
    sn::Identity host = Id(kHost);
    mx.SendMessageToUser(host, hello, sizeof(hello), sn::kMsgFlags, sn::kLinkChannel);
    Sleep(150);
    Check(rec.datagrams == 0 && t.steam_stats().accept_refused >= 1, "accepting=off: no accept");
    t.set_accepting(true);
    mx.SendMessageToUser(host, hello, sizeof(hello), sn::kMsgFlags, sn::kLinkChannel);  // re-request (fake posts once)
    t.add_known(kStranger, lab::KnownKind::invited);
    Check(WaitFor([&] { return rec.datagrams >= 2; }, 2000), "invited id poll-accepted; the held messages arrive");
    Check(t.steam_stats().accepted >= 1, "accepted counted");
    t.stop();
    net.Drain();
    g_net = nullptr;
}

// Invited friends who are not joining are polled at 4 Hz without using the
// 1251 rate tokens, and an established session (AcceptSessionWithUser true
// on every poll) is accepted, counted and logged once.
void TestPollKeepsRate() {
    static FakeNet net;
    g_net = &net;
    constexpr std::uint64_t kJoiner = 0x0110000100005005ull;
    net.Add(kHost);
    net.Add(kJoiner);
    static FakeMessages002 mh(&net, kHost), mj(&net, kJoiner);
    static ApiBox bh;
    bh.api.messages = &mh;
    static Recorder rec;
    static lab::SteamTransport t;
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::host;
    c.self_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &bh;
    c.pop_request = &PopHostRequest;
    c.log = &CountLog;
    g_accept_lines = 0;
    Check(t.start(c, Sink(rec)) && WaitFor([&] { return t.ready(); }, 2000), "host ready (poll rate)");
    for (std::uint64_t i = 1; i <= 6; ++i) t.add_known(0x0110000100004000ull + i, lab::KnownKind::invited);
    Sleep(600);  // at least two poll rounds of six invited friends who never connect
    std::uint8_t hello[64]{};
    const sn::Identity host = Id(kHost);
    mj.SendMessageToUser(host, hello, sizeof(hello), sn::kMsgFlags, sn::kLinkChannel);  // one 1251 (the fake posts it once)
    Check(WaitFor([&] { return rec.datagrams >= 1; }, 2000), "a code join is accepted while invited friends are polled");
    Check(t.steam_stats().accept_refused == 0, "no rate refusal caused by polls");
    t.set_peer(kJoiner);
    Check(WaitFor([&] { return net.GetStats().accepts_existing >= 4; }, 3000), "the live session answers the 4 Hz poll with true");
    Check(g_accept_lines == 1 && t.steam_stats().accepted == 1, "one accept counted and logged for the live session");
    t.stop();
    net.Drain();
    g_net = nullptr;
}

// ----------------------------------------- the connection leg (self-test rig)
void TestConnectionLeg() {
    static FakeSockets012 sockets;
    static ApiBox box;
    box.api.sockets = &sockets;
    std::uint32_t a = 0, b = 0;
    Check(static_cast<sn::ISteamNetworkingSockets012Abi&>(sockets).CreateSocketPair(&a, &b, false, nullptr, nullptr) && sockets.nulls_ &&
              !sockets.loopback_,
          "CreateSocketPair(false, null, null)");
    static End ea, eb;
    char secret[link::kSecretChars];
    std::memcpy(secret, "0123456789ABCDEFGHJK", 20);
    ea.link.Start(link::Role::host, false, lab::Endpoint{}, secret, "selftest", &EndSend, &ea, Now());
    eb.link.Start(link::Role::guest, false, lab::kConnectionEndpoint, secret, "selftest", &EndSend, &eb, Now());
    lab::SteamTransportConfig ca{}, cb{};
    ca.role = lab::SteamRole::host;
    cb.role = lab::SteamRole::guest;
    ca.leg = cb.leg = lab::SteamLeg::connection;
    ca.conn = a;
    cb.conn = b;
    ca.resolve = cb.resolve = &Resolve;
    ca.resolve_ctx = cb.resolve_ctx = &box;
    Check(ea.transport.start(ca, EndSink(ea)) && eb.transport.start(cb, EndSink(eb)), "pair transports");
    Check(WaitFor([&] { return ea.link.Connected() && eb.link.Connected(); }, 2000), "handshake on the connection leg");
    for (std::uint32_t i = 0; i < 500; ++i) {
        while (eb.transport.steam_stats().ring_depth > 32) Sleep(1);
        eb.link.SendFrame(i, Now());
    }
    Check(WaitFor([&] { return ea.link.GetCounters().frames_rx == 500; }, 3000), "500 frames on the pair");
    Check(sockets.last_flags_ == sn::kConnFlags, "connection sends use flags 1");
    const auto sa = ea.transport.steam_stats();
    ea.transport.stop();
    eb.transport.stop();
    Check(sa.release == sa.received && sa.bad_identity == 0, "IP-localhost identities accepted; released once");
    // A connection message with a SteamID identity is refused on this leg.
    sockets.Clear();
    sockets.ip_type_ = sn::kIdentitySteamId;
    static End ec, ed;
    ec.link.Start(link::Role::host, false, lab::Endpoint{}, secret, "selftest", &EndSend, &ec, Now());
    ed.link.Start(link::Role::guest, false, lab::kConnectionEndpoint, secret, "selftest", &EndSend, &ed, Now());
    Check(ec.transport.start(ca, EndSink(ec)) && ed.transport.start(cb, EndSink(ed)), "pair transports 2");
    Check(WaitFor([&] { return ec.transport.steam_stats().bad_identity >= 1; }, 2000) && !ec.link.Connected(), "wrong identity type refused");
    ec.transport.stop();
    ed.transport.stop();
    sockets.Clear();
    sockets.ip_type_ = sn::kIdentityIp;
}

// ------------------------------------------- the ABI slot indices
template <typename T>
const void* Slot(T* object, unsigned index) {
    return (*reinterpret_cast<const void* const* const*>(object))[index];
}
void TestSlots() {
    static FakeNet net;
    net.Add(kHost);
    static FakeMessages002 m(&net, kHost);
    sn::ISteamNetworkingMessages002Abi* abi = &m;
    // Call slots 1 and 2 through raw vtable indices, as `call [vptr+8N]` does.
    void* out[4]{};
    using ReceiveFn = std::int32_t (*)(void*, std::int32_t, void**, std::int32_t);
    const LONG calls = m.calls();
    reinterpret_cast<ReceiveFn>(Slot(abi, 1))(abi, sn::kLinkChannel, out, 4);
    Check(m.calls() == calls + 1, "Messages slot 1 is ReceiveMessagesOnChannel");
    using InfoFn = std::int32_t (*)(void*, const sn::Identity*, void*, void*);
    const sn::Identity g = Id(kGuest);
    std::uint8_t info[sn::kConnInfoSize]{};
    Check(reinterpret_cast<InfoFn>(Slot(abi, 5))(abi, &g, info, nullptr) == sn::kConnConnected, "Messages slot 5 is GetSessionConnectionInfo");
    static FakeSockets012 sockets;
    sn::ISteamNetworkingSockets012Abi* s = &sockets;
    using PairFn = bool (*)(void*, std::uint32_t*, std::uint32_t*, bool, const sn::Identity*, const sn::Identity*);
    std::uint32_t a = 0, b = 0;
    Check(reinterpret_cast<PairFn>(Slot(s, sn::kSocketsCreatePair))(s, &a, &b, true, nullptr, nullptr) && a == 11 && sockets.loopback_,
          "Sockets slot 19 is CreateSocketPair");
    using CloseFn = bool (*)(void*, std::uint32_t, std::int32_t, const char*, bool);
    const unsigned closed = sockets.closed_;
    reinterpret_cast<CloseFn>(Slot(s, sn::kSocketsCloseConnection))(s, 11, 0, nullptr, false);
    Check(sockets.closed_ == closed + 1, "Sockets slot 5 is CloseConnection");
    static FakeUtils004 u;
    sn::ISteamNetworkingUtils004Abi* ua = &u;
    using RelayFn = std::int32_t (*)(void*, void*);
    Check(reinterpret_cast<RelayFn>(Slot(ua, sn::kUtilsRelayStatus))(ua, nullptr) == 100, "Utils slot 1 is GetRelayNetworkStatus");
}

// ------------------------------------------- shared state (UI, report)
void TestState() {
    namespace st = mgscoop::steam_state;
    st::ResetForTest();
    st::SelftestReport r{};
    char line[161]{};
    Check(st::FormatStatusLine(r, line, sizeof(line)) && std::strcmp(line, "steam_selftest;verdict=off") == 0, "off");
    r.configured = true;
    std::snprintf(r.stage, sizeof(r.stage), "P");
    Check(st::FormatStatusLine(r, line, sizeof(line)) && std::strcmp(line, "steam_selftest;verdict=pending;stage=P") == 0, "pending");
    std::snprintf(r.verdict, sizeof(r.verdict), "refused");
    std::snprintf(r.reason, sizeof(r.reason), "fake_identity");
    Check(st::FormatStatusLine(r, line, sizeof(line)) && std::strncmp(line, "steam_selftest;verdict=refused;reason=fake_identity", 51) == 0,
          "refused line matches the scenario's expect");
    std::snprintf(r.verdict, sizeof(r.verdict), "pass");
    r.reason[0] = 0;
    r.pair = st::PairResult{true, 4, 500, 500, 64, 64, 1, 564, 564, 0};
    r.relay = 100;
    Check(st::FormatStatusLine(r, line, sizeof(line)) &&
              std::strcmp(line, "steam_selftest;verdict=pass;reason=;stage=P;pair=500/500;rel=64/64;release=564/564;relay=100;fn=none") == 0,
          "pass line");
    // Stage F/D (section 20): the fn verdict on the status line.
    r.fn.ran = true;
    r.fn.set204 = r.fn.reset204 = r.fn.conn_ok = r.fn.thread_ok = r.fn.d_install = r.fn.d_uninstall = true;
    r.fn.status_cb = 1;
    r.fn.old_state = 3;
    r.fn.new_state = 4;
    Check(std::strcmp(st::FnVerdict(r.fn), "ok") == 0, "fn ok");
    r.fn.pre_pump = 1;
    Check(std::strcmp(st::FnVerdict(r.fn), "fail") == 0, "fn: a callback before our pump fails");
    r.fn.pre_pump = 0;
    r.fn.thread_ok = false;
    Check(std::strcmp(st::FnVerdict(r.fn), "fail") == 0, "fn: another thread fails");
    r.fn.thread_ok = true;
    Check(st::FormatStatusLine(r, line, sizeof(line)) && std::strstr(line, ";relay=100;fn=ok") && std::strlen(line) <= 160, "fn=ok on the line");
    r.fn = st::FnResult{};
    r.fn.skipped = true;
    Check(st::FormatStatusLine(r, line, sizeof(line)) && std::strstr(line, ";fn=skip"), "fn=skip");
    r.fn = st::FnResult{};
    st::PublishReport(r);
    Check(st::GetReport().pair.frames_rx == 500, "report published");
    // UI mailboxes.
    bool accepting = true;
    Check(!st::TakeAccepting(accepting), "no request");
    st::RequestAccepting(false);
    Check(st::TakeAccepting(accepting) && !accepting && !st::TakeAccepting(accepting), "accept toggle taken once");
    std::uint64_t id = 0;
    st::RequestInvite(kGuest);
    Check(st::TakeInvite(id) && id == kGuest && !st::TakeInvite(id), "invite taken once");
    char paste[st::kPasteCapacity]{};
    Check(st::RequestJoin("MGS2-X") && st::TakeJoin(paste) && std::strcmp(paste, "MGS2-X") == 0 && !st::TakeJoin(paste) && paste[0] == 0,
          "paste taken once and cleared");
    char big[200];
    std::memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    Check(!st::RequestJoin(big), "oversize paste refused");
    char out[80]{};
    Check(!st::BuildCode(out, sizeof(out)), "no provider: no code");
    st::NoteInvitePage(1234);
    Check(st::InvitePageMs() == 1234, "invite page heartbeat");
    st::ResetForTest();
}
// ------------------------------------- 12. function-pointer dispatch (section 20)
// One fake "process": SetConfigValue stores the pointers, FakeNet queues
// 1251 / 1252 for them, and only RunCallbacks dispatches (on the caller).
void TestDispatchFunctions() {
    namespace sd = mgscoop::steam_dispatch;
    namespace cb = mgscoop::steam_cb;
    sd::ResetForTest();
    cb::ResetForTest();
    // Variants: who installs what, who keeps the Init wrapper.
    Check(sd::InstallBits(sd::Variant::both, true) == 3 && sd::InstallBits(sd::Variant::both, false) == 2 &&
              sd::InstallBits(sd::Variant::fnptr, true) == 3 && sd::InstallBits(sd::Variant::fnptr, false) == 2 &&
              sd::InstallBits(sd::Variant::callback, true) == 0 && sd::InstallBits(sd::Variant::poll, false) == 0,
          "host installs 204+205, guest 205, callback/poll none");
    Check(sd::UsesInitWrapper(sd::Variant::both, true) && sd::UsesInitWrapper(sd::Variant::callback, false) &&
              !sd::UsesInitWrapper(sd::Variant::fnptr, true) && sd::UsesInitWrapper(sd::Variant::fnptr, false) &&
              !sd::UsesInitWrapper(sd::Variant::poll, true) && !sd::UsesInitWrapper(sd::Variant::poll, false),
          "Init wrapper: both/callback, the fnptr guest (337), never poll");
    Check(std::strcmp(sd::VariantName(sd::Variant::fnptr), "fnptr") == 0, "variant names");
    // 1251 through the function: steam_cb with source fnptr.
    sn::Identity g = Id(kGuest);
    sd::SessionRequestFn(&g);
    std::uint64_t id = 0;
    std::uint8_t src = 0;
    Check(cb::PopSessionRequest(id, src) && id == kGuest && src == cb::kSourceFnptr && sd::GetStats().req_fn == 1 && cb::GetStats().req_fn == 1,
          "a function-pointer 1251 enqueues with source fnptr");
    // Both sources while queued: one entry, both bits.
    cb::OnSessionRequest(&g);
    sd::SessionRequestFn(&g);
    Check(cb::PopSessionRequest(id, src) && src == (cb::kSourceCallback | cb::kSourceFnptr) && std::strcmp(cb::SourceName(src), "fnptr+callback") == 0 &&
              !cb::PopSessionRequest(id, src),
          "a duplicate from both sources coalesces (fnptr+callback)");
    Check(cb::GetStats().req_cb == 1 && cb::GetStats().req_fn == 2, "per-source counters");
    Check(std::strcmp(cb::SourceName(cb::kSourceCallback), "callback") == 0 && std::strcmp(cb::SourceName(0), "none") == 0, "source names");
    // A bad identity is counted, never queued.
    sn::Identity ip{};
    ip.type = sn::kIdentityIp;
    ip.size = sn::kIdentityIpSize;
    sd::SessionRequestFn(&ip);
    Check(sd::GetStats().bad_payload == 1 && cb::GetStats().bad_identity == 1 && !cb::PopSessionRequest(id, src), "bad identity counted");
    // 1252: the newest failure in a mailbox, the debug text sanitized.
    std::uint8_t info[sn::kSessionFailedSize]{};
    const sn::Identity host = Id(kHost);
    std::memcpy(info + sn::kConnInfoIdentity, &host, sizeof(host));
    const std::int32_t state = sn::kConnProblemDetectedLocally, end = 5003;
    std::memcpy(info + sn::kConnInfoState, &state, 4);
    std::memcpy(info + sn::kConnInfoEndReason, &end, 4);
    std::memcpy(info + sn::kConnInfoEndDebug, "Timed out \"x\"\x01", 14);
    sd::SessionFailedFn(info);
    sd::SessionFailed f{};
    Check(sd::TakeSessionFailed(f) && f.peer == kHost && f.end_reason == 5003 && f.state == 5 && std::strcmp(f.debug, "Timed out ?x??") == 0 &&
              f.sequence == 1 && !sd::TakeSessionFailed(f),
          "1252 mailbox: peer, state, end reason, sanitized debug, taken once");
    // The debug text is logged: IPv4 addresses are masked (a.b.x.x), nothing else changes.
    std::memset(info + sn::kConnInfoEndDebug, 0, sn::kConnInfoEndDebugSize);
    std::memcpy(info + sn::kConnInfoEndDebug, "No route to 203.0.113.77:27015 via 10.1.2.3", 44);
    sd::SessionFailedFn(info);
    Check(sd::TakeSessionFailed(f) && std::strcmp(f.debug, "No route to 203.0.x.x:27015 via 10.1.x.x") == 0 && f.sequence == 2,
          "1252 debug: IPv4 masked");
    char masked[32]{};
    sd::MaskAddresses("v1.2.3.4.5 9.9.9.9 1234.1.1.1 1.2.3", masked, sizeof(masked));
    Check(std::strcmp(masked, "v1.2.3.4.5 9.9.x.x 1234.1.1.1 1") == 0, "MaskAddresses: only whole dotted quads, cut to capacity");
    sd::MaskAddresses(nullptr, masked, sizeof(masked));
    Check(masked[0] == 0, "MaskAddresses: null input");
    // 1221 (self-test record): conn, new state, old state, the calling thread.
    std::uint8_t status[sn::kStatusChangedSize]{};
    const std::uint32_t conn = 7;
    const std::int32_t closed = sn::kConnClosedByPeer, connected = sn::kConnConnected;
    std::memcpy(status + sn::kStatusChangedConn, &conn, 4);
    std::memcpy(status + sn::kStatusChangedInfo + sn::kConnInfoState, &closed, 4);
    std::memcpy(status + sn::kStatusChangedOldState, &connected, 4);
    sd::ConnectionStatusFn(status);
    const sd::StatusRecord record = sd::GetStatusRecord();
    Check(record.calls == 1 && record.entries[0].conn == 7 && record.entries[0].new_state == 4 && record.entries[0].old_state == 3 &&
              record.entries[0].thread == GetCurrentThreadId(),
          "1221 record (conn, 3 -> 4, thread)");
    // A fault inside one of our functions is caught and counted.
    sd::SessionRequestFn(reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10)));
    Check(sd::GetStats().faults == 1, "a fault inside the function is caught");
    sd::ResetForTest();
    cb::ResetForTest();
}


// Transport log capture (section 20): the dispatch and accept lines.
SRWLOCK g_log_lock = SRWLOCK_INIT;
std::vector<std::string> g_log_lines;
void CaptureLog(const char* line) noexcept {
    AcquireSRWLockExclusive(&g_log_lock);
    g_log_lines.emplace_back(line ? line : "");
    ReleaseSRWLockExclusive(&g_log_lock);
}
bool Logged(const char* needle) {
    AcquireSRWLockExclusive(&g_log_lock);
    bool found = false;
    for (const auto& l : g_log_lines) found = found || l.find(needle) != std::string::npos;
    ReleaseSRWLockExclusive(&g_log_lock);
    return found;
}
unsigned LoggedCount(const char* needle) {
    AcquireSRWLockExclusive(&g_log_lock);
    unsigned n = 0;
    for (const auto& l : g_log_lines) n += l.find(needle) != std::string::npos ? 1u : 0u;
    ReleaseSRWLockExclusive(&g_log_lock);
    return n;
}
void ClearLog() {
    AcquireSRWLockExclusive(&g_log_lock);
    g_log_lines.clear();
    ReleaseSRWLockExclusive(&g_log_lock);
}

// The play path: the host installs 204+205 once ready, pumps on its thread,
// accepts a function-pointer request (no Init-wrapper callback in this net),
// merges a second source into one accept, resets the pointers on stop() and
// before CloseSessionWithUser at shutdown; the guest installs only 205.
void TestDispatchTransport() {
    namespace sd = mgscoop::steam_dispatch;
    namespace cb = mgscoop::steam_cb;
    sd::ResetForTest();
    cb::ResetForTest();
    ClearLog();
    static FakeNet net;
    static FakeDispatchHub hub_h, hub_g;
    constexpr std::uint64_t kJoiner2 = 0x0110000100006006ull;
    net.Add(kHost);
    net.Add(kGuest);
    net.Add(kStranger);
    net.Add(kJoiner2);
    net.SetHub(kHost, &hub_h, false);
    static FakeMessages002 mh(&net, kHost), mx(&net, kStranger), mj(&net, kJoiner2), mg(&net, kGuest);
    static FakeSockets012 sh;
    static FakeUtils004 uh;
    sh.hub = &hub_h;
    uh.hub = &hub_h;
    static ApiBox bh;
    bh.api.messages = &mh;
    bh.api.sockets = &sh;
    bh.api.utils = &uh;
    bh.api.dispatch_refused = nullptr;
    static Recorder rec;
    static lab::SteamTransport t;
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::host;
    c.self_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &bh;
    c.pop_request = &cb::PopSessionRequest;
    c.dispatch = sd::InstallBits(sd::Variant::both, true);
    c.log = &CaptureLog;
    Check(t.start(c, Sink(rec)) && WaitFor([&] { return t.ready() && t.dispatch_state() == lab::DispatchState::on; }, 2000),
          "host ready, function pointer on");
    Check(hub_h.Global(sn::kConfigMessagesSessionRequest) == &sd::SessionRequestFn &&
              hub_h.Global(sn::kConfigMessagesSessionFailed) == &sd::SessionFailedFn && hub_h.calls.size() == 2 &&
              hub_h.calls[0].scope == sn::kConfigScopeGlobal && hub_h.calls[0].object == 0 && hub_h.calls[0].type == sn::kConfigTypePtr,
          "the host installs 204 and 205 (Global, Ptr)");
    Check(Logged("steam=dispatch; fnptr=on; ids=1251+1252"), "fnptr=on logged");
    std::uint8_t hello[64]{};
    mx.SendMessageToUser(Id(kHost), hello, sizeof(hello), sn::kMsgFlags, sn::kLinkChannel);
    Check(WaitFor([&] { return rec.datagrams >= 1; }, 2000), "a function-pointer request is accepted; the held message arrives");
    Check(hub_h.runs > 0 && hub_h.run_thread != GetCurrentThreadId(), "RunCallbacks runs on the transport thread");
    char masked[24]{};
    code::FormatPeer(kStranger, masked);
    Check(Logged((std::string("steam=accept; peer=") + masked + "; via=fnptr\r\n").c_str()), "accept via=fnptr");
    // Both sources for one joiner (the Init wrapper's 1251 and the function
    // pointer): one accept, whichever source the transport pops first (while
    // both are queued it is one entry, via=fnptr+callback: TestDispatchFunctions).
    net.SetHub(kHost, &hub_h, true);
    mj.SendMessageToUser(Id(kHost), hello, sizeof(hello), sn::kMsgFlags, sn::kLinkChannel);
    Check(WaitFor([&] { return rec.datagrams >= 2; }, 2000), "the second joiner is accepted");
    Check(WaitFor([&] { return mgscoop::steam_cb::GetStats().req_cb >= 1 && sd::GetStats().req_fn >= 2; }, 2000), "both sources reported it");
    code::FormatPeer(kJoiner2, masked);
    const std::string accept_joiner = std::string("steam=accept; peer=") + masked + "; via=";
    Check(WaitFor([&] { return LoggedCount(accept_joiner.c_str()) == 1; }, 1000) && t.steam_stats().accepted == 2,
          "a duplicate from both sources gives one accept");
    // The other source reporting an accepted request later is not a new accept.
    const std::uint64_t dup = t.steam_stats().accept_dup;
    const sn::Identity late = Id(kJoiner2);
    cb::OnSessionRequest(&late);
    Check(WaitFor([&] { return t.steam_stats().accept_dup > dup; }, 2000) && LoggedCount(accept_joiner.c_str()) == 1 &&
              t.steam_stats().accepted == 2,
          "a late second source is a dup, not a new accept");
    t.stop();
    Check(!hub_h.AnySet() && sd::Installed() == 0 && Logged("steam=dispatch; fnptr=off; reason=stopping"), "stop() resets 204 and 205");
    // Guest: 205 only.
    ClearLog();
    sd::ResetForTest();
    net.SetHub(kGuest, &hub_g, false);
    static FakeSockets012 sg;
    static FakeUtils004 ug;
    sg.hub = &hub_g;
    ug.hub = &hub_g;
    static ApiBox bg;
    bg.api.messages = &mg;
    bg.api.sockets = &sg;
    bg.api.utils = &ug;
    bg.api.dispatch_refused = nullptr;
    static Recorder rg;
    static lab::SteamTransport tg;
    lab::SteamTransportConfig cg{};
    cg.role = lab::SteamRole::guest;
    cg.self_id = kGuest;
    cg.host_id = kHost;
    cg.resolve = &Resolve;
    cg.resolve_ctx = &bg;
    cg.dispatch = sd::InstallBits(sd::Variant::both, false);
    cg.log = &CaptureLog;
    Check(tg.start(cg, Sink(rg)) && WaitFor([&] { return tg.dispatch_state() == lab::DispatchState::on; }, 2000), "guest fnptr on");
    Check(!hub_g.Global(sn::kConfigMessagesSessionRequest) && hub_g.Global(sn::kConfigMessagesSessionFailed) == &sd::SessionFailedFn &&
              Logged("ids=1252"),
          "the guest installs only 205");
    // A 1252 for the guest reaches the mailbox through its own pump.
    net.FailSession(kGuest, kHost, 5003, "timeout");
    sd::SessionFailed f{};
    Check(WaitFor([&] { return sd::TakeSessionFailed(f); }, 2000) && f.peer == kHost && f.end_reason == 5003, "1252 delivered by the pump");
    // Shutdown: the pointers are reset before CloseSessionWithUser.
    tg.request_shutdown();
    Check(WaitFor([&] { return Logged("steam=parked; reason=shutdown"); }, 2000), "guest parked");
    Check(!hub_g.AnySet() && net.ClosesWithPointer() == 0 && net.Closes(kGuest, kHost) == 1, "shutdown: reset, then CloseSessionWithUser");
    tg.stop();
    net.Drain();
}

// Refusals: a gate refusal or a failed SetConfigValue leaves the link on the
// Init-wrapper callback and polling; a fault in RunCallbacks latches and
// makes no further call (no reset either).
void TestDispatchRefusals() {
    namespace sd = mgscoop::steam_dispatch;
    namespace cb = mgscoop::steam_cb;
    sd::ResetForTest();
    cb::ResetForTest();
    ClearLog();
    static FakeNet net;
    g_net = &net;
    static FakeDispatchHub hub;
    net.Add(kHost);
    net.Add(kStranger);
    net.SetHub(kHost, &hub, false);
    static FakeMessages002 mh(&net, kHost), mx(&net, kStranger);
    static FakeSockets012 sh;
    static FakeUtils004 uh;
    sh.hub = &hub;
    uh.hub = &hub;
    static ApiBox bh;
    bh.api.messages = &mh;
    bh.api.sockets = &sh;
    bh.api.utils = &uh;
    bh.api.dispatch_refused = "sockets_slot41";
    lab::SteamTransportConfig c{};
    c.role = lab::SteamRole::host;
    c.self_id = kHost;
    c.resolve = &Resolve;
    c.resolve_ctx = &bh;
    c.pop_request = &PopHostRequest;
    c.dispatch = sd::InstallBits(sd::Variant::both, true);
    c.log = &CaptureLog;
    {
        static Recorder rec;
        static lab::SteamTransport t;
        Check(t.start(c, Sink(rec)) && WaitFor([&] { return t.dispatch_state() == lab::DispatchState::refused; }, 2000), "gate refusal");
        Check(std::strcmp(t.dispatch_reason(), "sockets_slot41") == 0 && hub.Calls() == 0 &&
                  Logged("steam=dispatch; fnptr=refused; reason=sockets_slot41; fallback=callback+poll"),
              "a refused gate makes no SetConfigValue call");
        std::uint8_t hello[64]{};
        mx.SendMessageToUser(Id(kHost), hello, sizeof(hello), sn::kMsgFlags, sn::kLinkChannel);
        Check(WaitFor([&] { return rec.datagrams >= 1; }, 2000) && Logged("via=callback"), "the link still accepts through the callback");
        t.stop();
    }
    // SetConfigValue(205) refused: all or nothing, then the callback path.
    bh.api.dispatch_refused = nullptr;
    hub.fail_value = sn::kConfigMessagesSessionFailed;
    ClearLog();
    {
        static Recorder rec;
        static lab::SteamTransport t;
        Check(t.start(c, Sink(rec)) && WaitFor([&] { return t.dispatch_state() == lab::DispatchState::refused; }, 2000), "install refused");
        Check(std::strcmp(t.dispatch_reason(), "set_205") == 0 && !hub.AnySet() && Logged("fnptr=refused; reason=set_205"),
              "a refused 205 resets 204 (all or nothing)");
        t.stop();
    }
    hub.fail_value = 0;
    // A fault inside RunCallbacks: the transport latches and makes no further call.
    hub.fault_run = true;
    {
        static Recorder rec;
        static lab::SteamTransport t;
        Check(t.start(c, Sink(rec)) && WaitFor([&] { return t.faulted(); }, 2000), "fault in RunCallbacks latches");
        const unsigned calls = hub.Calls();
        t.stop();
        Check(hub.Calls() == calls && hub.AnySet() && std::strcmp(rec.failed, "fault") == 0, "no call after the fault (the pointers stay set)");
    }
    hub.fault_run = false;
    net.Drain();
    g_net = nullptr;
    sd::ResetForTest();
}

// Section 20 gates on a synthetic image: Sockets012 slot 41 / Utils004 slot
// 16 disp32 forwarders down to the inner RTTI, and the Messages owner check.
void TestDispatchGates() {
    // disp32 forwarder shapes (exact bytes of steamclient caba4826...: 0x7166F0 slot 41, 0x716660 slot 16).
    const std::uint8_t s41[] = {0x48, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0xA0, 0x48, 0x01, 0x00, 0x00};
    const std::uint8_t u16[] = {0x48, 0x8B, 0x49, 0x08, 0x4C, 0x8B, 0x11, 0x49, 0xFF, 0xA2, 0x80, 0x00, 0x00, 0x00};
    const std::uint8_t d8_16[] = {0x48, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x80, 0xCC, 0xCC, 0xCC};
    const std::uint8_t d32_5[] = {0x48, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0xA0, 0x28, 0x00, 0x00, 0x00};
    Check(gate::IsForwarder(s41, sizeof(s41), 41) && !gate::IsForwarder(s41, sizeof(s41), 40), "rax disp32 forwarder (slot 41)");
    Check(gate::IsForwarder(u16, sizeof(u16), 16) && !gate::IsForwarder(u16, sizeof(u16), 15), "r10 disp32 forwarder (slot 16)");
    Check(!gate::IsForwarder(s41, 11, 41), "a disp32 forwarder needs 14 bytes");
    Check(!gate::IsForwarder(d8_16, sizeof(d8_16), 16), "disp8 0x80 is -0x80, never slot 16");
    Check(!gate::IsForwarder(d32_5, sizeof(d32_5), 5), "a small slot is disp8 only");
    // The image: .text stubs, .rdata adapter / inner / Messages subobject vtables with RTTI.
    Image img;
    img.bytes.assign(0x9000, 0xCC);
    std::memset(img.bytes.data(), 0, 0x400);
    img.bytes[0] = 'M';
    img.bytes[1] = 'Z';
    img.Put32(0x3C, 0x80);
    img.Put32(0x80, 0x00004550);
    img.bytes[0x84] = 0x64;
    img.bytes[0x85] = 0x86;
    img.bytes[0x86] = 3;
    img.bytes[0x94] = 0xF0;
    img.bytes[0x98] = 0x0B;
    img.bytes[0x99] = 0x02;
    img.Put32(0x98 + 56, 0x9000);
    const std::size_t table = 0x98 + 0xF0;
    const struct { const char* name; std::uint32_t va, size, chars; } sections[] = {
        {".text", 0x1000, 0x2000, 0x60000020}, {".rdata", 0x3000, 0x4000, 0x40000040}, {".data", 0x7000, 0x2000, 0xC0000040}};
    for (unsigned i = 0; i < 3; ++i) {
        const std::size_t sct = table + 40 * i;
        std::memcpy(&img.bytes[sct], sections[i].name, std::strlen(sections[i].name));
        img.Put32(sct + 8, sections[i].size);
        img.Put32(sct + 12, sections[i].va);
        img.Put32(sct + 16, sections[i].size);
        img.Put32(sct + 36, sections[i].chars);
    }
    std::memcpy(&img.bytes[0x1400], s41, sizeof(s41));
    std::memcpy(&img.bytes[0x1440], u16, sizeof(u16));
    const auto rtti = [&](std::uint32_t td, std::uint32_t col, std::uint32_t vtable, const char* name, std::uint32_t offset, unsigned slots,
                          std::uint32_t target) {
        std::memcpy(&img.bytes[td + 0x10], name, std::strlen(name) + 1);
        img.Put32(col + 0, 1);
        img.Put32(col + 4, offset);
        img.Put32(col + 12, td);
        img.Put32(col + 20, col);
        img.Put64(vtable - 8, img.base() + col);
        for (unsigned n = 0; n < slots; ++n) img.Put64(vtable + 8 * n, img.base() + target);
    };
    // Adapters (any RTTI here; acquisition checked theirs): slot 41 / 16 at the stubs.
    rtti(0x3000, 0x3100, 0x3200, sn::kRttiSockets, 0, 48, 0x1800);
    img.Put64(0x3200 + 8 * 41, img.base() + 0x1400);
    rtti(0x3600, 0x3700, 0x3800, sn::kRttiUtils, 0, 25, 0x1800);
    img.Put64(0x3800 + 8 * 16, img.base() + 0x1440);
    // Inner objects: CSteamNetworkingSocketsSteamClient / CSteamNetworkingUtilsSteamClient.
    rtti(0x4000, 0x4100, 0x4200, sn::kRttiSocketsInner, 0, 48, 0x1900);
    rtti(0x4600, 0x4700, 0x4800, sn::kRttiUtilsInner, 0, 25, 0x1900);
    // CSteamNetworkingMessages: the IClientNetworkingMessages subobject at +0xD0.
    rtti(0x5000, 0x5100, 0x5200, sn::kRttiMessagesImpl, sn::kMessagesSubobjectOffset, 6, 0x1A00);
    rtti(0x5600, 0x5700, 0x5800, sn::kRttiMessagesImpl, 0, 6, 0x1A00);  // the primary vtable (offset 0)
    gate::ModuleView view{};
    Check(gate::ReadModuleView(img.bytes.data(), img.bytes.size(), view), "dispatch image");
    // Heap objects.
    std::uint64_t sockets_inner[1] = {img.base() + 0x4200};
    std::uint64_t sockets_adapter[2] = {img.base() + 0x3200, reinterpret_cast<std::uint64_t>(sockets_inner)};
    std::uint64_t utils_inner[1] = {img.base() + 0x4800};
    std::uint64_t utils_adapter[2] = {img.base() + 0x3800, reinterpret_cast<std::uint64_t>(utils_inner)};
    alignas(8) std::uint8_t impl[0xE0]{};
    const std::uint64_t primary = img.base() + 0x5800, sub = img.base() + 0x5200, owner = reinterpret_cast<std::uint64_t>(sockets_inner);
    std::memcpy(impl, &primary, 8);
    std::memcpy(impl + sn::kMessagesOwnerSockets, &owner, 8);
    std::memcpy(impl + sn::kMessagesSubobjectOffset, &sub, 8);
    std::uint64_t messages_adapter[2] = {0, reinterpret_cast<std::uint64_t>(impl + sn::kMessagesSubobjectOffset)};
    const auto at = [](const void* p) { return reinterpret_cast<std::uintptr_t>(p); };
    Check(gate::CheckForwardedSlot(view, at(sockets_adapter), sn::kSocketsRunCallbacks, sn::kRttiSocketsInner, &ReadPlain, nullptr) == nullptr,
          "Sockets012 slot 41 forwards to CSteamNetworkingSocketsSteamClient");
    Check(gate::CheckForwardedSlot(view, at(utils_adapter), sn::kUtilsSetConfigValue, sn::kRttiUtilsInner, &ReadPlain, nullptr) == nullptr,
          "Utils004 slot 16 forwards to CSteamNetworkingUtilsSteamClient");
    Check(std::strcmp(gate::CheckForwardedSlot(view, at(sockets_adapter), 40, sn::kRttiSocketsInner, &ReadPlain, nullptr), "slot_shape") == 0,
          "another slot is not that forwarder");
    Check(std::strcmp(gate::CheckForwardedSlot(view, at(sockets_adapter), sn::kSocketsRunCallbacks, sn::kRttiUtilsInner, &ReadPlain, nullptr),
                      "inner_rtti") == 0,
          "wrong inner class refused");
    std::uint64_t bad_inner[1] = {img.base() + 0x7010};
    std::uint64_t bad_adapter[2] = {sockets_adapter[0], reinterpret_cast<std::uint64_t>(bad_inner)};
    Check(std::strcmp(gate::CheckForwardedSlot(view, at(bad_adapter), sn::kSocketsRunCallbacks, sn::kRttiSocketsInner, &ReadPlain, nullptr),
                      "inner_object") == 0,
          "an inner vptr outside rdata is refused");
    Check(std::strcmp(gate::CheckForwardedSlot(view, 0, 41, sn::kRttiSocketsInner, &ReadPlain, nullptr), "slot_object") == 0, "null object");
    // One per-user object: the Messages implementation owns the Sockets inner object.
    Check(gate::CheckMessagesOwner(view, at(messages_adapter), at(sockets_adapter), sn::kRttiMessagesImpl, sn::kMessagesSubobjectOffset,
                                   sn::kMessagesOwnerSockets, &ReadPlain, nullptr) == nullptr,
          "Messages and Sockets wrap the same per-user object");
    std::uint64_t other[1] = {img.base() + 0x4200};
    std::uint64_t other_adapter[2] = {sockets_adapter[0], reinterpret_cast<std::uint64_t>(other)};
    Check(std::strcmp(gate::CheckMessagesOwner(view, at(messages_adapter), at(other_adapter), sn::kRttiMessagesImpl, sn::kMessagesSubobjectOffset,
                                               sn::kMessagesOwnerSockets, &ReadPlain, nullptr),
                      "owner_mismatch") == 0,
          "another sockets object: owner_mismatch");
    std::uint64_t primary_adapter[2] = {0, reinterpret_cast<std::uint64_t>(impl)};  // the primary vtable has COL offset 0
    Check(std::strcmp(gate::CheckMessagesOwner(view, at(primary_adapter), at(sockets_adapter), sn::kRttiMessagesImpl, sn::kMessagesSubobjectOffset,
                                               sn::kMessagesOwnerSockets, &ReadPlain, nullptr),
                      "owner_rtti") == 0,
          "a subobject at another offset: owner_rtti");
    Check(gate::RttiNameAt(view, img.base() + 0x5200, sn::kRttiMessagesImpl, 0xD0) && !gate::RttiNameIs(view, img.base() + 0x5200, sn::kRttiMessagesImpl),
          "RttiNameAt checks the COL offset");
}

// ---------------------------- 13. plain-language reasons (shared vectors)
// A minimal reader for tests/fixtures/link-reasons.json (objects of strings
// and integers in two arrays).
struct Vector {
    std::string reason, remote, own, text;
    int verified = 1, end = 0;
    bool is_end = false;
};
bool ParseString(const std::string& s, std::size_t& i, std::string& out) {
    out.clear();
    if (i >= s.size() || s[i] != '"') return false;
    for (++i; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') { ++i; return true; }
        if (c == '\\' && i + 1 < s.size()) {
            c = s[++i];
            if (c == 'u' && i + 4 < s.size()) {
                out.push_back(static_cast<char>(std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16)));
                i += 4;
                continue;
            }
            out.push_back(c == 'n' ? '\n' : c == 't' ? '\t' : c);
            continue;
        }
        out.push_back(c);
    }
    return false;
}
std::vector<Vector> ReadVectors(const char* path) {
    std::vector<Vector> out;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") || !f) return out;
    std::string s;
    char buffer[4096];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0;) s.append(buffer, n);
    std::fclose(f);
    bool ends = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 11, "\"steam_end\"") == 0) ends = true;
        if (s[i] != '{' || i == s.find('{')) continue;
        Vector v{};
        v.is_end = ends;
        ++i;
        while (i < s.size() && s[i] != '}') {
            std::string key, value;
            while (i < s.size() && s[i] != '"' && s[i] != '}') ++i;
            if (i >= s.size() || s[i] == '}' || !ParseString(s, i, key)) break;
            while (i < s.size() && (s[i] == ':' || s[i] == ' ')) ++i;
            if (s[i] == '"') ParseString(s, i, value);
            else {
                const std::size_t start = i;
                while (i < s.size() && (s[i] == '-' || (s[i] >= '0' && s[i] <= '9'))) ++i;
                value = s.substr(start, i - start);
            }
            if (key == "reason") v.reason = value;
            else if (key == "remote") v.remote = value;
            else if (key == "own") v.own = value;
            else if (key == "text") v.text = value;
            else if (key == "verified") v.verified = std::atoi(value.c_str());
            else if (key == "end") v.end = std::atoi(value.c_str());
        }
        out.push_back(v);
    }
    return out;
}
void TestReasonVectors(const char* path) {
    namespace ct = mgscoop::coop_text;
    const std::vector<Vector> vectors = ReadVectors(path);
    unsigned reasons = 0, ends = 0;
    for (const Vector& v : vectors) {
        char out[400]{};
        if (v.is_end) {
            Check(ct::FormatSteamEnd(v.end, out, sizeof(out)), "FormatSteamEnd fits");
            if (v.text != out) std::fprintf(stderr, "steam_end %d:\n  want %s\n  got  %s\n", v.end, v.text.c_str(), out);
            Check(v.text == out, "native FormatSteamEnd == Get-CoopSteamEndText");
            ++ends;
        } else {
            Check(ct::FormatLinkReason(v.reason.c_str(), v.remote.c_str(), v.own.c_str(), v.verified != 0, out, sizeof(out)), "FormatLinkReason fits");
            if (v.text != out) std::fprintf(stderr, "reason %s:\n  want %s\n  got  %s\n", v.reason.c_str(), v.text.c_str(), out);
            Check(v.text == out, "native FormatLinkReason == Get-CoopLinkReasonText");
            ++reasons;
        }
    }
    Check(reasons >= 25 && ends >= 15, "the shared vector file was read (tests/fixtures/link-reasons.json)");
    char tiny[8]{};
    Check(!ct::FormatLinkReason("busy", "", "", true, tiny, sizeof(tiny)) && tiny[0] == 0, "too small: false, empty");
    Check(std::strcmp(ct::ShortReason("bad_code"), "wrong code?") == 0 && std::strcmp(ct::ShortReason("steam_iface_shape"), "Steam unavailable") == 0 &&
              std::strcmp(ct::ShortSteamEnd(5003), "host not answering") == 0,
          "short reasons");
}

// ------------------------------------------- 14. the in-game status line
void TestHud() {
    namespace hud = mgscoop::coop_hud;
    hud::ResetForTest();
    Check(!hud::Enabled(), "off until configured");
    Check(hud::ModeFor(mgscoop::CoopHud::unset, mgscoop::CoopHud::unset, false) == hud::Mode::off &&
              hud::ModeFor(mgscoop::CoopHud::unset, mgscoop::CoopHud::unset, true) == hud::Mode::compact &&
              hud::ModeFor(mgscoop::CoopHud::unset, mgscoop::CoopHud::full, true) == hud::Mode::full &&
              hud::ModeFor(mgscoop::CoopHud::unset, mgscoop::CoopHud::full, false) == hud::Mode::off &&
              hud::ModeFor(mgscoop::CoopHud::compact, mgscoop::CoopHud::off, false) == hud::Mode::compact,
          "lab default off, play compact, lab.ini wins, coop.ini in play only");
    hud::Configure(hud::Mode::compact, true);
    Check(hud::Enabled(), "configured");
    hud::LinkView v{};
    v.valid = true;
    v.state = static_cast<std::uint8_t>(link::LinkState::connected);
    v.host = true;
    v.steam = true;
    v.play = true;
    v.route = static_cast<std::uint8_t>(sn::Route::relay);
    v.rtt_ms = 61;
    std::snprintf(v.own_build, sizeof(v.own_build), "0.12.0+f64a8e9");
    hud::Line line{};
    Check(hud::FormatLine(v, "Snake_Fan", hud::Mode::compact, 0, line) && std::strcmp(line.text, "CO-OP [connected] Snake_Fan - Steam relay 61 ms") == 0 &&
              line.tone == hud::Tone::good && line.detail[0] == 0,
          "connected line");
    {
        hud::PartnerFacts f{};
        f.session = 7; f.mission = 30050; f.publish_ms = 1000;
        f.body_ready = f.native_selected = f.native_active = f.gun_enabled = true;
        f.fresh_gun = f.gun_fields_ok = f.remote_live = f.gate_current = true;
        f.gun_class = 1; f.body_equip = 46;
        f.gate = static_cast<std::uint8_t>(mgscoop::partner_slots::Gate::match);
        auto p = hud::MakePartnerView(f);
        Check(p.drive == hud::PartnerDrive::native && p.gun == hud::PartnerGun::match && hud::GunPresent(p.gun),
              "current verified fields and current gate: native matched gun");
        hud::Configure(hud::Mode::off, false); // F9/HUD disabled does not stop owner publication
        hud::PublishPartner(p);
        f.body_equip = 0;
        hud::PartnerView copied{};
        Check(hud::GetPartner(copied) && copied.gun == hud::PartnerGun::match && copied.mission == 30050,
              "partner publication is copied and available while HUD/menu are off");
        hud::Configure(hud::Mode::compact, true);
        f.body_equip = 46;
        v.session = 7; v.mission = 30050;
        Check(hud::FormatLine(v, "Snake_Fan", hud::Mode::compact, 1200, line, &p) &&
              std::strcmp(line.text, "CO-OP [connected] Snake_Fan - Steam relay 61 ms | native; gun matched") == 0,
              "connected line consumes the fresh copied partner view");
        f.substitute = true; p = hud::MakePartnerView(f);
        Check(p.gun == hud::PartnerGun::substitute, "verified current slot substitute is distinct from an exact match");
        f.substitute = false; f.standin = true; f.gate = static_cast<std::uint8_t>(mgscoop::partner_slots::Gate::standin);
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::standin, "qualified current alternate slot is a stand-in");
        f.standin = false;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::present, "historical stand-in verdict without current plan proof is only gun present");
        f.gate = static_cast<std::uint8_t>(mgscoop::partner_slots::Gate::match); f.gate_current = false;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::present, "old gate at a current slot transition cannot claim matching");
        f.gate_current = true; f.remote_live = false;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::present, "stale remote equip cannot claim matching");
        f.remote_live = true; f.switching = true;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::switching, "current gun switch is reported before matching");
        f.switching = false; f.gate = static_cast<std::uint8_t>(mgscoop::partner_slots::Gate::mismatch);
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::mismatch, "current mismatched gun is distinct from unavailable");
        f.gate = static_cast<std::uint8_t>(mgscoop::partner_slots::Gate::projectile);
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::projectile, "projectile gun does not claim bullet match");
        f.gun_fields_ok = false;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::unavailable, "unread current gun fields ignore historical presence");
        f.gun_fields_ok = true; f.fresh_gun = false;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::unavailable, "retained gun fields from a skipped owner tick are unavailable");
        f.fresh_gun = true; f.gun_class = 0;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::unavailable, "non-gun class does not establish current gun presence");
        f.gun_class = 1; f.body_equip = 0;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::unavailable, "equip zero does not establish current gun presence");
        f.body_equip = 46; f.native_active = false;
        Check(hud::MakePartnerView(f).drive == hud::PartnerDrive::clips, "native fallback reports clips");
        f.body_ready = false;
        Check(hud::MakePartnerView(f).drive == hud::PartnerDrive::loading && hud::MakePartnerView(f).gun == hud::PartnerGun::unavailable,
              "hidden/loading body cannot advertise a present gun");
        f.gun_enabled = false;
        Check(hud::MakePartnerView(f).gun == hud::PartnerGun::off, "clip/loco gun profile reports off");
        for (const auto stale : {hud::CurrentPartner(p, 7, 30050, 2001), hud::CurrentPartner(p, 8, 30050, 1200),
                                 hud::CurrentPartner(p, 7, 30020, 1200), hud::CurrentPartner(p, 7, 30050, 999),
                                 hud::CurrentPartner(hud::PartnerView{}, 7, 30050, 1200)})
            Check(!stale.valid && stale.drive == hud::PartnerDrive::loading && stale.gun == hud::PartnerGun::unavailable,
                  "stale/future/mismatched session/mission cache refuses historical native/gun status");
        Check(hud::CurrentPartner(p, 7, 30050, 2000).valid, "one-second freshness boundary is inclusive");
        Check(hud::FormatLine(v, "Snake_Fan", hud::Mode::compact, 2001, line, &p) &&
              std::strstr(line.text, "| loading; gun unavailable"), "HUD refuses a stale cached partner view");
        char status[192]{}, tiny[8]{};
        Check(hud::FormatPartnerJson(p, 7, 30050, 1200, status, sizeof(status)) &&
              std::strcmp(status, "\"partner_view\":\"native\",\"partner_gun\":\"substitute\",\"partner_gun_present\":1,\"partner_view_fresh\":1,") == 0,
              "link status consumes the same current copied substitute view");
        Check(hud::FormatPartnerJson(p, 7, 30050, 2001, status, sizeof(status)) &&
              std::strstr(status, "\"partner_gun_present\":0,\"partner_view_fresh\":0"), "stale link status cannot claim gun presence");
        Check(!hud::FormatPartnerJson(p, 7, 30050, 1200, tiny, sizeof(tiny)) && tiny[0] == 0 &&
              !hud::FormatPartnerJson(p, 7, 30050, 1200, nullptr, 0), "partner JSON is bounded and empty on refusal");
        v.rtt_ms = 0xFFFFFFFFu;
        Check(hud::FormatLine(v, "A_partner_name_that_is_much_longer_than_the_compact_line", hud::Mode::compact, 1200, line, &p) &&
              std::strlen(line.text) < sizeof(line.text) && std::strstr(line.text, "; gun substitute"), "long names keep the bounded current status suffix");
        v.rtt_ms = 61;
        v.state = static_cast<std::uint8_t>(link::LinkState::rejected); std::snprintf(v.reason, sizeof(v.reason), "bad_code");
        Check(hud::FormatLine(v, "Snake_Fan", hud::Mode::compact, 1200, line, &p) &&
              std::strcmp(line.text, "CO-OP [refused] wrong code?") == 0, "cached partner never replaces a connection error");
        v.session = v.mission = 0; v.reason[0] = 0;
    }
    v.state = static_cast<std::uint8_t>(link::LinkState::listening);
    v.accepting = true;
    v.code_ready = true;
    Check(hud::FormatLine(v, "", hud::Mode::compact, 0, line) && std::strcmp(line.text, "CO-OP [waiting] code ready - F9 to copy/invite") == 0,
          "host waiting line");
    v.left = true;
    Check(hud::FormatLine(v, "Snake_Fan", hud::Mode::compact, 0, line) &&
              std::strcmp(line.text, "CO-OP [Snake_Fan left] waiting for them to come back") == 0,
          "partner left line");
    v.left = false;
    v.host = false;
    v.state = static_cast<std::uint8_t>(link::LinkState::reconnecting);
    v.down_ms = 7600;
    v.publish_ms = 1000;
    Check(hud::FormatLine(v, "", hud::Mode::compact, 1500, line) && std::strcmp(line.text, "CO-OP [reconnecting 8 s]") == 0 && line.tone == hud::Tone::warn,
          "reconnecting seconds");
    v.state = static_cast<std::uint8_t>(link::LinkState::rejected);
    std::snprintf(v.reason, sizeof(v.reason), "bad_code");
    Check(hud::FormatLine(v, "", hud::Mode::compact, 0, line) && std::strcmp(line.text, "CO-OP [refused] wrong code?") == 0 && line.tone == hud::Tone::bad,
          "refused line");
    v.state = static_cast<std::uint8_t>(link::LinkState::connecting);
    v.reason[0] = 0;
    v.steam_end = 5003;
    v.down_ms = 0;
    Check(hud::FormatLine(v, "", hud::Mode::full, 1000, line) && std::strcmp(line.text, "CO-OP [joining 0 s] host not answering") == 0 &&
              std::strncmp(line.detail, "rtt 61 ms | jitter 0 ms | loss 0.0% | 0.12.0+f64a8e9", 50) == 0,
          "joining with a Steam end reason; full detail line");
    v.state = static_cast<std::uint8_t>(link::LinkState::off);
    Check(!hud::FormatLine(v, "", hud::Mode::compact, 0, line), "link off: nothing");
    v.state = static_cast<std::uint8_t>(link::LinkState::connected);
    Check(!hud::FormatLine(v, "", hud::Mode::off, 0, line), "mode off: nothing");
    // Downed facts are a bounded copied publication, never a historical pose.
    {
        hud::DownedView d{}, copied{};
        hud::Line prompt{}; float progress = -1;
        Check(!hud::GetDowned(copied), "no downed view before a natural owner publication");
        d.valid = true; d.mission = 30050; d.epoch = 7; d.publish_ms = 1000; d.localDown = true;
        hud::Configure(hud::Mode::off, false);
        hud::PublishDowned(d); d.localDown = false;
        Check(hud::GetDowned(copied) && copied.localDown && copied.epoch == 7,
              "downed publication is copied and remains available while HUD/menu are off");
        hud::Configure(hud::Mode::compact, true);
        Check(hud::FormatDowned(copied,30050,1200,prompt,progress) && prompt.tone == hud::Tone::bad &&
              std::strstr(prompt.text,"waiting") && !std::strstr(prompt.text,"Hold") && progress == 0,
              "downed local player sees waiting rather than an impossible revive instruction");
        d.teammateDown = true;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && std::strstr(prompt.text,"get within 3 m") &&
              !prompt.keys[0] && progress == 0,
              "out-of-range teammate gets approach guidance without unavailable action keycaps");
        d.teammateCause = 2;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && std::strstr(prompt.text,"asleep") &&
              !prompt.keys[0] && !std::strstr(prompt.text,"revive"), "a distant sleeping teammate keeps approach guidance, not a revive hint");
        d.teammateCause = 0;
        d.closeEnough = true;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && std::strstr(prompt.text,"please wait") &&
              !prompt.keys[0] && progress == 0,
              "inside3m unobserved Action prepares; pickup is left solely to the stock carry icon");
        d.closeEnough = false;
        d.carry = 1;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && std::strstr(prompt.text,"Carrying") &&
              !prompt.keys[0] && !std::strstr(prompt.keys,"Load"),
              "a copied carrier role leaves contextual put-down/loading controls to the stock prompt");
        d.carry = 3;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && !prompt.keys[0] &&
              !std::strstr(prompt.text,"riding"), "provisional cargo does not advertise native seats or unloading");
        d.carry = 5;
        Check(!hud::FormatDowned(d,30050,1200,prompt,progress), "unknown carry roles cannot become action prompts");
        d.carry = 2; d.inRange = true;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && !prompt.keys[0] && progress == 0,
              "a healthy readback still being carried cannot advertise a rescue action");
        d.carry = 4;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && !prompt.keys[0],
              "a provisional carried/cargo role cannot advertise Wake or Revive");
        d.carry = 0;
        d.inRange = true;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) &&
              std::strcmp(prompt.keys,"Action\tHold to revive") == 0 && !std::strstr(prompt.keys,"Pick up"),
              "ready rescue is concise semantic Action with no keyboard-default prose or duplicate pickup");
        d.progressMs = 1500;
        Check(hud::FormatDowned(d,30050,1500,prompt,progress) && std::strstr(prompt.text,"Reviving") &&
              std::strstr(prompt.text,"50%") && std::strcmp(prompt.keys,"Action\tKeep holding") == 0 &&
              !std::strstr(prompt.keys,"Pick up") && progress == .5f,
              "actual hold keeps its keycap and progress while hiding the competing pickup hint");
        d.progressMs = 3000;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && progress == 1.f && std::strstr(prompt.text,"100%") && !prompt.keys[0],
              "completed hold remains100percent but does not request more input during pending native/network recovery");
        for (const auto now : {999u,1501u}) {
            Check(!hud::FormatDowned(d,30050,now,prompt,progress) && !prompt.text[0] && progress == 0,
                  "future clock and expired facts cannot show a revive command or old progress");
        }
        Check(!hud::FormatDowned(d,30020,1200,prompt,progress) && !prompt.text[0],
              "recent old-scene downed facts cannot show in a different mission");
        d.mission = 10010;
        Check(!hud::FormatDowned(d,10010,1200,prompt,progress), "unsupported mission cannot show downed gameplay");
        d.mission = 30050; d.epoch = 0;
        Check(!hud::FormatDowned(d,30050,1200,prompt,progress), "unpaired epoch cannot show revive");
        d.epoch = 7; d.progressMs = 3001;
        Check(!hud::FormatDowned(d,30050,1200,prompt,progress), "oversized hold progress is refused rather than overflowing a bar");
        d.progressMs = 1000; d.inRange = false;
        Check(!hud::FormatDowned(d,30050,1200,prompt,progress), "a released or distant hold cannot retain previous progress");
        d.inRange = true; d.teammateDown = false;
        Check(!hud::FormatDowned(d,30050,1200,prompt,progress), "progress requires a downed teammate");
        d.teammateDown = true; d.localDown = true;
        Check(!hud::FormatDowned(d,30050,1200,prompt,progress), "both-down facts cannot present local revive progress");
        d.inRange = false; d.progressMs = 0;
        Check(hud::FormatDowned(d,30050,1200,prompt,progress) && std::strstr(prompt.text,"waiting"),
              "both-down recovery transition retains a safe wait instruction");
        d.localDown = d.teammateDown = false;
        Check(!hud::FormatDowned(d,30050,1200,prompt,progress) && !prompt.text[0], "healthy pair clears the gameplay instruction");
        {
            // The world-anchored prompt states follow the same validated facts.
            hud::DownedView r{};
            r.valid = true; r.mission = 30050; r.epoch = 7; r.publish_ms = 1000;
            r.teammateDown = true; r.teammateCause = 2;
            hud::RescuePrompt rp{};
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::away && rp.sleeping, "distant sleeper is an away pill");
            r.closeEnough = true;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::wait, "beside an unadmitted hold waits");
            r.inRange = true;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::ready, "admitted hold offers the Action button");
            r.progressMs = 1500;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::holding && rp.progress == .5f, "hold fills the ring");
            r.progressMs = 3000;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::done && rp.progress == 1.f, "completed hold waits for recovery");
            r.progressMs = 0; r.carry = 1;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::carrying, "carrier leaves controls to the stock prompt");
            r.carry = 0; r.inRange = r.closeEnough = false; r.teammateDown = false; r.teammateCause = 0;
            r.localDown = true; r.localCause = 1;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::self_wounded && !rp.sleeping, "wounded self status");
            r.localCause = 2;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::self_asleep && rp.sleeping, "sleeping self status");
            r.carry = 2;
            Check(hud::ClassifyDowned(r,30050,1200,rp) && rp.state == hud::Rescue::carried, "carried self status");
            Check(!hud::ClassifyDowned(r,30050,1600,rp) && rp.state == hud::Rescue::none, "stale facts draw nothing");
            Check(std::strcmp(hud::RescueName(hud::Rescue::holding),"holding") == 0, "rescue state names");
        }
        // Parse the natural owner's copied V2 cause, never infer sleep from pose.
        auto parseHud = [&](const char* text) { return hud::ParseDownedHud(text, std::strlen(text), copied); };
        Check(parseHud("return 'MGS_COOP_DOWNED_HUD_V1' -- 30050 7 1 0 0 0") && copied.localCause == 1,
              "legacy down receipt infers wounded and keeps its existing prompt");
        Check(parseHud("return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 1 0 0 0 2 0") && copied.localCause == 2,
              "explicit protected sleep is separate from wounded incapacitation");
        copied.publish_ms = 1000;
        Check(hud::FormatDowned(copied,30050,1200,prompt,progress) && std::strstr(prompt.text,"Asleep") &&
              !std::strstr(prompt.text,"revive") && prompt.tone == hud::Tone::warn && progress == 0,
              "sleeping player gets a wake instruction without a lethal down label");
        Check(parseHud("return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 0 1 1 1500 0 2"), "teammate sleep progress parsed");
        copied.publish_ms = 1000;
        Check(hud::FormatDowned(copied,30050,1200,prompt,progress) && std::strstr(prompt.text,"Waking teammate") &&
              std::strcmp(prompt.keys,"Action\tKeep holding") == 0 && progress == .5f,
              "held Action wakes a sleeping teammate with real copied hold progress and persistent keycap");
        Check(parseHud("return 'MGS_COOP_DOWNED_HUD_V3' -- 30050 7 0 1 0 0 0 1 1") && copied.closeEnough && !copied.inRange &&
              copied.teammateCause == 1, "V3 can be close enough without an observed Action hold");
        copied.publish_ms = 1000;
        Check(hud::FormatDowned(copied,30050,1200,prompt,progress) && !std::strstr(prompt.text,"%") &&
              !prompt.keys[0] && progress == 0,
              "close-enough receipt cannot advertise Action before its native mask is observed");
        Check(parseHud("return 'MGS_COOP_DOWNED_HUD_V3' -- 30050 7 0 1 1 0 0 2 1"), "ready sleep state parsed");
        copied.publish_ms = 1000;
        Check(hud::FormatDowned(copied,30050,1200,prompt,progress) && std::strcmp(prompt.keys,"Action\tHold to wake") == 0,
              "ready sleeper gets the concise Wake action");
        Check(!parseHud("return 'MGS_COOP_DOWNED_HUD_V3' -- 30050 7 0 1 1 0 0 1 0"),
              "a hold range without the 3m approach is refused");
        Check(parseHud("return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 0 1 1 1500 0 2") && copied.closeEnough && copied.inRange,
              "V2 still treats the hold range as the approach");
        Check(parseHud("return 'MGS_COOP_DOWNED_HUD_V2' -- 0 0 0 0 0 0 0 0") && !copied.valid,
              "world stop zeroes every explicit incapacity field");
        for (const char* bad : {
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 1 0 0 0 0 0",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 0 0 0 0 2 0",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 1 0 0 0 3 0",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 0 1 1 3001 0 2",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 1 1 1 100 2 1",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 0 7 0 0 0 0 0 0",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 10010 7 1 0 0 0 2 0",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 0 1 0 0 0 2 0",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 1 0 0 0 2 0 ",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 1 0 0 0 2",
                "return 'MGS_COOP_DOWNED_HUD_V3' -- 30050 7 1 0 0 0 2 0",
                "return 'MGS_COOP_DOWNED_HUD_V2' -- 30050 7 1 0 0 0 2 0 1",
                "return 'MGS_COOP_DOWNED_HUD_V1' -- 30050 7 1 0 0 0 2 0"}) {
            Check(!parseHud(bad) && !copied.valid && copied.localCause == 0 && copied.teammateCause == 0,
                  "malformed/stale/contradictory cause receipt cannot advertise a wake or revive action");
        }
        Check(!hud::ParseDownedHud(nullptr,100,copied), "null receipt refused without dereference");
        Check(!hud::ParseDownedHud("",193,copied), "oversized receipt refused before reading");
        hud::PublishDowned({});
        Check(!hud::GetDowned(copied), "world teardown immediately clears copied downed HUD facts");
    }
    // Events -> toasts.
    hud::Post(hud::EventKind::connected, 1, "steam:...1234");
    hud::Post(hud::EventKind::steam_end, 5003, "");
    hud::Post(hud::EventKind::version, 0, "0.11.0+1234567");
    hud::Model model;
    hud::Event events[8]{};
    const unsigned n = hud::Take(events, 8, model.cursor());
    Check(n == 3 && events[0].kind == hud::EventKind::connected && model.cursor() == 3 && hud::Take(events, 8, model.cursor()) == 0,
          "events taken once, in order");
    v.host = true;
    for (unsigned i = 0; i < n; ++i) model.OnEvent(events[i], v, "Snake_Fan", 10000);
    const hud::Toast* toasts[hud::kMaxToasts]{};
    Check(model.Active(10000, toasts, hud::kMaxToasts) == 3, "three toasts");
    bool connected = false, ended = false, version = false;
    for (const hud::Toast* t : toasts) {
        connected = connected || std::strcmp(t->text, "Connected to Snake_Fan (Steam relay).") == 0;
        ended = ended || std::strncmp(t->text, "The host's game did not answer.", 31) == 0;
        version = version || std::strcmp(t->text, "Your friend runs mgs-coop 0.11.0+1234567; you run 0.12.0+f64a8e9. Both must install the same package.") == 0;
    }
    Check(connected && ended && version, "toast sentences (coop_text)");
    model.Add("Connected to Snake_Fan (Steam relay).", hud::Tone::good, 12000);
    Check(model.Active(12000, toasts, hud::kMaxToasts) == 3 && toasts[0]->until_ms == 12000 + hud::kToastMs, "an identical toast is extended");
    Check(model.Active(10000 + hud::kToastMs, toasts, hud::kMaxToasts) == 1, "toasts expire after 4 s");
    // no_body: a partner mission on a refused pack list (docs/player-package-selection.md).
    {
        char text[hud::kToastChars]{};
        hud::Tone tone = hud::Tone::info;
        hud::Event e{};
        e.kind = hud::EventKind::no_body;
        e.arg = 30050;
        const struct {
            const char* scene;
            const char* sentence;
        } cases[] = {
            {"demo_stage", "No partner body in this scene (Mother Base demo stage). It returns when the scene reloads normally."},
            {"hangar", "No partner body in this scene (battle-gear hangar). It returns when the scene reloads normally."},
            {"story", "No partner body in this scene (story variant). It returns when the scene reloads normally."},
            {"layout", "No partner body in this scene (unknown Mother Base layout). Please send Collect-Logs."},
            {"other", "No partner body in this scene (unexpected pack list). Please send Collect-Logs."},
            {"", "No partner body in this scene (unexpected pack list). Please send Collect-Logs."},
        };
        for (const auto& c : cases) {
            std::snprintf(e.text, sizeof(e.text), "%s", c.scene);
            Check(hud::FormatToast(e, v, "Snake_Fan", text, sizeof(text), tone) && std::strcmp(text, c.sentence) == 0 && tone == hud::Tone::warn,
                  "no_body toast sentence per scene");
        }
        char small[16]{};
        Check(!mgscoop::coop_text::FormatNoBody("hangar", small, sizeof(small)) && small[0] == 0, "no_body: a short buffer stays empty");
    }
    // Telemetry for state.json and hud_status.
    hud::NoteFrame(&line, "Connected", false, 3, 3, 500);
    char json[512]{}, status[161]{};
    Check(hud::FormatJson(hud::GetTelemetry(), json, sizeof(json)) && std::strstr(json, ",\"hud\":{\"enabled\":true,\"mode\":\"compact\",\"frames\":1") &&
              std::strstr(json, "\"toast\":\"Connected\""),
          "state.json hud member");
    Check(hud::FormatStatus(hud::GetTelemetry(), status, sizeof(status)) && std::strncmp(status, "hud;mode=compact;on=1;frames=1;line=", 36) == 0,
          "hud_status line");
    hud::ResetForTest();
}
}  // namespace

int main(int argc, char** argv) {
    // The shared vectors: argv[1], else the path CMake compiles in.
#ifdef MGS_LINK_REASONS_JSON
    const char* reasons = argc >= 2 ? argv[1] : MGS_LINK_REASONS_JSON;
#else
    const char* reasons = argc >= 2 ? argv[1] : nullptr;
#endif
    TestIdentity();
    TestJoinCodes();
    TestGates();
    TestRoute();
    TestCallbacks();
    TestRing();
    TestAcceptPolicy();
    TestGuestFilter();
    TestSlots();
    TestState();
    TestReleaseAndFilter();
    TestFault();
    TestResolveFailure();
    TestShutdown();
    TestHostAccept();
    TestPollKeepsRate();
    TestConnectionLeg();
    TestEndToEnd();
    TestDispatchFunctions();
    TestDispatchGates();
    TestDispatchTransport();
    TestDispatchRefusals();
    Check(reasons != nullptr, "usage: steam_transport_tests <tests/fixtures/link-reasons.json>");
    TestReasonVectors(reasons);
    TestHud();
    Check(g_allocated == g_released, "every fake message freed exactly once");
    std::printf("steam_transport_tests: %u checks passed\n", g_checks);
    return 0;
}
