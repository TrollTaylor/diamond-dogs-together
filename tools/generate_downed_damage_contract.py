"""Generate hash gates for the exact retail damage, mask and local health paths.
Read-only executable inspection. No game calls, patching or retail writes.
"""
import hashlib, json, sys, struct
from pathlib import Path
import pefile
ROOT=Path(__file__).resolve().parent.parent
EXE=Path(r'D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe')
SHA='085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45'
PINS=[
(0x11f5350,1893,'Complete damage calculation including declared jump table, nine-argument ABI'),
(0x11f6190,274,'Health max/current initialization and 0x4c stride'),
(0x11f5fb0,64,'SetLife native clamped integer setter'),
(0x11f81f8,67,'Natural calculation caller including all nine actual arguments'),
(0x11f8fdb,112,'Friendly sleep null-synchronizer notification path'),
(0x9df360,451,'Player.SetPadMask Lua binding'),
(0x9dc510,224,'Player.ResetPadMask Lua binding'),
(0x11f76c0,0xd4,'Natural damage update player/local/record ownership'),
(0x11f7899,0xd8,'Natural current-life and script-vars U16 mirror agreement'),
(0x998a34,0x6b,'playerLife registration to script-vars word +0x11c'),
(0x9aaa90,0x70,'playerLife Lua word getter/setter ABI'),
(0xbfefa0,0x9d,'Registry root locator and fixed variables bank lifecycle'),
(0xff9d30,3168,'Complete five-argument DefenseCallback and declared jump tables'),
(0xffab40,78,'Complete five-argument DefenseCallback CanExec admission'),
(0xff5bb0,549,'Natural five-argument Defense callback registration and player ownership'),
(0x2023e70,43,'Static Defense callback construction'),
(0x20811a0,45,'Static Defense callback alternative construction'),
(0x1be2b60,145,'Observed natural five-argument Defense invocation return 1BE2BD6'),
(0x1be2dbd,96,'Natural five-argument CanExec invocation routes'),
(0xff3480,261,'Complete body collision-part classifier domain 0 through 11'),
(0x5c9fc0,2927,'Complete Lua damage serializer: field16 sleep -> native flags bit8'),
(0x23abaa8,8,'Exact Lua serializer field16 double constant'),
(0x21557a0,8,'Exact Lua serializer field17 faint double constant'),
(0x178910,103,'Complete public Fox.StrCode32 Lua wrapper returning unsigned low32'),
(0xf1680,11,'String hash accessor to retail hash producer'),
(0x234e0,5,'Exact retail string hash thunk'),
(0x1a098d0,5,'Exact retail hash producer thunk'),
(0x1a09ac0,149,'Complete hash producer adds NUL and signed-first-byte/length seed'),
(0x1a09770,110,'Complete seeded hash mixing and low48 result'),
(0x1a091f0,450,'Complete aligned short hash function, exact len14 branch'),
(0x1a08cb0,456,'Complete unaligned short hash function, same exact len14 result'),
(0x1d630,14,'Complete cached named-setting hash accessor'),
(0x1206550,120,'Complete SetButtonMask eight unique name/value entries and OR aggregate'),
(0x1206410,84,'Complete ResetButtonMask removes matching name only and recomputes OR'),
(989984, 113, 'Complete public Lua string conversion for Fox.StrCode32'),
(27688640, 105, 'Complete named-setting Lua string field conversion'),
(400224, 23, 'Complete setting-name string construction wrapper'),
(164832, 84, 'Complete setting-name strlen and hash construction route'),
(120384, 103, 'Complete constructed cached string assignment route'),
(121648, 141, 'Complete cached string allocation wrapper'),
(121216, 286, 'Complete cached string allocation dispatch'),
(120880, 151, 'Complete cached string record creation: producer hash stored at record+10'),
(120496, 209, 'Complete cached string reference assignment preserving hash record')]

