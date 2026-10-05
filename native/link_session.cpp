// Original MGS Co-op link v2 session state machine (docs/link-protocol.md).
#include "mgscoop/link_session.hpp"

#include <cstring>

namespace mgscoop::link {
namespace {
std::uint8_t ModeByte(bool play) noexcept { return play ? kModePlay : kModeLab; }

void CopyCString(char* out, std::size_t capacity, const char* in) noexcept {
    std::size_t i = 0;
    for (; in && in[i] && i + 1 < capacity; ++i) out[i] = in[i];
    out[i] = 0;
}

bool SameBuild(const char* a, const char* b) noexcept {
    return std::strncmp(a, b, kBuildSize) == 0;
}

std::uint64_t AddressHash(std::uint32_t address) noexcept {
    return (address * 0x9E3779B97F4A7C15ull) >> 58;  // 6 bits: kRateTable entries
}

bool AllZero(const std::uint8_t* p, std::size_t size) noexcept {
    unsigned acc = 0;
    for (std::size_t i = 0; i < size; ++i) acc |= p[i];
    return acc == 0;
}
} // namespace

const char* LinkStateName(LinkState state) noexcept {
    switch (state) {
    case LinkState::disabled: return "disabled";
    case LinkState::listening: return "listening";
    case LinkState::connecting: return "connecting";
    case LinkState::handshake: return "handshake";
    case LinkState::connected: return "connected";
    case LinkState::reconnecting: return "reconnecting";
    case LinkState::lost: return "lost";
    case LinkState::rejected: return "rejected";
    default: return "off";
    }
}

const char* RejectReasonName(RejectReason reason) noexcept {
    switch (reason) {
    case RejectReason::proto_mismatch: return "proto_mismatch";
    case RejectReason::build_mismatch: return "build_mismatch";
    case RejectReason::mode_mismatch: return "mode_mismatch";
    case RejectReason::bad_code: return "bad_code";
    case RejectReason::busy: return "busy";
    case RejectReason::role_mismatch: return "role_mismatch";
    default: return "";
    }
}

const char* DropReasonName(DropReason reason) noexcept {
    switch (reason) {
    case DropReason::wrong_session: return "wrong_session";
    case DropReason::wrong_endpoint: return "wrong_endpoint";
    case DropReason::replay: return "replay";
    case DropReason::bad_mac: return "bad_mac";
    case DropReason::rate: return "rate";
    case DropReason::unknown_msg: return "unknown_msg";
    default: return "malformed";
    }
}

const char* HandshakeStepName(HandshakeStep step) noexcept {
    switch (step) {
    case HandshakeStep::welcome_rx: return "welcome_rx";
    case HandshakeStep::confirm_tx: return "confirm_tx";
    case HandshakeStep::confirm_rx: return "confirm_rx";
    default: return "hello_rx";
    }
}

void Session::Start(const SessionConfig& config, const SessionIo& io, std::uint64_t now_ms) noexcept {
    config_ = config;
    io_ = io;
    DeriveHandshakeKey(config_.psk, hs_);
    current_ = Live{};
    next_ = Live{};
    stats_ = SessionStats{};
    mac_milli_tokens_ = kMacBudgetPerSec * 1000;
    mac_last_ms_ = now_ms;
    guest_mac_milli_tokens_ = kGuestMacPerSec * 1000;
    guest_mac_last_ms_ = now_ms;
    if (config_.role == Role::host) {
        state_ = LinkState::listening;
        Push(MakeEvent(EventKind::listening));
    } else {
        BeginConnecting(now_ms, false);
    }
}

Event Session::MakeEvent(EventKind kind) const noexcept {
    Event e{};
    e.kind = kind;
    e.session = session_number_;
    e.peer = peer();
    e.rtt_ms = rtt_.have() ? rtt_.srtt_ms() : 0;
    CopyCString(e.remote_build, sizeof(e.remote_build), remote_build_);
    return e;
}

void Session::Push(const Event& event) noexcept {
    if (event_count_ == kEvents) {  // drop the oldest
        event_head_ = (event_head_ + 1) % kEvents;
        --event_count_;
    }
    events_[(event_head_ + event_count_) % kEvents] = event;
    ++event_count_;
}

bool Session::PopEvent(Event& out) noexcept {
    if (!event_count_) return false;
    out = events_[event_head_];
    event_head_ = (event_head_ + 1) % kEvents;
    --event_count_;
    return true;
}

Endpoint Session::peer() const noexcept {
    if (config_.role == Role::guest) return config_.peer;
    return current_.valid ? current_.endpoint : Endpoint{};
}

bool Session::has_peer() const noexcept { return config_.role == Role::guest || current_.valid; }

bool Session::Send(const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    if (!io_.send) return false;
    const bool sent = io_.send(io_.context, to, data, size);
    if (sent) {
        ++stats_.tx_packets;
        stats_.tx_bytes += size;
    }
    return sent;
}

bool Session::Random(std::uint8_t* out, std::size_t size) noexcept {
    if (io_.random && io_.random(io_.context, out, size)) return true;
    state_ = LinkState::disabled;
    Push(MakeEvent(EventKind::crypto));
    return false;
}

bool Session::AckPendingFor(std::uint64_t now_ms, std::uint64_t ms) const noexcept {
    return current_.valid && current_.ack_pending && now_ms - current_.ack_since_ms >= ms;
}

bool Session::SendOn(Live& live, PacketType type, const std::uint8_t* body, std::size_t size, std::uint32_t seq,
                     std::uint64_t now_ms) noexcept {
    if (size > kLinkMaxBody) return false;
    Header h{};
    h.type = type;
    h.session_id = live.sid;
    h.seq = seq;
    if (live.window.any()) {
        h.flags = HeaderFlag::HasAck;
        h.ack = live.window.highest();
        h.ack_bits = live.window.AckBits();
        const std::uint64_t delay = now_ms - live.highest_rx_ms;
        h.ack_delay_ms = static_cast<std::uint16_t>(delay > 0xFFFF ? 0xFFFF : delay);
    }
    h.send_ms = static_cast<std::uint32_t>(now_ms);
    h.body_len = static_cast<std::uint16_t>(size);
    EncodeHeader(h, scratch_);
    if (size) std::memcpy(scratch_ + kLinkHeaderSize, body, size);
    const std::size_t total = kLinkOverhead + size;
    WriteTag(live.tx, scratch_, total);
    live.sent.Record(seq, h.send_ms);
    live.last_tx_ms = now_ms;
    live.ack_pending = false;
    const Endpoint to = config_.role == Role::guest ? config_.peer : live.endpoint;
    return Send(to, scratch_, total);
}

bool Session::SendDataOn(Live& live, const std::uint8_t* body, std::size_t size, std::uint64_t now_ms) noexcept {
    if (!live.valid) return false;
    if (live.tx_seq + 1 >= kSeqLimit) {
        Exhausted(now_ms);
        return false;
    }
    ++live.tx_seq;
    return SendOn(live, PacketType::data, body, size, live.tx_seq, now_ms);
}

bool Session::SendData(const std::uint8_t* body, std::size_t size, std::uint64_t now_ms) noexcept {
    if (!CanSendData()) return false;
    return SendDataOn(current_, body, size, now_ms);
}

void Session::Exhausted(std::uint64_t now_ms) noexcept {
    // No wrap: stop DATA. The host stops at lost; the guest starts a new handshake.
    if (config_.role == Role::host) {
        if (state_ == LinkState::connected) {
            Event e = MakeEvent(EventKind::lost);
            e.after_ms = static_cast<std::uint32_t>(now_ms - current_.last_rx_ms);
            Push(e);
        }
        state_ = LinkState::lost;
    } else {
        current_.valid = false;
        BeginConnecting(now_ms, true);
    }
}

Delivery Session::Accept(Live& live, const Header& h, const std::uint8_t* data, std::uint64_t now_ms) noexcept {
    const bool had_any = live.window.any();
    const std::uint32_t before = live.window.highest();
    live.window.Mark(h.seq);
    if (!had_any || h.seq > before) live.highest_rx_ms = now_ms;
    if (h.type == PacketType::data) loss_.OnAccepted(h.seq, before, had_any, now_ms);
    if (h.flags & HeaderFlag::HasAck) {
        std::uint32_t sent_ms = 0;
        if (live.sent.Take(h.ack, sent_ms)) {
            const std::int64_t sample = static_cast<std::int64_t>(static_cast<std::uint32_t>(now_ms) - sent_ms) - h.ack_delay_ms;
            if (sample >= 0 && sample < 60000) rtt_.Sample(static_cast<std::uint32_t>(sample));
        }
    }
    live.last_rx_ms = now_ms;
    if (h.body_len || h.type == PacketType::confirm) {
        if (!live.ack_pending) live.ack_since_ms = now_ms;
        live.ack_pending = true;
    }
    Delivery d{};
    if (h.type == PacketType::data) {
        d.data = true;
        d.body = data + kLinkHeaderSize;
        d.size = h.body_len;
        d.send_ms = h.send_ms;
    }
    return d;
}

void Session::SendReject(RejectReason reason, const HmacKey* key, const std::uint8_t* echo, const Endpoint& to) noexcept {
    std::uint8_t datagram[kLinkOverhead + kRejectBodySize]{};
    Header h{};
    h.type = PacketType::reject;
    h.flags = key ? 0 : HeaderFlag::Unauth;
    h.body_len = static_cast<std::uint16_t>(kRejectBodySize);
    EncodeHeader(h, datagram);
    Reject r{};
    r.reason = static_cast<std::uint8_t>(reason);
    r.proto = kLinkProto;
    std::memcpy(r.build, config_.build, kBuildSize);
    if (echo) std::memcpy(r.echo_nonce_g, echo, kNonceSize);
    EncodeRejectBody(r, datagram + kLinkHeaderSize);
    if (key) WriteTag(*key, datagram, sizeof(datagram));
    Send(to, datagram, sizeof(datagram));
}

bool Session::SendUnauthReject(RejectReason reason, const Endpoint& to, std::uint64_t now_ms) noexcept {
    if (unauth_sent_ && now_ms - unauth_last_ms_ < kUnauthRejectGapMs) return false;
    unauth_sent_ = true;
    unauth_last_ms_ = now_ms;
    SendReject(reason, nullptr, nullptr, to);
    return true;
}

Delivery Session::OnDatagram(const std::uint8_t* data, std::size_t size, const Endpoint& from, std::uint64_t now_ms) noexcept {
    ++stats_.rx_packets;
    stats_.rx_bytes += size;
    if (state_ == LinkState::off || state_ == LinkState::disabled) return Delivery{};
    Header h{};
    const DecodeResult decoded = DecodeHeader(data, size, h);
    if (decoded == DecodeResult::version_mismatch) {
        // Only ever a >= 1200-byte datagram: the 88-byte answer cannot amplify.
        if (config_.role == Role::host && SendUnauthReject(RejectReason::proto_mismatch, from, now_ms)) return Delivery{};
        Drop(DropReason::malformed);
        return Delivery{};
    }
    if (decoded != DecodeResult::ok) {
        Drop(DropReason::malformed);
        return Delivery{};
    }
    return config_.role == Role::host ? HostDatagram(h, data, size, from, now_ms) : GuestDatagram(h, data, size, from, now_ms);
}

// ---------------------------------------------------------------- host
bool Session::RateAllow(const Endpoint& from, std::uint64_t now_ms) noexcept {
    // Keyed on the address: one machine cycling source ports shares one bucket.
    RateEntry& e = rate_[AddressHash(from.address) % kRateTable];
    const std::uint64_t key = from.address;
    if (!e.used) e = RateEntry{true, key, kHelloBurst * 1000, now_ms};
    else if (e.key != key && now_ms - e.last_ms >= kRateIdleMs) e = RateEntry{true, key, kRateReplaceTokens * 1000, now_ms};
    // Otherwise a colliding address draws on the occupant's tokens.
    const std::uint64_t refill = (now_ms - e.last_ms) * kHelloRatePerSec;  // milli-tokens per ms
    e.last_ms = now_ms;
    const std::uint64_t tokens = e.milli_tokens + refill;
    e.milli_tokens = static_cast<std::uint32_t>(tokens > kHelloBurst * 1000ull ? kHelloBurst * 1000ull : tokens);
    if (e.milli_tokens < 1000) return false;
    e.milli_tokens -= 1000;
    return true;
}

bool Session::MacBudget(std::uint64_t now_ms) noexcept {
    const std::uint64_t refill = (now_ms - mac_last_ms_) * kMacBudgetPerSec;
    mac_last_ms_ = now_ms;
    const std::uint64_t tokens = mac_milli_tokens_ + refill;
    mac_milli_tokens_ = static_cast<std::uint32_t>(tokens > kMacBudgetPerSec * 1000ull ? kMacBudgetPerSec * 1000ull : tokens);
    if (mac_milli_tokens_ < 1000) return false;
    mac_milli_tokens_ -= 1000;
    return true;
}

bool Session::GuestMacBudget(std::uint64_t now_ms) noexcept {
    const std::uint64_t refill = (now_ms - guest_mac_last_ms_) * kGuestMacPerSec;
    guest_mac_last_ms_ = now_ms;
    const std::uint64_t tokens = guest_mac_milli_tokens_ + refill;
    guest_mac_milli_tokens_ = static_cast<std::uint32_t>(tokens > kGuestMacPerSec * 1000ull ? kGuestMacPerSec * 1000ull : tokens);
    if (guest_mac_milli_tokens_ < 1000) return false;
    guest_mac_milli_tokens_ -= 1000;
    return true;
}

bool Session::Seen(const std::uint8_t* ng) const noexcept {
    for (unsigned i = 0; i < seen_count_; ++i)
        if (std::memcmp(seen_[i], ng, kNonceSize) == 0) return true;
    return false;
}

void Session::HostHello(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from,
                        std::uint64_t now_ms) noexcept {
    // O(1) budgets before any MAC. The current (or lost) guest's address has
    // its own budget, so a flood from elsewhere cannot block its reconnect.
    const bool known = current_.valid && from.address == current_.endpoint.address;
    if (known ? !GuestMacBudget(now_ms) : (!RateAllow(from, now_ms) || !MacBudget(now_ms))) { Drop(DropReason::rate); return; }
    if (h.session_id != 0 || h.seq != 0 || h.body_len != kHelloBodySize || h.flags != 0) { Drop(DropReason::malformed); return; }
    if (!VerifyTag(hs_, data, size)) {
        Drop(DropReason::bad_mac);
        SendUnauthReject(RejectReason::bad_code, from, now_ms);  // 88 bytes for a 1200-byte HELLO
        return;
    }
    Hello hello{};
    DecodeHelloBody(data + kLinkHeaderSize, h.body_len, hello);
    RejectReason reason = RejectReason::none;
    if (hello.min_proto > kLinkProto || hello.proto < kLinkProto) reason = RejectReason::proto_mismatch;
    else if (hello.mode != ModeByte(config_.play)) reason = RejectReason::mode_mismatch;
    else if (hello.role != kRoleGuest) reason = RejectReason::role_mismatch;
    else if (!SameBuild(hello.build, config_.build)) {
        if (config_.play) {
            reason = RejectReason::build_mismatch;
        } else if (!SameBuild(warned_build_, hello.build)) {
            CopyCString(warned_build_, sizeof(warned_build_), hello.build);
            Event e = MakeEvent(EventKind::warn_build);
            CopyCString(e.remote_build, sizeof(e.remote_build), hello.build);
            Push(e);
        }
    }
    if (reason != RejectReason::none) {
        SendReject(reason, &hs_, hello.nonce_g, from);
        return;
    }
    if (Seen(hello.nonce_g)) { Drop(DropReason::replay); return; }
    // A live session keeps its endpoint: no takeover until it is lost.
    if (state_ == LinkState::connected && current_.valid && !SameEndpoint(from, current_.endpoint)) {
        SendReject(RejectReason::busy, &hs_, hello.nonce_g, from);
        return;
    }
    for (auto& slot : pending_) {
        if (slot.used && std::memcmp(slot.ng, hello.nonce_g, kNonceSize) == 0) {
            Send(from, slot.welcome, sizeof(slot.welcome));  // the same WELCOME again
            return;
        }
    }
    Pending* slot = &pending_[0];
    for (auto& candidate : pending_) {
        if (!candidate.used) { slot = &candidate; break; }
        if (candidate.created_ms < slot->created_ms) slot = &candidate;
    }
    std::uint8_t nh[kNonceSize];
    if (!Random(nh, sizeof(nh))) return;
    Pending fresh{};
    fresh.used = true;
    std::memcpy(fresh.ng, hello.nonce_g, kNonceSize);
    DeriveSessionKeys(config_.psk, hello.nonce_g, nh, fresh.keys);
    fresh.endpoint = from;
    fresh.created_ms = now_ms;
    CopyCString(fresh.build, sizeof(fresh.build), hello.build);
    Header w{};
    w.type = PacketType::welcome;
    w.session_id = fresh.keys.sid;
    w.send_ms = static_cast<std::uint32_t>(now_ms);
    w.body_len = static_cast<std::uint16_t>(kWelcomeBodySize);
    EncodeHeader(w, fresh.welcome);
    Welcome body{};
    body.proto = kLinkProto;
    body.mode = ModeByte(config_.play);
    body.role = kRoleHost;
    std::memcpy(body.nonce_h, nh, kNonceSize);
    std::memcpy(body.echo_nonce_g, hello.nonce_g, kNonceSize);
    std::memcpy(body.build, config_.build, kBuildSize);
    EncodeWelcomeBody(body, fresh.welcome + kLinkHeaderSize);
    WriteTag(hs_, fresh.welcome, sizeof(fresh.welcome));
    *slot = fresh;
    SecureZero(nh, sizeof(nh));
    Event e = MakeEvent(EventKind::handshake);
    e.step = HandshakeStep::hello_rx;
    e.peer = from;
    CopyCString(e.remote_build, sizeof(e.remote_build), hello.build);
    Push(e);
    Send(from, slot->welcome, sizeof(slot->welcome));
}

void Session::Commit(Pending& slot, const Endpoint& from, std::uint64_t now_ms) noexcept {
    const bool had = current_.valid;
    const bool same = had && SameEndpoint(current_.endpoint, from);
    const std::uint64_t gap = had ? now_ms - current_.last_rx_ms : 0;
    current_ = Live{};
    current_.valid = true;
    current_.sid = slot.keys.sid;
    current_.tx = slot.keys.h2g;
    current_.rx = slot.keys.g2h;
    current_.endpoint = from;
    current_.last_rx_ms = now_ms;
    current_.last_tx_ms = now_ms;
    std::memcpy(current_.ng, slot.ng, kNonceSize);
    std::memcpy(seen_[seen_head_], slot.ng, kNonceSize);
    seen_head_ = (seen_head_ + 1) % kSeenRing;
    if (seen_count_ < kSeenRing) ++seen_count_;
    CopyCString(remote_build_, sizeof(remote_build_), slot.build);
    remote_proto_ = kLinkProto;
    reject_reason_ = RejectReason::none;
    reject_verified_ = false;
    rtt_.Reset();
    loss_.Reset();
    jitter_.Reset();
    slot = Pending{};
    for (auto& other : pending_) other = Pending{};  // older attempts are superseded
    committed_ = true;
    last_commit_ms_ = now_ms;
    ++session_number_;
    state_ = LinkState::connected;
    Event e = MakeEvent(!had ? EventKind::connected : same ? EventKind::reconnected : EventKind::rebound);
    e.gap_ms = static_cast<std::uint32_t>(gap > 0xFFFFFFFFull ? 0xFFFFFFFFull : gap);
    Push(e);
}

Delivery Session::HostDatagram(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from,
                               std::uint64_t now_ms) noexcept {
    if (h.type == PacketType::welcome || h.type == PacketType::reject) { Drop(DropReason::unknown_msg); return Delivery{}; }
    if (h.type == PacketType::hello) {
        HostHello(h, data, size, from, now_ms);
        return Delivery{};
    }
    // CONFIRM or DATA.
    if (h.type == PacketType::confirm && (h.seq != 1 || h.body_len != 0)) { Drop(DropReason::malformed); return Delivery{}; }
    if (current_.valid && h.session_id == current_.sid) {
        if (!SameEndpoint(from, current_.endpoint)) { Drop(DropReason::wrong_endpoint); return Delivery{}; }
        if (h.type == PacketType::confirm) {
            // Our first DATA was lost: answer the repeated CONFIRM (<= 4/s).
            // CONFIRM always carries seq 1, so the window cannot catch a replay:
            // it counts only in the guest's resend phase (no guest DATA yet,
            // kConfirmAnswerMs after the commit), and it is never a liveness
            // signal (only fresh DATA brings a lost session back).
            if (!VerifyTag(current_.rx, data, size)) { Drop(DropReason::bad_mac); return Delivery{}; }
            if (state_ != LinkState::connected || current_.window.highest() > 1 || now_ms - last_commit_ms_ > kConfirmAnswerMs) {
                Drop(DropReason::replay);
                return Delivery{};
            }
            if (now_ms - dup_confirm_last_ms_ >= kDupConfirmGapMs) {
                dup_confirm_last_ms_ = now_ms;
                SendDataOn(current_, nullptr, 0, now_ms);
            }
            return Delivery{};
        }
        if (!current_.window.Fresh(h.seq)) { Drop(DropReason::replay); return Delivery{}; }
        if (!VerifyTag(current_.rx, data, size)) { Drop(DropReason::bad_mac); return Delivery{}; }
        Delivery d = Accept(current_, h, data, now_ms);
        if (state_ == LinkState::lost) {
            state_ = LinkState::connected;
            Push(MakeEvent(EventKind::connected));
        }
        return d;
    }
    for (auto& slot : pending_) {
        if (!slot.used || slot.keys.sid != h.session_id) continue;
        if (!SameEndpoint(from, slot.endpoint)) { Drop(DropReason::wrong_endpoint); return Delivery{}; }
        if (!h.seq) { Drop(DropReason::replay); return Delivery{}; }
        if (!VerifyTag(slot.keys.g2h, data, size)) { Drop(DropReason::bad_mac); return Delivery{}; }
        // At most one commit per kCommitMinGapMs; a later CONFIRM stays pending.
        if (committed_ && now_ms - last_commit_ms_ < kCommitMinGapMs) return Delivery{};
        Commit(slot, from, now_ms);
        Event e = MakeEvent(EventKind::handshake);
        e.step = HandshakeStep::confirm_rx;
        Push(e);
        Delivery d = Accept(current_, h, data, now_ms);
        d.new_session = true;
        SendDataOn(current_, nullptr, 0, now_ms);  // the guest completes on our first DATA
        return d;
    }
    Drop(DropReason::wrong_session);
    return Delivery{};
}

void Session::HostTimer(std::uint64_t now_ms) noexcept {
    if (state_ != LinkState::connected || !current_.valid) return;  // nothing at all is sent while lost
    if (now_ms - current_.last_rx_ms >= kHostLostMs) {
        state_ = LinkState::lost;
        Event e = MakeEvent(EventKind::lost);
        e.after_ms = static_cast<std::uint32_t>(now_ms - current_.last_rx_ms);
        Push(e);
        return;
    }
    TimerLive(current_, now_ms);
}

void Session::TimerLive(Live& live, std::uint64_t now_ms) noexcept {
    if (live.ack_pending && now_ms - live.ack_since_ms >= kAckDelayMs) SendDataOn(live, nullptr, 0, now_ms);
    else if (now_ms - live.last_tx_ms >= kKeepaliveMs) SendDataOn(live, nullptr, 0, now_ms);
}

// ---------------------------------------------------------------- guest
int Session::PendingNonce(const std::uint8_t* echo) const noexcept {
    for (unsigned i = 0; i < nonce_count_; ++i)
        if (ConstantTimeEqual(nonces_[i], echo, kNonceSize)) return static_cast<int>(i);
    return -1;
}

void Session::BeginConnecting(std::uint64_t now_ms, bool reconnect) noexcept {
    state_ = reconnect ? LinkState::reconnecting : LinkState::connecting;
    connecting_since_ms_ = now_ms;
    next_hello_ms_ = now_ms;
    attempt_ = 0;
    next_ = Live{};
    if (reconnect) {
        Event e = MakeEvent(EventKind::reconnecting);
        e.attempt = 1;
        Push(e);
    }
}

std::uint64_t Session::HelloInterval(std::uint64_t now_ms) const noexcept {
    if (state_ == LinkState::rejected) return kRejectedRetryMs;
    const std::uint64_t since = now_ms - connecting_since_ms_;
    if (since < kHelloFastPhaseMs) return kHelloFastMs;
    if (since < kHelloBackoffAfterMs) return kHelloSlowMs;
    return kHelloBackoffMs;
}

void Session::SendHello(std::uint64_t now_ms) noexcept {
    std::uint8_t ng[kNonceSize];
    if (!Random(ng, sizeof(ng))) return;
    std::memcpy(nonces_[nonce_head_], ng, kNonceSize);
    nonce_head_ = (nonce_head_ + 1) % kGuestNonces;
    if (nonce_count_ < kGuestNonces) ++nonce_count_;
    Header h{};
    h.type = PacketType::hello;
    h.send_ms = static_cast<std::uint32_t>(now_ms);
    h.body_len = static_cast<std::uint16_t>(kHelloBodySize);
    EncodeHeader(h, scratch_);
    Hello hello{};
    hello.proto = kLinkProto;
    hello.min_proto = kLinkProto;
    hello.mode = ModeByte(config_.play);
    hello.role = kRoleGuest;
    std::memcpy(hello.nonce_g, ng, kNonceSize);
    std::memcpy(hello.build, config_.build, kBuildSize);
    EncodeHelloBody(hello, scratch_ + kLinkHeaderSize);
    WriteTag(hs_, scratch_, kLinkMaxDatagram);
    ++attempt_;
    if (attempt_ == 1 || attempt_ % 10 == 0) {
        Event e = MakeEvent(state_ == LinkState::reconnecting ? EventKind::reconnecting : EventKind::connecting);
        e.attempt = attempt_;
        if (!(state_ == LinkState::reconnecting && attempt_ == 1)) Push(e);  // the transition already logged attempt 1
    }
    Send(config_.peer, scratch_, kLinkMaxDatagram);
}

void Session::SendConfirm(std::uint64_t now_ms) noexcept {
    next_.tx_seq = 1;
    confirm_sent_ms_ = now_ms;
    SendOn(next_, PacketType::confirm, nullptr, 0, 1, now_ms);
}

void Session::GuestWelcome(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from,
                           std::uint64_t now_ms) noexcept {
    if (state_ != LinkState::connecting && state_ != LinkState::reconnecting && state_ != LinkState::rejected) {
        Drop(DropReason::unknown_msg);
        return;
    }
    if (!config_.exact_filter && from.port != config_.peer.port) { Drop(DropReason::wrong_endpoint); return; }
    Welcome w{};
    if (h.flags != 0 || h.seq != 0 || !DecodeWelcomeBody(data + kLinkHeaderSize, h.body_len, w)) { Drop(DropReason::malformed); return; }
    const int index = PendingNonce(w.echo_nonce_g);
    if (index < 0) { Drop(DropReason::wrong_session); return; }
    if (!VerifyTag(hs_, data, size)) { Drop(DropReason::bad_mac); return; }
    SessionKeys keys{};
    DeriveSessionKeys(config_.psk, nonces_[index], w.nonce_h, keys);
    if (keys.sid != h.session_id || w.role != kRoleHost || w.mode != ModeByte(config_.play)) { Drop(DropReason::bad_mac); return; }
    if (from.address != config_.peer.address) ++stats_.src_changed;
    if (!SameBuild(w.build, config_.build) && !config_.play && !SameBuild(warned_build_, w.build)) {
        CopyCString(warned_build_, sizeof(warned_build_), w.build);
        Event e = MakeEvent(EventKind::warn_build);
        CopyCString(e.remote_build, sizeof(e.remote_build), w.build);
        Push(e);
    }
    next_ = Live{};
    next_.valid = true;
    next_.sid = keys.sid;
    next_.tx = keys.g2h;
    next_.rx = keys.h2g;
    next_.endpoint = config_.peer;
    next_.last_rx_ms = now_ms;
    std::memcpy(next_.ng, nonces_[index], kNonceSize);
    CopyCString(remote_build_, sizeof(remote_build_), w.build);
    remote_proto_ = w.proto;
    nonce_count_ = 0;
    nonce_head_ = 0;
    confirm_resends_ = 0;
    state_ = LinkState::handshake;
    Event e = MakeEvent(EventKind::handshake);
    e.step = HandshakeStep::welcome_rx;
    Push(e);
    SendConfirm(now_ms);
    e.step = HandshakeStep::confirm_tx;
    Push(e);
}

void Session::GuestReject(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from,
                          std::uint64_t now_ms) noexcept {
    if (!config_.exact_filter && from.port != config_.peer.port) { Drop(DropReason::wrong_endpoint); return; }
    Reject r{};
    if (h.seq != 0 || !DecodeRejectBody(data + kLinkHeaderSize, h.body_len, r)) { Drop(DropReason::malformed); return; }
    const auto reason = static_cast<RejectReason>(r.reason);
    if (h.flags & HeaderFlag::Unauth) {
        // Unauthenticated: a status hint while connecting, never a state change.
        const bool hint_state = state_ == LinkState::connecting || state_ == LinkState::reconnecting;
        const bool hint_reason = reason == RejectReason::bad_code || reason == RejectReason::proto_mismatch;
        if (!hint_state || !hint_reason || h.session_id != 0 || !AllZero(data + size - kLinkTagSize, kLinkTagSize)) {
            ++stats_.reject_ignored;
            return;
        }
        if (!hint_logged_ || now_ms - hint_logged_ms_ >= 10000) {
            hint_logged_ = true;
            hint_logged_ms_ = now_ms;
            Event e = MakeEvent(EventKind::rejected);
            e.reason = reason;
            e.verified = false;
            e.remote_proto = r.proto;
            CopyCString(e.remote_build, sizeof(e.remote_build), r.build);
            Push(e);
        }
        reject_reason_ = reason;
        reject_verified_ = false;
        remote_proto_ = r.proto;
        return;
    }
    bool verified = false;
    if (h.session_id == 0) {
        // K_hs: only an answer to one of our pending HELLOs (fresh echo).
        const bool waiting = state_ == LinkState::connecting || state_ == LinkState::reconnecting || state_ == LinkState::rejected;
        verified = waiting && PendingNonce(r.echo_nonce_g) >= 0 && VerifyTag(hs_, data, size);
    } else if (current_.valid && h.session_id == current_.sid) {
        verified = VerifyTag(current_.rx, data, size);
    }
    if (!verified || reason == RejectReason::none || r.reason > static_cast<std::uint8_t>(RejectReason::role_mismatch)) {
        ++stats_.reject_ignored;
        return;
    }
    if (reason == RejectReason::busy && h.session_id == 0 && state_ == LinkState::reconnecting && current_.valid &&
        now_ms - connecting_since_ms_ < kHostLostMs) {
        // Our own address moved (NAT rebinding): the host still holds the old
        // endpoint until it is lost. Keep the HELLO cadence, no state change.
        ++stats_.reject_ignored;
        return;
    }
    const bool changed = state_ != LinkState::rejected || reject_reason_ != reason;
    if (h.session_id != 0) current_.valid = false;
    state_ = LinkState::rejected;
    reject_reason_ = reason;
    reject_verified_ = true;
    remote_proto_ = r.proto;
    CopyCString(remote_build_, sizeof(remote_build_), r.build);
    next_ = Live{};
    next_hello_ms_ = now_ms + kRejectedRetryMs;
    if (changed) {
        Event e = MakeEvent(EventKind::rejected);
        e.reason = reason;
        e.verified = true;
        e.remote_proto = r.proto;
        Push(e);
    }
}

Delivery Session::GuestDatagram(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from,
                                std::uint64_t now_ms) noexcept {
    switch (h.type) {
    case PacketType::hello:
    case PacketType::confirm:
        Drop(DropReason::unknown_msg);
        return Delivery{};
    case PacketType::welcome:
        GuestWelcome(h, data, size, from, now_ms);
        return Delivery{};
    case PacketType::reject:
        GuestReject(h, data, size, from, now_ms);
        return Delivery{};
    default:
        break;
    }
    Live* live = nullptr;
    if (next_.valid && h.session_id == next_.sid) live = &next_;
    else if (current_.valid && h.session_id == current_.sid) live = &current_;
    if (!live) { Drop(DropReason::wrong_session); return Delivery{}; }
    if (!live->window.Fresh(h.seq)) { Drop(DropReason::replay); return Delivery{}; }
    if (!VerifyTag(live->rx, data, size)) { Drop(DropReason::bad_mac); return Delivery{}; }
    if (from.address != config_.peer.address) ++stats_.src_changed;
    bool fresh = false;
    if (live == &next_) {
        // The host's first DATA completes the handshake.
        const bool had = current_.valid;
        const std::uint64_t gap = had ? now_ms - current_.last_rx_ms : 0;
        current_ = next_;
        next_ = Live{};
        live = &current_;
        ++session_number_;
        state_ = LinkState::connected;
        reject_reason_ = RejectReason::none;
        reject_verified_ = false;
        connecting_since_ms_ = now_ms;
        rtt_.Reset();
        loss_.Reset();
        jitter_.Reset();
        fresh = true;
        Delivery d = Accept(current_, h, data, now_ms);
        d.new_session = true;
        Event e = MakeEvent(had ? EventKind::reconnected : EventKind::connected);
        e.gap_ms = static_cast<std::uint32_t>(gap > 0xFFFFFFFFull ? 0xFFFFFFFFull : gap);
        Push(e);
        return d;
    }
    Delivery d = Accept(*live, h, data, now_ms);
    if (state_ == LinkState::reconnecting) {
        // The old session came back before the new handshake completed.
        state_ = LinkState::connected;
        connecting_since_ms_ = now_ms;
        nonce_count_ = 0;
        Push(MakeEvent(EventKind::connected));
    }
    (void)fresh;
    return d;
}

void Session::GuestTimer(std::uint64_t now_ms) noexcept {
    switch (state_) {
    case LinkState::connecting:
    case LinkState::reconnecting:
    case LinkState::rejected:
        if (now_ms >= next_hello_ms_) {
            SendHello(now_ms);
            next_hello_ms_ = now_ms + HelloInterval(now_ms);
        }
        break;
    case LinkState::handshake:
        if (now_ms - confirm_sent_ms_ >= kConfirmResendMs) {
            if (confirm_resends_ < kConfirmMaxResends) {
                ++confirm_resends_;
                SendConfirm(now_ms);
            } else {
                // No DATA came back: start over with a fresh nonce.
                const bool reconnect = current_.valid;
                const std::uint64_t since = connecting_since_ms_;
                BeginConnecting(now_ms, reconnect);
                connecting_since_ms_ = since;  // the backoff counts from the first attempt
            }
        }
        break;
    case LinkState::connected:
        if (now_ms - current_.last_rx_ms >= kGuestReconnectMs) {
            BeginConnecting(now_ms, true);
            break;
        }
        break;
    default:
        break;
    }
    // The current session (also the old one while reconnecting) keeps its
    // keepalives and acknowledgements.
    if (current_.valid && (state_ == LinkState::connected || state_ == LinkState::reconnecting)) TimerLive(current_, now_ms);
}

void Session::OnTimer(std::uint64_t now_ms) noexcept {
    if (state_ == LinkState::off || state_ == LinkState::disabled) return;
    if (config_.role == Role::host) HostTimer(now_ms);
    else GuestTimer(now_ms);
}

} // namespace mgscoop::link
