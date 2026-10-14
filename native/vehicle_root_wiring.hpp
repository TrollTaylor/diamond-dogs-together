#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "vehicle_mirror_state.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "mgscoop/partner_player.hpp"
namespace mgscoop::vehicle_root_wiring {
namespace vm=vehicle_mirror_state;
inline constexpr char query[]="return 'MGS_COOP_VEHICLE_NAMES_QUERY_V1'";
inline constexpr char publish[]="return 'MGS_COOP_VEHICLE_NAMES_V1' -- ";
inline constexpr std::size_t names=19;
inline bool Age(std::uint64_t now,std::uint64_t at,std::uint64_t max) noexcept{return at&&now>=at&&now-at<=max;}
struct Identity {
 std::uintptr_t image=0,player=0,data=0,state=0,block[2]{},cs[2]{},entry[2]{};
 std::uint64_t birth[2]{},lifecycle=0;
 std::uint32_t generation=0,sceneEpoch=0,thread=0,mission=0;
 std::uint16_t epoch=0;
};
inline bool Valid(const Identity& a) noexcept {
 return a.image&&a.player&&a.data&&a.state&&a.block[0]&&a.block[1]&&a.cs[0]&&a.cs[1]&&a.entry[0]&&a.entry[1]&&
 a.birth[0]&&a.birth[1]&&a.lifecycle&&a.generation&&a.sceneEpoch&&a.thread&&a.epoch&&
 (a.mission==30010||a.mission==30020||a.mission==30050);
}
inline bool Same(const Identity& a,const Identity& b) noexcept {
 if(!Valid(a)||!Valid(b)||a.image!=b.image||a.player!=b.player||a.data!=b.data||a.state!=b.state||a.lifecycle!=b.lifecycle||
 a.generation!=b.generation||a.sceneEpoch!=b.sceneEpoch||a.thread!=b.thread||a.mission!=b.mission||a.epoch!=b.epoch)return false;
 for(unsigned k=0;k<2;++k)if(a.block[k]!=b.block[k]||a.cs[k]!=b.cs[k]||a.entry[k]!=b.entry[k]||a.birth[k]!=b.birth[k])return false;
 return true;
}
inline Identity FromBirth(const partner_input_hook::NativePoseBirthCensus& a,std::uint32_t mission,std::uint16_t epoch) noexcept {
 Identity b{};b.image=a.image;b.player=a.player;b.data=a.data;b.state=a.state;b.lifecycle=a.lifecycle;
 b.generation=a.generation;b.sceneEpoch=a.sceneEpoch;b.thread=a.thread;b.mission=mission;b.epoch=epoch;
 for(unsigned k=0;k<2;++k){b.block[k]=a.block[k];b.cs[k]=a.cs[k];b.entry[k]=a.entry[k];b.birth[k]=a.birth[k];}return b;
}
struct Cache {
 Identity owner{},pendingOwner{};std::uint16_t ids[names]{};
 std::uint32_t serial=0,pending=0;std::uint64_t requested=0,published=0;
};
inline std::uint32_t Begin(Cache& c,const Identity& owner,std::uint64_t now) noexcept {
 if(!Valid(owner)||!now||c.serial==UINT32_MAX)return 0;
 c.pending=++c.serial;c.pendingOwner=owner;c.requested=now;return c.pending;
}
inline bool Accept(Cache& c,const Identity& owner,std::uint32_t nonce,const std::uint16_t(&ids)[names],std::uint64_t now) noexcept {
 const bool pending=nonce&&nonce==c.pending;c.pending=0; // every owned publication consumes the challenge
 if(!pending||!Same(owner,c.pendingOwner)||!Age(now,c.requested,500))return false;
 for(std::size_t k=0;k<names;++k)if(ids[k]!=0xffff)for(std::size_t j=0;j<k;++j)if(ids[k]==ids[j])return false;
 c.owner=owner;std::memcpy(c.ids,ids,sizeof(c.ids));c.published=now;return true;
}
inline bool Index(vm::Name n,std::size_t& at) noexcept {
 if(n.kind==vm::NameKind::light&&n.index<13){at=n.index;return true;}
 if(n.kind==vm::NameKind::truck&&n.index<6){at=13+n.index;return true;}return false;
}
inline bool Resolve(const Cache& c,const Identity& current,vm::Name name,std::uint64_t now,std::uint16_t& id) noexcept {
 id=0xffff;std::size_t at=0;if(!Same(c.owner,current)||!Age(now,c.published,750)||!Index(name,at)||c.ids[at]==0xffff)return false;
 id=c.ids[at];return true;
}
// A current owned selected1 receipt is an advisory name-cache scope only.
// It grants no native call; mirror rereads the full unit, both models and core lease.
inline bool OwnedCache(const Cache& c,const partner_input_hook::NativePoseJobContext& x,std::uint32_t mission,
                       std::uint16_t epoch,std::uint64_t now) noexcept {
 return Valid(c.owner)&&Age(now,c.published,750)&&x.modelReady&&x.selectedBodyOwned&&x.selectedBody==1&&(x.sequence&1u)&&
 x.player==c.owner.player&&x.block==c.owner.block[1]&&x.modelCS==c.owner.cs[1]&&x.modelEntry==c.owner.entry[1]&&
 x.birthSerial==c.owner.birth[1]&&x.generation==c.owner.generation&&x.sceneEpoch==c.owner.sceneEpoch&&x.thread==c.owner.thread&&
 mission==c.owner.mission&&epoch==c.owner.epoch;
}
inline bool Number(const char*& p,const char* end,std::uint32_t max,std::uint32_t& v) noexcept {
 v=0;if(p==end||*p<'0'||*p>'9')return false;
 do{const auto digit=static_cast<unsigned>(*p-'0');if(v>(max-digit)/10)return false;v=v*10+digit;++p;}
 while(p<end&&*p>='0'&&*p<='9');return v<=max;
}
inline bool Parse(const char* text,std::size_t size,std::uint32_t& nonce,std::uint16_t(&ids)[names]) noexcept {
 nonce=0;if(!text||size<sizeof(publish)||size>256||std::memcmp(text,publish,sizeof(publish)-1))return false;
 const char* p=text+sizeof(publish)-1;const char* end=text+size;if(!Number(p,end,UINT32_MAX,nonce)||!nonce)return false;
 for(auto& id:ids){std::uint32_t value=0;if(p==end||*p++!=' '||!Number(p,end,65535,value))return false;id=static_cast<std::uint16_t>(value);}return p==end;
}
inline bool SameOrigin(const partner_net::ReliableOrigin& a,const partner_net::ReliableOrigin& b) noexcept {
 return a.incarnation&&a.incarnation==b.incarnation&&a.session&&a.session==b.session&&a.localRole==b.localRole&&a.peerRole==b.peerRole&&
 a.peer.address==b.peer.address&&a.peer.port==b.peer.port;
}
inline bool Link(const partner_net::ReliableOrigin& a,std::uint32_t mission,std::uint16_t epoch,vm::Link& out) noexcept {
 out={};out.serial=a.incarnation;out.mission=mission;out.epoch=epoch;out.localRole=a.localRole;out.peerRole=a.peerRole;
 return a.session&&vm::Valid(out);
}
// Production must refuse until sender capture-to-enqueue age is proven.
inline constexpr bool productionActivation=false;
}
