"""Map the game's existing preparing-member teardown; never execute game code."""
import hashlib
import json
import struct
from pathlib import Path
from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256

root = Path(__file__).resolve().parent.parent
path = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
raw = path.read_bytes()
assert hashlib.sha256(raw).hexdigest() == GAME_SHA256
a = Analysis(raw)
entries = [0xA1B580, 0x313490, 0x7DE090, 0x80D2E0, 0x7DD710,
           0x7DE0D0, 0x80D0A0, 0x80D2A0, 0x7DEBF0, 0x7DED50,
           0x80D3B0, 0x80D1A0]
functions = [a.function(rva) for rva in entries]
assert all(f['complete_declared_range_decoded'] for f in functions)
rows = {int(i['rva'], 16): (i['mnemonic'], i['operands'])
        for f in functions for i in f['instructions']}
checks = {
    0xA1B584: ('call', '0x140313780'),
    0xA1B58F: ('call', 'qword ptr [rdx + 0x78]'),
    0xA1B592: ('call', '0x1407de090'),
    0xA1B597: ('mov', 'rcx, qword ptr [rax + 0x58]'),
    0xA1B59E: ('je', '0x140a1b5a5'),
    0xA1B5A0: ('call', '0x14080d2e0'),
    0x313490: ('mov', 'byte ptr [rcx + 0xe3], 1'),
    0x80D2E0: ('or', 'byte ptr [rcx + 0x30], 4'),
    0x7DE090: ('mov', 'rax, qword ptr [rip + 0x2411d39]'),
    0x7DD733: ('lea', 'rax, [rip + 0x1a3e71e]'),
    0x7DD79C: ('mov', 'qword ptr [rsi + 0x58], rbp'),
    0x7DE0FD: ('call', '0x1407dd710'),
    0x7DE103: ('mov', 'qword ptr [rip + 0x2411cc6], rax'),
    0x7DEC00: ('call', '0x14080d2a0'),
    0x7DEC05: ('mov', 'qword ptr [rbx + 0x58], rax'),
    0x80D2C4: ('call', '0x14080d0a0'),
    0x80D124: ('lea', 'rax, [rip + 0x1a220cd]'),
    0x80D144: ('mov', 'qword ptr [rdi + 0x28], rdi'),
    0x80D3E3: ('test', 'al, 4'),
    0x7DEDDA: ('call', '0x14080d1a0'),
    0x7DEDE2: ('call', '0x140004190'),
    0x7DEDE7: ('mov', 'qword ptr [rbx + 0x58], 0'),
}
assert all(rows.get(rva) == expected for rva, expected in checks.items())
assert struct.unpack('<Q', a.pe.rva_read(0x2126F70 + 0x78, 8))[0] == a.pe.image_base + 0x313490
lines = ['// Generated from the fingerprinted executable; static inspection only.',
         '#pragma once', 'constexpr SessionBodyContract preparing_disconnect_bodies[]={']
for f in functions:
    rva = int(f['entry_rva'], 16)
    body = a.pe.rva_read(rva, f['decoded_bytes'])
    lines.append('{' + hex(rva) + ',' + str(len(body)) + ',{' +
                 ','.join(f'0x{x:02x}' for x in hashlib.sha256(body).digest()) + '}},')
lines.extend(['};', ''])
(root / 'native/preparing_disconnect_contract.hpp').write_text('\n'.join(lines))
report = {
    'executable': str(path), 'executable_sha256': GAME_SHA256,
    'upstream': 'Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e TppMain.OnTerminate',
    'functions': functions, 'instruction_checks': len(checks),
    'session_primary_slot': '0x78 -> 0x313490; sets session byte +0xE3 to 1',
    'auxiliary_owner': 'global 0x2BEFDD0; constructor table 0x221BE58; optional child +0x58',
    'auxiliary_child': 'constructor installs interface table 0x222F1F8 at +0x18 and self at +0x28',
    'request': '0x80D2E0 ORs child byte +0x30 with 4; 0x80D3B0 consumes that flag',
    'limitations': [
        'Static inspection only; no game code or Lua executed.',
        'The wrapper obtains a main session lazily and dereferences the auxiliary singleton without a null check; both must already exist before authorization.',
        'The two requests do not prove preparing members have disconnected or asynchronous work has completed.',
        'Constructor and owner-chain checks are not an atomic lifetime lease.',
        'The different full CloseSession FOB parameter-service route remains unmapped and unmodified.',
    ],
}
(root / 'docs/evidence/preparing-disconnect-contract-2026-09-10.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'Emitted {len(functions)} complete static preparing-disconnect contracts; no game execution.')
