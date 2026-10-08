// Native puppet equipment manifest codec (docs/native-puppet.md 6.2): bounds,
// revision 0 refused, epoch check, receiver store. Pure; no game, no network.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/partner_equip_wire.hpp"

namespace {
namespace ew = mgscoop::partner_equip_wire;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

ew::Manifest Sample() {
    ew::Manifest m{};
    m.revision = 7;
    m.epoch = 0x11223344u;
    m.inv_slot = 1;
    for (std::size_t i = 0; i < ew::kSlots; ++i) {
        m.slots[i].base_id = static_cast<std::uint16_t>(100 + i);
        m.slots[i].alias = static_cast<std::uint16_t>(0x2E + i);
        for (std::size_t k = 0; k < ew::kRecipe; ++k) m.slots[i].recipe[k] = static_cast<std::uint8_t>(i * 16 + k);
    }
    return m;
}
} // namespace

int main() {
    std::uint8_t buf[ew::kSize]{};
    const ew::Manifest m = Sample();
    Check(ew::kSize == 60 && ew::Encode(m, buf) == 60, "encode 60 bytes");
    Check(buf[0] == 0x45 && buf[1] == 1 && buf[2] == 7 && buf[3] == 0 && buf[4] == 0x44 && buf[7] == 0x11 && buf[8] == 1 && buf[9] == 3 &&
          buf[10] == 0 && buf[11] == 0, "header layout");
    Check(buf[12] == 100 && buf[14] == 0x2E && buf[16] == 0 && buf[12 + 16] == 101 && buf[59] == 0x2B, "slot layout");
    ew::Manifest got{};
    Check(ew::Decode(buf, sizeof(buf), m.epoch, got) == ew::Result::ok && got.revision == 7 && got.epoch == m.epoch && got.inv_slot == 1 &&
          std::memcmp(&got.slots, &m.slots, sizeof(m.slots)) == 0, "round trip");
    Check(ew::Decode(buf, sizeof(buf), 0, got) == ew::Result::ok, "epoch 0 accepts any epoch");
    Check(ew::Decode(buf, sizeof(buf), m.epoch + 1, got) == ew::Result::stale_epoch, "another epoch is stale");
    Check(ew::Decode(buf, sizeof(buf) - 1, 0, got) == ew::Result::size && ew::Decode(nullptr, 60, 0, got) == ew::Result::size, "size");
    ew::Manifest bad = m;
    bad.revision = 0;
    Check(ew::Encode(bad, buf) == 0 && buf[0] == 0, "revision 0 refused by encode (zeroed)");
    bad = m;
    bad.epoch = 0;
    Check(ew::Encode(bad, buf) == 0, "epoch 0 refused");
    bad = m;
    bad.inv_slot = 16;
    Check(ew::Encode(bad, buf) == 0, "inv_slot 16 refused");
    bad.inv_slot = 0xFF;
    Check(ew::Encode(bad, buf) == ew::kSize, "inv_slot unknown accepted");
    struct Tweak { std::size_t at; std::uint8_t value; ew::Result expect; };
    const Tweak tweaks[] = {{0, 0x46, ew::Result::magic}, {1, 2, ew::Result::version}, {8, 16, ew::Result::inv_slot},
                            {9, 2, ew::Result::slot_count}, {10, 1, ew::Result::reserved}, {11, 1, ew::Result::reserved}};
    for (const Tweak& t : tweaks) {
        ew::Encode(m, buf);
        buf[t.at] = t.value;
        Check(ew::Decode(buf, sizeof(buf), 0, got) == t.expect, ew::ResultName(t.expect));
    }
    ew::Encode(m, buf);
    buf[2] = buf[3] = 0;
    Check(ew::Decode(buf, sizeof(buf), 0, got) == ew::Result::revision, "revision 0 refused on decode");
    ew::Encode(m, buf);
    buf[4] = buf[5] = buf[6] = buf[7] = 0;
    Check(ew::Decode(buf, sizeof(buf), 0, got) == ew::Result::epoch, "epoch 0 refused on decode");
    // Store: new revisions stored, duplicates/invalid/stale counted only.
    ew::Store st{};
    ew::Encode(m, buf);
    Check(ew::Accept(st, buf, sizeof(buf), m.epoch) && st.have && st.received == 1 && st.manifest.revision == 7, "store accepts");
    Check(!ew::Accept(st, buf, sizeof(buf), m.epoch) && st.duplicates == 1 && st.received == 1, "duplicate counted");
    Check(!ew::Accept(st, buf, sizeof(buf), m.epoch + 1) && st.stale == 1, "stale epoch counted");
    Check(!ew::Accept(st, buf, 3, m.epoch) && st.invalid == 1 && st.manifest.revision == 7, "invalid counted, store kept");
    ew::Manifest next = m;
    next.revision = 8;
    ew::Encode(next, buf);
    Check(ew::Accept(st, buf, sizeof(buf), m.epoch) && st.manifest.revision == 8 && st.received == 2, "next revision stored");
    Check(std::strcmp(ew::ResultName(ew::Result::stale_epoch), "stale_epoch") == 0, "result names");
    std::printf("%u equipment manifest checks passed: layout, round trip, revision/epoch/inv_slot/slot_count/reserved refusals, "
                "stale epoch, receiver store\n", checks);
    return 0;
}
