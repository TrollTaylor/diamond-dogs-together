#include "whole_player_lifetime_checks.hpp"
#include <cstring>
#include <iostream>
using namespace mgscoop;
struct Reader {
 std::uintptr_t image=0x140000000ull,corrupt=0;bool unreadable=false,wrongSlot=false;
 bool operator()(std::uintptr_t p,void* out,std::size_t n)noexcept{
  if(unreadable)return false;const auto off=p-image;
  for(const auto& s:whole_player_lifetime_contract::sites)if(off>=s.rva&&off-s.rva<=s.size&&n<=s.size-(off-s.rva)){
   std::memcpy(out,s.bytes+off-s.rva,n);if(corrupt>=off&&corrupt-off<n)static_cast<unsigned char*>(out)[corrupt-off]^=1;return true;
  }
  for(const auto& s:whole_player_lifetime_contract::slots)if(off==s.rva&&n==8){auto v=image+s.target+(wrongSlot?1:0);std::memcpy(out,&v,n);return true;}return false;
 }
};
int main(){unsigned checks=0,fail=0;auto C=[&](bool b,const char* s){++checks;if(!b){++fail;std::cerr<<s<<"\n";}};Reader r;std::uintptr_t mismatch=0;
 C(whole_player_lifetime_checks::CodeGate(r,r.image,0xa01d000,mismatch),"full actual expected code/slots");
 for(const auto& s:whole_player_lifetime_contract::sites){r.corrupt=s.rva+s.size-1;C(!whole_player_lifetime_checks::CodeGate(r,r.image,0xa01d000,mismatch)&&mismatch==s.rva,"late-byte corruption refuses whole function");}r.corrupt=0;
 r.wrongSlot=true;C(!whole_player_lifetime_checks::CodeGate(r,r.image,0xa01d000,mismatch),"wrong deleting/reset slot");r.wrongSlot=false;
 r.unreadable=true;C(!whole_player_lifetime_checks::CodeGate(r,r.image,0xa01d000,mismatch),"unreadable image");r.unreadable=false;
 C(!whole_player_lifetime_checks::CodeGate(r,0,0xa01d000,mismatch),"zero image");C(!whole_player_lifetime_checks::CodeGate(r,UINTPTR_MAX-3,8,mismatch),"overflow image");
 C(!whole_player_lifetime_checks::CodeGate(r,r.image,0x2295488+7,mismatch),"truncated final deleting slot");
 C(whole_player_lifetime_contract::slots[0].target==0x9bb7a0,"deleting target correct");
 for(const auto& s:whole_player_lifetime_contract::sites)C(s.rva!=0x9bd7a0,"initialization is not mislabeled deleting");
 native_pose_birth::Completion completion{};completion.identity={0x1020,0x2000,0x3000,0x4000,0x5000};
 completion.installedAtEntry=completion.installedAtExit=completion.workerAtEntry=completion.workerAtExit=true;
 completion.exactCaller=completion.normalReturn=completion.typedPair=true;completion.entryThread=completion.exitThread=7;
 std::uint64_t epoch=1;completion.lifecycleEpoch=epoch;native_pose_birth::Receipt empty{};
 whole_player_lifetime_checks::InvalidateWhole(empty,epoch,0x1000);
 C(epoch==2&&!empty.ready,"empty whole reset advances publication nonce");
 C(!native_pose_birth::Complete(completion,1,epoch).ready,"inflight constructor cannot publish after whole reset");
 completion.lifecycleEpoch=epoch;auto matching=native_pose_birth::Complete(completion,2,epoch);
 C(matching.ready,"fresh constructor publishes after new epoch");
 whole_player_lifetime_checks::InvalidateWhole(matching,epoch,0x1000);
 C(!matching.ready&&epoch==3,"matching whole lifecycle clears completed receipt");
 completion.lifecycleEpoch=epoch;auto other=native_pose_birth::Complete(completion,3,epoch);
 whole_player_lifetime_checks::InvalidateWhole(other,epoch,0x6000);
 C(other.ready&&epoch==4,"unrelated whole lifecycle retains receipt but invalidates inflight epoch");
 C(!native_pose_birth::Complete(completion,4,epoch).ready,"unrelated concurrent lifecycle also refuses stale publication");
 epoch=UINT64_MAX;whole_player_lifetime_checks::InvalidateWhole(empty,epoch,0);
 C(epoch==1,"epoch wrap skips zero even with null notification");
 std::cout<<checks<<" checks; "<<fail<<" failures\n";return fail?1:0;
}
