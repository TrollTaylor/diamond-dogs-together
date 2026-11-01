// Supply drops synced (docs/world-sync-protocol.md section 25, lua/coop/world_supply.lua): the observer's request
// decode, event ring, receipt grammar and poll reply, the SUPPLY wire op (0x45) and world note 21. Pure: no game, UI or
// OS call.
#include "mgscoop/supply_sync.hpp"
#include "mgscoop/coop_reason_text.hpp"
#include "mgscoop/world_receipt.hpp"
#include "mgscoop/world_wire.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ss = mgscoop::supply_sync;
namespace {
unsigned checks{}, failures{};
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
bool Has(const char* text, const char* part) { return text && part && std::strstr(text, part) != nullptr; }

void PutU32(unsigned char* p, std::uint32_t v) { std::memcpy(p, &v, 4); }
void PutF(unsigned char* p, float v) { std::memcpy(p, &v, 4); }

void Decode() {
    unsigned char req[ss::kRequestSize]{};
    PutU32(req + 0x0, 3); PutU32(req + 0x4, 0x1234); PutU32(req + 0x8, 7);
    PutF(req + 0x100, -1234.5674f); PutF(req + 0x104, 321.0f); PutF(req + 0x108, 0.0004f);
    const std::uint16_t aim = 0xFFFF; std::memcpy(req + 0x110, &aim, 2);
    req[0x112] = 7; req[0x120] = 2; req[0x12d] = 0x16;
    ss::Event e{};
    Check(ss::DecodeRequest(req, sizeof(req), e), "a full request decodes");
    Check(e.kind == ss::kRequest && e.drop_type == 3 && e.equip_id == 0x1234 && e.parts_type == 7, "drop type, equip, parts");
    Check(e.pos_mm[0] == -1234567 && e.pos_mm[1] == 321000 && e.pos_mm[2] == 0, "the aim position in mm, rounded to nearest");
    Check(e.aim_object == 0xFFFF && e.wait_level == 7 && e.level == 2 && e.flags == 0x16, "aim object, wait level, level, flags");
    ss::Event keep{};
    keep.drop_type = 99;
    Check(!ss::DecodeRequest(req, sizeof(req) - 1, keep) && keep.drop_type == 99, "a short buffer is refused, out untouched");
    Check(!ss::DecodeRequest(nullptr, sizeof(req), keep), "no buffer");
    PutF(req + 0x104, std::numeric_limits<float>::quiet_NaN());
    Check(!ss::DecodeRequest(req, sizeof(req), keep), "a NaN position is refused");
    PutF(req + 0x104, 2.0e6f);
    Check(!ss::DecodeRequest(req, sizeof(req), keep), "a position beyond 1e6 m is refused");
    PutF(req + 0x104, std::numeric_limits<float>::infinity());
    Check(!ss::DecodeRequest(req, sizeof(req), keep), "an infinite position is refused");
}

void RingOrder() {
    ss::Ring r;
    std::uint32_t lost = 9;
    ss::Event out[ss::kRing]{};
    Check(r.Last() == 0 && r.Since(0, out, ss::kRing, lost) == 0 && lost == 0, "empty ring");
    ss::Event e{};
    e.kind = ss::kRequest; e.drop_type = 1;
    Check(r.Push(e) == 1, "first seq is 1");
    e.kind = ss::kReset;
    Check(r.Push(e) == 2 && r.Last() == 2, "second seq is 2");
    auto n = r.Since(0, out, ss::kRing, lost);
    Check(n == 2 && out[0].seq == 1 && out[0].kind == ss::kRequest && out[1].seq == 2 && out[1].kind == ss::kReset && lost == 0,
          "oldest first");
    n = r.Since(1, out, ss::kRing, lost);
    Check(n == 1 && out[0].seq == 2, "only the events after the given seq");
    Check(r.Since(2, out, ss::kRing, lost) == 0, "nothing after the last");
    n = r.Since(0, out, 1, lost);
    Check(n == 1 && out[0].seq == 1, "max caps the count, oldest first (the rest on the next poll)");
    for (unsigned i = 0; i < ss::kRing + 4; ++i) r.Push(e);
    n = r.Since(0, out, ss::kRing, lost);
    Check(n == ss::kRing && lost == 6 && out[0].seq == 7 && out[ss::kRing - 1].seq == 22, "an overflowed ring reports the lost events");
    n = r.Since(10, out, ss::kRing, lost);
    Check(n == 12 && lost == 0 && out[0].seq == 11, "a reader within the ring loses nothing");
    Check(r.Since(500, out, ss::kRing, lost) == 0 && lost == 0, "a seq from an older session (beyond last) reads nothing");
    r.Clear();
    Check(r.Last() == 0 && r.Since(0, out, ss::kRing, lost) == 0, "clear");
}

void Parse() {
    ss::Parsed p{};
    const auto parse = [&](const char* s) { return ss::ParseCommand(s, std::strlen(s), p); };
    Check(parse("poll 0") && p.cmd == ss::Command::poll && p.arg == 0, "poll 0");
    Check(parse("poll 4294967295") && p.arg == 4294967295u, "poll u32 max");
    Check(parse("mirror 1") && p.cmd == ss::Command::mirror && p.arg == 1, "mirror 1");
    Check(parse("mirror 0") && p.cmd == ss::Command::mirror && p.arg == 0, "mirror 0");
    Check(parse("remove 7") && p.cmd == ss::Command::remove && p.arg == 7, "remove 7");
    Check(!parse("mirror 2") && !parse("poll") && !parse("poll 01") && !parse("poll -1") && !parse("poll 1 ") &&
              !parse("poll  1") && !parse("poll 4294967296") && !parse("drop 1") && !parse("") && !parse("remove x"),
          "bad commands refuse");
}

void Poll() {
    char out[1024]{};
    ss::Snapshot off{};
    Check(ss::FormatPoll(off, nullptr, 0, 0, 0, out, sizeof(out)) && std::strcmp(out, "return {on=0}") == 0, "key off: on=0");
    ss::Snapshot s{};
    s.on = s.installed = s.box_known = true; s.state = 12; s.flags = 8;
    ss::Event ev[2]{};
    ev[0].seq = 3; ev[0].kind = ss::kRequest; ev[0].mirror = 0; ev[0].drop_type = 3; ev[0].equip_id = 5; ev[0].parts_type = 0;
    ev[0].pos_mm[0] = -100; ev[0].pos_mm[1] = 200; ev[0].pos_mm[2] = 300; ev[0].level = 1; ev[0].wait_level = 7; ev[0].flags = 4; ev[0].aim_object = 0xFFFF;
    ev[0].caller_rva = 0x15C3ED9; ev[0].state = 0;
    ev[1].seq = 4; ev[1].kind = ss::kReset; ev[1].mirror = 1; ev[1].caller_rva = 0x15CB885; ev[1].state = 14;
    const auto n = ss::FormatPoll(s, ev, 2, 4, 1, out, sizeof(out));
    Check(n && n == std::strlen(out), "poll reply length");
    Check(Has(out, "return {on=1,inst=1,box=1,st=12,fl=8,last=4,lost=1,ev={"), "the state header");
    Check(Has(out, "{3,1,0,3,5,0,-100,200,300,1,7,4,65535,22822617,0,1}"), "a request event (accepted 1 by default)");
    Check(Has(out, "{4,2,1,0,0,0,0,0,0,0,0,0,65535,22853765,14,1}"), "a reset event");
    Check(out[n - 1] == '}' && out[n - 2] == '}', "closed");
    Check(ss::FormatPoll(s, ev, 2, 4, 0, out, 40) == 0, "a reply that does not fit is 0");
    ss::Snapshot noBox = s;
    noBox.box_known = false;
    Check(ss::FormatPoll(noBox, nullptr, 0, 0, 0, out, sizeof(out)) && Has(out, "box=0,st=0,fl=0") && Has(out, "ev={}"),
          "no box core seen yet: box=0 and no state");
    char line[256]{};
    Check(ss::FormatLogLine(ev[0], line, sizeof(line)) && Has(line, "supply=request;seq=3;mirror=0;drop=3;equip=5;parts=0;") &&
              Has(line, "x_mm=-100;y_mm=200;z_mm=300;level=1;wait=7;flags=0x4;aim=65535;caller=0x15c3ed9;state=0;accepted=1") &&
              Has(line, "\r\n"), "request log line");
    Check(ss::FormatLogLine(ev[1], line, sizeof(line)) && Has(line, "supply=reset;seq=4;mirror=1;caller=0x15cb885;state=14"),
          "reset log line");
}

// Review r1101 (supply-refused-request-mirrored): the detour calls the original first and records whether the box core took
// the request. The core's refused branch ([Z+0x20]->vt[0]() true: jnz to the epilogue at +0x140) touches nothing in Z;
// an accepted request copies itself to Z+0x4E0 and writes the state (Z+0xF0) and the flags (Z+0x124 |= 2 | 8 or 0x10).
void Accepted() {
    ss::CoreView before{};
    before.ok = true; before.state = 8; before.flags = 0x8;
    std::memset(before.stored, 0x11, sizeof(before.stored));
    ss::CoreView after = before;
    Check(!ss::RequestAccepted(before, after), "nothing in Z changed: refused");
    after.flags = 0x8 | 0x2;
    Check(ss::RequestAccepted(before, after), "the reset bit newly set: accepted");
    after = before; after.state = 0;
    Check(ss::RequestAccepted(before, after), "the state written: accepted");
    after = before; after.stored[0x100] = 0x22;
    Check(ss::RequestAccepted(before, after), "the stored request changed: accepted");
    after = before; after.ok = false;
    Check(ss::RequestAccepted(before, after), "an unreadable core after the call: accepted (as before the fix)");
    ss::CoreView unread{};
    Check(ss::RequestAccepted(unread, before), "an unreadable core before the call: accepted");
    ss::Snapshot s{};
    s.on = s.installed = s.box_known = true;
    ss::Event ev{};
    ev.seq = 9; ev.kind = ss::kRequest; ev.accepted = 0; ev.aim_object = 0xFFFF;
    char out[512]{};
    Check(ss::FormatPoll(s, &ev, 1, 9, 0, out, sizeof(out)) && Has(out, "{9,1,0,0,0,0,0,0,0,0,0,0,65535,0,0,0}"),
          "a refused request: the 16th field is 0");
    char line[256]{};
    Check(ss::FormatLogLine(ev, line, sizeof(line)) && Has(line, ";accepted=0\r\n"), "and its log line says accepted=0");
}

void Wire() {
    namespace w = mgscoop::world;
    const auto* spec = w::FindOp(ss::kOp);
    Check(spec && std::strcmp(spec->name, "SUPPLY") == 0 && spec->code == 0x45 && spec->queue == 1 && spec->dir == w::Dir::both &&
              spec->wire == w::Wire::reliable && spec->level == 3 && !spec->request && !spec->lab_only && spec->lua == 14 &&
              spec->native == 0 && spec->body == 31,
          "SUPPLY 0x45: world queue, both directions, paired, 14 Lua fields, 31 bytes");
    if (!spec) return;
    Check(w::FieldValid(spec->fields[0], 1) && !w::FieldValid(spec->fields[0], 0), "seq 1..");
    Check(w::FieldValid(spec->fields[1], 1) && w::FieldValid(spec->fields[1], 2) && !w::FieldValid(spec->fields[1], 0) &&
              !w::FieldValid(spec->fields[1], 3), "kind 1 drop | 2 gone");
    Check(w::FieldValid(spec->fields[2], 1) && w::FieldValid(spec->fields[2], 2) && !w::FieldValid(spec->fields[2], 3),
          "origin 1 host | 2 guest");
    Check(w::FieldValid(spec->fields[3], 1) && !w::FieldValid(spec->fields[3], 0), "the origin's box seq 1..");
    Check(w::FieldValid(spec->fields[7], -1000000000) && w::FieldValid(spec->fields[9], 1000000000), "xyz mm");
    Check(w::FieldValid(spec->fields[10], 0) && w::FieldValid(spec->fields[10], 255) && w::FieldValid(spec->fields[11], 7) &&
              w::FieldValid(spec->fields[12], 0x16), "level, wait level and flags are bytes");
    Check(w::FieldValid(spec->fields[13], 0) && w::FieldValid(spec->fields[13], 3) && !w::FieldValid(spec->fields[13], 4),
          "reason 0..3");
}

void Note() {
    namespace text = mgscoop::coop_text;
    char s[256]{};
    Check(mgscoop::world::NoteShown(21) && !mgscoop::world::NoteShown(20) && !mgscoop::world::NoteShown(22),
          "note 21 is shown (20 and 22 are the other tracks')");
    Check(text::FormatWorldNote(21, 1, 0, 0, 0, s, sizeof(s)) && Has(s, "supply drop") && Has(s, "both"), "note 21 a=1: the partner's drop");
    Check(text::FormatWorldNote(21, 2, 0, 0, 0, s, sizeof(s)) && Has(s, "partner") && Has(s, "supply"), "note 21 a=2: the partner took it");
    Check(!text::FormatWorldNote(21, 3, 0, 0, 0, s, sizeof(s)) && !text::FormatWorldNote(21, 0, 0, 0, 0, s, sizeof(s)),
          "note 21 other values: no toast");
    std::uint8_t code = 0;
    std::uint32_t a = 0, b = 9;
    char receipt[96]{};
    std::snprintf(receipt, sizeof(receipt), "%s21 1 0", mgscoop::world::note_prefix);
    Check(mgscoop::world::ParseNote(receipt, std::strlen(receipt), code, a, b) && code == 21 && a == 1 && b == 0,
          "the NOTE_V1 receipt admits note 21");
}
} // namespace

int main() {
    Decode();
    RingOrder();
    Parse();
    Poll();
    Accepted();
    Wire();
    Note();
    std::printf("supply_sync_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
