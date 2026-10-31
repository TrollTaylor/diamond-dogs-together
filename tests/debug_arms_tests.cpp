// Debug menu arms (docs/debug-menu.md "Give weapon" and "Friendly fire"): the menu rows, the loaded-gun reply, the
// Lua state the menu shows, the command text, the ARMS wire op (0x3F) and world note 18. Pure: no game, UI or OS call.
#include "mgscoop/debug_arms.hpp"
#include "mgscoop/debug_actions.hpp"
#include "mgscoop/coop_reason_text.hpp"
#include "mgscoop/world_receipt.hpp"
#include "mgscoop/world_wire.hpp"
#include <cstdio>
#include <cstring>

namespace arms = mgscoop::debug_arms;
namespace act = mgscoop::debug_actions;
namespace {
unsigned checks{}, failures{};
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
bool Is(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

void Rows() {
    const auto* give = act::FindAction("arms_give");
    const auto* ff = act::FindAction("arms_friendly_fire");
    Check(give && give->category == act::Category::player && Is(give->command, "give_weapon equip") &&
              (give->flags & act::kParam) && !(give->flags & act::kHostOnly), "Give weapon: a Player row, both players, a parameter row");
    Check(ff && ff->category == act::Category::partner && Is(ff->command, "friendly_fire 0") && (ff->flags & act::kParam) &&
              (ff->flags & act::kToggle) && (ff->flags & act::kSyncs) && !(ff->flags & act::kHostOnly),
          "Friendly fire: a Partner row, both players press it (a guest's press asks the host), synced");
    Check(give && ff && Is(give->label, "Give weapon") && Is(ff->label, "Friendly fire"), "short plain labels");
    // Contiguous blocks at the end of their categories (mergeable with the other tracks' rows).
    std::size_t count = 0;
    const auto* all = act::Actions(count);
    std::size_t give_at = count, ff_at = count, last_player = 0, last_partner = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Is(all[i].id, "arms_give")) give_at = i;
        if (Is(all[i].id, "arms_friendly_fire")) ff_at = i;
        if (all[i].category == act::Category::player) last_player = i;
        if (all[i].category == act::Category::partner) last_partner = i;
    }
    Check(give_at == last_player && ff_at == last_partner, "the arms rows close the Player and Partner lists");
    const act::ActionDef* found[8]{};
    Check(act::SearchActions("give weapon", found, 8) >= 1 && act::SearchActions("friendly fire", found, 8) >= 1,
          "search finds both rows");
}

void Reply() {
    const arms::Entry entries[] = {{1, 2, 3}, {32, 1, 3}, {871, 1, 1}, {873, 4, 1}, {46, 4, 3}, {9, 2, 3}, {300, 12, 3}, {40, 1, 1}};
    char out[256]{};
    auto n = arms::FormatResidentReply(true, 30050, entries, sizeof(entries) / sizeof(entries[0]), out, sizeof(out));
    Check(n && Is(out, "return {30050,1,2,32,1,46,4,9,2}"),
          "loaded guns only: protected (flags 3), stock ids below 0x230, gun types 1..8");
    n = arms::FormatResidentReply(false, 30050, entries, 3, out, sizeof(out));
    Check(n && Is(out, "return nil"), "no census: nil");
    arms::Entry many[40]{};
    for (unsigned i = 0; i < 40; ++i) many[i] = {static_cast<std::uint16_t>(i + 1), 1, 3};
    n = arms::FormatResidentReply(true, 30010, many, 40, out, sizeof(out));
    unsigned commas = 0;
    for (const char* p = out; *p; ++p) commas += *p == ',';
    Check(n && commas == 2 * arms::kMaxGuns, "at most 16 guns");
    char tiny[8]{};
    Check(arms::FormatResidentReply(true, 30010, entries, 8, tiny, sizeof(tiny)) == 0, "a reply that does not fit is refused");
}

void StateParse() {
    arms::State s{};
    const char good[] = "0 30010 2 39.1.East_ar_010 6.2.West_hg_010";
    Check(arms::ParseState(good, sizeof(good) - 1, s) && s.known && !s.friendly_fire && s.mission == 30010 && s.count == 2 &&
              s.guns[0].equip == 39 && s.guns[0].type == 1 && Is(s.guns[0].code, "East_ar_010") && s.guns[1].equip == 6 &&
              s.guns[1].type == 2, "state: friendly fire, mission and the listed guns");
    const char none[] = "1 30050 0";
    Check(arms::ParseState(none, sizeof(none) - 1, s) && s.friendly_fire && s.count == 0, "an empty list");
    for (const char* bad : {"", "2 30010 0", "1 30010 1", "1 30010 1 39.1", "1 30010 1 39.9.East_ar_010", "1 30010 1 0.1.x",
                            "1 30010 1 39.1.East-ar", "1 30010 0 39.1.x", "1 30010 17", "1 30010 1 39.1.East_ar_010 ",
                            "1 30010 1 39.1.ABCDEFGHIJKLMNOPQRSTUVWXYZ", "1 x 0", "1 30010 1 600.1.x"}) {
        arms::State t{};
        Check(!arms::ParseState(bad, std::strlen(bad), t) && !t.known, "malformed state refused");
    }
    arms::State stored{};
    Check(!arms::Get().known && arms::Get().friendly_fire, "nothing stored yet: unknown, friendly fire shown on");
    arms::ParseState(good, sizeof(good) - 1, stored);
    arms::Store(stored);
    const auto got = arms::Get();
    Check(got.known && !got.friendly_fire && got.count == 2 && got.guns[1].equip == 6, "store round trip");
}

