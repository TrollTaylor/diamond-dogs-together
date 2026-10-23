#include "mgscoop/downed_damage.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
using namespace mgscoop::downed_damage;
int main() {
    unsigned checks=0;
    auto check=[&](bool v){++checks;if(!v){std::fprintf(stderr,"downed damage failed %u\n",checks);std::exit(1);}};
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    for(unsigned part=0;part<256;++part) {
        check(NaturalBodyPart(static_cast<std::uint8_t>(part))==(part<=11));
        for(unsigned local=0;local<2;++local) {
            const auto target=1-local;
            const auto body=static_cast<std::uint8_t>(part);
            check(FriendlyReplicaHit(true,target,local,static_cast<std::uint16_t>(local),1,body,0,2,857)==(part<=11 && local==0));
            check(FriendlyReplicaHit(true,target,local,static_cast<std::uint16_t>(local),1,body,400,0,0)==(part<=11 && local==0));
            const auto d=Evaluate(true,local,local,static_cast<std::uint16_t>(target),2,body,100,0,857,2,false);
            check(d.bypass==(part<=11) && d.cause==(part<=11?2:0));
            check(!FriendlyReplicaHit(true,target,local,static_cast<std::uint16_t>(local),1,body,0,1,857));
            check(UnsafeOfflineSleep(true,target,static_cast<std::uint16_t>(local),2,body,857)==(part==0));
        }
    }
    for(unsigned mode=0;mode<256;++mode) check(SleepDamage(static_cast<std::uint8_t>(mode),857)==(mode==2));
    for(float bad:{0.f,-1.f,nan,inf}) check(!SleepDamage(2,bad));
    for(unsigned local=0;local<2;++local) for(unsigned target=0;target<2;++target) {
        const auto other=static_cast<std::uint16_t>(1-target);
        auto d=Evaluate(true,target,local,other,2,2,100,0,857,2,false);
        check(d.bypass && d.cause==(target==local ? 2 : 0));
        d=Evaluate(true,target,local,0x1000,0,2,100,100,0,0,false);
        check(d.bypass && d.cause==(target==local ? 1 : 0));
        check(!Evaluate(true,target,local,0x1000,2,2,100,1,857,2,false).bypass);
        check(!Evaluate(true,target,local,static_cast<std::uint16_t>(target),2,2,100,1,857,2,false).bypass);
        check(!Evaluate(true,target,local,other,0,2,100,1,857,2,false).bypass);
        check(!Evaluate(true,target,local,other,2,12,100,1,857,2,false).bypass);
        check(!Evaluate(true,target,local,other,2,2,100,99,0,0,false).bypass);
        check(!Evaluate(true,target,local,other,2,2,100,0,857,1,false).bypass);
        check(!Evaluate(false,target,local,other,2,2,100,100,857,2,false).bypass);
        check(!Evaluate(true,target,local,other,2,2,0,100,857,2,false).bypass);
        check(!Evaluate(true,target,local,other,2,2,-1,100,857,2,false).bypass);
        check(!Evaluate(true,target,local,other,2,2,100,nan,857,2,false).bypass);
        check(!Evaluate(true,target,local,other,2,2,100,100,inf,2,false).bypass);
        check(Evaluate(true,target,local,other,0,2,100,1,0,0,true).bypass==(target==local));
    }
    // Lethal-to-downed only for natural body parts on the local player. The
    // sea/out-of-bounds kill (part 12, flags 0x2419, damage 10000, attacker =
    // self) stays the stock death; the replica stays protected for every part.
    for(unsigned part=0;part<256;++part) for(unsigned local=0;local<2;++local) for(unsigned target=0;target<2;++target) {
        const auto body=static_cast<std::uint8_t>(part);
        const auto d=Evaluate(true,target,local,static_cast<std::uint16_t>(target),0x2419,body,7200,10000,0,0,false);
        if(target==local) {
            check(d.bypass==(part<=11) && d.cause==(part<=11?1:0));
            check(EnvironmentKill(true,target,local,body,7200,10000)==(part>11));
        } else {
            check(d.bypass && d.cause==0);
            check(!EnvironmentKill(true,target,local,body,7200,10000));
        }
        // Evaluate keeps the protected (already downed) player's shield; Calculate asks
        // ProtectedEnvironmentKill first for a non-natural part (below).
        check(Evaluate(true,target,local,static_cast<std::uint16_t>(target),0x2419,body,7200,10000,0,0,true).bypass);
        // A downed player (protected health 5%: 360 of 7200) who crawls into the sea: the kill
        // (10000 >= the 7200 maximum) is the stock death, for the local player and part > 11 only.
        check(ProtectedEnvironmentKill(true,target,local,body,360,7200,10000)==(target==local && part>11));
    }
    // A part-12 packet lethal only at the downed health keeps the shield; an unknown maximum too.
    check(ProtectedEnvironmentKill(true,0,0,12,360,6000,10000));
    check(ProtectedEnvironmentKill(true,0,0,12,360,7200,7200));
    check(!ProtectedEnvironmentKill(true,0,0,12,360,7200,7199));
    check(!ProtectedEnvironmentKill(true,0,0,12,360,7200,359));
    check(!ProtectedEnvironmentKill(true,0,0,12,360,0,10000));
    check(!ProtectedEnvironmentKill(true,0,0,12,360,nan,10000));
    check(!ProtectedEnvironmentKill(true,0,0,12,360,inf,10000));
    check(!ProtectedEnvironmentKill(false,0,0,12,360,7200,10000));
    check(!ProtectedEnvironmentKill(true,0,0,4,360,7200,10000));
    check(!ProtectedEnvironmentKill(true,0,0,12,360,7200,nan));
    // The real session samples: host (life 7200) and guest (life 6000).
    check(!Evaluate(true,0,0,0,0x2419,12,7200,10000,0,0,false).bypass);
    check(!Evaluate(true,0,0,0,0x2419,12,6000,10000,0,0,false).bypass);
    check(Evaluate(true,1,0,0,0x2419,12,6000,10000,0,0,false).bypass);
    check(Evaluate(true,0,0,1,0x401,4,7800,7800,0,0,false).cause==1);   // explosion part 4 still downs
    check(!EnvironmentKill(false,0,0,12,7200,10000));
    check(!EnvironmentKill(true,0,0,12,7200,100));
    check(!EnvironmentKill(true,0,0,12,0,10000));
    check(!EnvironmentKill(true,2,2,12,7200,10000));
    check(!EnvironmentKill(true,0,0,12,nan,10000));
    // Active accepted ordinary packets qualify irrespective of the optional bit1.
    check(FriendlyReplicaHit(true,1,0,0,0x4401,2,942,0,0));
    check(FriendlyReplicaHit(true,1,0,0,0x4481,1,942,0,0));
    check(!FriendlyReplicaHit(true,1,0,0,0x4000,2,942,0,0));
    check(!FriendlyReplicaHit(true,1,0,0,0x4402,2,942,0,0));
    check(!FriendlyReplicaHit(true,1,0,0,0x4400,2,942,0,0)); // already-neutralized packet
    check(!FriendlyReplicaHit(true,0,1,1,0x4401,2,942,0,0)); // only the independently offline local0 shooter
    check(!FriendlyReplicaHit(true,1,0,0,0x4401,2,0,0,0));
    check(FriendlyReplicaHit(true,1,0,0,0x4401,2,0,2,857));
    // Shooter-owned replica forwarding rejects stale, inactive and invalid data.
    check(FriendlyReplicaHit(true,1,0,0,0x4403,2,0,2,857));
    check(!FriendlyReplicaHit(false,1,0,0,1,2,0,2,857));
    check(!FriendlyReplicaHit(true,0,0,0,1,2,0,2,857));
    check(!FriendlyReplicaHit(true,1,0,1,1,2,0,2,857));
    check(!FriendlyReplicaHit(true,1,0,0x200,1,2,0,2,857));
    check(!FriendlyReplicaHit(true,1,0,0,0,2,0,2,857));
    check(!FriendlyReplicaHit(true,2,0,0,1,2,0,2,857));
    check(!FriendlyReplicaHit(true,1,2,2,1,2,0,2,857));
    for(float bad:{-1.f,nan,inf}) check(!FriendlyReplicaHit(true,1,0,0,1,2,bad,2,857));
    for(float bad:{0.f,-1.f,nan,inf}) check(!FriendlyReplicaHit(true,1,0,0,1,2,0,2,bad));
    // Crash protection is narrower than downed forwarding and needs no active owner lease.
    for (unsigned target=0;target<2;++target) {
        const auto other=static_cast<std::uint16_t>(1-target);
        check(UnsafeOfflineSleep(true,target,other,2,0,857));
        check(!UnsafeOfflineSleep(false,target,other,2,0,857));
        check(!UnsafeOfflineSleep(true,target,other,0,0,857));
        check(!UnsafeOfflineSleep(true,target,other,2,2,857));
        check(!UnsafeOfflineSleep(true,target,other,2,0,0));
        check(!UnsafeOfflineSleep(true,target,other,2,0,inf));
        check(UnsafeOfflineSleep(true,target,static_cast<std::uint16_t>(target),2,0,857));
        check(UnsafeOfflineSleep(true,target,0x1000,2,0,857));
    }
    // Attacker is notification payload, not a native branch predicate. Exhaust
    // all U16 IDs, including both players, NPC ranges and invalid/sentinel IDs.
    for(unsigned attacker=0;attacker<=65535;++attacker)for(unsigned target=0;target<2;++target){
        const auto who=static_cast<std::uint16_t>(attacker);
        check(UnsafeOfflineSleep(true,target,who,0x4402,0,857));
        check(!UnsafeOfflineSleep(false,target,who,0x4402,0,857));
        check(!UnsafeOfflineSleep(true,target,who,0x4400,0,857));
        check(!UnsafeOfflineSleep(true,target,who,0x4402,1,857));
    }
    for(auto who:{std::uint16_t(2),std::uint16_t(0x1000),std::uint16_t(0xfffe),std::uint16_t(0xffff)}){
        for(unsigned target=0;target<2;++target){
            check(UnsafeOfflineSleep(true,target,who,2,0,857));
            // The independent crash shield is useful after owner refusal, but
            // cannot authenticate a logical downed event or replica forwarding.
            check(!Evaluate(false,target,0,who,2,0,100,0,857,2,false).bypass);
            check(!Evaluate(true,target,0,who,2,0,100,0,857,2,false).bypass);
            check(!FriendlyReplicaHit(true,target,0,who,3,0,0,2,857));
            for(float bad:{0.f,-1.f,nan,inf})check(!UnsafeOfflineSleep(true,target,who,2,0,bad));
            check(!UnsafeOfflineSleep(true,target,who,1,0,857));
            check(!UnsafeOfflineSleep(true,target,who,2,255,857));
        }
    }
    check(!UnsafeOfflineSleep(true,2,0,2,0,857));
    check(!Evaluate(true,2,0,1,2,2,100,100,857,2,true).bypass);
    check(!Evaluate(true,0,2,1,2,2,100,100,857,2,true).bypass);
    std::printf("downed_damage_tests: %u passed\n",checks);
}
