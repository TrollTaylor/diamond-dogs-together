#include "dual_parts_fixture.hpp"
#include "dual_parts_cold_profile.hpp"
#include <cstdio>
namespace cold=mgscoop::dual_parts_cold_profile;namespace profile=mgscoop::dual_parts_local_profile;
int main(){unsigned count=0,failed=0;const auto check=[&](bool x,const char* why){++count;if(!x){++failed;std::printf("FAILED %s\n",why);}};
 check(cold::Reason(nullptr)==0,"no refusal maps numeric0");
 check(cold::Reason("profile_type_unsupported")==1&&cold::Reason("profile_face_default_unqualified")==3,"explicit supported numeric reason mapping");
 check(cold::Reason("profile_desired_current_changed")==6&&cold::Reason("unknown")==255,"mismatch and unknown never invent ready");
 cold::Stamp a{},b{};a.models.player=b.models.player=0x10000;a.profile.player=b.profile.player=0x10000;
 check(cold::Same(a,b),"diagnostic equality carries no authority");b.profile.currentFace=7;check(!cold::Same(a,b),"late numeric field change refuses cold snapshot");
 CarryFixture m;cold::Stamp out{};std::uint32_t reason=0;
 check(!cold::Read(m,m.image,m.player,out,reason)&&reason==cold::Reason("profile_owner")&&!out.models.player,"missing typed current profile refuses read and clears private stamp");
 m.Put(m.Block(0)+0x138,m.player+8);check(!cold::Read(m,m.image,m.player,out,reason)&&reason==cold::Reason("cold_models"),"replaced actual local0 model refuses cold read");
 std::printf("cold_profile_tests: %u checks; %u failures\n",count,failed);return failed?1:0;
}