void Text() {
    Check(Is(arms::KindName(1), "Assault rifle") && Is(arms::KindName(2), "Handgun") && Is(arms::KindName(3), "SMG") &&
              Is(arms::KindName(4), "Sniper rifle") && Is(arms::KindName(5), "Shotgun") && Is(arms::KindName(6), "Machine gun") &&
              Is(arms::KindName(7), "Grenade launcher") && Is(arms::KindName(8), "Missile") && !arms::KindName(0) && !arms::KindName(9),
          "gun kinds");
    arms::Gun gun{};
    gun.equip = 39; gun.type = 1; std::snprintf(gun.code, sizeof(gun.code), "East_ar_010");
    char label[64]{};
    Check(arms::FormatGunLabel(gun, label, sizeof(label)) && Is(label, "Assault rifle (East_ar_010)"), "gun label");
    std::size_t n = 0;
    const auto* items = arms::SupportItems(n);
    Check(n == 7 && Is(items[0].token, "EQP_SWP_Grenade") && Is(items[0].label, "Grenade") && Is(items[5].token, "EQP_SWP_C4"),
          "the stock support items, grenade first");
    char cmd[64]{};
    Check(arms::FormatGiveGun(cmd, sizeof(cmd), 39) && Is(cmd, "give_weapon 39"), "give a gun by id");
    Check(!arms::FormatGiveGun(cmd, sizeof(cmd), 0) && !arms::FormatGiveGun(cmd, sizeof(cmd), arms::kAliasFirst), "no id 0, no alias");
    Check(arms::FormatGiveSupport(cmd, sizeof(cmd), "EQP_SWP_C4") && Is(cmd, "give_weapon EQP_SWP_C4"), "give a support item");
    Check(!arms::FormatGiveSupport(cmd, sizeof(cmd), "EQP_SWP_Kibidango") && !arms::FormatGiveSupport(cmd, sizeof(cmd), nullptr),
          "only listed support items");
    Check(arms::FormatFriendlyFire(cmd, sizeof(cmd), false) && Is(cmd, "friendly_fire 0") &&
              arms::FormatFriendlyFire(cmd, sizeof(cmd), true) && Is(cmd, "friendly_fire 1"), "friendly fire text");
}

void Wire() {
    namespace w = mgscoop::world;
    const auto* spec = w::FindOp(0x3F);
    Check(spec && Is(spec->name, "ARMS") && spec->code == 0x3F && spec->queue == 0 && spec->dir == w::Dir::both &&
              spec->wire == w::Wire::reliable && spec->level == 3 && !spec->request && !spec->lab_only && spec->lua == 3 &&
              spec->native == 0 && spec->body == 4, "ARMS 0x3F: control queue, both directions, paired, 4 bytes");
    Check(spec && w::FieldValid(spec->fields[1], 1) && w::FieldValid(spec->fields[1], 2) && !w::FieldValid(spec->fields[1], 3) &&
              w::FieldValid(spec->fields[2], 0) && w::FieldValid(spec->fields[2], 1) && !w::FieldValid(spec->fields[2], 2) &&
              !w::FieldValid(spec->fields[0], 0), "ARMS fields: seq 1.., kind 1 ask | 2 state, friendly fire 0 | 1");
}

void Note() {
    namespace text = mgscoop::coop_text;
    char s[256]{};
    Check(mgscoop::world::NoteShown(18) && !mgscoop::world::NoteShown(15) && !mgscoop::world::NoteShown(19),
          "note 18 is shown (15..17 stay for the other tracks)");
    Check(text::FormatWorldNote(18, 1, 0, 0, 0, s, sizeof(s)) && std::strstr(s, "Friendly fire is OFF"), "note 18 off sentence");
    Check(text::FormatWorldNote(18, 1, 1, 0, 0, s, sizeof(s)) && std::strstr(s, "Friendly fire is ON"), "note 18 on sentence");
    Check(!text::FormatWorldNote(18, 1, 2, 0, 0, s, sizeof(s)) && !text::FormatWorldNote(18, 2, 0, 0, 0, s, sizeof(s)),
          "note 18 other values: no toast");
}
} // namespace

int main() {
    Rows();
    Reply();
    StateParse();
    Text();
    Wire();
    Note();
    std::printf("debug_arms_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