PINS += [
(0x11f4f80,163,'Complete natural enqueue scans sixteen 0x40-byte slots and sets active flag bit0'),
(0x11f81e0,24,'Natural damage update admits queued Calc only when active bit0 is set'),
(0x11f9051,94,'Actual positive hurt raises per-player C0 recovery timer to retail4 seconds'),
(0x20e00e0,4,'Exact ordinary positive-hurt recovery timer float4.0'),
(0x11f92d0,1075,'Complete per-player C0 timer countdown and zero regen multiplier while timer positive')]
PINS += [
(0x9cb200,2210,'Complete instance rate initialization: owned crawl base and retail product'),
(0x109f690,89,'Complete movement rate getter: crawl uses record+44 only'),
(0x114030d,304,'Complete retail crawl-factor writer preserving base and recomputing product'),
(0x11312be,567,'Complete retail crawl-factor reset preserving base and recomputing product'),
(0x10b9b8e,1408,'Complete normal locomotion movement consumer multiplying native base by crawl rate'),
(0x10ba10e,4152,'Complete alternate locomotion movement consumer multiplying native base by crawl rate')]
PINS += [
(0x1206a00,2331,'Complete PlayerPad update: twelve contiguous chained unwind segments; RCX-only natural ABI'),
(0x10bcd90,634,'Complete movement sampler: eight contiguous chained unwind segments; PP+10 -> action+2C0'),
(0x9c2a8e,12,'Exact natural primary GO+D0 PlayerPad virtual update caller; return9C2A9A ignores result')]
assert len(PINS)==57
# Literal exact retail len14 short branches and seeded mixer; no native calls.
U64=(1<<64)-1
K=0x9ddfea08eb382d69
def hash_len16(a,b):
 a=((a^b)*K)&U64; a^=a>>47
 b=((b^a)*K)&U64; b^=b>>47
 return (b*K)&U64
def revive_setting_hash(name):
 data=name.encode('ascii')+b'\0'; n=len(data)
 assert 8<n<=16
 first=int.from_bytes(data[:8],'little'); last=int.from_bytes(data[-8:],'little')
 rotated=(last+n)&U64; rotated=((rotated>>n)|(rotated<<(64-n)))&U64
 plain=hash_len16(first,rotated)^last
 return hash_len16((plain+0x651e95c4d06fbfb1)&U64,(data[0]<<16)+n-1)&0xffffffff
assert revive_setting_hash('MgsCoopRevive')==0x71fe74bf
assert revive_setting_hash('MgsCoopDowned')==0x6786ae0b
raw=EXE.read_bytes()
assert hashlib.sha256(raw).hexdigest().upper()==SHA
pe=pefile.PE(data=raw,fast_load=True)
assert pe.OPTIONAL_HEADER.ImageBase==0x140000000
assert not pe.OPTIONAL_HEADER.DATA_DIRECTORY[5].Size
assert struct.unpack('<d',pe.get_data(0x23abaa8,8))[0]==16
assert struct.unpack('<d',pe.get_data(0x21557a0,8))[0]==17
assert struct.unpack('<f',pe.get_data(0x20e00e0,4))[0]==4.0
# Each declared complete input function includes every contiguous PE unwind
# segment, not only the first/chained prologue returned by some disassemblers.
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
INPUT_BODIES=[(0x1206a00,0x120731b,12,'ca7dfe673f4ca88352b2c46f4b07ba40edc1429fc5417170bbd3e26cb8f33976'),
              (0x10bcd90,0x10bd00a,8,'c57bf54d891222b22349d3282f6995c80dec00249db686be77627a1b2e4fe4f7')]
