#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cmath>
#include <cstring>
#include <limits>
#include "mgscoop/carry_info.hpp"
#include "mgscoop/downed_player.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_defer.hpp"
#include "partner_player_contract.hpp"
#include "carry_info_contract.hpp"
#include "carry_info_policy.hpp"
#include "carry_plugin_read.hpp"
#include "carry_provider_read.hpp"
#include "carry_recipient_read.hpp"
#include "carry_request_retirement_read.hpp"
namespace mgscoop::carry_info {
namespace c=carry_info_contract;
namespace {
SRWLOCK lock=SRWLOCK_INIT;
partner_player::Addresses addresses{};
bool verified=false,stopped=false,failed=false,configured=false;
std::uint32_t boundThread=0;
std::uint64_t lastQuery=0;
void (*logger)(const char*) noexcept=nullptr;
unsigned readRefusals=0,providerRefusals=0;
struct Preserve { DWORD value=GetLastError(); ~Preserve(){SetLastError(value);} };
bool ReadBytes(std::uintptr_t p,void* out,std::size_t size) noexcept {
    SIZE_T count=0;
    return p&&size&&p<=std::numeric_limits<std::uintptr_t>::max()-size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,size,&count)&&count==size;
}
template<class T> bool Read(std::uintptr_t p,T& out) noexcept { return ReadBytes(p,&out,sizeof(out)); }
bool Aligned(std::uintptr_t p,unsigned alignment) noexcept {return p&&!(p&(alignment-1));}
bool Finite4(const float (&v)[4]) noexcept {for(float f:v)if(!std::isfinite(f))return false;return true;}
// Read every input dereferenced by the exact 873AC63AC9BE receiver and its
// CS+130 getter before invoking it. The receiver writes only our message/out;
// the getter constructs its matrices entirely on its caller's stack.
bool ReadIdentity(detail::Identity& out) noexcept {
    out={};partner_player::State s{};
    if(partner_player::ReadState(addresses,s)!=partner_player::Refusal::none||s.count!=2||s.local!=0||s.ids[0]!=0||s.ids[1]!=1||s.realized_count!=2||!(s.flags&1u)||(s.flags&0x100u)||s.sync||!s.arrays_ok||!s.bits_ok||!s.slots_ok)return false;
    if(!partner_player::Realized(s,0)||!partner_player::Realized(s,1)||s.parts[0]!=partner_player_contract::parts_loaded_state||s.parts[1]!=partner_player_contract::parts_loaded_state||s.pool[0]==s.pool[1])return false;
    out.player=s.P;out.data=s.Q;out.bitset=s.S;out.exists=s.exists;out.locals=s.local_bits;out.slots=s.slots;
    out.count=s.count;out.local=s.local;out.realized=s.realized_count;
    std::uintptr_t lifeBase=0,flagsBase=0;
    if(!Read(s.Q+8,lifeBase)||!Aligned(lifeBase,4)||!Read(s.Q+0x168,flagsBase)||!Aligned(flagsBase,4))return false;
    partner_player::Block blocks[2]{};
    for(unsigned k=0;k<2;++k){
        out.ids[k]=s.ids[k];out.pool[k]=s.pool[k];out.parts[k]=s.parts[k];
        auto& body=out.body[k];auto& b=blocks[k];
        if(partner_player::ResolveBlock(addresses,s,k,b))return false;
        body.block=b.blk;body.phase=b.ps+0x3e8;body.cs=b.cs;body.transform=b.t;
        std::uintptr_t phaseVT=0,callback=0,blockCS=0,csVT=0,cs130=0,cs380=0;
        if(!Read(body.phase,phaseVT)||phaseVT!=b.ps_vt+0x1e0||!Read(phaseVT+0x28,callback)||callback!=addresses.image_base+c::receiver_rva||
            !Read(b.blk+0x10,blockCS)||blockCS!=b.cs||!Read(b.cs,csVT)||csVT!=addresses.cs_vtable||
            !Read(csVT+0x130,cs130)||cs130!=addresses.image_base+0xaf0ea0||!Read(csVT+0x380,cs380)||cs380!=addresses.image_base+0xaf1130)return false;
        std::uintptr_t psRecords=0;
        if(!Read(b.ps+0x280,psRecords)||!Aligned(psRecords,4))return false;
        body.psRecord=psRecords+(b.id-b.base_id)*0x20;
        if(!ReadBytes(body.psRecord,body.actionBits,sizeof(body.actionBits)))return false;
        body.carryFlags=body.actionBits[0];
        body.life=lifeBase+std::uintptr_t(s.ids[k])*0x4c;body.flags=flagsBase+std::uintptr_t(s.ids[k])*4;
        float health=0,maximum=0;std::uint32_t flags=0;
        if(!Read(body.life,health)||!Read(body.life+4,maximum)||!std::isfinite(health)||!std::isfinite(maximum)||health<=0||maximum<1||maximum>65535.f||health>maximum||!Read(body.flags,flags))return false;
        body.lifeValue=health;body.maximum=maximum;body.qFlags=flags;
        // The CS+130 thunk reads entry+68 -> model; AF0E10 reads the
        // model+0xA0 header and joint0. No vtable call is made on the model.
        if(!Read(b.entry+0x68,body.model)||!Aligned(body.model,16))return false;
        float matrix[4][4]{},joint[4][4]{};std::int16_t count=0;std::uint8_t transformed=0;
        if(!ReadBytes(body.model+0xa0,matrix,sizeof(matrix))||!Read(body.model+0xf8,count)||count<=0||count>4096||
            !Read(body.model+0xfa,transformed)||transformed>1||!Read(body.model+0xe0,body.joints)||!Aligned(body.joints,16)||!ReadBytes(body.joints,joint,sizeof(joint)))return false;
        for(const auto& v:matrix)if(!Finite4(v))return false;
        for(const auto& v:joint)if(!Finite4(v))return false;
        // AF1130 returns the already resolved transform; FF4180 copies its
        // opaque +1B0 attachment into our message and never dereferences it.
        std::uintptr_t attachment=0;if(!Read(body.transform+0x1b0,attachment))return false;
        partner_player::Gun1 gun{};
        if(!partner_player::ReadGun(addresses,s,k,gun)||!gun.rec)return false;
        body.gun=gun.rec;
        if(!Read(gun.rec+0x59e,body.gunTarget)||!Read(gun.rec+0x3d0,body.gunFlags))return false;
    }
    return blocks[0].t_vt==blocks[1].t_vt;
}
// ABI and arguments copied from BF6490 phase-component dispatch. Nothing
// accepts a Lua address/hash. No start/attach/transform request is exposed.
bool Invoke(std::uintptr_t phase,std::uint32_t id,detail::Message& message,std::int32_t& result) noexcept {
    if(!detail::MessageFresh(message)||id>1)return false;
    using Fn=void(*)(void*,std::uint32_t,std::int32_t*,void*);
    __try {
        reinterpret_cast<Fn>(addresses.image_base+c::receiver_rva)(reinterpret_cast<void*>(phase),id,&result,&message);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
detail::Scope Scope(std::uint32_t mission,std::uint16_t epoch,bool admitted,std::uint32_t thread,std::uint32_t seq,const downed_player::Snapshot& own) noexcept {
    detail::Scope s{};s.verified=verified;s.stopped=stopped;s.failed=failed;s.admitted=admitted;s.ready=own.ready;s.armed=own.armed;
    s.thread=GetCurrentThreadId();s.ownerThread=thread;s.mission=mission;s.ownedMission=own.mission;s.epoch=epoch;s.ownedEpoch=own.epoch;s.generation=own.generation;s.ownedGeneration=own.generation;
    s.now=GetTickCount64();s.poll=own.lastPoll;s.sequenceBefore=seq;s.sequenceAfter=partner_input_hook::WindowSeq();s.batchBefore=partner_defer::InBatch();s.batchAfter=partner_defer::InBatch();return s;
}
}
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept {
    const Preserve preserve;mismatch=0;const auto base=reinterpret_cast<std::uintptr_t>(image);
    if(!base||!span||base>std::numeric_limits<std::uintptr_t>::max()-span)return false;
    unsigned char buffer[8192]{};
    for(const auto& site:c::sites){
        if(site.size>sizeof(buffer)||site.rva>span||site.size>span-site.rva||!ReadBytes(base+site.rva,buffer,site.size)||std::memcmp(buffer,site.bytes,site.size)){mismatch=site.rva;return false;}
    }
    for(const auto& slot:c::slots){
        const auto at=slot.vtable_rva+slot.offset;std::uintptr_t target=0;
        if(at>span||sizeof(target)>span-at||!Read(base+at,target)||target!=base+slot.target_rva){mismatch=slot.vtable_rva;return false;}
    }
    auto reader=ReadBytes;
    return carry_plugin_read::CodeGate(reader,base,span,mismatch)&&carry_provider_read::CodeGate(reader,base,span,mismatch)&&carry_recipient_read::CodeGate(reader,base,span,mismatch)&&carry_request_retirement_read::CodeGate(reader,base,span,mismatch);
}
void Configure(const partner_player::Addresses& source,bool passed,void (*log)(const char*) noexcept) noexcept {
    const Preserve preserve;AcquireSRWLockExclusive(&lock);
    // A repeated configure cannot clear a fault or reopen a stopped module.
    if(!configured){addresses=source;verified=passed;logger=log;configured=true;}
    ReleaseSRWLockExclusive(&lock);
}
bool Observe(std::uint32_t mission,std::uint16_t epoch,bool admitted,std::uint32_t ownerThread,Reply& out) noexcept {
    const Preserve preserve;out={};
    if(!TryAcquireSRWLockExclusive(&lock)){out.reason="busy";return false;}
    const auto finish=[&](const char* why,bool ok=false) noexcept {if(!ok)out={};out.reason=why;out.ready=ok;ReleaseSRWLockExclusive(&lock);return ok;};
    const auto seq=partner_input_hook::WindowSeq();const auto own=downed_player::GetSnapshot();
    out.generation=own.generation;
    if(const char* why=detail::ScopeRefusal(Scope(mission,epoch,admitted,ownerThread,seq,own)))return finish(why);
    if(boundThread&&boundThread!=ownerThread)return finish("owner_changed");
    const auto now=GetTickCount64();if(lastQuery&&now>=lastQuery&&now-lastQuery<100)return finish("throttled");
    detail::Identity first{},second{},after{};
    if(!ReadIdentity(first)||!ReadIdentity(second))return finish("target_chain");
    if(!detail::SameIdentity(first,second))return finish("state_changed");
    if(const char* why=detail::ScopeRefusal(Scope(mission,epoch,admitted,ownerThread,seq,own)))return finish(why);
    namespace plugin=carry_plugin_read;
    std::uintptr_t lastAt=0,lastValue=0;std::size_t lastSize=0;bool lastRead=false;
    auto reader=[&](std::uintptr_t at,void* target,std::size_t size) noexcept {
        lastAt=at;lastValue=0;lastSize=size;lastRead=ReadBytes(at,target,size);
        if(lastRead&&size<=sizeof(lastValue))std::memcpy(&lastValue,target,size);
        return lastRead;
    };
    plugin::Scope ps{};
    ps.exactGate=verified;ps.admitted=admitted;ps.ready=own.ready;ps.armed=own.armed;
    ps.image=addresses.image_base;ps.player=second.player;ps.data=second.data;ps.state=second.bitset;
    ps.mission=mission;ps.ownedMission=own.mission;ps.epoch=epoch;ps.ownedEpoch=own.epoch;
    ps.generation=own.generation;ps.ownedGeneration=own.generation;
    ps.thread=GetCurrentThreadId();ps.ownerThread=ownerThread;
    ps.now=now;ps.poll=own.lastPoll;ps.sequenceBefore=seq;ps.sequenceAfter=partner_input_hook::WindowSeq();ps.batch=partner_defer::InBatch();
    carry_request_retirement_read::Observation requestBefore{},requestAfter{};
    const bool requestRead=carry_request_retirement_read::ReadPair(reader,ps,requestBefore);
    out.requestReason=requestRead?"request_observed":"request_chain";
    carry_recipient_read::Observation recipientBefore{},recipientAfter{};
    const bool recipientRead=carry_recipient_read::ReadPair(reader,ps,recipientBefore);
    out.recipientReason=recipientRead?"recipient_observed":"recipient_chain";
    carry_provider_read::Reply providerBefore{},providerAfter{};
    const auto imageSpan=addresses.image_end-addresses.image_base;
    const bool providerRead=carry_provider_read::ReadPair(reader,ps,imageSpan,providerBefore);
    out.providerReason=providerBefore.reason;
    if(!providerRead&&logger&&std::strcmp(providerBefore.reason,"provider_services")==0&&providerRefusals++<16){
        // Refusal telemetry only. Preserve the typed gate; never export a
        // heap pointer or accept the final type from this diagnostic value.
        const auto rva=lastRead&&lastSize==sizeof(std::uintptr_t)&&lastValue>=addresses.image_begin&&lastValue<addresses.image_end?lastValue-addresses.image_base:0;
        char line[192]{};StringCchPrintfA(line,ARRAYSIZE(line),"carry_provider_read_refused;stage=provider_services;vtable_rva=0x%llx;services_is_player=%u;read=%u\r\n",
            static_cast<unsigned long long>(rva),lastAt==second.player?1u:0u,lastRead?1u:0u);logger(line);
    }
    plugin::Observation pluginFirst{},pluginSecond{},pluginAfter{};
    const char* pluginRefusal="plugin_chain";
    const bool pluginRead=plugin::ReadOnce(reader,ps,pluginFirst,&pluginRefusal)&&plugin::ReadOnce(reader,ps,pluginSecond,&pluginRefusal);
    const bool pluginCoherent=pluginRead&&plugin::Same(pluginFirst,pluginSecond);
    out.pluginReason=pluginRead?(pluginCoherent?"ready":"plugin_changed"):pluginRefusal;
    if(!pluginRead&&logger&&readRefusals++<32){
        // Bounded first-failure telemetry: component-relative offset and an
        // image RVA only. Heap pointers never leave this native reader.
        const auto& seen=pluginSecond.player?pluginSecond:pluginFirst;
        const char* object="other";std::uintptr_t offset=0;
        const auto locate=[&](std::uintptr_t start,std::size_t length,const char* name) noexcept {
            if(start&&lastAt>=start&&lastAt-start<length){object=name;offset=lastAt-start;return true;}return false;
        };
        bool located=locate(seen.player,0x388,"player")||locate(seen.data,0x1f0,"data")||locate(seen.state,0x48,"state");
        for(unsigned k=0;k<2&&!located;++k){const auto& b=seen.body[k];
            located=locate(b.block,0x6a8,k?"block1":"block0")||locate(b.ps,0x410,k?"ps1":"ps0")||
                locate(b.node,0x40,k?"node1":"node0")||locate(b.primary,0x78,k?"primary1":"primary0")||
                locate(b.record,0x6c0,k?"record1":"record0");
        }
        if(!located)locate(seen.selector,0x3f30,"selector");
        const auto imageValue=lastValue>=addresses.image_begin&&lastValue<addresses.image_end?lastValue-addresses.image_base:0;
        char line[256]{};StringCchPrintfA(line,ARRAYSIZE(line),"carry_plugin_read_refused;stage=%s;object=%s;offset=0x%llx;size=%u;read=%u;image_value=0x%llx;zero=%u\r\n",
            pluginRefusal,object,static_cast<unsigned long long>(offset),unsigned(lastSize),lastRead?1u:0u,static_cast<unsigned long long>(imageValue),lastValue==0?1u:0u);
        logger(line);
        // Refusal-only type telemetry. An alternate scheduler link does not
        // weaken the reader or publish a controller as valid.
        if((std::strcmp(pluginRefusal,"carry_primary0")==0||std::strcmp(pluginRefusal,"carry_primary1")==0)&&lastRead&&lastSize==8){
            std::uintptr_t nodeVT=0,phase=0,afterVT=0;std::uint32_t count=0,base=0,row=0;
            const auto node=lastValue;
            if(plugin::Aligned(node,8)&&plugin::Range(node,0x40)&&Read(node,nodeVT)&&nodeVT==addresses.image_base+0x2306628&&
                Read(node+0x20,count)&&Read(node+0x24,base)&&Read(node+0x2c,row)&&Read(node+0x38,phase)&&Read(node,afterVT)&&afterVT==nodeVT){
                const unsigned k=std::strcmp(pluginRefusal,"carry_primary1")==0?1u:0u;
                const auto expected=seen.body[k].primary+0x30;
                const bool rowPhase=Read(seen.body[k].node+0x38,afterVT)&&afterVT==expected;
                StringCchPrintfA(line,ARRAYSIZE(line),"carry_bound_node_refused;body=%u;node_vt=0x2306628;count=%lu;base=%lu;row=0x%lx;phase_matches=%u;row33_phase_matches=%u\r\n",
                    k,static_cast<unsigned long>(count),static_cast<unsigned long>(base),static_cast<unsigned long>(row),phase==expected?1u:0u,rowPhase?1u:0u);logger(line);
            }
        }
    }
    BodyReply result[2]{};
    for(unsigned k=0;k<2;++k){
        detail::Message message{};std::int32_t nativeResult=0;
        if(partner_input_hook::WindowSeq()!=seq||partner_defer::InBatch())return finish("input_window");
        if(!Invoke(second.body[k].phase,second.ids[k],message,nativeResult)){failed=true;return finish("native_fault");}
        const auto status=detail::ValidateReply(message,nativeResult);
        if(status==detail::QueryResult::anomaly){failed=true;return finish("reply_anomaly");}
        auto& r=result[k];r.eligible=status==detail::QueryResult::eligible;r.rejected=status==detail::QueryResult::rejected;r.attachmentPresent=message.attachment!=0;
        r.radius=message.radius;r.height=message.height;std::memcpy(r.origin,message.origin,sizeof(r.origin));
        r.carryFlags=second.body[k].carryFlags;r.gunFlags=second.body[k].gunFlags;r.gunTarget=second.body[k].gunTarget;
        r.nativeCarryBit=detail::NativeCarryBit(r.carryFlags);r.gunTargetsPartner=detail::CarrierGunTargetsPartner(r.carryFlags,r.gunTarget,second.ids[1-k]);
    }
    const auto ownAfter=downed_player::GetSnapshot();
    if(ownAfter.mission!=own.mission||ownAfter.epoch!=own.epoch||ownAfter.generation!=own.generation||ownAfter.down!=own.down||ownAfter.cause!=own.cause)return finish("scope_changed");
    if(const char* why=detail::ScopeRefusal(Scope(mission,epoch,admitted,ownerThread,seq,ownAfter)))return finish(why);
    if(!ReadIdentity(after)||!detail::SameIdentity(second,after))return finish("state_changed");
    ps.now=GetTickCount64();ps.poll=ownAfter.lastPoll;ps.sequenceAfter=partner_input_hook::WindowSeq();ps.batch=partner_defer::InBatch();
    if(pluginCoherent&&!plugin::ScopeRefusal(ps)){
        if(plugin::ReadOnce(reader,ps,pluginAfter)&&plugin::Same(pluginSecond,pluginAfter)){
            out.pluginReady=true;out.pluginReason="ready";
            out.selectorOwnerMatches=pluginAfter.selectorOwner==pluginAfter.body[0].primary;
            out.selectorAccepts=plugin::SelectorAccepts(pluginAfter);out.selectorTarget=pluginAfter.selectorTarget;
            out.playerCarryEnabled=(pluginAfter.playerFlags&0x400u)!=0;
            out.cachedKind=pluginAfter.cachedKind;out.currentKind=pluginAfter.currentKind;
            out.cachedFlags=pluginAfter.cachedFlags;out.currentFlags=pluginAfter.currentFlags;
            for(unsigned k=0;k<2;++k){const auto& x=pluginAfter.body[k];auto& y=result[k];
                y.carryTarget=x.target;y.carryPrevious=x.previous;y.carrier=x.carrier;y.carryNodeActive=x.nodeActive;
                std::memcpy(y.carryState,x.state,sizeof(y.carryState));std::memcpy(y.pluginFlags,x.flags,sizeof(y.pluginFlags));
            }
        }else out.pluginReason="plugin_changed";
    }
    if(providerRead&&!plugin::ScopeRefusal(ps)&&carry_provider_read::ReadPair(reader,ps,imageSpan,providerAfter)){
        if(providerBefore.ready==providerAfter.ready&&providerBefore.present==providerAfter.present&&providerBefore.vtableRva==providerAfter.vtableRva){
            out.providerReady=true;out.providerPresent=providerAfter.present;out.providerVtableRva=providerAfter.vtableRva;out.providerReason=providerAfter.reason;
        }else out.providerReason="provider_changed";
    }else if(providerRead)out.providerReason=providerAfter.reason;
    if(recipientRead&&!plugin::ScopeRefusal(ps)&&carry_recipient_read::ReadPair(reader,ps,recipientAfter)){
        bool same=plugin::Same(recipientBefore.carrier,recipientAfter.carrier);
        for(unsigned k=0;k<2;++k)same=same&&carry_recipient_read::SameRecipient(recipientBefore.recipient[k],recipientAfter.recipient[k]);
        if(same){out.recipientReady=true;out.recipientReason="recipient_observed";
            for(unsigned k=0;k<2;++k){const auto& x=recipientAfter.recipient[k];auto& y=result[k];
                y.recipientActive=x.active;y.recipientEndPending=x.endPending;y.recipientCarrier=x.carrier;
                y.recipientCurrent=x.current;y.recipientNext=x.next;y.reciprocalBinding=carry_recipient_read::CoherentBinding(recipientAfter,k,1-k);
            }
        }else out.recipientReason="recipient_changed";
    }else if(recipientRead)out.recipientReason="recipient_changed";
    if(requestRead&&!plugin::ScopeRefusal(ps)&&carry_request_retirement_read::ReadPair(reader,ps,requestAfter)) {
        if(carry_request_retirement_read::Same(requestBefore,requestAfter)) {
            out.requestReady=true;out.requestReason="request_observed";
            for(unsigned k=0;k<2;++k){result[k].requestKind=requestAfter.packet[k].kind;
                result[k].requestSequence=requestAfter.packet[k].sequence;result[k].requestForce=requestAfter.packet[k].force;}
        } else out.requestReason="request_changed";
    } else if(requestRead)out.requestReason="request_changed";
    // Query receiver writes our stack only. Re-sample the owner and input
    // window after the final telemetry read too; no plugin value unlocks input.
    const auto finalOwn=downed_player::GetSnapshot();
    if(finalOwn.mission!=own.mission||finalOwn.epoch!=own.epoch||finalOwn.generation!=own.generation||finalOwn.down!=own.down||finalOwn.cause!=own.cause)return finish("scope_changed");
    if(const char* why=detail::ScopeRefusal(Scope(mission,epoch,admitted,ownerThread,seq,finalOwn)))return finish(why);
    boundThread=ownerThread;lastQuery=now;out.bodies[0]=result[0];out.bodies[1]=result[1];
    return finish("query_observed",true);
}
void Shutdown() noexcept {
    const Preserve preserve;AcquireSRWLockExclusive(&lock);stopped=true;ReleaseSRWLockExclusive(&lock);
}
} // namespace mgscoop::carry_info


