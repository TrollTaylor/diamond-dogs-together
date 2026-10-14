#pragma once
// Read-only actualAF2F90 callback-frame observer. This does NOT qualify a job
// lease, strong reference, epoch, source selection, or native engine call.
#include "qualified_sleep_lookup_observer.hpp"
#include "native_motion_frame_contract.hpp"
namespace mgscoop::native_motion_frame_observer {
namespace q=qualified_sleep_lookup_observer;
struct Event {std::uintptr_t cs=0,payload=0,context=0,caller=0,actorArgument=0;std::uint32_t thread=0;};
struct Frame {
 Event event{};q::Owner owner{};std::uintptr_t rowArgument=0,engineArgument=0;
 std::uint64_t payloadId=0,payloadSecondary=0;std::uint32_t payload10=0,payload14=0,payload18=0,payload1c=0;std::uint16_t payload20=0,payload22=0;
 bool typedCurrent=false,actorArgumentMatches=false;
 // These cannot be set by this observer or inferred from its typedCurrent.
 bool originalTaskRetentionProven=false,nativeJobLeaseGranted=false;
 const char* reason="unobserved";
};
inline bool Compiled() noexcept {
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER)
 return true;
#else
 return false;
#endif
}
template<class Reader>bool CodeGate(Reader& r,std::uintptr_t image) noexcept {
 unsigned char bytes[1024]{};for(const auto& p:native_motion_frame_contract::pins){if(p.size>sizeof(bytes)||!r(image+p.rva,bytes,p.size)||std::memcmp(bytes,p.data,p.size))return false;}return true;
}
template<class Reader,class PhaseReader>bool Read(Reader& r,PhaseReader& phase,const q::Gate& gate,const Event& event,Frame& out) noexcept {
 out={};out.event=event;
 if(!Compiled()||!q::Admitted(gate)||!event.thread||!q::Aligned(event.cs,8)||!q::Aligned(event.context,8)||!q::Aligned(event.payload,8)||event.caller<gate.image||event.caller-gate.image>=0xa01d000){out.reason="frame_observer_scope";return false;}
 out.reason="native_callback_context";
 if(!q::Get(r,event.context+0x28,out.rowArgument)||!q::Get(r,event.context+0x30,out.engineArgument)||!q::Aligned(out.rowArgument,16)||!q::Aligned(out.engineArgument,8))return false;
 q::Event raw{};raw.path=q::Path::raw;raw.engine=out.engineArgument;
 q::Owner first{},second{};
 if(!q::OwnerOnce(r,gate,raw,first)||!q::OwnerOnce(r,gate,raw,second)||!q::SameOwner(first,second)){out.reason="cold_callback_owner_unresolved";return false;}
 if(second.cs!=event.cs||second.rows!=out.rowArgument||second.engine!=out.engineArgument){out.reason="native_callback_row_engine_mismatch";return false;}
 out.owner=second;phase(r,gate.image,out.owner,out.owner.phase);out.actorArgumentMatches=event.actorArgument==second.body;
 // Copy exactly the fields ORIGINALAF2F90 reads to build its native spec.
 // No label or interpreted MGO semantics is assigned to the opaque fields.
 if(!q::Get(r,event.payload,out.payloadId)||!q::Get(r,event.payload+8,out.payloadSecondary)||!q::Get(r,event.payload+0x10,out.payload10)||
  !q::Get(r,event.payload+0x14,out.payload14)||!q::Get(r,event.payload+0x18,out.payload18)||
  !q::Get(r,event.payload+0x1c,out.payload1c)||!q::Get(r,event.payload+0x20,out.payload20)||!q::Get(r,event.payload+0x22,out.payload22)){out.reason="native_callback_payload_unreadable";return false;}
 out.typedCurrent=true;out.reason="cold_native_callback_observed_retention_unknown";return true;
}
inline bool Stable(const Frame& a,const Frame& z) noexcept {
 return a.typedCurrent&&z.typedCurrent&&q::SameOwner(a.owner,z.owner)&&a.rowArgument==z.rowArgument&&a.engineArgument==z.engineArgument&&
 a.payloadId==z.payloadId&&a.payloadSecondary==z.payloadSecondary&&a.payload10==z.payload10&&a.payload14==z.payload14&&a.payload18==z.payload18&&a.payload1c==z.payload1c&&a.payload20==z.payload20&&a.payload22==z.payload22;
}
struct Receipt {Frame before{},after{};bool stable=false;std::uintptr_t originalResult=0;};
template<class Original,class Reader,class PhaseReader,class Report>std::uintptr_t Forward(Original original,Reader& read,PhaseReader& phase,Report& report,const q::Gate& gate,const Event& event) {
 Receipt receipt{};{q::ErrorPreserve error{};Read(read,phase,gate,event,receipt.before);}
 // Full originalRAX is forwarded without interpreting it as bool. AF2F90's
 // final GS check is not an authored selection/success result.
 receipt.originalResult=original(event.cs,event.actorArgument,event.payload,event.context);
 {q::ErrorPreserve error{};Read(read,phase,gate,event,receipt.after);receipt.stable=Stable(receipt.before,receipt.after);report(receipt);}
 return receipt.originalResult;
}
} // namespace mgscoop::native_motion_frame_observer