for begin,end,count,digest in INPUT_BODIES:
 entries=sorted((e.struct.BeginAddress,e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION if begin<=e.struct.BeginAddress<end)
 assert len(entries)==count and entries[0][0]==begin and entries[-1][1]==end
 assert all(a[1]==b[0] for a,b in zip(entries,entries[1:])), 'input body chain gap'
 # Every later unwind range must chain only to an earlier exact entry in
 # this same qualified body, whose chain ultimately ends at the root prologue.
 bodies={e.struct.BeginAddress:(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION if begin<=e.struct.BeginAddress<end}
 for at,(_,_,unwind) in bodies.items():
  header=pe.get_data(unwind,4);flags=header[0]>>3
  if at==begin: assert flags==0
  else:
   assert flags==4
   chained=struct.unpack('<III',pe.get_data(unwind+((4+2*header[2]+3)//4)*4,12))
   assert chained[0]<at and bodies.get(chained[0])==chained
 assert hashlib.sha256(pe.get_data(begin,end-begin)).hexdigest()==digest
assert hashlib.sha256(pe.get_data(0x9c2a8e,12)).hexdigest()=='16e32fcc431c1c8386339db314cdcb9101138ee72e3abdf6ba5f7f2aa106eccd'
for slot in (0x23c73e8,0x23c7548):
 assert struct.unpack('<Q',pe.get_data(slot,8))[0]==0x1411f5350
head='// Generated by tools/generate_downed_damage_contract.py. Exact retail SHA256 '+SHA+'.\n#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace mgscoop::downed_damage_contract {\nstruct Pin {std::uintptr_t rva; std::size_t size; std::uint8_t digest[32];};\ninline constexpr Pin pins[] = {\n'
rows=[]
for rva,size,note in PINS:
 data=pe.get_data(rva,size)
 assert len(data)==size
 digest=hashlib.sha256(data).digest()
 head+='    {'+hex(rva)+','+str(size)+',{'+','.join(hex(b) for b in digest)+'}}, // '+note+'\n'
 rows.append(dict(rva=hex(rva),size=size,sha256=digest.hex().upper(),meaning=note))
head+='};\ninline constexpr std::uintptr_t slots[]={0x23C73E8,0x23C7548};\ninline constexpr std::uintptr_t calculate=0x11F5350;\ninline constexpr std::uintptr_t defense_slot=0x23B0998, defense=0xFF9D30;\ninline constexpr std::uintptr_t can_slot=0x23B0990, can=0xFFAB40;\n}\n'
head=head[:-2]+'inline constexpr std::uintptr_t pad_update_slot=0x23C8D40, pad_update=0x1206A00, pad_update_return=0x9C2A9A;\n}\n'
assert struct.unpack('<Q',pe.get_data(0x23c8d40,8))[0]==0x141206a00
assert struct.unpack('<Q',pe.get_data(0x23b0998,8))[0]==0x140ff9d30
assert struct.unpack('<Q',pe.get_data(0x23b0990,8))[0]==0x140ffab40
evidence=json.dumps(dict(exe_sha256=SHA,pins=rows,body_part='queued packet+26 is collision-part classification 0..11; FF3480 unknown hash defaults1; part0 has native head-style special path',sleep_mode='Lua field16 serialized to native param+14 bit8; original Calc mode2 identifies sleep; mode1 general stun is distinct',friendly_rule='fresh exact independent offline pair; local shooter0 targets replica1; active queued flags bit0; valid body part; finite positive life damage or mode2 with finite positive stun',crash_rule='exact offline other-player direct part0 positive stun alone protects null synchronizer path; no owner lease required',friendly_dedup='Defense hit+24 UID is rejected by retail four-entry per-target cache; enqueue11F4F80 sets bit0 only on a vacant slot; natural Calc update requires bit0; forwarding Neutralize clears bit0, so inactive re-entry such as live0x4000 is excluded',friendly_recovery='retail positive hurt raises C0 timer+0 to at least4f and disables regen multiplier while positive; accepted owner damage uses a bounded4s wall-clock HP ceiling via existing dual owned StoreLife instead of any new native call or timer pointer; lower native HP retained and lease reset on context/death/down/revive',live_ordinary_observation='ab69b918bb394f548eb91bea5c0ed2b7: actual positive942 flags0x4401 reduced clone HP while oldbit2 forwarding refused; positiveinactive0x4000 Calc is excluded; only oldflag4403 forwardedseq1..3 and owner healed immediately',revive_action='MgsCoopRevive exact Fox.StrCode32 low32 0x71FE74BF; cmd4 reads coherent stable named mask, excludes only its own 0x20 entry from receipt while other named restrictions remain authoritative; native input unchanged',last_error='incoming error restored before every original callback; original result error retained after inspection and logging',calc_abi='float Calculate(interface,uint32 id,packet,lifeRecord,byte* mode,float* stun,float* rawLife,float* rawStun,byte* special)',runtime_verified=False,crash_source='guest script-observer.log RVA11F9040 null read',contract='interface+8=P; P+60=Q; Q+8+id*4C=lifeRecord; vtable+48 natural calculation; owned health dual store to Q current and exact registered vars.playerLife U16'),indent=2)+'\n'
for path,text in [(ROOT/'native/downed_damage_contract.hpp',head),(ROOT/'docs/evidence/downed-damage-2026-10-10.json',evidence)]:
 if '--check' in sys.argv: assert path.read_text()==text, str(path)+' differs'
 else: path.write_text(text)
print(f'Downed damage contract: exact retail hash and {len(PINS)} calculation/control/health/defense/part/sleep/revive/recovery sites; static only')
