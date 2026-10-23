// Original MGS Co-op: the gesture lab lever, pure part (partner_gesture_lever.hpp).
#include "mgscoop/partner_gesture_lever.hpp"
#include <cstdio>
#include <cstring>

namespace mgscoop::partner_gesture_lever {

const char* RequestName(Request request) noexcept {
    switch (request) {
    case Request::knock: return "knock";
    case Request::sign_go: return "sign_go";
    case Request::sign_stop: return "sign_stop";
    default: return "none";
    }
}

bool IsCommand(const char* text) noexcept {
    if (!text) return false;
    const std::size_t n = sizeof(kCommand) - 1;
    return std::strncmp(text, kCommand, n) == 0 && (text[n] == 0 || text[n] == ' ');
}

Request Parse(const char* text) noexcept {
    if (!IsCommand(text)) return Request::none;
    const char* arg = text + sizeof(kCommand) - 1;
    if (*arg != ' ') return Request::none;
    ++arg;
    if (std::strcmp(arg, "knock") == 0) return Request::knock;
    if (std::strcmp(arg, "sign_go") == 0) return Request::sign_go;
    if (std::strcmp(arg, "sign_stop") == 0) return Request::sign_stop;
    return Request::none;
}

Slot SlotFor(Request request) noexcept {
    switch (request) {
    case Request::knock: return Slot{0x28, 0x11E5100, 1, 0x17};
    case Request::sign_go: return Slot{0x30, 0x11E5060, 2, 0x18};
    case Request::sign_stop: return Slot{0x38, 0x11E50B0, 3, 0x19};
    default: return Slot{0, 0, 0, 0};
    }
}

const char* Admit(const Gate& g) noexcept {
    if (!g.key) return "key_off";
    if (!g.lab_mode) return "mode";
    if (!g.lab_active) return "lab_inactive";
    if (!g.saves_redirected) return "saves_not_redirected";
    if (!g.offline) return "offline_branch";
    if (!g.partner_on) return "partner_off";
    if (!g.code_gate) return "code_gate";
    if (!g.gameplay) return "gameplay";
    if (g.shutting_down) return "shutting_down";
    if (g.latched) return "latched";
    return nullptr;
}

std::uint8_t StanceOf(std::uint8_t stance, std::uint32_t recc) noexcept {
    return stance == 3 && (recc & kRecCCrawlBit) ? std::uint8_t{4} : stance;
}

const char* Precondition(Request request, const Local& l, std::uint32_t recc) noexcept {
    switch (request) {
    case Request::knock:
        // UpdateCommandSetKnock 0x110F8D0: the call menu's knock entry.
        if (!((l.act >= 1 && l.act <= 20) || (l.act >= 31 && l.act <= 37))) return "act";
        if (l.upper != 0) return "upper";
        if (l.q28 != 0) return "q28";
        if (l.speed == 2 || l.speed == 3) return "speed";
        return nullptr;
    case Request::sign_go:
    case Request::sign_stop:
        // FUN_11E3A20 kinds 2 / 3 and the polling's GetStance test (0x11E5160).
        if (l.speed > 1) return "speed";
        if (l.act == 0x79 || l.act == 0x7A) return "act";
        if (recc & kRecCSignBit) return "rec_c28";
        if (StanceOf(l.stance, recc) == 4) return "stance";
        if (l.upper != 0) return "upper";
        return nullptr;
    default:
        return "args";
    }
}

std::size_t FormatResult(const Outcome& o, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    int n = 0;
    if (o.refusal) {
        n = std::snprintf(out, capacity, "gesture_lever=refused;reason=%s;kind=%s;act=%u;upper=0x%02x;stance=%u;speed=%u", o.refusal,
                          RequestName(o.request), unsigned(o.local.act), unsigned(o.local.upper), unsigned(o.local.stance),
                          unsigned(o.local.speed));
    } else {
        n = std::snprintf(out, capacity, "gesture_lever=requested;kind=%s;id=%u;act=%u;stance=%u;speed=%u;row=%u,0x%02x,%u;other=same",
                          RequestName(o.request), unsigned(o.id), unsigned(o.local.act), unsigned(o.local.stance),
                          unsigned(o.local.speed), unsigned(o.row_kind), unsigned(o.row_d0), unsigned(o.row_life));
    }
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

std::size_t FormatLog(const Outcome& o, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const int n = std::snprintf(
        out, capacity,
        "partner_gesture=lever; result=%s; reason=%s; kind=%s; id=%u; act=%u; upper=0x%02x; q28=%u; stance=%u; speed=%u; called=%u; "
        "row_kind=%u; row_d0=0x%02x; row_life=%u; frame=%u; rec4=0x%08x; rec8=0x%08x; recc=0x%08x; other_same=%u\r\n",
        o.refusal ? "refused" : "requested", o.refusal ? o.refusal : "none", RequestName(o.request), unsigned(o.id), unsigned(o.local.act),
        unsigned(o.local.upper), unsigned(o.local.q28), unsigned(o.local.stance), unsigned(o.local.speed), o.called ? 1u : 0u,
        unsigned(o.row_kind), unsigned(o.row_d0), unsigned(o.row_life), unsigned(o.row_frame), unsigned(o.rec4), unsigned(o.rec8),
        unsigned(o.recc), o.other_same ? 1u : 0u);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

}  // namespace mgscoop::partner_gesture_lever
