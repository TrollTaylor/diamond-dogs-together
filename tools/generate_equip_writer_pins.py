"""Generalized equip writer pins (docs/native-puppet.md "Generalized writer").

Generalizes work/generate-stock46-writer-pins.py (the qualified stock46
writer's gates): the same complete declared code ranges and protected/data
spans, plus the code and read-only data the generalized preload (W2) and
writer (W3) rely on. native/equipment_stock_writer_pins.hpp is this tool's
output; both writers SHA-256 every span at runtime before their one dispatch
and refuse on any mismatch (full_body_gate). The static facts the pure
derivation in equipment_stock_writer_contract.hpp / equipment_preload_contract.hpp
uses are asserted from the exact retail bytes here (assert_static):

- type -> slot (ChangeEquip 11AC4AA, lower writer 120A5D2, emulated);
- the per-slot binder record (B0C720), effect record (9C5980) and the 13
  attachment items per slot record (B0CF80);
- ChangeEquip's flags-4 SetCurrentSlot of the new slot (11AC4A0..11AC57B);
- the package row table 2C20FD0 + id*24 and the queue growth site (A03C40);
- DB6AC0's dependency LUTs and id tables, DB69E0's hash table 2349348;
- the type / row getters for ids below 0x230 (A2A3E0, A2A520);
- the optional descriptor index rule (1307EF2..1307F31);
- DC3700's first-used-entry selection, protect bit, unused-entry build and
  eviction path (the preload refuses the last);
- the NULL-optional render path (ADE358..ADE3D6) and its size
  512 + align16(8g+88) (1AB7940, 1E19200, 1A7DA20, 1AA49E0);
- the optional asset's constructor shapes: 1A95470(n, mode 0) over the
  per-parameter table 1A953D0(0) returns (0x2B4A078), emulated for n = 1..8,
  equal to kOptionalShapes in equipment_stock_writer_contract.hpp (header word0
  n, word1 m(n), the request size; stock46 is n4/m6/544).

Read-only static analysis of mgsvtpp.exe 1.0.15.4; never runs the game.

    python tools/generate_equip_writer_pins.py          # write the header
    python tools/generate_equip_writer_pins.py --check  # verify, write nothing
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from analyze_network_bindings import Analysis  # noqa: E402

EXPECTED = "085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45"
DEFAULT_EXE = Path("D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe")
HEADER = ROOT / "native/equipment_stock_writer_pins.hpp"
CONTRACT = ROOT / "native/include/mgscoop/equipment_stock_writer_contract.hpp"

# The stock46 writer's declared ranges (work/remote-equipment-static-pins.py,
# unchanged): (name, start, end exclusive, last instruction end).
RANGES = [
    ("public_change_equip", 0x9D0D60, 0x9D1035, 0x9D1035),
    ("change_equip_dispatch", 0x11AC2E0, 0x11AC719, 0x11AC719),
    ("alias_cache_preparation_mutator", 0x9E03C0, 0x9E050A, 0x9E050A),
    ("body_restriction_predicate", 0x1156640, 0x1156661, 0x1156661),
    ("lower_equipment_writer", 0x1209EC0, 0x120C118, 0x120C0F9),
    ("interface_equip_allocate", 0x11B0700, 0x11B08F7, 0x11B08F7),
    ("interface_inventory_and_model", 0x11B0900, 0x11B09BE, 0x11B09BE),
    ("interface_slot_record_update", 0x11AFF30, 0x11B042C, 0x11B0410),
    ("interface_remove_slot", 0x11B2490, 0x11B250E, 0x11B250E),
    ("inventory_setup_primary", 0xEE92F0, 0xEE957F, 0xEE957F),
    ("inventory_allocate", 0xEE9880, 0xEE9ACA, 0xEE9ACA),
    ("inventory_remove", 0xEE9D70, 0xEE9E5B, 0xEE9E5B),
    ("inventory_flag_mutator", 0xEE9810, 0xEE9877, 0xEE9877),
    ("model_queue_handle", 0xB0C720, 0xB0C7F7, 0xB0C7F7),
    ("model_load_update", 0xB0CF80, 0xB0D340, 0xB0D340),
    ("model_natural_update", 0xB0CD30, 0xB0CF7E, 0xB0CF7E),
    ("engine_allocate", 0xDC3250, 0xDC3461, 0xDC3461),
    ("engine_metadata_owner_flag", 0xDC2A50, 0xDC2AF3, 0xDC2AF3),
    ("engine_release", 0xDC63A0, 0xDC6477, 0xDC6477),
    ("engine_loaded_predicate", 0xDC0350, 0xDC0397, 0xDC0397),
    ("engine_request_load", 0xDC0920, 0xDC0989, 0xDC0989),
    ("engine_model_resource_extract", 0xDBEF30, 0xDBF17C, 0xDBF17C),
    ("engine_stock_cache_selection_primary", 0xDC3700, 0xDC3831, 0xDC3831),
    ("engine_stock_cache_build_primary", 0xDC3470, 0xDC36F9, 0xDC36F9),
    ("body_weapon_resource_attach", 0x9C5980, 0x9C5C7A, 0x9C5C7A),
    ("body_weapon_resource_clear", 0x9CC6F0, 0x9CC845, 0x9CC845),
    ("public_mission_preload", 0xA03C40, 0xA03DB8, 0xA03DB8),
    ("public_mission_dependency_append", 0xA02290, 0xA02462, 0xA02462),
    ("recipe_dependency_selection", 0xDB6AC0, 0xDB6CAA, 0xDB6CAA),
    ("dependency_hash_getter", 0xDB69E0, 0xDB69F8, 0xDB69F8),
    ("stock_cache_protect_thunk", 0xDC3840, 0xDC384C, 0xDC384C),
    ("mission_group_entry_getter", 0x6D790, 0x6D7A6, 0x6D7A6),
    ("scriptblock_public_state", 0x183A80, 0x183AEB, 0x183AEB),
    ("scriptblock_state_mapping", 0x182A90, 0x182AF3, 0x182AF3),
    ("scriptblock_hash_vector_copy", 0x6DEC0, 0x6DF8F, 0x6DF8F),
    ("scriptblock_load_record_allocation", 0x6E040, 0x6E3EB, 0x6E3EB),
    ("scriptblock_load_natural_update", 0x6E4E0, 0x6F1F8, 0x6F1B4),
    ("scriptblock_asset_activation", 0x6CFB0, 0x6D092, 0x6D092),
    ("file_record_constructor", 0xDD230, 0xDD308, 0xDD308),
    ("file_record_array_allocation", 0x6BFF0, 0x6C118, 0x6C118),
    ("file_record_base_constructor", 0xDB650, 0xDB69B, 0xDB69B),
    ("file_record_load_begin", 0xDDD00, 0xDDDA8, 0xDDDA8),
    ("file_record_load_update", 0xDDF60, 0xDEC1F, 0xDEC1F),
    ("file_record_result_getter", 0x7D2260, 0x7D2269, 0x7D2269),
    ("file_record_asset_getter", 0xDD810, 0xDD829, 0xDD829),
    ("file_record_asset_count_getter", 0xDD830, 0xDD837, 0xDD837),
    ("file_record_set_owner", 0x2DD300, 0x2DD305, 0x2DD305),
    ("hash_reference_copy", 0x857A0, 0x857CF, 0x857CF),
    ("metadata_setup", 0xDB70B0, 0xDB7297, 0xDB7297),
    ("metadata_array_allocation", 0x7E69190, 0x7E69306, 0x7E69306),
    ("load_map_setup", 0xDB7840, 0xDB7B1B, 0xDB7B1B),
    ("load_map_natural_update", 0xDB7590, 0xDB77F4, 0xDB77F4),
    ("empty_effect_context_getter", 0xAF0A00, 0xAF0A15, 0xAF0A15),
    ("resource_group_select", 0xDBF1F0, 0xDBF2C7, 0xDBF2C7),
    ("resource_group_setup", 0x13068D0, 0x1306E1C, 0x1306E1C),
    ("resource_bank_getter", 0xADBE00, 0xADBE0B, 0xADBE0B),
    ("metadata_allocate_row", 0xDB72A0, 0xDB72B4, 0xDB72B4),
    ("per_instance_slot_activation", 0x11AFA80, 0x11AFEA4, 0x11AFE6C),
    ("engine_globals_getter", 0xBFF050, 0xBFF062, 0xBFF062),
    ("engine_root_getter", 0xBFEFA0, 0xBFF03D, 0xBFF03D),
    ("native_owner_wrapper", 0x1E02AD0, 0x1E02B12, 0x1E02B12),
    ("native_owner_getter_thunk", 0x1E02B40, 0x1E02B48, 0x1E02B48),
    ("native_owner_scalar_getter", 0x53C990, 0x53C99F, 0x53C99F),
    ("model_clear_slot", 0xB0C800, 0xB0C8CB, 0xB0C8CB),
    ("resource_group_lookup", 0xDBF370, 0xDBF3AC, 0xDBF3AC),
    ("resource_group_prepare", 0x1307D70, 0x1308931, 0x1308931),
    ("resource_group_release", 0x130B2F0, 0x130B47A, 0x130B47A),
    ("stock_package_parameter_setup", 0xA06260, 0xA06315, 0xA06315),
    ("stock_package_parameter_getter", 0xA047D0, 0xA047DC, 0xA047DC),
    ("stock_package_ready_predicate", 0xA06440, 0xA0648A, 0xA0648A),
    ("stock_package_parts_getter", 0xA05B40, 0xA05B95, 0xA05B95),
    ("stock_package_active_entry_getter", 0xA057C0, 0xA059C8, 0xA059C8),
    ("package_group_entry_lookup", 0xA03320, 0xA03399, 0xA03399),
    ("package_asset_hash_lookup", 0x6D9A0, 0x6DA54, 0x6DA54),
    ("package_file_hash_lookup", 0x6DC40, 0x6DC72, 0x6DC72),
    ("package_file_asset_tree_lookup", 0xDDA00, 0xDDA53, 0xDDA53),
    ("stock_optional_inline_bank_setup", 0x2000E90, 0x2000FAA, 0x2000FAA),
    ("model_equipment_setup", 0xB0C4B0, 0xB0C64A, 0xB0C64A),
    ("attachment_manager_factory", 0x786EC0, 0x786F05, 0x786F05),
    ("attachment_manager_setup", 0x7C73C0, 0x7C7469, 0x7C7469),
    ("attachment_item_setup", 0x7CC170, 0x7CC1AE, 0x7CC1AE),
    ("attachment_bind_thunk", 0x7C7530, 0x7C7542, 0x7C7542),
    ("attachment_clear_thunk", 0x7C7550, 0x7C755F, 0x7C755F),
    ("attachment_bind", 0x7CC210, 0x7CC2AC, 0x7CC2AC),
    ("attachment_clear", 0x7CC2B0, 0x7CC2F4, 0x7CC2F4),
    ("attachment_model_instance_factory", 0x1E5920, 0x1E5A13, 0x1E5A13),
    ("bank_resource_prepare", 0xADDE80, 0xADE472, 0xADE472),
    ("bank_resource_activation", 0xADDDB0, 0xADDE7E, 0xADDE7E),
    ("optional_package_asset_lookup", 0xACCC10, 0xACCC2D, 0xACCC2D),
    ("bank_constructor", 0xADA980, 0xADAA7D, 0xADAA7D),
    ("bank_array_setup", 0xADC080, 0xADC6E9, 0xADC6E9),
    ("bank_ordinary_cache_selection", 0xADCCD0, 0xADCD99, 0xADCD99),
    ("bank_ordinary_cache_build", 0xADC970, 0xADCCCB, 0xADCCCB),
    ("bank_shared_reference_array_setup", 0xADA560, 0xADA676, 0xADA676),
    ("bank_empty_reference_constructor", 0xADA900, 0xADA973, 0xADA973),
    ("empty_hash_reference_getter", 0x85990, 0x859F4, 0x859F4),
    ("hash_reference_constructor", 0x855E0, 0x85639, 0x85639),
    ("optional_resource_factory",0x1A75540,0x1A755A2,0x1A755A2),
    ("optional_resource_loader",0x1A75340,0x1A754BC,0x1A754BC),
    ("optional_source_allocate",0x102B20,0x102BAA,0x102BAA),
    ("optional_source_import",0x102970,0x102A0E,0x102A0E),
    ("optional_dictionary_getter_alltails",0x1A755B0,0x1A755DC,0x1A755DC),
    ("optional_render_request_init",0x1AB7520,0x1AB7535,0x1AB7535),
    ("optional_render_size",0x1AB7940,0x1AB79DE,0x1AB79DE),
    ("optional_render_constructor",0x1AB7360,0x1AB7520,0x1AB7520),
    ("optional_body_size_alltails",0x1E19200,0x1E19348,0x1E19348),
    ("optional_body_constructor",0x1E18CF0,0x1E18FF7,0x1E18FF7),
    ("optional_size_mode_getter_alltails",0x1A953D0,0x1A95420,0x1A95420),
    ("optional_size_fixed_table_loop",0x1A95470,0x1A9550E,0x1A9550E),
    ("optional_render_global_size",0x1AA49E0,0x1AA4A01,0x1AA4A01),
    ("optional_render_workspace_constructor",0x1A70520,0x1A70617,0x1A70617),
    ("optional_render_workspace_item",0x1A6E670,0x1A6E6E3,0x1A6E6E3),
    ("optional_request_count_constructor",0x1A7C720,0x1A7C76F,0x1A7C76F),
    ("optional_request_count_size",0x1A7DA20,0x1A7DA32,0x1A7DA32),
    ("bank_extra_array_allocate",0xADA7B0,0xADA900,0xADA900),
    ("bank_callback_setter",0xADDC70,0xADDC7D,0xADDC7D),
    ("bank_callback_tuple_setup",0xADF430,0xADF50A,0xADF50A),
    ("optional_extra_bind_thunk",0x1AB76D0,0x1AB76DB,0x1AB76DB),
    ("optional_extra_bind_redirect",0x1E19110,0x1E19115,0x1E19115),
    ("bank_resource_callback",0x1306FF0,0x1307038,0x1307038),
    ("bank_resource_callback_alltails",0x1307040,0x130711F,0x130711F),
]
OPAQUE_RANGES=[("optional_fallback_constructor_thunk",0x1A94B60,5),
    ("optional_fallback_constructor_protected",0x8A2AB70,0x8A2C824-0x8A2AB70),
    ("optional_extra_bind_protected",0x9B980D0,0x9B98AD7-0x9B980D0)]
DATA_RANGES=[("optional_mode_jump_table",0x1A95420,24),
    ("optional_mode0_size_table",0x2B4A078,48),
    ("optional_asset_vtable",0x24C2890,0x58),
    ("optional_extra_vtable",0x22E2950,0x20)]
# The stock46 gate set (work/generate-stock46-writer-pins.py, unchanged).
names={"change_equip_dispatch","body_restriction_predicate","lower_equipment_writer",
       "interface_equip_allocate","interface_inventory_and_model","interface_slot_record_update",
       "interface_remove_slot","inventory_allocate","inventory_remove","inventory_flag_mutator",
       "model_queue_handle","model_load_update","model_natural_update","engine_allocate",
       "engine_metadata_owner_flag","engine_release","engine_loaded_predicate","engine_request_load",
       "engine_model_resource_extract","engine_stock_cache_selection_primary",
       "body_weapon_resource_attach","body_weapon_resource_clear","empty_effect_context_getter",
       "load_map_natural_update","resource_group_select","resource_bank_getter",
       "metadata_allocate_row","per_instance_slot_activation","engine_globals_getter","engine_root_getter",
       "native_owner_wrapper","native_owner_getter_thunk","native_owner_scalar_getter","model_clear_slot",
       "resource_group_lookup","resource_group_prepare","resource_group_release",
       "stock_package_parameter_getter","stock_package_ready_predicate","stock_package_parts_getter",
       "stock_package_active_entry_getter","package_group_entry_lookup","package_asset_hash_lookup",
       "package_file_hash_lookup","package_file_asset_tree_lookup","attachment_bind_thunk","attachment_clear_thunk",
       "attachment_bind","attachment_clear","attachment_model_instance_factory",
       "bank_resource_prepare","bank_resource_activation","optional_package_asset_lookup",
       "bank_ordinary_cache_selection","bank_ordinary_cache_build",
       "empty_hash_reference_getter","hash_reference_constructor"}
names.update(name for name,_,_,_ in RANGES if name.startswith("optional_") or name in {
    "bank_array_setup","bank_constructor","bank_extra_array_allocate","bank_callback_setter",
    "bank_callback_tuple_setup","bank_resource_callback","bank_resource_callback_alltails",
    "resource_group_setup","stock_optional_inline_bank_setup"})
# The generalized pieces add these declared ranges (code end == range end).
GENERAL_RANGES = [
    ("type_table_type_getter", 0xA2A3E0, 0xA2A501, 0xA2A501),
    ("type_table_row_getter", 0xA2A520, 0xA2A5A3, 0xA2A5A3),
    ("dependency_lut_lane0", 0xDB6CB0, 0xDB6CBE, 0xDB6CBE),
    ("dependency_lut_lane1", 0xDB69D0, 0xDB69DE, 0xDB69DE),
    ("dependency_lut_lane2", 0xDB6A20, 0xDB6A2E, 0xDB6A2E),
    ("dependency_lut_lane3", 0xDB6CC0, 0xDB6CCE, 0xDB6CCE),
    ("dependency_lut_lane10", 0xDB6CD0, 0xDB6CDE, 0xDB6CDE),
    # Complete exclusive ends, including RET/REP RET and the final release JMP.
    ("model_binder_record_constructor", 0xB0BCD0, 0xB0BD55, 0xB0BD55),
    ("model_binder_records_allocate", 0xB0BAF0, 0xB0BC05, 0xB0BC05),
    ("model_binder_records_reset", 0xB0CA40, 0xB0CABD, 0xB0CABD),
    ("bank_shared_key_selection", 0xADEA10, 0xADEA70, 0xADEA70),
    ("engine_first_used_stock_entry", 0xDBD5C0, 0xDBD607, 0xDBD607),
    ("inventory_actor_release", 0xEE9E60, 0xEE9EF0, 0xEE9EF0),
    # WS4 v1 review (WS4-2): ctl vt+0x1F0, the one callee only the slot-6 (arm) dispatch runs (ChangeEquip type 14
    # calls it instead of the restriction predicate and the lower writer). Whole body, ret at 0x11B1D66.
    ("arm_equip_dispatch", 0x11B1B60, 0x11B1D67, 0x11B1D67),
    # F2 (partner_cbox_equip, docs/native-puppet.md "Cardboard box"): the box off call ctl vt+0x68 (its tail jmp to
    # OnEquipItem at 0x11AFA75), the item remove / equip hooks both run, and the two pure reads the box wake and the
    # writer's own mapping rely on. GetEquipIdSlot (ctl vt+0x90, no Ghidra function) ends with its 7-entry jump table
    # 0x11AD4A0..0x11AD4BC: the range covers the table, the decode stops at the last case's ret (0x11AD49A).
    # OnRemoveItem ends with a two-byte repz ret at 0x11AF09E.
    ("set_current_item_index", 0x11AF9A0, 0x11AFA7A, 0x11AFA7A),
    ("on_remove_item", 0x11AF000, 0x11AF0A0, 0x11AF0A0),
    ("on_equip_item", 0x11AEF10, 0x11AEFF7, 0x11AEFF7),
    ("get_item_stock_count", 0x11AD660, 0x11AD6FB, 0x11AD6FB),
    ("get_equip_id_slot", 0x11AD420, 0x11AD4BC, 0x11AD49A),
]
GENERAL_NAMES = {
    "recipe_dependency_selection", "dependency_hash_getter", "public_mission_preload",
    "public_mission_dependency_append", "stock_cache_protect_thunk", "engine_stock_cache_build_primary",
    "per_instance_slot_activation", "resource_group_prepare", "model_queue_handle", "model_load_update",
    "body_weapon_resource_attach",
} | {name for name, _, _, _ in GENERAL_RANGES}
# Read-only executable data: the dependency LUTs, id tables and the hash table
# (ids are bytes: 2349348 + 255*16 + 16 = 234A348 bounds every lookup).
GENERAL_DATA = [("dependency_tables", 0x2349348, 0x234A348 - 0x2349348)]


def u64(analysis, rva):
    return struct.unpack("<Q", analysis.pe.rva_read(rva, 8))[0]


def expect(analysis, rva, hex_bytes, what):
    want = bytes.fromhex(hex_bytes)
    got = analysis.pe.rva_read(rva, len(want))
    if got != want:
        raise SystemExit(f"{what}: {rva:#x} is {got.hex(' ')}, expected {want.hex(' ')}")


def rip_target(analysis, rva, opcode_len, total_len):
    disp = struct.unpack("<i", analysis.pe.rva_read(rva + opcode_len, 4))[0]
    return rva + total_len + disp


def emulate_slot(analysis, begin, end, type_reg, slot_reg, entry_r8):
    """Runs the type -> slot branch region for every type 0..31 (the small
    x86 subset the region uses) and returns {type: ("slot", n) or ("leave", rva)}."""
    instructions = analysis.instructions(begin, end - begin)
    index = {i.address - analysis.pe.image_base: k for k, i in enumerate(instructions)}
    result = {}
    for t in range(32):
        # r12d is zero in both regions (120A022 xor r12d, r12d); stores to the
        # stack are not part of the decision.
        regs = {type_reg: t, "eax": 0, "ecx": 0, "esi": 0, "r8d": entry_r8, "r12d": 0}
        flags = ("z", False)
        k = 0
        steps = 0
        leave = None

        wide = {"rax": "eax", "rcx": "ecx", "rdx": "edx", "rdi": "edi", "rsi": "esi", "r8": "r8d", "r9": "r9d"}

        def val(op):
            op = wide.get(op, op)
            if op in regs:
                return regs[op] & 0xFFFFFFFF
            return int(op, 0) & 0xFFFFFFFF
        while k < len(instructions) and steps < 64:
            steps += 1
            i = instructions[k]
            m, ops = i.mnemonic, [o.strip() for o in i.op_str.split(",")]
            nxt = k + 1
            if m == "lea":
                inner = ops[1][ops[1].index("[") + 1:-1].replace(" ", "")
                reg, sign, off = inner.partition("-")
                if not sign:
                    reg, sign, off = inner.partition("+")
                delta = int(off, 0) if off else 0
                regs[ops[0]] = (val(reg) + (-delta if sign == "-" else delta)) & 0xFFFFFFFF
            elif m == "mov":
                if "[" not in ops[0]:
                    regs[ops[0]] = val(ops[1])
            elif m == "dec":
                regs[ops[0]] = (val(ops[0]) - 1) & 0xFFFFFFFF
                flags = ("z", regs[ops[0]] == 0)
            elif m == "sub":
                regs[ops[0]] = (val(ops[0]) - val(ops[1])) & 0xFFFFFFFF
                flags = ("z", regs[ops[0]] == 0)
            elif m == "xor":
                regs[ops[0]] = val(ops[0]) ^ val(ops[1])
            elif m == "cmp":
                flags = ("cmp", val(ops[0]), val(ops[1]))
            elif m == "bt":
                flags = ("cf", (val(ops[0]) >> (val(ops[1]) & 31)) & 1)
            elif m.startswith("j"):
                target = int(ops[0], 0) - analysis.pe.image_base
                if m == "jmp":
                    taken = True
                elif m in ("je", "jne"):
                    zero = flags[1] if flags[0] == "z" else flags[1] == flags[2]
                    taken = zero if m == "je" else not zero
                elif m in ("ja", "jbe"):
                    above = flags[1] > flags[2]
                    taken = above if m == "ja" else not above
                elif m == "jae":
                    taken = flags[1] == 0
                else:
                    raise SystemExit(f"slot emulation: unexpected {m} at {i.address:#x}")
                if taken:
                    if target == end:  # the merge point after the region: decided
                        break
                    if target not in index:
                        leave = target
                        break
                    nxt = index[target]
            else:
                raise SystemExit(f"slot emulation: unexpected {m} at {i.address:#x}")
            k = nxt
        result[t] = ("slot", regs[slot_reg] & 0xFFFFFFFF) if leave is None else ("leave", leave)
    return result


def assert_static(analysis):
    facts = []
    # The retail unwind table splits these functions; include every span and tail.
    expected_ranges = {
        0xB0BAF0: [(0xB0BAF0,0xB0BC05)],
        0xB0CA40: [(0xB0CA40,0xB0CA4A),(0xB0CA4A,0xB0CABB),(0xB0CABB,0xB0CABD)],
        0xEE9E60: [(0xEE9E60,0xEE9E76),(0xEE9E76,0xEE9ED5),(0xEE9ED5,0xEE9EF0)],
        0xADEA10: [(0xADEA10,0xADEA70)],
    }
    for start, wanted in expected_ranges.items():
        stop = wanted[-1][1]
        got = [(s,e) for s,e,_ in analysis.ranges if start <= s < stop]
        if got != wanted:
            raise SystemExit(f"Weapons4 unwind spans changed at {start:X}: {got}")
    expect(analysis,0xB0BD54,"c3","binder constructor return")
    expect(analysis,0xDBD605,"f3 c3","first-used getter complete REP RET")
    expect(analysis,0xEE9EEB,"e9 a0 c0 b1 00","actor release final memset tail")
    # ChangeEquip: with request flags 4 (the qualified request) the new
    # weapon's type picks the slot passed to SetCurrentSlot (vt+0x10).
    expect(analysis, 0x11AC4A0, "f6 46 13 04 0f 84 31 02 00 00", "ChangeEquip flags-4 test")
    expect(analysis, 0x11AC57B, "ff 50 10", "ChangeEquip SetCurrentSlot call")
    change = emulate_slot(analysis, 0x11AC4AA, 0x11AC50D, "edi", "esi", 0)
    lower = emulate_slot(analysis, 0x120A5D2, 0x120A617, "r9d", "esi", 1)
    want = {1: 0, 5: 0, 7: 0, 4: 1, 6: 1, 8: 1, 9: 1, 2: 2, 3: 2}
    for t, s in want.items():
        if change[t] != ("slot", s):
            raise SystemExit(f"ChangeEquip slot of type {t}: {change[t]}, expected {s}")
        if lower[t] != ("slot", s):
            raise SystemExit(f"lower writer slot of type {t}: {lower[t]}, expected {s}")
    facts.append("type->slot 1/5/7:0 4/6/8/9:1 2/3:2 (ChangeEquip and the lower writer agree)")
    # Per-slot layout: binder record id*0x34 + slot*12 (B0C720), effect record
    # ((id-base)*4 + slot)*0x50 (9C5980), 13 attachment items per slot record
    # starting at model80*(id-base) (B0CF80).
    expect(analysis, 0xB0C742, "45 85 c0 74 28 41 ff c8 74 1c 41 ff c8 74 10 41 83 f8 02", "B0C720 slot switch")
    expect(analysis, 0xB0C79C, "8b d6 2b 57 5c 48 6b d2 34 48 03 57 30 48 63 c3 48 8d 0c 40 8b 44 24 68 89 44 8a 04",
           "B0C720 record")
    expect(analysis, 0x9C5A2F, "8b cf 41 2b 4c 24 0c 48 63 f0 8d 0c 8b 48 8d 1c 89 48 c1 e3 04 49 03 9c 24 28 06 00 00",
           "9C5980 effect record")
    expect(analysis, 0xB0CFE1, "44 0f b7 a5 80 00 00 00 4c 8b 4f 08 44 0f af e6", "B0CF80 attachment base")
    expect(analysis, 0xB0D1E9, "41 83 c4 0d", "B0CF80 13 items per slot record")
    expect(analysis, 0xB0D06C, "bf 03 00 00 00", "B0CF80 three gun slot records")
    facts.append("binder +slot*12, effect +slot*0x50, attachments 13*slot..+12 for body 1")
    # A03C40: the package row table and its id mapping, the main hash at +8,
    # the queue's growth branch (refused by the preload's room check).
    if rip_target(analysis, 0xA03CA4, 3, 7) != 0x2C20FD0:
        raise SystemExit("A03C40 package row table moved")
    expect(analysis, 0xA03CC5, "81 ff 00 06 00 00 7c 08 8d 87 80 fc ff ff eb 0e 81 ff 00 04 00 00 7c 06 8d 87 30 fe ff ff "
           "48 8d 0c 40 49 8d 34 cf 4c 8b 46 08 4d 85 c0", "A03C40 row index")
    expect(analysis, 0xA03D2D, "3b 53 04 72 19", "A03C40 queue growth")
    expect(analysis, 0xA03D70, "e8 1b e5 ff ff", "A03C40 -> A02290")
    expect(analysis, 0xA03D7D, "ff 90 80 02 00 00", "A03C40 engine vt+0x280")
    facts.append("package row 2C20FD0+id*24 (+8 main FPK) for ids below 0x400; growth refused")
    # DB6AC0 for gun types 1..8, non-alias: lanes 0/1/2/3/10 through the LUT
    # getters, then the five id tables.
    expect(analysis, 0xDB6AE9, "ff 10 8b c8 ff c8 83 f8 07 0f 87 82 01 00 00", "DB6AC0 gun types")
    for rva, hx in [(0xDB6B66, "0f b6 10"), (0xDB6B69, "0f b6 68 01"), (0xDB6B6D, "44 0f b6 60 02"),
                    (0xDB6B72, "44 0f b6 68 03"), (0xDB6B77, "0f b6 40 0a"), (0xDB6B85, "ff 50 20"),
                    (0xDB6B93, "ff 50 28"), (0xDB6BA2, "ff 50 30"), (0xDB6BB1, "ff 50 38"), (0xDB6BC1, "ff 50 40"),
                    (0xDB6BCB, "49 8d 91 80 99 34 02"), (0xDB6BD5, "48 8d 14 7a"), (0xDB6BF2, "83 fb 02 72 e9"),
                    (0xDB6BF7, "42 0f b6 8c 0d e0 99 34 02"), (0xDB6C0C, "49 8d 89 10 9a 34 02 4a 8d 0c 61"),
                    (0xDB6C34, "83 fe 02 72 e7"), (0xDB6C39, "43 0f b6 94 0d 08 9a 34 02"),
                    (0xDB6C53, "43 0f b6 94 08 68 9a 34 02")]:
        expect(analysis, rva, hx, "DB6AC0")
    slots = [0x20, 0x28, 0x30, 0x38, 0x40]
    functions = [u64(analysis, 0x2349EE0 + s) - analysis.pe.image_base for s in slots]
    if functions != [0xDB6CB0, 0xDB69D0, 0xDB6A20, 0xDB6CC0, 0xDB6CD0]:
        raise SystemExit("dependency vtable slots moved")
    luts = []
    for fn in functions:
        body = analysis.pe.rva_read(fn, 14)
        if body[:3] != bytes.fromhex("48 8d 0d") or body[7:] != bytes.fromhex("8b c2 0f b6 04 08 c3"):
            raise SystemExit(f"LUT getter {fn:#x} shape")
        luts.append(fn + 7 + struct.unpack("<i", body[3:7])[0])
    if luts != [0x2349A90, 0x2349B80, 0x2349C00, 0x2349CC0, 0x2349A78]:
        raise SystemExit("LUT tables moved")
    if rip_target(analysis, 0xDB69E3, 3, 7) != 0x2349348:
        raise SystemExit("DB69E0 hash table moved")
    expect(analysis, 0xDB69EA, "48 03 c0 48 8b 04 c1", "DB69E0 16-byte entries")
    # stock46 through the generalized derivation: its recipe gives ids [36,55,64].
    recipe = [157, 85, 110, 36, 0, 0, 17, 0, 0, 8, 0, 4]
    k = [analysis.pe.rva_read(luts[i] + recipe[lane], 1)[0] for i, lane in enumerate([0, 1, 2, 3, 10])]
    ids = []
    for j in range(2):
        b = analysis.pe.rva_read(0x2349980 + k[0] * 2 + j, 1)[0]
        if not b:
            break
        ids.append(b)
    b = analysis.pe.rva_read(0x23499E0 + k[1], 1)[0]
    if b:
        ids.append(b)
    for j in range(2):
        b = analysis.pe.rva_read(0x2349A10 + k[2] * 2 + j, 1)[0]
        if not b:
            break
        ids.append(b)
    for table, entry in [(0x2349A08, k[3]), (0x2349A68, k[4])]:
        b = analysis.pe.rva_read(table + entry, 1)[0]
        if b:
            ids.append(b)
    if ids != [36, 55, 64]:
        raise SystemExit(f"stock46 dependency ids {ids}")
    if [u64(analysis, 0x2349348 + i * 16) for i in ids] != [0x5228A3047E1132FC, 0x522A31AC933DC74B, 0x522B39FC57ECA880]:
        raise SystemExit("stock46 dependency hashes")
    facts.append("DB6AC0 LUTs, id tables and hash table 2349348; stock46 -> [36,55,64]")
    # Type and row getters for ids below 0x230: packed & 0x3F / packed >> 6.
    expect(analysis, 0xA2A433, "8d 82 d0 fd ff ff 3d 36 01 00 00 76 6e", "A2A3E0 blueprint range")
    expect(analysis, 0xA2A49D, "48 63 c3 0f b7 44 41 08 83 e0 3f", "A2A3E0 packed type")
    expect(analysis, 0xA2A581, "48 63 c2 0f b7 44 41 08 66 c1 e8 06 c3", "A2A520 packed row")
    facts.append("ids 1..0x22F: type packed&0x3F, row packed>>6 (types+8+id*2)")
    # 1307EF2: the optional descriptor index (id <= 0x7C or 0x400..0x44F).
    expect(analysis, 0x1307EF2, "83 fe 7c 7e 0b 8d 8e 00 fc ff ff 83 f9 4f 77 2f 48 8b 47 70 8b ce 81 fe 00 04 00 00 "
           "7c 06 8d 8e 7d fc ff ff 48 63 d1 48 8b 54 d0 08 48 85 d2 74 0e", "1307EF2 optional index")
    expect(analysis, 0x1307EE2, "4c 89 7d d0 4c 89 7d d8", "1307EE2 zeroed descriptor")
    facts.append("optional descriptor [2A6B400+8+id*8] for ids up to 0x7C; NULL otherwise")
    # DC3700: first used entry of the id, protect bit, first unused entry
    # build, then the eviction scan (zero refs, unprotected) the preload refuses.
    expect(analysis, 0xDC3721, "ff c8 83 f8 07 76 15 b8 30 00 00 00", "DC3700 gun types")
    expect(analysis, 0xDC3755, "41 f6 00 01 74 11 48 8b 87 f8 01 00 00 42 0f b7 4c 08 5e 3b ce 74 2e",
           "DC3700 first used match")
    expect(analysis, 0xDC37AA, "40 84 ed 74 6b 8b c3 48 6b c0 38 4a 8d 0c 10 42 0f b6 44 10 34 a8 02 75 57 0c 02 88 41 34",
           "DC3700 protect bit")
    expect(analysis, 0xDC3788, "f6 01 01 74 3d", "DC3700 first unused")
    expect(analysis, 0xDC37E0, "83 38 00 75 05 f6 02 02 74 16", "DC3700 eviction scan")
    expect(analysis, 0xDC3840, "48 83 c1 e0 41 b0 01 e9 b4 fe ff ff", "DC3840 protect thunk")
    facts.append("DC3700 selection/protect/build/evict paths")
    # Weapons4: no alias construction or protect mutation on a first-used
    # entry; native ref-counted release, bank sharing, and empty binder words.
    expect(analysis, 0xDC32E0, "45 33 c0 41 8b d7 e8 15 04 00 00",
           "engine allocate requests protect=0")
    expect(analysis, 0xDC3308, "e8 43 bd e3 ff 40 0f b6 de 48 8b 48 18 49 8b 84 24 e8 01 00 00 4c 8b 01 48 8d 14 98 41 ff 50 20",
           "engine atomic stock reference increment")
    expect(analysis, 0xDC3374, "41 88 70 08",
           "metadata records first-used stock cache index")
    expect(analysis, 0xDC63B5, "83 e0 1f 0f 84 b9 00 00 00",
           "empty handle returns without release")
    expect(analysis, 0xDC63EB, "48 8d 4f e0 e8 cc 71 ff ff 8b d8 e8 55 8c e3 ff 0f b6 d3 48 8b 48 18 48 8b 87 e8 01 00 00 4c 8b 01 48 8d 14 90 41 ff 50 30",
           "release first-used stock entry and atomic refs decrement")
    expect(analysis, 0xEE9E1E, "ff 50 08 8b 4f 0c 48 8b 47 40 0f af cd 03 ce 33 d2 c7 04 88 00 00 00 00",
           "slot releases once then zeroes handle")
    expect(analysis, 0xEE9EA0, "4c 8b 01 8b 14 30 41 ff 50 08 48 8b 47 40 81 e3 00 00 ff ff 8d 6d 01 0f b7 db 89 1c 06",
           "actor releases once then zeroes handle")
    expect(analysis, 0xADE0D7, "49 39 34 c1 75 0d 48 8b 83 90 00 00 00 4c 39 04 c8 74 08 ff c7 3b fa 72 e1 8b fa 45 32 e4 3b fa 75 2b",
           "bank shares only matching asset and mission entry")
    expect(analysis, 0xADE126, "4c 8d 34 12 48 8b 8b 98 00 00 00 66 41 ff 04 0e",
           "shared bank reference increment")
    expect(analysis, 0xADEA30, "49 39 12 75 0d 48 8b 81 90 00 00 00 4d 39 04 03 74 13 41 ff c1 49 83 c2 18 49 83 c3 08 44 3b cb 72 de 44 8b cb 44 3b cb 72 10 45 33 c9 c7 44 24 20 00 00 00 00 e8 66 e2 ff ff",
           "bank lookup compares both keys before cache allocation")
    expect(analysis, 0xB0BB59, "48 6b ff 34 48 03 cf 49 89 4e 10",
           "binder allocation stride 0x34")
    expect(analysis, 0xB0BBA3, "48 c7 43 18 34 00 00 00 48 89 7b 20",
           "binder allocated array stride and count")
    expect(analysis, 0xB0BBD0, "48 8b cb 48 6b c9 34 48 03 ce 48 89 4c 24 28 74 06 e8 ea 00 00 00",
           "binder constructor on each bounded array record")
    expect(analysis, 0xB0BCE0, "c7 40 fc e0 ff ff ff c7 00 e0 ff ff ff 80 60 04 fe",
           "binder constructor both handles FFFFFFE0")
    expect(analysis, 0xB0CA6F, "80 60 08 fe c7 00 e0 ff ff ff c7 40 0c e0 ff ff ff c7 40 04 e0 ff ff ff c7 40 10 e0 ff ff ff",
           "binder reset both handles FFFFFFE0")
    expect(analysis, 0xB0C7B4, "89 44 8a 04 80 64 8a 08 fe",
           "binder pending handle queued with flags clear")
    expect(analysis, 0xB0D043, "66 39 72 fc 75 16 66 39 32 75 11 8b c8 c1 e9 10 66 3b ce 74 07 80 62 02 fe 89 42 fe",
           "binder empty handle update predicate")
    expect(analysis, 0xB0D18B, "41 8b 06 be ff ff 00 00 41 89 46 fc",
           "binder binding current from pending")
    expect(analysis, 0xB0D1CD, "41 80 66 04 fe 41 c7 46 fc e0 ff ff ff 48 8b bc 24 f0 00 00 00 41 c7 06 e0 ff ff ff",
           "binder unbind and clear pending FFFFFFE0")
    expect(analysis, 0x1A753E9, "b8 00 10 00 00 66 41 85 47 12 74 55",
           "optional header dictionary bit 0x1000")
    facts.append("first-used stock sharing/release, bank key sharing, and native empty binder lifecycle")
    # The NULL optional: ADE358 renders with no asset on a bank with a policy
    # (the main bank); the size is 0x20 + 0x150 + 1E19200(NULL) where 1E1921F
    # returns 0x90 + align16(1A7DA20(1,1) = 8g+88) for count 1.
    expect(analysis, 0xADE358, "49 83 3e 00 75 12 48 83 7b 78 00 0f 84 e1 00 00 00 48 8b 45 0f 80 08 02",
           "ADE358 NULL optional")
    expect(analysis, 0xADE384, "b8 00 02 00 00 b9 08 02 00 00 48 83 7b 78 00 0f 45 c1", "ADE384 render flags")
    expect(analysis, 0xADE3CD, "41 8b 46 08 ff c0 89 45 bb", "ADE3CD count")
    expect(analysis, 0x1E1921A, "bb 90 00 00 00 48 85 c9 0f 84 c2 00 00 00", "1E19200 NULL early-out")
    expect(analysis, 0x1E192EA, "40 f6 c6 05 74 0a", "1E19200 flags")
    expect(analysis, 0x1E19318, "45 85 f6 74 20 ba 01 00 00 00 41 8b ce e8 f6 46 c6 ff 48 83 c0 0f 48 83 e0 f0 48 03 c3",
           "1E19200 count tail")
    expect(analysis, 0x1A7DA24, "e8 b7 6f 02 00 48 83 c0 30", "1A7DA20")
    expect(analysis, 0x1AA49E6, "ff c0 4c 63 c0 48 63 c2 4c 0f af c0 48 63 c1 4c 03 c0 4a 8d 04 c5 18 00 00 00", "1AA49E0")
    if rip_target(analysis, 0x1AA49E0, 2, 6) != 0x2C71680:
        raise SystemExit("render global moved")
    expect(analysis, 0x1AB79A0, "48 8b 4f 18 48 8d 58 20", "1AB7940 +0x20")
    expect(analysis, 0x1AB79C4, "f6 07 08 48 8d 83 50 01 00 00", "1AB7940 +0x150")
    expect(analysis, 0x1E18D7F, "48 85 d2 0f 84 9f 01 00 00", "1E18CF0 NULL asset")
    facts.append("NULL optional render 512 + align16(8g+88) <= 1824 for g <= 153")
    # The optional asset's constructor shapes (OptionalReady): 1A95470(n, mode)
    # takes the per-parameter records 1A953D0(mode) returns (16 bytes: count,
    # kinds[3]; parameter i uses record min(i, 2)), sums the counts (m) and the
    # kind bytes (0 -> 0x40, 1..6 -> 0x60, else 0) and returns
    # align16(8*m + 0x1F) + kinds. Mode 0 is the jump table's first entry.
    expect(analysis, 0x1A953D0, "83 f9 05 77 46 48 8d 15 24 ac 56 fe 48 63 c1 8b 8c 82 20 54 a9 01 48 03 ca ff e1",
           "1A953D0 mode jump table")
    if struct.unpack("<I", analysis.pe.rva_read(0x1A95420, 4))[0] != 0x1A953EB:
        raise SystemExit("1A953D0 mode 0 target moved")
    expect(analysis, 0x1A953EB, "48 8d 05 86 4c 0b 01 c3", "1A953D0 mode 0")
    if rip_target(analysis, 0x1A953EB, 3, 7) != 0x2B4A078:
        raise SystemExit("mode 0 table moved")
    expect(analysis, 0x1A95470, "40 53 48 83 ec 20 44 8b d1 8b ca e8 50 ff ff ff 45 33 db 45 33 c0 45 33 c9 48 8b d8 45 85 d2 7e 62 "
           "b9 02 00 00 00 44 3b c9 41 0f 4c c9 48 63 c9 48 c1 e1 04 48 03 cb 48 63 01 44 03 d8 48 8b d0 85 c0 7e 37 48 83 c1 04 "
           "8b 01 85 c0 74 1c 7e 16 83 f8 04 7e 0a 83 f8 05 74 05 83 f8 06 75 07 b8 60 00 00 00 eb 09 33 c0 eb 05 b8 40 00 00 00 "
           "44 03 c0 48 83 c1 04 48 ff ca 75 cd 41 ff c1 45 3b ca 7c 9e 41 8d 03 41 8b c8 48 8d 04 c5 1f 00 00 00 48 83 e0 f0 "
           "48 03 c1 48 83 c4 20 5b c3", "1A95470 size loop")
    records = [struct.unpack("<4i", analysis.pe.rva_read(0x2B4A078 + 16 * i, 16)) for i in range(3)]

    def kind_bytes(kind):
        return 0x40 if kind == 0 else 0x60 if 1 <= kind <= 6 else 0
    shapes = []
    for n in range(1, 9):
        m = size = 0
        for i in range(n):
            count, *kinds = records[min(i, 2)]
            if count > 3:
                raise SystemExit(f"mode 0 record {min(i, 2)} count {count}")
            m += count
            size += sum(kind_bytes(k) for k in kinds[:max(count, 0)])
        shapes.append((n, m, ((8 * m + 0x1F) & ~0xF) + size))
    if shapes[3] != (4, 6, 544):
        raise SystemExit(f"stock46 shape n4: {shapes[3]}")
    import re
    text = CONTRACT.read_text(encoding="utf-8")
    found = re.search(r"kOptionalShapes\{\{((?:\{\d+,\d+,\d+\},?)+)\}\}", text)
    if not found:
        raise SystemExit("kOptionalShapes not found in the writer contract")
    committed = [tuple(int(x) for x in item.split(",")) for item in re.findall(r"\{(\d+,\d+,\d+)\}", found.group(1))]
    if committed != shapes:
        raise SystemExit(f"kOptionalShapes {committed} differs from the exe's 1A95470 mode 0 {shapes}")
    facts.append("optional shapes n1..8 -> m " + ",".join(str(m) for _, m, _ in shapes) + " size " +
                 ",".join(str(s) for _, _, s in shapes) + " (kOptionalShapes)")
    return facts


def build(analysis):
    stock = [(name, start, end - start) for name, start, end, _ in RANGES if name in names]
    if len(stock) != len(names):
        raise SystemExit("stock46 gate set incomplete")
    extra = []
    seen = {name for name, _, _ in stock}
    for name, start, end, code_end in RANGES + GENERAL_RANGES:
        if name in GENERAL_NAMES and name not in seen:
            instructions = analysis.instructions(start, code_end - start)
            if not instructions or instructions[-1].address + instructions[-1].size != analysis.pe.image_base + code_end:
                raise SystemExit("Incomplete decode: " + name)
            extra.append((name, start, end - start))
            seen.add(name)
    if seen != names | GENERAL_NAMES:
        raise SystemExit(f"missing ranges: {sorted((names | GENERAL_NAMES) - seen)}")
    spans = stock + [tuple(x) for x in OPAQUE_RANGES + DATA_RANGES] + extra + GENERAL_DATA
    lines = ["// Generated from exact retail executable by tools/generate_equip_writer_pins.py",
             "// (generalizes work/generate-stock46-writer-pins.py; its gates come first, unchanged).",
             "// SHA256 " + EXPECTED, "#pragma once", "#include <cstddef>", "#include <cstdint>",
             "namespace mgscoop::equipment_stock_writer_pins {",
             "struct Body {std::uintptr_t rva;std::size_t size;unsigned char sha256[32];};",
             "inline constexpr Body bodies[]={"]
    for name, start, size in spans:
        digest = hashlib.sha256(analysis.pe.rva_read(start, size)).digest()
        lines.append("    {0x%X,0x%X,{%s}}, // %s" % (start, size, ",".join("0x%02X" % b for b in digest), name))
    lines.extend(["};", "}", ""])
    return "\n".join(lines), len(spans), len(extra) + len(GENERAL_DATA)


def main():
    parser = argparse.ArgumentParser(description="Generalized equip writer pins")
    parser.add_argument("--check", action="store_true", help="verify the committed header; write nothing")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    actual = hashlib.sha256(raw).hexdigest().upper()
    if actual != EXPECTED:
        raise SystemExit(f"wrong executable {actual}; no analysis performed")
    analysis = Analysis(raw)
    facts = assert_static(analysis)
    text, count, added = build(analysis)
    if args.check:
        committed = HEADER.read_text(encoding="utf-8").replace("\r\n", "\n")
        if committed != text:
            raise SystemExit(f"{HEADER.relative_to(ROOT)} differs from the generator output (run without --check)")
        print(f"equip writer pins: {count} SHA-256 gates ({added} for the generalized pieces) and {len(facts)} static "
              f"fact groups match {EXPECTED[:8].lower()}...{EXPECTED[-4:].lower()}; "
              f"{HEADER.relative_to(ROOT).as_posix()} is the generator output")
        return
    HEADER.write_text(text.replace("\n", "\r\n"), encoding="utf-8", newline="")
    print(f"wrote {HEADER.relative_to(ROOT).as_posix()}: {count} gates ({added} new); facts: " + "; ".join(facts))


if __name__ == "__main__":
    main()
