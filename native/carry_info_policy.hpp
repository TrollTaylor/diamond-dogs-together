#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
namespace mgscoop::carry_info::detail {
// This is the stock 104B1F0 query message, not a carry-start request.
inline constexpr std::uint64_t kQueryHash=0x873ac63ac9beull;
struct alignas(16) Message {
    std::uint64_t hash=kQueryHash, reserved=0;
    float origin[4]{};
    std::uintptr_t attachment=0;
    float radius=0, height=0;
    std::uint8_t handled=0, padding[15]{};
};
static_assert(sizeof(std::uintptr_t)==8 && sizeof(Message)==0x40 && alignof(Message)==16);
static_assert(offsetof(Message,origin)==0x10 && offsetof(Message,attachment)==0x20);
static_assert(offsetof(Message,radius)==0x28 && offsetof(Message,height)==0x2c && offsetof(Message,handled)==0x30);
struct Scope {
    bool verified=false,stopped=false,failed=false,admitted=false,ready=false,armed=false;
    bool batchBefore=false,batchAfter=false;
    std::uint32_t thread=0,ownerThread=0,mission=0,ownedMission=0,generation=0,ownedGeneration=0;
    std::uint16_t epoch=0,ownedEpoch=0;
    std::uint32_t sequenceBefore=0,sequenceAfter=0;
    std::uint64_t now=0,poll=0;
};
inline const char* ScopeRefusal(const Scope& s) noexcept {
    if(!s.verified||s.stopped)return "disabled";
    if(s.failed)return "fault_latched";
    if(!s.admitted||!s.ready||!s.armed)return "scope";
    if(!s.ownerThread||s.thread!=s.ownerThread)return "owner_thread";
    if((s.mission!=30010&&s.mission!=30020&&s.mission!=30050)||!s.epoch||!s.generation||s.mission!=s.ownedMission||s.epoch!=s.ownedEpoch||s.generation!=s.ownedGeneration)return "scene";
    if(!s.poll||s.now<s.poll||s.now-s.poll>750)return "scope_expired";
    if(s.batchBefore||s.batchAfter)return "batch";
    if((s.sequenceBefore&1u)||s.sequenceBefore!=s.sequenceAfter)return "input_window";
    return nullptr;
}
struct Identity {
    std::uintptr_t player=0,data=0,bitset=0,exists=0,locals=0,slots=0;
    std::uint32_t count=0,local=0,realized=0;
    std::uint16_t ids[2]{};
    std::uint8_t pool[2]{},parts[2]{};
    struct Body {
        std::uintptr_t block=0,phase=0,cs=0,transform=0,psRecord=0,life=0,flags=0;
        std::uintptr_t model=0,joints=0,gun=0;
        std::uint16_t carryTarget=0xffff;
        std::uint32_t actionBits[8]{};
        std::uint32_t carryFlags=0,gunFlags=0,qFlags=0; float lifeValue=0,maximum=0;
    } body[2];
};
inline bool SameIdentity(const Identity& a,const Identity& b) noexcept {
    if(a.player!=b.player||a.data!=b.data||a.bitset!=b.bitset||a.exists!=b.exists||a.locals!=b.locals||a.slots!=b.slots||a.count!=b.count||a.local!=b.local||a.realized!=b.realized)return false;
    for(unsigned k=0;k<2;++k){
        if(a.ids[k]!=b.ids[k]||a.pool[k]!=b.pool[k]||a.parts[k]!=b.parts[k])return false;
        const auto& x=a.body[k];const auto& y=b.body[k];
        if(x.block!=y.block||x.phase!=y.phase||x.cs!=y.cs||x.transform!=y.transform||x.psRecord!=y.psRecord||x.life!=y.life||x.flags!=y.flags||x.model!=y.model||x.joints!=y.joints||x.gun!=y.gun||x.qFlags!=y.qFlags||x.lifeValue!=y.lifeValue||x.maximum!=y.maximum||x.carryTarget!=y.carryTarget||x.gunFlags!=y.gunFlags)return false; for(unsigned j=0;j<8;++j)if(x.actionBits[j]!=y.actionBits[j])return false;
    }
    return a.player&&a.count==2&&a.local==0&&a.ids[0]==0&&a.ids[1]==1;
}
inline bool MessageFresh(const Message& m) noexcept {
    if(m.hash!=kQueryHash||m.reserved||m.attachment||m.radius||m.height||m.handled)return false;
    for(float f:m.origin)if(f!=0)return false;
    for(auto b:m.padding)if(b)return false;
    return true;
}
enum class QueryResult : std::uint8_t { eligible,rejected,anomaly };
inline QueryResult ValidateReply(const Message& m,std::int32_t result) noexcept {
    if(m.hash!=kQueryHash||m.reserved)return QueryResult::anomaly;
    for(auto b:m.padding)if(b)return QueryResult::anomaly;
    if(result==-1&&!m.handled&&!m.attachment&&!m.radius&&!m.height){
        for(float f:m.origin)if(f!=0)return QueryResult::anomaly;
        return QueryResult::rejected;
    }
    if(result!=0||m.handled!=1||m.radius!=.45f||m.height!=.8f)return QueryResult::anomaly;
    for(float f:m.origin)if(!std::isfinite(f))return QueryResult::anomaly;
    // An opaque attachment may legitimately be null. No dereference/export.
    return QueryResult::eligible;
}
inline bool NativeCarryBit(std::uint32_t flags) noexcept { return (flags&(1u<<22))!=0; }
// Distinguish the stock carrier flag from an actually proven target attachment.
inline bool CarrierTargetsPartner(std::uint32_t flags,std::uint16_t target,std::uint16_t partner) noexcept {
    return NativeCarryBit(flags)&&target==partner&&partner!=0xffff;
}
} // namespace mgscoop::carry_info::detail


