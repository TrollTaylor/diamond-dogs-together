// Enemy sync M7 (docs/world-sync-design.md 7.13): the perception patch block check and the soldier damage authority.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/soldier_authority.hpp"
#include "mgscoop/soldier_perceive.hpp"

namespace {
unsigned checks = 0;
void Check(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
}  // namespace

int main() {
    namespace sp = mgscoop::soldier_perceive;
    namespace sa = mgscoop::soldier_authority;
    // The block: the patched dword sits at offset 0x3b (74 65 8b 55), one cache line, and only it may differ.
    Check(sp::kPatchOffset == 0x3b && sp::kBlockRva + sp::kPatchOffset == sp::kPatchRva, "the patch dword is at 0x132562b");
    Check(sp::kBlock[0x3b] == 0x74 && sp::kBlock[0x3c] == 0x65 && sp::kBlock[0x3d] == 0x8b && sp::kBlock[0x3e] == 0x55,
          "the original dword is je +0x65 and the next instruction's head");
    Check(((sp::kPatchRva & 63u) + 4u) <= 64u, "the dword does not span two cache lines");
    unsigned char live[sp::kBlockSize];
    std::memcpy(live, sp::kBlock, sizeof(live));
    bool patched = true;
    Check(sp::BlockMatches(live, sizeof(live), &patched) && !patched, "the retail block matches, unpatched");
    live[0x3b] = 0xeb;
    Check(sp::BlockMatches(live, sizeof(live), &patched) && patched, "the patched block matches, patched");
    live[0x3c] = 0x66;
    Check(!sp::BlockMatches(live, sizeof(live), &patched), "another branch distance refuses");
    std::memcpy(live, sp::kBlock, sizeof(live));
    live[0x20] ^= 1;
    Check(!sp::BlockMatches(live, sizeof(live)), "any other changed byte refuses");
    Check(!sp::BlockMatches(nullptr, sizeof(live)) && !sp::BlockMatches(sp::kBlock, sizeof(live) - 1), "null or short refuses");
    // The authority: a soldier's hit (GameObjectId type 2) on a player.
    constexpr std::uint16_t soldier = (2u << 9) | 37u, player = 0, dog = (5u << 9) | 1u;
    Check(sa::IsSoldier(soldier) && !sa::IsSoldier(player) && !sa::IsSoldier(dog) && !sa::IsSoldier(0xFFFF), "soldier ids");
    Check(sa::Decide(false, 1, 0, soldier, false) == sa::Action::stock, "off: stock");
    Check(sa::Decide(true, 0, 0, player, false) == sa::Action::stock, "a player attacker: stock");
    Check(sa::Decide(true, 0, 0, soldier, false) == sa::Action::stock, "an owned guard on this player: stock");
    Check(sa::Decide(true, 1, 0, soldier, false) == sa::Action::forward, "an owned guard on the replica: forward");
    Check(sa::Decide(true, 0, 0, soldier, true) == sa::Action::neutralize, "a remote guard on this player: neutralize");
    Check(sa::Decide(true, 1, 0, soldier, true) == sa::Action::neutralize, "a remote guard on the replica: neutralize");
    Check(sa::Decide(true, 2, 0, soldier, false) == sa::Action::stock && sa::Decide(true, 0, 2, soldier, false) == sa::Action::stock,
          "only players 0 and 1");
    Check(std::strcmp(sa::ActionName(sa::Action::forward), "forward") == 0, "action names");
    // Ownership bits and the role default.
    sa::Configure(true, false);
    sa::Clear();
    Check(sa::Enabled() && !sa::Remote(soldier) && sa::RemoteCount() == 0, "host: unknown soldiers are its own");
    Check(sa::Set(37, true) && sa::Remote(soldier) && sa::RemoteCount() == 1, "host: a guest-owned guard is remote");
    Check(!sa::Remote(player) && !sa::Remote(dog), "only soldiers are ever remote");
    Check(sa::Set(37, false) && !sa::Remote(soldier), "back to the host");
    Check(!sa::Set(512, true), "index 0..511");
    sa::Configure(true, true);
    sa::Clear();
    Check(sa::Remote(soldier) && sa::RemoteCount() == sa::kSoldiers, "guest: unknown soldiers are the host's");
    Check(sa::Set(37, false) && !sa::Remote(soldier) && sa::RemoteCount() == sa::kSoldiers - 1, "guest: its own guard");
    sa::Clear();
    Check(sa::Remote(soldier), "clear: the role default again");
    sa::Configure(false, false);
    Check(!sa::Enabled(), "off");
    // Probe P11 census: the scorer head pin and the category-5 word counts.
    Check(sp::kScorerHead[0] == 0x40 && sp::kScorerHead[1] == 0x55 && sp::kScorerHead[0x3b] == 0xb8, "the scorer head pin");
    sp::CensusReset();
    const unsigned char one[] = {0x10, 0x00, 0x00, 0x05, 0x22, 0x04, 0x00, 0x03};  // a player word 0x10, a category-3 entry
    sp::CensusAdd(one, 2);
    sp::CensusAdd(nullptr, 0);
    const unsigned char two[] = {0x10, 0x00, 0x00, 0x05, 0x11, 0x00, 0x00, 0x05};
    sp::CensusAdd(two, 2);
    auto cs = sp::CensusRead();
    Check(cs.calls == 3 && cs.entries == 4 && cs.players == 3 && cs.multi == 1 && cs.maxPerCall == 2 && cs.overflow == 0,
          "census counters");
    Check(cs.words == 2 && cs.word[0] == 0x10 && cs.count[0] == 2 && cs.word[1] == 0x11 && cs.count[1] == 1, "census words");
    char line[161]{};
    Check(sp::CensusLine(line, sizeof(line)) && std::strstr(line, "players=3;multi=1;max=2;overflow=0;words=10:2,11:1") != nullptr,
          "census line");
    for (unsigned w = 0; w < sp::kCensusWords + 2; ++w) {
        const unsigned char e[] = {static_cast<unsigned char>(0x40 + w), 0x00, 0x00, 0x05};
        sp::CensusAdd(e, 1);
    }
    cs = sp::CensusRead();
    Check(cs.words == sp::kCensusWords && cs.overflow == 4, "census keeps 8 words and counts the rest as overflow");
    Check(!sp::CensusLine(line, 16), "a short buffer refuses");
    sp::CensusReset();
    Check(sp::CensusRead().calls == 0 && sp::CensusRead().words == 0, "census reset");
    // Probe P11 sites: the getter head pin and the return address counts.
    Check(sp::kOffenseHead[0] == 0x48 && sp::kOffenseHead[4] == 0xe8 && sp::kOffenseHead[0x1f] == 0x50, "the getter head pin");
    sp::SitesReset();
    sp::SitesAdd(0x132561f);
    sp::SitesAdd(0x132561f);
    sp::SitesAdd(0x1d00000);
    auto st = sp::SitesRead();
    Check(st.sites == 2 && st.rva[0] == 0x132561f && st.count[0] == 2 && st.rva[1] == 0x1d00000 && st.count[1] == 1, "sites");
    Check(sp::SitesLine(line, sizeof(line)) && std::strcmp(line, "sites;n=2;overflow=0;132561f:2,1d00000:1") == 0, "sites line");
    for (unsigned i = 0; i < sp::kSites + 1; ++i) sp::SitesAdd(0x100 + i);
    st = sp::SitesRead();
    Check(st.sites == sp::kSites && st.overflow == 3, "sites keep 24 and count the rest");
    Check(sp::SitesLine(line, sizeof(line)) && std::strlen(line) < sizeof(line), "a long list is cut to fit");
    Check(!sp::SitesLine(line, 8), "a short buffer refuses");
    sp::SitesReset();
    Check(sp::SitesRead().sites == 0, "sites reset");
    std::printf("soldier_m7_tests: %u checks passed\n", checks);
    return 0;
}
