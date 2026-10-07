// Reuse the established detached body0/body1 memory fixture without editing
// its frozen source. Only this file's main executes the manual candidate tests.
#define main upper_phase_fixture_main
#include "partner_upper_phase_observer_tests.cpp"
#undef main
#include "mgscoop/partner_upper_manual_idle.hpp"
namespace mi=mgscoop::partner_upper_manual_idle;
namespace {
Memory* selected=nullptr;
unsigned submits=0,queries=0;
unsigned pin_calls=0;std::uintptr_t refuse_pin=0;bool clear_gate=false;
bool query_ok=true,change_lower=false,reenter=false,clear_during_read=false,return_zero=false;
void* submit_cs=nullptr;
std::uint32_t submit_body=0,submit_channel=0,submit_option=1;
std::uint64_t submit_state=0;float submit_blend=1;
bool Query(void*,std::uintptr_t at,std::size_t n) noexcept {
    ++queries;SetLastError(902);
    return query_ok&&((at==buffer&&n==0x18)||(at==records&&n==4*0x28));
}
bool ReadManual(void* context,std::uintptr_t at,void*out,std::size_t n) noexcept {
    if(clear_during_read){clear_during_read=false;mi::Clear();}
    return Memory::Read(context,at,out,n);
}
bool FakePin(mi::Reader,std::uintptr_t image,const e::Pin&pin) noexcept {
    ++pin_calls;bool known=false;
    for(const auto&p:mi::kAdditionalPins)if(p.rva==pin.rva&&p.size==pin.size&&Is(p.sha256,pin.sha256))known=true;
    Check(image==base&&known,"all extra complete native idle-path spans must participate in preconfigure gate");
    if(clear_gate){clear_gate=false;mi::Clear();}
    return pin.rva!=refuse_pin;
}
std::uintptr_t FakeSubmit(void*this_cs,std::uint32_t body,std::uint32_t channel,
                          std::uint64_t state,float blend,std::uint32_t option) {
    ++submits;submit_cs=this_cs;submit_body=body;submit_channel=channel;
    submit_state=state;submit_blend=blend;submit_option=option;
    Check(this_cs==reinterpret_cast<void*>(cs)&&body==1&&channel==2&&state==mi::kIdle
        &&blend==0.0f&&option==0,"real candidate submit preserves exact qualified CS170 ABI and idle arguments");
    Check(selected!=nullptr,"test cannot reach game function without a fake");
    for(unsigned offset:{0u,2u})selected->Put(buffer+offset,std::uint16_t(4));
    selected->Put(records+2*40+32,state);
    if(change_lower)selected->U8(records+7,1);
    if(reenter){reenter=false;SetLastError(0x9876);
        const auto raw=mi::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(0x8888),base+0x1234,444,GetTickCount64());
        Check(raw==0xfedcba9876543210&&original_error==0x9876&&GetLastError()==0x1234,
              "reentrant foreign original preserves its own arguments return and errors");}
    SetLastError(0x7777);return return_zero?0:0xabcdef0123456789;
}
void Ready(Memory&m) {
    selected=&m;submits=queries=0;query_ok=true;change_lower=reenter=clear_during_read=return_zero=false;
    pin_calls=0;refuse_pin=0;clear_gate=false;mi::TestPins(FakePin);
    m.Ptr(base+0x22e6070+0x170,base+0xaf2ba0);
    m.Ptr(upper+0x1a0,upper_table+116*0x48);m.Ptr(upper+0x1a8,0);
    m.Put(0x220000+89*8,mi::kIdle);m.U8(upper_table+89*0x48+0x14,3);
    mi::TestSubmit(FakeSubmit);
}
mi::Owner ManualOwner(std::uint64_t generation=100) {auto o=Owner(generation);o.body_epoch=0;return o;}
bool ManualConfigure(Memory&m) {
    const mi::Reader reader{&m,ReadManual};
    return mi::CodeGate(base,0xa01d000,reader)&&mi::Configure(base,{true,true,true,true},reader,{nullptr,Query});
}
std::vector<mi::Receipt> ManualDrain() {
    std::vector<mi::Receipt> out;mi::Receipt r{};while(mi::Drain(r))out.push_back(r);return out;
}
void CodeGates() {
    Memory m;Ready(m);const mi::Reader reader{&m,ReadManual};
    Check(!mi::Configure(base,{true,true,true,true},reader,{nullptr,Query}),"authorization alone cannot bypass new loaded idle-path gate");
    if(!mi::Compiled()) {
        Check(!mi::CodeGate(base,0xa01d000,reader)&&pin_calls==0,"default OFF does no new code-gate reads or validation");return;
    }
    Check(!mi::CodeGate(base,0xa01c000,reader)&&pin_calls==0,"wrong image span cannot validate loaded routines");
    for(const auto&pin:mi::kAdditionalPins) {
        refuse_pin=pin.rva;pin_calls=0;
        std::uintptr_t mismatch=0;
        Check(!mi::CodeGate(base,0xa01d000,reader,&mismatch)&&mismatch==pin.rva
            &&!mi::Configure(base,{true,true,true,true},reader,{nullptr,Query}),
            "each new loaded routine mismatch independently prevents Configure and native commands");
    }
    refuse_pin=0;pin_calls=0;
    clear_gate=true;Check(!mi::CodeGate(base,0xa01d000,reader)&&!mi::Configure(base,{true,true,true,true},reader,{nullptr,Query}),
        "revocation during validation cannot publish stale code-gate authorization");pin_calls=0;
    Check(mi::CodeGate(base,0xa01d000,reader)&&pin_calls==3,"all three full loaded spans are required");
    Check(!mi::Configure(base,{true,true,true,true},m.Reader(),{nullptr,Query}),"a different reader cannot borrow code-gate authorization");
    Check(mi::Configure(base,{true,true,true,true},reader,{nullptr,Query}),"matched code gate plus existing prehook pins can configure");
    m.U8(base+0x100,'a');m.U8(base+0x101,'b');m.U8(base+0x102,'c');
    e::Pin small{0x100,3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
    Check(mi::TestPinHash(reader,base,small),"actual BCrypt hash path accepts exact detached abc bytes");
    m.U8(base+0x102,'d');Check(!mi::TestPinHash(reader,base,small),"actual full-span hash refuses a changed byte");
    small.size=0x1001;const auto reads=m.reads;
    Check(!mi::TestPinHash(reader,base,small)&&m.reads==reads,"oversize full-code requests refuse before reader");
    mi::Clear();
}
void DefaultsAndPermission() {
    Check(mi::IsTrigger("upperidle=1",11)&&mi::IsTrigger(" \tupperidle=1\r\n",15),"standalone manual command accepts only exact text plus whitespace");
    Check(!mi::IsTrigger("upperidle=1 dmplay=24",21)&&!mi::IsTrigger("upperidle=0",11)
        &&!mi::IsTrigger("upperidle=1\0x",13)&&!mi::IsTrigger(nullptr,0),"combined disabled malformed or binary commands cannot arm through parser");
    void* arena=VirtualAlloc(nullptr,1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(arena!=nullptr,"detached query fixture allocates only its own scratch mapping");
    const auto at=reinterpret_cast<std::uintptr_t>(arena);
    Check(mi::TestWritable(at,4*40),"bounded records inside a larger arena pass native writable query");
    DWORD old=0;Check(VirtualProtect(arena,4096,PAGE_READONLY,&old)!=0,"detached fixture changes only its own page protection");
    Check(!mi::TestWritable(at,4*40)&&!mi::TestWritable(at,0x50001),"readonly or excessive request spans refuse");
    Check(VirtualFree(arena,0,MEM_RELEASE)!=0,"detached writable fixture releases its own arena");
    Memory m;Ready(m);const auto now=GetTickCount64();auto o=ManualOwner();
    Check(Configure(m),"existing read-only phase lease configures independently");
    Check(p::PublishGhostOwner(o,o.owner_thread,now),"observation permission exists");
    if(!mi::Compiled()) {
        const auto reads=m.reads;
        Check(!ManualConfigure(m)&&!mi::PublishOwner(o,o.owner_thread,now)&&!mi::Arm(1,o.owner_thread,now),
              "default-OFF manual candidate cannot configure publish or arm");
        original_calls=0;SetLastError(0x2468);
        const auto raw=mi::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(0x8888),base+0x1234,999,now);
        Check(raw==0xfedcba9876543210&&original_calls==1&&original_entry==reinterpret_cast<void*>(0x8888)
              &&original_error==0x2468&&GetLastError()==0x1234&&submits==0&&m.reads==reads,
              "OFF foreign forwarding preserves original and adds no source reads or submissions");
        p::Clear();return;
    }
    Check(ManualConfigure(m),"manual module configures separately");
    const auto reads=m.reads;
    Check(!mi::Arm(1,o.owner_thread,now)&&m.reads==reads&&submits==0,
          "observation generation alone cannot arm native request");
    Check(!mi::PublishOwner(o,o.owner_thread+1,now)&&m.reads==reads,"worker cannot publish manual permission");
    Check(mi::PublishOwner(o,o.owner_thread,now),"natural owner publishes a separate epoch0 body1 lease");
    Check(!mi::Arm(1,o.owner_thread+1,now),"worker cannot issue manual nonce");
    Check(mi::Arm(1,o.owner_thread,now),"exact Fire116 and idle89 writable request can arm");
    Check(!mi::Arm(2,o.owner_thread,now),"one pending command refuses replacement");
    const auto before=m.reads;original_calls=0;SetLastError(0x4567);
    auto raw=mi::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(0x8888),base+0x1234,999,now);
    Check(raw==0xfedcba9876543210&&original_calls==1&&original_error==0x4567&&GetLastError()==0x1234
          &&m.reads==before&&submits==0,"foreign tuple has zero reads and no native submission");
    auto rows=ManualDrain();Check(rows.size()==1&&rows[0].stage==mi::Stage::armed,"arming emits only bounded command receipt");
    mi::Clear();p::Clear();
}
std::uintptr_t ApplyIdleOriginal(void* value) {
    const auto raw=FakeOriginal(value);
    if(value==reinterpret_cast<void*>(entry)) {
        selected->Ptr(upper+0x1a0,upper_table+89*0x48);
        selected->U8(records,0x33); // legitimate later lower update, not an idle submit write
    }
    return raw;
}
void ForwardingAndIsolation() {
    if(!mi::Compiled())return;Memory m;Ready(m);AddLocalBody(m);
    const auto now=GetTickCount64();auto o=ManualOwner();
    Check(ManualConfigure(m)&&mi::PublishOwner(o,o.owner_thread,now)&&mi::Arm(1,o.owner_thread,now),"manual lifecycle ready");
    Check(Configure(m)&&p::PublishGhostOwner(o,o.owner_thread,now),"remote observation lease independently active");
    if(e::Compiled()) {
        Check(e::Configure(base,{true,true,true,true},m.Reader())&&e::PublishOwner(o,o.owner_thread,now),"local End lease independently active");
        const e::Submission q{local_cs,base+0x105d3b3,0,2,999,0x41800000,0,e::kEnd,false};
        auto t=e::BeforeSubmission(q,now);Check(t.qualified,"local End qualifies separately from ghost idle nonce");
        e::AfterSubmission(t,7,now);Check(DrainEnd().size()==2,"local End pair preserved");
    }
    original_calls=0;SetLastError(0x4511);
    auto raw=mi::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(local_entry),base+0xaf0381,999,now);
    Check(raw==0xfedcba9876543210&&original_calls==1&&original_entry==reinterpret_cast<void*>(local_entry)
          &&original_error==0x4511&&GetLastError()==0x1234&&submits==0,"body0 original forwards once and cannot consume body1 permission");
    if(e::Compiled())Check(DrainEnd().size()==2,"new wrapper multiplexes qualified local End before and after update");
    Check(Drain().empty(),"local phase does not use ghost observation budget");
    reenter=true;SetLastError(0x4512);
    raw=mi::ForwardUpdate(ApplyIdleOriginal,reinterpret_cast<void*>(entry),base+0xaf0381,456,now);
    Check(raw==0xfedcba9876543210&&original_calls==3&&original_entry==reinterpret_cast<void*>(entry)
          &&original_error==0x4512&&GetLastError()==0x1234&&submits==1,"worker submission and recursive foreign original each forward once without changing outer RAX/error");
    auto rows=ManualDrain();Check(rows.size()==4,"one manual command has four bounded correlated receipts");
    Check(rows[1].stage==mi::Stage::before_submit&&rows[2].stage==mi::Stage::after_submit
          &&rows[2].immediate_lower_unchanged&&rows[2].raw_submit_rax==0xabcdef0123456789,
          "immediate CS170 pair proves fake lower record isolation and retains opaque submit return");
    Check(rows[3].stage==mi::Stage::after_update&&rows[3].upper_idle_current&&rows[3].upper_noqueued
          &&rows[3].raw_update_rax==raw&&rows[3].snapshot.lower.record[0]==0x33,
          "after-update correlates idle readback while allowing natural lower locomotion advancement");
    Check(Drain().size()==2,"new wrapper retains real remote phase forwarding observations");
    const auto reads=m.reads;SetLastError(0x4513);
    raw=mi::ForwardUpdate(FakeOriginal,reinterpret_cast<void*>(entry),base+0xaf0381,456,now);
    Check(raw==0xfedcba9876543210&&submits==1,"same nonce cannot submit twice");
    Check(m.reads>=reads,"unrelated independently authorized observers may still sample");
    mi::Clear();p::Clear();e::Clear();
}
void RefusalsAndLifecycle() {
    if(!mi::Compiled())return;
    for(unsigned mode=0;mode<11;++mode) {
        Memory m;Ready(m);auto o=ManualOwner();const auto now=GetTickCount64();
        Check(ManualConfigure(m)&&mi::PublishOwner(o,o.owner_thread,now),"refusal fixture has owner");
        if(mode==0){m.Put(0x220000+89*8,std::uint64_t(1));Check(!mi::Arm(1,o.owner_thread,now),"wrong idle code refuses before any game call");continue;}
        Check(mi::Arm(1,o.owner_thread,now),"refusal command arms under positive guards");
        if(mode==1)m.U32(cs+8,2);
        if(mode==2)m.Ptr(base+0x22e6070+0x170,base+0xaf2ba1);
        if(mode==3)query_ok=false;
        if(mode==4)m.Ptr(upper+0x1a8,upper_table+89*72);
        if(mode==5)clear_during_read=true;
        if(mode==6)mi::Clear();
        if(mode==7){++o.body_epoch;Check(!mi::PublishOwner(o,o.owner_thread,now),"epoch change cannot reuse generation");++o.generation;
            Check(mi::PublishOwner(o,o.owner_thread,now),"new epoch generation gets independent lease");}
        if(mode==8){++o.equip;Check(mi::PublishOwner(o,o.owner_thread,now),"profile refresh invalidates pending command");}
        if(mode==9){Check(mi::PublishOwner(o,o.owner_thread,now+1),"ordinary refresh does not reset nonce");}
        if(mode==10){Check(mi::PublishOwner(o,o.owner_thread,now+1),"owner refresh precedes stale command check");}
        const auto when=mode==10?now+mi::kCommandMs+1:(mode==9?now+1:now);
        auto ticket=mi::BeforeUpdate(Update(456),when);
        if(mode==9){Check(ticket.submitted&&submits==1,"same profile refresh keeps one pending explicit command");mi::AfterUpdate(ticket,7,now+1);}
        else Check(!ticket.submitted&&submits==0,"changed storage caller lifecycle epoch profile or expiration never invokes fake original170");
        mi::Clear();p::Clear();e::Clear();
    }
    Memory m;Ready(m);auto o=ManualOwner();const auto now=GetTickCount64();
    Check(ManualConfigure(m)&&mi::PublishOwner(o,o.owner_thread,now),"budget owner ready");
    for(unsigned i=0;i<mi::kCommandCap;++i) {
        Check(mi::Arm(i+1,o.owner_thread,now),"fresh increasing manual command fits lifetime budget");
        auto ticket=mi::BeforeUpdate(Update(456),now);Check(ticket.submitted,"one claim consumes each command exactly once");
        Check(!mi::Arm(i+2,o.owner_thread,now),"a claimed command cannot be replaced before original update returns");
        mi::AfterUpdate(ticket,7,now);mi::AfterUpdate(ticket,8,now);
        Check(ManualDrain().size()==4,"duplicate after callback cannot add a receipt or reset lifetime budget");
        Check(mi::PublishOwner(o,o.owner_thread,now),"ordinary owner refresh does not reset budget");
    }
    Check(!mi::Arm(mi::kCommandCap+1,o.owner_thread,now)&&submits==mi::kCommandCap,"lifetime cap refuses ninth command");
    mi::Clear();++o.generation;Check(mi::PublishOwner(o,o.owner_thread,now),"new lifecycle owner may refresh");
    Check(!mi::Arm(mi::kCommandCap+2,o.owner_thread,now),"Clear and new generation cannot reset lifetime manual cap");
    p::Clear();e::Clear();mi::Clear();
    Memory broken;Ready(broken);const auto time=GetTickCount64();o=ManualOwner();
    Check(ManualConfigure(broken)&&mi::PublishOwner(o,o.owner_thread,time)&&mi::Arm(1,o.owner_thread,time),"isolation failure command ready");
    change_lower=true;return_zero=true;
    const auto ticket=mi::BeforeUpdate(Update(456),time);
    Check(!ticket.submitted&&submits==1,"unexpected lower mutation invalidates permission without a retry or rollback");
    auto failed=ManualDrain();Check(failed.size()==1&&failed[0].stage==mi::Stage::refused
        &&failed[0].submit_attempted&&failed[0].raw_submit_rax==0&&Is(failed[0].refusal,"lower_record_changed"),
        "terminal refusal remains observable even when opaque submit return is zero");
    Check(!mi::Arm(2,o.owner_thread,time),"terminal submission failure cannot arm another native call with old permission");
    mi::Clear();
}
} // namespace
int main() {
    CodeGates();DefaultsAndPermission();ForwardingAndIsolation();RefusalsAndLifecycle();
    std::printf("PASS %u manual idle checks; compiled=%d; native_game_calls=0; all_submissions=fake\n",checks,mi::Compiled());
}
