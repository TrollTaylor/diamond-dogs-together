#pragma once
#include "standing_selection_policy.hpp"
#include "sleep_standing_cycle_asset.hpp"
namespace mgscoop::standing_selection_bank_read {
namespace p=standing_selection_policy;namespace a=sleep_standing_cycle_asset;namespace sp=sleep_plugin_read;
// Read only. expectedFileIdentity must be the root's independently captured
// exact unique4 FilePtr identity/path/registration receipt, never copied from
// whichever bound bank this scan happens to find. It is NOT an owner grant.
template<class Reader,class Hasher>bool Read(Reader& r,Hasher& hash,const p::Owner& owner,
 std::uint64_t expectedFileIdentity,unsigned expectedCommonSlot,p::Bank& out) noexcept {
 out={};if(expectedFileIdentity!=p::kFileIdentity||expectedCommonSlot!=p::kCommonSlot||!p::Valid(owner))return false;
 std::uint32_t completedCommon=0;if(!sp::Get(r,owner.record+0x680,completedCommon)||!(completedCommon&(1u<<owner.body)))return false;
 const auto commonEntry=owner.image+0x2a7f388+std::uintptr_t(expectedCommonSlot)*0x28;std::uintptr_t registeredBank=0;
 if(!sp::Get(r,commonEntry,registeredBank)||!sp::Aligned(registeredBank,8))return false;
 // Independently captured common slot17 and actual engine slots2/18 are
 // DIFFERENT tables. Require the registered pointer itself, not slot parity.
 for(unsigned slot=16;slot<32;++slot){std::uintptr_t item=0;if(!sp::Get(r,owner.image+0x2a7f388+std::uintptr_t(slot)*0x28,item))return false;
  if(slot!=expectedCommonSlot&&item==registeredBank)return false;}
 std::uintptr_t vt=0,data=0,banks=0;std::uint32_t capacity=0;
 if(!sp::Get(r,owner.engine,vt)||vt!=owner.image+0x24c1988||!sp::Get(r,owner.engine+0x10,data)||data!=owner.engineData||
  !sp::Get(r,owner.engine+0x1c,capacity)||capacity!=owner.capacity||!capacity||capacity>64||
  !sp::Get(r,owner.engine+0x98,banks)||banks!=owner.banks||!sp::Aligned(banks,8)||!sp::Range(banks,std::size_t(capacity)*0x30))return false;
 unsigned matches=0;
 for(unsigned slot=0;slot<capacity;++slot){std::uintptr_t bank=0;std::uint64_t identity=0;
  if(!sp::Get(r,banks+std::uintptr_t(slot)*0x30+0x18,bank))return false;if(!bank)continue;
  if(!sp::Get(r,bank,vt)||vt!=owner.image+0x24c2890||!sp::Get(r,bank+0x30,identity))return false;if(identity!=expectedFileIdentity||bank!=registeredBank)continue;
  p::Bank candidate{};candidate.bank=bank;candidate.slot=slot;candidate.fileIdentity=identity;candidate.commonEntry=commonEntry;candidate.commonSlot=expectedCommonSlot;
  // ExactWHOLE13824B hash includes original912B/56-channel master and every
  // main/end payload. VerifyBank additionally reads ALL four native source
  // descriptors and checks bank owner, full64 ID, kind, duration and index.
  if(!a::VerifyBank(r,hash,owner.image,bank,candidate.blob,candidate.sources,identity)||identity!=expectedFileIdentity)return false;
  candidate.master=candidate.blob+0xa0;candidate.count=4;candidate.verifiedEntries=15;
  for(unsigned i=0;i<4;++i)candidate.descriptors[i]=candidate.sources+std::uintptr_t(i)*40;
  if(!p::Valid(candidate,owner)||++matches!=1)return false;out=candidate;
 }
 return matches==1;
}
} // namespace mgscoop::standing_selection_bank_read
