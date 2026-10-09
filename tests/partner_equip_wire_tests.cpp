// Native puppet equipment manifest codec (docs/native-puppet.md 6.2): bounds,
// revision 0 refused, epoch check, receiver store, version 2 (the slots' own
// gun types) next to version 1, and the sender's base-id scan. Pure; no game,
// no network.
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
        m.slots[i].type = static_cast<std::uint8_t>(i == 0 ? 1 : i == 1 ? 4 : 2);
    }
    return m;
}
} // namespace

int main() {
    std::uint8_t buf[ew::kSize]{};
    const ew::Manifest m = Sample();
    Check(ew::kSizeV1 == 60 && ew::kSize == 64 && ew::Encode(m, buf) == 64, "encode 64 bytes (version 2)");
    Check(buf[0] == 0x45 && buf[1] == 2 && buf[2] == 7 && buf[3] == 0 && buf[4] == 0x44 && buf[7] == 0x11 && buf[8] == 1 && buf[9] == 3 &&
          buf[10] == 0 && buf[11] == 0, "header layout");
    Check(buf[12] == 100 && buf[14] == 0x2E && buf[16] == 0 && buf[12 + 16] == 101 && buf[59] == 0x2B, "slot layout");
    Check(buf[60] == 1 && buf[61] == 4 && buf[62] == 2 && buf[63] == 0, "type[3] at 60..62, 63 reserved");
    ew::Manifest got{};
    Check(ew::Decode(buf, sizeof(buf), m.epoch, got) == ew::Result::ok && got.revision == 7 && got.epoch == m.epoch && got.inv_slot == 1 &&
          got.version == 2 && ew::SameSlots(got, m) && got.slots[0].type == 1 && got.slots[1].type == 4 && got.slots[2].type == 2,
          "round trip");
    Check(ew::Decode(buf, sizeof(buf), 0, got) == ew::Result::ok, "epoch 0 accepts any epoch");
    Check(ew::Decode(buf, sizeof(buf), m.epoch + 1, got) == ew::Result::stale_epoch, "another epoch is stale");
    Check(ew::Decode(buf, sizeof(buf) - 1, 0, got) == ew::Result::size && ew::Decode(nullptr, 64, 0, got) == ew::Result::size, "size");
    // Version 1 (the 0.12.0 layout, 60 bytes): still accepted, types unknown.
    {
        std::uint8_t v1[ew::kSizeV1]{};
        std::memcpy(v1, buf, sizeof(v1));
        v1[1] = 1;
        ew::Manifest old{};
        Check(ew::Decode(v1, sizeof(v1), m.epoch, old) == ew::Result::ok && old.version == 1 && old.slots[1].base_id == 101 &&
                  old.slots[0].type == 0 && old.slots[1].type == 0 && old.slots[2].type == 0, "version 1 decodes (types 0)");
        Check(ew::Decode(buf, sizeof(buf), 0, old) == ew::Result::ok, "version 2 decodes");
        std::uint8_t mixed[ew::kSize]{};
        std::memcpy(mixed, buf, sizeof(mixed));
        mixed[1] = 1;
        Check(ew::Decode(mixed, sizeof(mixed), 0, old) == ew::Result::size, "version 1 with 64 bytes: size");
        v1[1] = 2;
        Check(ew::Decode(v1, sizeof(v1), 0, old) == ew::Result::size, "version 2 with 60 bytes: size");
        ew::Store st{};
        v1[1] = 1;
        Check(ew::Accept(st, v1, sizeof(v1), m.epoch) && st.have && st.manifest.version == 1, "the store accepts version 1");
    }
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
    bad = m;
    bad.slots[2].type = 9;
    Check(ew::Encode(bad, buf) == 0, "a type above 8 refused by encode");
    struct Tweak { std::size_t at; std::uint8_t value; ew::Result expect; };
    const Tweak tweaks[] = {{0, 0x46, ew::Result::magic}, {1, 3, ew::Result::version}, {8, 16, ew::Result::inv_slot},
                            {9, 2, ew::Result::slot_count}, {10, 1, ew::Result::reserved}, {11, 1, ew::Result::reserved},
                            {63, 1, ew::Result::reserved}, {60, 9, ew::Result::type}, {62, 0x20, ew::Result::type}};
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
    Check(std::strcmp(ew::ResultName(ew::Result::stale_epoch), "stale_epoch") == 0 && std::strcmp(ew::ResultName(ew::Result::type), "type") == 0,
          "result names");
    // SameSlots compares every field (the type too).
    ew::Manifest other = m;
    Check(ew::SameSlots(other, m), "same slots");
    other.slots[1].type = 6;
    Check(!ew::SameSlots(other, m), "a type change is a change");
    other = m;
    other.slots[2].recipe[11] ^= 1;
    Check(!ew::SameSlots(other, m), "a recipe change is a change");
    // The type table index -> equip id (A2A3E0): ids below 0x230 themselves, 0x230.. -> 0x400.., 0x280.. -> 0x600...
    Check(ew::TableIndexToId(46) == 46 && ew::TableIndexToId(0x22F) == 0x22F && ew::TableIndexToId(0x230) == 0x400 &&
              ew::TableIndexToId(0x27F) == 0x44F && ew::TableIndexToId(0x280) == 0x600 && ew::TableIndexToId(0x288) == 0x608,
          "table index -> id");
    Check(ew::GunType(1) && ew::GunType(8) && !ew::GunType(0) && !ew::GunType(9) && !ew::GunType(11), "gun types 1..8");
    // The base-id scan of run 53e36944: alias 871 (type 1, receiver 69). Index 343 is type 11 whose row 105 holds receiver
    // 69: never a base. The first type-1 index with receiver 69 is 0x232 (id 0x402); type-1 rows with other receivers lose.
    {
        static std::uint16_t packed[649]{};
        static std::uint8_t receivers[514]{};
        packed[36] = static_cast<std::uint16_t>((20 << 6) | 1);    // type 1, row 20: receiver 70
        receivers[19] = 70;
        packed[343] = static_cast<std::uint16_t>((105 << 6) | 11); // type 11, row 105: receiver 69 (not a gun)
        receivers[104] = 69;
        packed[0x232] = static_cast<std::uint16_t>((300 << 6) | 1); // type 1, row 300: receiver 69
        receivers[299] = 69;
        packed[0x234] = static_cast<std::uint16_t>((301 << 6) | 4); // type 4, row 301: receiver 69 (another type)
        receivers[300] = 69;
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 69) == 0x402, "alias 871: the type-1 index 0x232 -> id 0x402");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 4, 69) == 0x404, "a type-4 alias: index 0x234 -> id 0x404");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 11, 69) == 0, "type 11 is no gun type: base 0");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 2, 69) == 0, "no type-2 match: base 0");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 0) == 0 && ew::BaseFromTables(nullptr, 649, receivers, 514, 1, 69) == 0,
              "no receiver or no table: base 0");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 70) == 36, "receiver 70: stock id 36");
        packed[0x232] = static_cast<std::uint16_t>((600 << 6) | 1);  // a row beyond GunBasic: skipped
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 69) == 0, "row out of range: skipped");
    }
    std::printf("%u equipment manifest checks passed: version 2 layout (types), version 1 decode, round trip, "
                "revision/epoch/inv_slot/slot_count/reserved/type refusals, stale epoch, receiver store, field-wise change, "
                "table index mapping, gun-type base scan\n", checks);
    return 0;
}
