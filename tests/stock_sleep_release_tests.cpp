#include "stock_sleep_release_policy.hpp"
#include <cstdio>
namespace p=mgscoop::stock_sleep_release_policy;
int main(){unsigned count=0,fail=0;const auto check=[&](bool b){++count;if(!b)++fail;};
 p::Release r{};const p::Episode e{30010,0x10000004,3};
 check(r.MaySeed(e,true,2));check(!r.Request(e,false,2));check(!r.Request(e,true,1));
 auto invalid=e;invalid.epoch=0;check(!r.Request(invalid,true,2));
 check(r.Request(e,true,2)&&r.Pending(e,true,2)&&!r.MaySeed(e,true,2));
 check(!r.ObservePhysicalRetirement(e,false)&&!r.physicalRetired);
 check(r.Request(e,true,2)&&!r.physicalRetired);check(r.ObservePhysicalRetirement(e,true));
 // Actual native wake is done; mask removal/revive take subsequent real updates.
 for(unsigned tick=0;tick<4;++tick){check(r.Request(e,true,2)&&r.physicalRetired);check(!r.MaySeed(e,true,2));r.LogicalExit(false);check(r.Pending(e,true,2));}
 auto foreign=e;foreign.generation=4;check(!r.Request(foreign,true,2));check(!r.ObservePhysicalRetirement(foreign,true));
 foreign=e;foreign.mission=30020;check(!r.Request(foreign,true,2));foreign=e;foreign.epoch=4;check(!r.Request(foreign,true,2));
 check(!r.Pending(e,false,2));r.LogicalExit(true);check(!r.requested&&!r.physicalRetired&&r.MaySeed(e,true,2));
 check(r.Request(e,true,2)&&!r.physicalRetired);r.LogicalExit(true);check(r.MaySeed(foreign,true,2));
 std::printf("stock sleep release: %u checks, %u failures\n",count,fail);return fail?1:0;}
