#pragma once
#include <charconv>
#include <cstring>
#include "mgscoop/sleep_partner_test.hpp"
#include "mgscoop/partner_input.hpp"
#include "mgscoop/lab_config.hpp"
namespace mgscoop::sleep_partner_bridge {
// The offline world uses configured host/guest, while session diagnostics can be none.
inline constexpr std::uint32_t ConfiguredRole(LabRole role) noexcept {
 return role==LabRole::host?1u:role==LabRole::guest?2u:0u;
}
inline constexpr char kPrefix[]="return 'MGS_COOP_PARTNER_SLEEP_V1' -- ";
inline constexpr char kCapability[]="return 'MGS_COOP_PARTNER_SLEEP_CAP_V1'";
struct Record {bool clear=false;std::uint32_t mission=0,role=0,sequence=0,generation=0,down=0,cause=0,ready=0,age=0;std::uint16_t epoch=0;};
inline bool Parse(const char* source,std::size_t size,Record& out) noexcept {
 out={};if(!source||size<=sizeof(kPrefix)-1||size>192||std::memcmp(source,kPrefix,sizeof(kPrefix)-1))return false;
 const char* cursor=source+sizeof(kPrefix)-1;const char* end=source+size;std::uint32_t v[9]{};
 for(unsigned i=0;i<9;++i){auto parsed=std::from_chars(cursor,end,v[i]);if(parsed.ec!=std::errc()||parsed.ptr==cursor)return false;cursor=parsed.ptr;if(i!=8){if(cursor==end||*cursor!=' ')return false;++cursor;}}
 if(cursor!=end)return false;bool allZero=true;for(auto n:v)allZero=allZero&&!n;
 if(allZero){out.clear=true;return true;}
 if((v[0]!=30010&&v[0]!=30020&&v[0]!=30050)||!v[1]||v[1]>65535||(v[2]!=1&&v[2]!=2)||!v[3]||v[3]>65535||v[5]>1||v[6]>2||v[7]!=1||v[8]>500)return false;
 if((v[5]&&(!v[4]||!v[6]))||(!v[5]&&v[6]))return false;
 out={false,v[0],v[2],v[3],v[4],v[5],v[6],v[7],v[8],static_cast<std::uint16_t>(v[1])};return true;
}
struct ArmOwner {bool ready=false;std::uintptr_t player=0,go=0,block=0,ps=0,pad=0,source=0;std::uint32_t generation=0,epoch=0,thread=0;std::uint64_t at=0;};
inline ArmOwner CaptureArm(const partner_input::Arm& a,bool naturalOwner,std::uint32_t thread,std::uint64_t now) noexcept {
 ArmOwner out{};if(!naturalOwner||!thread||!now||!a.enabled||!a.native||!a.gen||!a.rates_epoch||a.holdlog||a.cam!=PartnerInputCam::sandbox||!a.P||a.go+0x20!=a.P||!a.blk1||!a.ps1||!a.pp||!a.src||!a.root0||!a.root1||a.pool1>=16||a.ps1_idx>1)return out;
 out={true,a.P,a.go,a.blk1,a.ps1,a.pp,a.src,a.gen,a.rates_epoch,thread,now};return out;
}
struct Context {bool worker=false,admitted=false,naturalOwner=false,fieldReady=false,demo=false,stopping=false;std::uint32_t mission=0,role=0,thread=0;std::uint16_t epoch=0;std::uint64_t now=0;};
inline bool Current(const Context& c,const ArmOwner& a,const Record& r) noexcept {
 return c.worker&&c.admitted&&c.naturalOwner&&c.fieldReady&&!c.demo&&!c.stopping&&c.now&&a.ready&&
 c.thread&&a.thread==c.thread&&a.at&&c.now>=a.at&&c.now-a.at<=250&&c.mission==r.mission&&c.epoch==r.epoch&&c.role==r.role&&c.now>=r.age;
}
inline bool Newer(std::uint32_t n,std::uint32_t old,std::uint64_t modulus) noexcept {const auto delta=(std::uint64_t(n)+modulus-old)%modulus;return delta&&delta<=modulus/2;}
struct Ledger {bool have=false;Record record{};std::uint64_t acceptedAt=0;};
struct Plan {bool ready=false,clear=false;Ledger next{};sleep_partner_test::Target target{};const char* reason="peer_sleep_refused";};
inline Plan Accept(const Ledger& old,const Record& r,const Context& c,const ArmOwner& arm) noexcept {
 Plan p{};p.next=old;
 if(r.clear){p.clear=true;p.next={};p.reason="peer_sleep_clear";return p;}
 if(!Current(c,arm,r)){p.clear=true;p.next={};p.reason="peer_sleep_scope";return p;}
 const bool sameScene=old.have&&old.record.mission==r.mission&&old.record.epoch==r.epoch&&old.record.role==r.role;
 auto at=c.now-r.age;
 if(sameScene){const auto& o=old.record;
  if(r.sequence==o.sequence){
   if(r.generation!=o.generation||r.down!=o.down||r.cause!=o.cause||r.ready!=o.ready||r.age<o.age){p.reason="peer_sleep_duplicate_changed";return p;}
   // A repeated Lua poll cannot make one old received packet fresh again.
   if(old.acceptedAt<at)at=old.acceptedAt;
  }else{
   if(!Newer(r.sequence,o.sequence,65535)||(r.generation!=o.generation&&!Newer(r.generation,o.generation,0x100000000ull))||
      (r.down&&!o.down&&r.generation==o.generation)||(r.down&&o.down&&r.generation==o.generation&&r.cause!=o.cause)||
      (o.down&&!r.down&&r.generation!=o.generation)){p.reason="peer_sleep_record_order";return p;}
  }
 }
 if(!at||c.now-at>250){p.clear=true;p.next={};p.reason="peer_sleep_expired";return p;}
 p.next={true,r,at};p.target={true,true,false,false,r.down&&r.cause==2,r.mission,r.generation,arm.generation,arm.epoch,r.epoch,at};
 p.target.peerSequence=r.sequence;p.target.localRole=r.role;p.target.peerCause=r.cause;p.target.peerDown=r.down!=0;p.target.peerReady=r.ready==1;
 p.ready=true;p.reason="peer_sleep_accepted";return p;
}
}
