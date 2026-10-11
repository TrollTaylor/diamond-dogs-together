"""Fingerprint and verify native reticle/UI object lifetime for read-only census.
No game execution, patch or call. --check compares generated header/evidence.
"""
import hashlib, json, struct, sys
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
ROOT=Path(__file__).resolve().parent.parent
EXE=Path(r'D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
SHA='085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
raw=EXE.read_bytes()
assert hashlib.sha256(raw).hexdigest()==SHA, 'EXE fingerprint mismatch'
pe=pefile.PE(data=raw,fast_load=True)
assert pe.OPTIONAL_HEADER.ImageBase==0x140000000
assert not pe.OPTIONAL_HEADER.DATA_DIRECTORY[5].Size
md=Cs(CS_ARCH_X86,CS_MODE_64)
sites=[]
def expect(rva,size,rows,note):
    data=pe.get_data(rva,size)
    ins=list(md.disasm(data,rva))
    assert sum(i.size for i in ins)==size, hex(rva)
    decoded={i.address:(i.mnemonic,i.op_str) for i in ins}
    for a,m,o in rows:
        assert decoded.get(a)==(m,o), (hex(a),decoded.get(a),(m,o))
    sites.append((rva,data,note))
expect(0xfe6fda,0x15,[(0xfe6fda,'lea','rax, [rip + 0x13c86ef]'),(0xfe6fe1,'mov','qword ptr [rbx], rax'),(0xfe6fe4,'lea','rax, [rip + 0x13c8715]'),(0xfe6feb,'mov','qword ptr [rbx + 0x20], rax')],'UI factory final primary and secondary vtables')
expect(0x9bdcc5,0x4f,[(0x9bdcc5,'movabs','rbx, 0x3604c28fbae1'),(0x9bdcd9,'call','0xbf1650'),(0x9bdcfc,'call','qword ptr [rax + 0x18]'),(0x9bdd04,'add','rax, -8'),(0x9bdd0d,'mov','qword ptr [rsi + 0xf8], rax')],'GO+F8 = secondary UI, P=GO+20 therefore P+D8')
expect(0xfe7956,0xf,[(0xfe7956,'mov','rax, qword ptr [rdx]'),(0xfe7961,'mov','qword ptr [rcx + 0x48], rax')],'UI init owner primary+48=P')
expect(0xfe6690,0x2f,[(0xfe6690,'call','0xbff050'),(0xfe669c,'mov','rcx, qword ptr [rdx + 0x40]'),(0xfe66a4,'mov','rax, qword ptr [rcx + 0x18]'),(0xfe66a8,'mov','qword ptr [rbx + 0x60], rax'),(0xfe66b0,'mov','qword ptr [rbx + 0x68], rax')],'UI ctor owns HudSystem primary+60 and shared HUD provider primary+68')
expect(0xfe7af0,9,[(0xfe7af0,'add','rcx, -0x20'),(0xfe7af4,'jmp','0xfea420')],'secondary UI vt+8 native reticle update thunk')
expect(0xfea684,0x9d,[(0xfea688,'mov','rdx, qword ptr [rax + 0xb8]'),(0xfea68f,'mov','ecx, dword ptr [rdx + 0x32c]'),(0xfea6eb,'mov','rax, qword ptr [rdx + 0x1e0]'),(0xfea6f4,'mov','rax, qword ptr [rdx + 0x1e8]'),(0xfea706,'shr','rdx, 0x3b'),(0xfea70d,'and','ecx, 0xf')],'native camera ray handle selection and validity checks')
expect(0xfea721,0x35,[(0xfea721,'movzx','r14d, ax'),(0xfea72c,'mov','word ptr [rbx + 0x44], r14w'),(0xfea731,'call','0xfe7270'),(0xfea738,'cmp','ebp, dword ptr [rbx + 0x76c]'),(0xfea740,'mov','rcx, qword ptr [rbx + 0x60]'),(0xfea749,'call','qword ptr [r8 + 0x140]'),(0xfea750,'mov','dword ptr [rbx + 0x76c], ebp')],'actual native hit ID -> classifier -> HudSystem vt+140; cache primary+76C')
expect(0xfe7270,0x13f,[(0xfe7270,'mov','word ptr [rsp + 0x10], dx'),(0xfe7279,'movzx','r9d, dx'),(0xfe7280,'shr','eax, 9'),(0xfe72aa,'mov','eax, 1'),(0xfe72db,'lea','eax, [rcx + 1]'),(0xfe7351,'mov','eax, 2'),(0xfe73a8,'xor','eax, eax')],'int classification(UI primary*,u16 gameObjectId), results 0/1/2; category1 red observed on owned ID1; category2 native friendly case')
expect(0xfe71b0,0xa,[(0xfe71b0,'mov','rcx, qword ptr [rcx + 0x48]'),(0xfe71b4,'mov','rax, qword ptr [rcx]'),(0xfe71b7,'jmp','qword ptr [rax]')],'UI vt+190 returns shared HUD data from U+48 provider vt+0')
expect(0xfe8b30,0x61,[(0xfe8b3f,'cmp','edx, dword ptr [rcx + 0x744]'),(0xfe8b47,'mov','rcx, qword ptr [rcx + 0x40]'),(0xfe8b4e,'call','qword ptr [rax + 0x100]'),(0xfe8b8b,'mov','dword ptr [rbx + 0x744], edi')],'native UI vt+60 action setter writes U+744 cache; no visual meaning inferred')
expect(0x124a67e,0x2b,[(0x124a67e,'mov','rcx, qword ptr [rcx + 0x60]'),(0x124a684,'mov','rcx, qword ptr [rcx + 0x20]'),(0x124a688,'movzx','ebx, byte ptr [rcx + r14]'),(0x124a6a6,'cmp','ebx, 0x12')],'HUD producer uses Q+20 action byte by gameObject ID; action0x12 not proven Quiet')
expect(0x124a6d5,0x1a,[(0x124a6df,'call','qword ptr [r8 + 0x60]'),(0x124a6e8,'or','byte ptr [rax + 0x328], 8')],'action0x12 producer updates UI action and shared HUD+328 bit3')
# Actual native HUD/provider objects captured in private run 6eef887c...
expect(0x94af70,0x27,[(0x94af78,'call','0x866910'),(0x94af8c,'jmp','0x868610')],'actual HudSystem category wrapper obtains CommonDataManager')
expect(0x866910,8,[(0x866910,'mov','rax, qword ptr [rip + 0x238b029]'),(0x866917,'ret','')],'CommonDataManager singleton pointer global2BF1940')
expect(0x868610,0x29,[(0x868620,'jmp','0x9420e0'),(0x868629,'jmp','0x9421c0'),(0x868632,'jmp','0x9420c0')],'categories2/1/0 dispatch to original reticle style setters')
expect(0x9420c0,0x1d,[],'category0 releases existing target color toward white')
expect(0x9420e0,0x67,[(0x94210c,'mov','r8d, 0xae284a4e'),(0x942129,'mov','r8d, 0xae284a4e')],'category2 applies native friendly style to existing reticle nodes148/150')
expect(0x9421c0,0x8e,[(0x9421ec,'mov','r8d, 0x8818a2d9'),(0x942209,'mov','r8d, 0x8818a2d9')],'category1 native target style; live owned ID1/category1 red WU reticle observed')
expect(0x941ed0,0x1ed,[(0x941f72,'movss','xmm0, dword ptr [rip + 0x1765352]'),(0x94200f,'call','qword ptr [rax + 0x328]'),(0x942047,'call','qword ptr [rax + 0x328]'),(0x94207c,'call','0x1dbe160'),(0x942092,'call','0x1dbe160')],'complete original RGB update across three declared pdata fragments')
expect(0x1dbe160,0x43,[(0x1dbe179,'call','0x1dad560'),(0x1dbe184,'call','0x1dad510'),(0x1dbe19e,'jmp','0x1dad4c0')],'RGB scalar setter calls three vector lane writers')
expect(0x1dad560,0x4d,[(0x1dad597,'movaps','xmmword ptr [rcx + 0x50], xmm1')],'reticle node color vector50 X writer')
expect(0x1dad510,0x4d,[(0x1dad547,'movaps','xmmword ptr [rcx + 0x50], xmm1')],'reticle node color vector50 Y writer')
expect(0x1dad4c0,0x4d,[(0x1dad4f7,'movaps','xmmword ptr [rcx + 0x50], xmm1')],'reticle node color vector50 Z writer')
expect(0x862b60,0x629,[(0x862b86,'lea','rax, [rip + 0x19e1c5b]'),(0x862b8d,'mov','qword ptr [rdi], rax'),(0x862b90,'lea','rcx, [rdi + 0x30]'),(0x862b94,'call','0x941c20')],'complete CommonDataManager ctor final vt22447E8 embedded reticle30')
expect(0x941c20,0x24b,[(0x941d26,'mov','qword ptr [rbx + 0x148], rdi'),(0x941d2d,'mov','qword ptr [rbx + 0x150], rdi')],'complete embedded original reticle initialization')
expect(0x20f90,5,[(0x20f90,'lea','rax, [rcx + 0x10]'),(0x20f94,'ret','')],'actual shared HUD provider leaf getter returns provider10 without call')
expect(0x1ffe200,0x38,[(0x1ffe20d,'lea','rax, [rip + 0x27ac8c]'),(0x1ffe214,'mov','qword ptr [rip + 0xa5eef5], rax'),(0x1ffe21b,'lea','rcx, [rip + 0xa5eefe]')],'complete shared HUD provider singleton initialization final vt2278EA0')
expect(0x864d70,0x4c,[(0x864da0,'call','0x862b60'),(0x864da9,'lea','rcx, [rip + 0x238cb90]')],'complete CommonDataManager singleton construction publication')
expect(0x10a4950,0x47,[(0x10a495d,'mov','edx, 0x78'),(0x10a4962,'mov','rcx, qword ptr [rax + 0x138]'),(0x10a4972,'mov','rcx, qword ptr [rcx + 0xd8]'),(0x10a4988,'call','qword ptr [rax + 0x280]')],'complete Quiet-camo callers helper queues native hint78 priority2 through owned UI')
expect(0xfe8690,0x70,[(0xfe86b4,'mov','dword ptr [rcx + 0x370], eax'),(0xfe86c4,'mov','byte ptr [rcx + 0x3a0], 1'),(0xfe86cd,'mov','byte ptr [rcx + 0x3a3], r8b'),(0xfe86f7,'or','dword ptr [r10 + 0x374], edx')],'complete UI hint queue leaf; enable argument5 produces flag bit9, no owner gate')
# Complete natural Quiet producer path; exclude embedded jump-table data.
expect(0x109e130,0x2b1,[(0x109e17c,'cmp','edx, dword ptr [rax + 0x218]'),(0x109e182,'setne','bpl'),(0x109e1c5,'lea','r8d, [r9 + 4]'),(0x109e1dd,'call','rax'),(0x109e244,'lea','r8d, [r9 + 5]'),(0x109e25c,'call','rax'),(0x109e2c8,'cmp','byte ptr [rsi + rax], 0x74'),(0x109e2d7,'mov','byte ptr [rbx + 0x2a1], 0x21')],'complete phase1 natural locality event4/event5 and Quiet state33 selection; installed before existing local detour')
expect(0x10b52f0,0x813,[(0x10b5346,'mov','ecx, dword ptr [rdx + rax*4 + 0x10b5b04]'),(0x10b5640,'call','0x10a4950')],'complete state32/36 callback; only local event4 Quiet camo calls helper')
expect(0x10b72a0,0x6ca,[(0x10b72ea,'mov','ecx, dword ptr [rdx + rax*4 + 0x10b796c]'),(0x10b7644,'cmp','byte ptr [rdi + rax], 0x74'),(0x10b767b,'call','0x10a4950')],'complete state33 callback including cold tail and request clearing')
expect(0x109efa0,0x109,[(0x109eff4,'lea','rdx, [rip + 0x1bb6ae5]'),(0x109f005,'mov','qword ptr [rbx + 0x290], rax'),(0x109f01f,'mov','qword ptr [rbx + 0x298], rax')],'complete per-instance callback selection from state table2C55AE0')
expect(0x20262a0,0x6ec,[(0x20265ba,'movaps','xmmword ptr [rip + 0xc2f71f], xmm0'),(0x20265cd,'movaps','xmmword ptr [rip + 0xc2f71c], xmm0'),(0x202661b,'movaps','xmmword ptr [rip + 0xc2f6fe], xmm0')],'complete state callback table constructor states32/36=10B52F0 state33=10B72A0 with zero this-adjust')
for rva,local,remote in [(0x10b5b04,0x10b55c1,0x10b565c),(0x10b796c,0x10b7617,0x10b7697)]:
    data=pe.get_data(rva,14*4)
    assert struct.unpack_from('<I',data,4*4)[0]==local and struct.unpack_from('<I',data,5*4)[0]==remote
    sites.append((rva,data,'exact 14-entry event table data, event4 local helper / event5 remote bypass'))
assert struct.unpack('<Q',pe.get_data(0x23af700+0x280,8))[0]==0x140fe8690
assert struct.unpack('<Q',pe.get_data(0x2278360+0x140,8))[0]==0x14094af70
assert struct.unpack('<Q',pe.get_data(0x2278ea0,8))[0]==0x140020f90
assert struct.unpack('<Q',pe.get_data(0x23af708,8))[0]==0x140fe7af0
assert struct.unpack('<Q',pe.get_data(0x23af890,8))[0]==0x140fe71b0
# ID type zero (player) selects case zero, returning 1. This is classification,
# not proof of color or aim assist semantics.
assert pe.get_data(0xfe73d4,1)==b'\x00'
assert struct.unpack('<I',pe.get_data(0xfe73b0,4))[0]==0xfe72aa
header='// Generated by tools/generate_native_ui_contract.py; do not edit.\n// SHA256 '+SHA+'; validated original bodies; hooks observe by default, scoped filters explicitly selected.\n#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace mgscoop::native_ui_contract {\n'
header+='inline constexpr std::uintptr_t ui_vtable_rva=0x23af700, ui_primary_vtable_rva=0x23af6d0;\n'
header+='inline constexpr std::uintptr_t hud_vtable_rva=0x2278360, category_setter_rva=0x94af70, provider_vtable_rva=0x2278ea0, provider_get_rva=0x20f90, common_global_rva=0x2bf1940, common_vtable_rva=0x22447e8, classifier_rva=0xfe7270, classifier_return=0xfea736;\n'
header+='inline constexpr std::uintptr_t quiet_queue_rva=0xfe8690, quiet_queue_return=0x10a498e;\n'
header+='inline constexpr std::size_t ui_hint=0x370, ui_hint_flags=0x374, ui_hint_active=0x3a0, ui_hint_priority=0x3a3;\n'
header+='inline constexpr std::size_t p_ui=0xd8, ui_owner=0x28, ui_hit=0x24, ui_category=0x74c, ui_hud=0x40;\n'
header+='inline constexpr std::size_t ui_provider=0x48, provider_data=0x10, shared_selected=0x44, shared_flags=0x328, reticle=0x30, reticle_state=0x14, reticle_nodes[2]={0x148,0x150}, node_color=0x50;\n'
header+='inline constexpr std::size_t cam_flags=0x32c, cam_hit_a=0x1e0, cam_hit_b=0x1e8, hud_set_category=0x140;\n'
for n,(r,data,note) in enumerate(sites):
    header+='// '+note+'\ninline constexpr unsigned char site'+str(n)+'[]={'+','.join(hex(b) for b in data)+'};\n'
header+='struct Site {std::uintptr_t rva; const unsigned char* bytes; std::size_t size;};\ninline constexpr Site sites[]={\n'
for n,(r,data,note) in enumerate(sites): header+='{'+hex(r)+',site'+str(n)+',sizeof(site'+str(n)+')},\n'
header+='};\n}\n'
evidence=json.dumps(dict(executable=str(EXE),sha256=SHA,scope='Static natural lifecycle, native RGB setters and classifier; private run 6eef887cb2e04cd785bee9eb6f968119 observed actual ID1 category1 red original WU reticle. Friendly color mapping remains measured by RGB census.',abi='Windows x64: FE7270 int(UI primary*, uint16 ID); caller FEA731. FE8690 void(UI secondary*,uint16 hint,uint32 priority,uint8 flag,uint8 enable,uint8 arg6,uint8 arg7); helper return10A498E. Helper10A4950 bool(movement*,uint8 enable), natural callers10B5640/10B767B.',sites=[dict(rva=hex(r),size=len(data),sha256=hashlib.sha256(data).hexdigest(),purpose=note) for r,data,note in sites]),indent=2)+'\n'
for path,text in [(ROOT/'native/native_ui_contract.hpp',header),(ROOT/'docs/evidence/native-ui-contract-2026-10-09.json',evidence)]:
    if '--check' in sys.argv: assert path.read_text()==text, str(path)+' differs'
    else: path.write_text(text)
print('native UI contract: exact EXE hash and 37 lifecycle/consumer/color/hint sites verified; static only')
