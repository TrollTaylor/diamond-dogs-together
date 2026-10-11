// Pure down-cycle and positive-health policy; no game process or native calls.
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#include "mgscoop/downed_player.hpp"
#include "mgscoop/partner_pad_bits.hpp"
#include "mgscoop/partner_rates.hpp"
namespace {
unsigned checks = 0;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"downed_player check %u: %s\n",checks,why); std::exit(1); }
}
}
int main() {
    namespace d = mgscoop::downed_player;
    namespace p = mgscoop::partner_pad;
    Check(d::detail::FreshScope(1000,1000),"fresh owner poll authorizes same-tick protection");
    Check(d::detail::FreshScope(1750,1000),"exact 750ms owner lease remains admitted");
    Check(!d::detail::FreshScope(1751,1000),"first expired millisecond refuses protection without extending immunity");
    Check(!d::detail::FreshScope(999,1000),"reversed clock cannot authorize stale owner protection");
    Check(!d::detail::FreshScope(UINT64_MAX,1000),"arbitrarily old lease cannot wrap into fresh admission");
    Check(d::detail::FreshScope(UINT64_MAX,UINT64_MAX-750),"near-limit time subtraction preserves exact admitted bound");
    Check(!d::detail::FreshScope(UINT64_MAX,UINT64_MAX-751),"near-limit expired lease remains refused");
    // A real native hit may leave a transient hurt reaction before the stance
    // request can settle. It gets a bounded first acquisition grace, while a
    // previously observed lock loss must still fail after the shorter bound.
    for (std::uint64_t age : {std::uint64_t{0}, std::uint64_t{1500}, std::uint64_t{1501}, std::uint64_t{5999}, std::uint64_t{6000}}) {
        Check(!d::detail::ControlsExpired(10000 + age, 10000, 0, false, false),
              "initial hurt reaction retains only its bounded six-second acquisition grace");
    }
    Check(d::detail::ControlsExpired(16001, 10000, 0, false, false),
          "first millisecond after acquisition grace refuses prolonged unconfirmed controls");
    Check(!d::detail::ControlsExpired(21500, 10000, 20000, true, false),
          "confirmed lock uses last confirmation and preserves exact shorter boundary");
    Check(d::detail::ControlsExpired(21501, 10000, 20000, true, false),
          "lost confirmed lock fails after 1500ms rather than receiving new six-second immunity");
    Check(d::detail::ControlsExpired(9999, 10000, 0, false, false) &&
          d::detail::ControlsExpired(19999, 10000, 20000, true, false),
          "reversed acquisition or confirmed clock cannot authorize a stale pending transition");
    Check(d::detail::ControlsExpired(UINT64_MAX, 10000, 0, false, false),
          "arbitrarily old acquisition cannot wrap into a fresh grace");
    Check(!d::detail::ControlsExpired(UINT64_MAX, UINT64_MAX - 6000, 0, false, false) &&
          d::detail::ControlsExpired(UINT64_MAX, UINT64_MAX - 6001, 0, false, false),
          "near-limit acquisition subtraction preserves the exact bounded interval");
    for (bool previouslyApplied : {false, true}) {
        Check(!d::detail::ControlsExpired(999999, 10000, 20000, previouslyApplied, true),
              "current prone mask and crawl confirmation takes priority over an old timestamp");
    }
    Check(std::strcmp(d::detail::ControlsPendingReason(false, true, true), "controls_pending_prone") == 0,
          "pending native stance is distinct from mask or crawl refusal");
    Check(std::strcmp(d::detail::ControlsPendingReason(true, false, true), "controls_pending_mask") == 0,
          "missing named restrictions have a separate refusal diagnostic");
    Check(std::strcmp(d::detail::ControlsPendingReason(true, true, false), "controls_pending_crawl") == 0,
          "rate ownership refusal remains separate from a prone readback delay");
    Check(std::strcmp(d::detail::ControlsPendingReason(true, true, true), "downed") == 0,
          "fully confirmed controls have no pending phase");
    d::detail::CrawlLease slow{};
    auto cp = d::detail::PlanCrawl(slow, 1.2f, 1.f, 1.2f, false);
    Check(cp.valid && cp.write && cp.base == .5f && cp.product == .5f && cp.next.original == 1.2f,
          "downed crawl caps original equipment/staff speed at existing wire minimum");
    slow = cp.next;
    for (unsigned poll=0; poll<100; ++poll) {
        cp = d::detail::PlanCrawl(slow, .5f, 1.f, .5f, false);
        Check(cp.valid && !cp.write && cp.next.original == 1.2f && cp.base == .5f,
              "repeated natural polls do not compound the speed reduction");
        slow = cp.next;
    }
    cp = d::detail::PlanCrawl(slow, .5f, .8f, .5f*.8f, false);
    Check(cp.valid && !cp.write && cp.next.original == 1.2f && cp.product == .5f*.8f,
          "retail crawl factor changes remain untouched and keep original base");
    cp = d::detail::PlanCrawl(slow, .5f, .8f, .5f*.8f, true);
    Check(cp.valid && cp.write && cp.base == 1.2f && cp.product == 1.2f*.8f && !cp.next.applied,
          "revive restores owned base with current retail factor and releases lease");
    cp = d::detail::PlanCrawl(slow, .9f, 1.f, .9f, true);
    Check(cp.valid && !cp.write && cp.base == .9f && !cp.next.applied,
          "a retail base rewrite survives conditional restore without an old original");
    cp = d::detail::PlanCrawl(slow, .9f, 1.f, .9f, false);
    Check(cp.valid && cp.write && cp.base == .5f && cp.next.original == .9f,
          "a real new retail base becomes the new restore original instead of previous value");
    cp = d::detail::PlanCrawl({}, .2f, 1.f, .2f, false);
    Check(cp.valid && !cp.write && cp.base == .2f && cp.next.original == .2f,
          "an already slower native crawl is never sped up");
    for (float invalid : {0.f,-1.f,17.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        Check(!d::detail::PlanCrawl(slow,invalid,1,1,false).valid,"invalid base refuses before native write");
        Check(!d::detail::PlanCrawl(slow,1,invalid,1,true).valid,"invalid retail factor refuses cleanup write");
        Check(!d::detail::PlanCrawl(slow,1,1,invalid,false).valid,"invalid product refuses before native write");
    }
    Check(!d::detail::PlanCrawl(slow,.5f,1,.6f,false).valid &&
          !d::detail::PlanCrawl(slow,.5f,1,.6f,true).valid,
          "incoherent product refuses apply and restore without touching either field");
    Check(!d::detail::PlanCrawl({true,std::numeric_limits<float>::quiet_NaN(),.5f},.5f,1,.5f,true).valid,
          "invalid owned original cannot authorize a revive write");
    mgscoop::partner_rates::Record cappedRecord{};
    cappedRecord.f[0]=.5f; cappedRecord.f[1]=.8f; cappedRecord.f[2]=.5f*.8f;
    cappedRecord.f[3]=1.2f; cappedRecord.f[4]=1.f; cappedRecord.f[5]=1.2f;
    mgscoop::partner_rates::Wire cappedWire{};
    Check(mgscoop::partner_rates::ToWire(cappedRecord,cappedWire) && cappedWire.crawl==500 &&
          cappedWire.crawl_factor==800 && cappedWire.dash==1200 && mgscoop::partner_rates::ValidWire(cappedWire),
          "downed0.5 base and retail factor survive existing rate wire validation for partner mirror");
    cappedRecord.f[0]=1.2f; cappedRecord.f[2]=1.2f*.8f;
    Check(mgscoop::partner_rates::ToWire(cappedRecord,cappedWire) && cappedWire.crawl==1200 &&
          cappedWire.crawl_factor==800 && cappedWire.dash==1200,
          "restored equipment/staff base is transmitted again after revive without changing dash or factor");
    const d::detail::CrawlIdentity crawlOwner{10,20,30,40};
    Check(d::detail::CheckCrawlOwner(crawlOwner,true,crawlOwner,true,crawlOwner)==d::detail::CrawlOwner::owned,
          "same exact typed player/rate identity authorizes speed restore independently of health max or vars");
    Check(d::detail::CheckCrawlOwner(crawlOwner,false,crawlOwner,true,crawlOwner)==d::detail::CrawlOwner::retry &&
          d::detail::CheckCrawlOwner(crawlOwner,true,crawlOwner,false,crawlOwner)==d::detail::CrawlOwner::retry,
          "either transient typed read failure retains the old cleanup lease");
    for (unsigned field=0;field<4;++field) {
        auto changed=crawlOwner;
        if(field==0) changed.player=11;
        if(field==1) changed.state=21;
        if(field==2) changed.service=31;
        if(field==3) changed.record=41;
        Check(d::detail::CheckCrawlOwner(crawlOwner,true,changed,true,changed)==d::detail::CrawlOwner::retired,
              "two exact typed replacement identities retire old speed lease without old-pointer writes");
        Check(d::detail::CheckCrawlOwner(crawlOwner,true,crawlOwner,true,changed)==d::detail::CrawlOwner::retry &&
              d::detail::CheckCrawlOwner(crawlOwner,true,changed,true,crawlOwner)==d::detail::CrawlOwner::retry,
              "identity drift between reads cannot restore or retire cleanup authority");
        auto absent=crawlOwner;
        if(field==0) absent.player=0;
        if(field==1) absent.state=0;
        if(field==2) absent.service=0;
        if(field==3) absent.record=0;
        Check(d::detail::CheckCrawlOwner(crawlOwner,true,absent,true,absent)==d::detail::CrawlOwner::retry,
              "null or partial current identity is transient refusal, never proof of replacement");
        Check(d::detail::CheckCrawlOwner(absent,true,crawlOwner,true,crawlOwner)==d::detail::CrawlOwner::retry,
              "missing original ownership cannot authorize cleanup writes");
    }
    // Regression: death/reload can recreate only life/max/vars while the owned
    // P/Q and speed record remain live. Restore still recovers their old base.
    cp=d::detail::PlanCrawl(slow,.5f,1.f,.5f,true);
    Check(d::detail::CheckCrawlOwner(crawlOwner,true,crawlOwner,true,crawlOwner)==d::detail::CrawlOwner::owned &&
          cp.valid && cp.base==1.2f && !cp.next.applied,
          "health-only drift cannot drop a surviving0.5 crawl record without restoring its1.2 base");
    const d::detail::Identity identity{10,20,30,40,6600};
    Check(d::detail::CompareIdentity(identity,identity)==d::detail::IdentityChange::none,"unchanged live owner retains its lease");
    for (unsigned field=0;field<5;++field) {
        auto changed=identity;
        if(field==0) changed.player=11;
        if(field==1) changed.state=21;
        if(field==2) changed.life=31;
        if(field==3) changed.vars=41;
        if(field==4) changed.maximum=6300;
        const auto kind=d::detail::CompareIdentity(identity,changed);
        Check(kind!=d::detail::IdentityChange::none,"each ownership field detects actual healthy retail drift");
        Check(d::detail::HealthyRebind(kind,false,0,true),"coherent healthy poll can replace stale lease without native restore");
        Check(!d::detail::HealthyRebind(kind,true,0,true),"active down cycle cannot migrate to another owner or maximum");
        Check(!d::detail::HealthyRebind(kind,false,0,false),"unstable native reread cannot establish a new lease");
        for (std::uint8_t command=1;command<=4;++command)
            Check(!d::detail::HealthyRebind(kind,false,command,true),"lock, revive, cleanup and raw-use reads cannot change native ownership");
        Check(std::strncmp(d::detail::IdentityReason(kind),"identity_",9)==0,"refusal names the exact changed identity component");
        Check(std::strncmp(d::detail::RebindReason(kind),"alive_rebound_",14)==0,"healthy rebind has distinct observed diagnostics");
    }
    Check(!d::detail::HealthyRebind(d::detail::IdentityChange::none,false,0,true),"unchanged owner does not clear hit queue or advance generation");
    d::detail::Cycle reboundCycle{4,false,0};
    d::detail::NewScene(reboundCycle);
    Check(reboundCycle.generation==5 && !d::detail::Revive(reboundCycle,4),"new healthy binding rejects old queued revive generation");
    Check(d::detail::Enter(reboundCycle,2) && reboundCycle.generation==6 &&
        !d::detail::Revive(reboundCycle,4) && d::detail::Revive(reboundCycle,6),"new binding can down and revive with only its new exact cycle");
    d::detail::Cycle s{};
    Check(!d::detail::Revive(s,0) && !d::detail::Revive(s,1),"live body never accepts revive");
    Check(!d::detail::Enter(s,0) && !d::detail::Enter(s,3),"only verified lethal and teammate-sleep causes enter");
    Check(s.generation==0 && !s.down && s.cause==0,"refused cause does not mutate cycle");
    for (std::uint32_t generation=1;generation<=32;++generation) {
        const std::uint8_t cause = generation&1 ? 1 : 2;
        Check(d::detail::Enter(s,cause) && s.down && s.generation==generation && s.cause==cause,"each independent incapacity gets one generation");
        Check(d::detail::Enter(s,cause==1?2:1) && s.generation==generation && s.cause==cause,"additional damage cannot rewrite cause or advance generation");
        Check(!d::detail::Revive(s,generation-1) && !d::detail::Revive(s,generation+1) && s.down,"stale and future grants cannot unlock body");
        Check(!d::detail::Enter(s,255) && s.down && s.generation==generation && s.cause==cause,"invalid cause cannot change a protected cycle");
        Check(d::detail::Revive(s,generation) && !s.down && s.cause==0 && s.generation==generation,"one matching grant returns live state and preserves generation");
        Check(!d::detail::Revive(s,generation) && !s.down,"duplicate grant cannot heal a second time");
    }
    s = {std::numeric_limits<std::uint32_t>::max(),false,0};
    Check(d::detail::Enter(s,2) && s.generation==1 && s.down,"generation wrap skips zero");
    Check(!d::detail::Revive(s,std::numeric_limits<std::uint32_t>::max()),"old pre-wrap grant cannot clear new generation");
    Check(d::detail::Revive(s,1),"wrapped exact grant succeeds");
    d::detail::NewScene(s);
    Check(s.generation==2 && !s.down && s.cause==0,"new scene barrier rejects previous mission-cycle generation");
    Check(d::detail::Enter(s,1) && s.generation==3,"post-reload incapacitation advances beyond barrier");
    for (float maximum : {1.f,1.5f,2.f,19.f,20.f,100.f,1000.f,100000000.f}) {
        const float protectedHp=d::detail::ProtectedHealth(maximum), revivedHp=d::detail::RevivedHealth(maximum);
        Check(protectedHp>=1 && protectedHp<=maximum,"protected current health stays positive and below max");
        Check(revivedHp>=1 && revivedHp<=maximum && revivedHp>=protectedHp,"revive restores valid positive health");
    }
    Check(d::detail::ProtectedHealth(1000)==50 && d::detail::RevivedHealth(1000)==500,"protected and revive health use bounded configured fractions");
    for (float invalid : {0.f,-1.f,.999f,100000008.f,std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        Check(d::detail::ProtectedHealth(invalid)==0 && d::detail::RevivedHealth(invalid)==0,"invalid health configuration is rejected without a write value");
    }
    std::uint16_t healthWord = 0;
    for (float maximum : {1.f,1.5f,2.f,19.f,1000.f,65535.f}) {
        for (float value : {1.f,maximum,d::detail::ProtectedHealth(maximum),d::detail::RevivedHealth(maximum)}) {
            Check(d::detail::HealthWord(value,maximum,healthWord),"valid current life has a positive U16 mirror");
            Check(healthWord==static_cast<std::uint16_t>(std::ceil(value)),"mirror matches exact retail ceil comparison and avoids next-update replacement");
        }
    }
    Check(d::detail::HealthWord(32767.5f,65535.f,healthWord) && healthWord==32768,"word values across signed-short boundary retain U16 bits");
    Check(d::detail::HealthWord(.001f,1000.f,healthWord) && healthWord==1,"positive sub-one nonlethal life cannot become zero/dead in mirror");
    for (float invalid : {0.f,-1.f,65536.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        Check(!d::detail::HealthWord(invalid,65535.f,healthWord) && healthWord==0,"invalid current rejects the whole proposed mirror transaction");
        Check(!d::detail::HealthWord(1.f,invalid,healthWord) && healthWord==0,"unsupported U16 max rejects mirror transaction");
    }
    Check(!d::detail::HealthWord(1001.f,1000.f,healthWord),"above-max current is refused rather than clamped silently");
    Check((d::kBlockedButtons&(p::kKbDash|p::kKbWalk))==0,"keyboard walk and movement remain available");
    Check((d::kBlockedButtons&(p::kStance|p::kDash|p::kEvade|p::kWorldBits))==(p::kStance|p::kDash|p::kEvade|p::kWorldBits),"standing, combat and world actions are masked");
    Check(d::kBlockedTriggers==3,"both weapon triggers blocked by the named mask");
    d::detail::ButtonMaskEntry entries[8]{};
    std::uint32_t others = 99; bool present = true;
    Check(d::detail::ReviveActionMask(entries,0,others,present) && others==0 && !present,"coherent empty table proves absence without authorizing raw action");
    entries[0] = {d::kReviveActionHash,p::kAction};
    Check(d::detail::ReviveActionMask(entries,p::kAction,others,present) && present && others==0,"sole coherent owned ACTION mask permits receipt exemption only");
    for (unsigned slot=1;slot<8;++slot) {
        entries[slot]={slot,p::kAction};
        Check(d::detail::ReviveActionMask(entries,p::kAction,others,present) && (others&p::kAction),"every independent occupied action mask still blocks revive input, including full tables");
    }
    entries[7]={0,0xFFFFFFFFu};
    Check(d::detail::ReviveActionMask(entries,p::kAction,others,present) && others==p::kAction,"vacant stale values do not enter retail aggregate");
    entries[1]={1,p::kHold};
    Check(!d::detail::ReviveActionMask(entries,p::kAction,others,present) && others==0,"changed or incoherent aggregate cannot exempt action");
    Check(d::detail::ReviveActionMask(entries,p::kAction|p::kHold,others,present) && others==(p::kAction|p::kHold),"all other occupied bits remain authoritative");
    entries[1]={d::kReviveActionHash,p::kAction};
    Check(!d::detail::ReviveActionMask(entries,p::kAction,others,present) && others==0,"duplicate owned hashes cannot authorize action");
    entries[1]={2,p::kAction};
    Check(!d::detail::ReviveActionMask(entries,p::kAction,others,present),"duplicate independent names refuse even if aggregate matches");
    entries[1]={1,p::kAction};
    for (std::uint32_t invalid : {0u,p::kHold,p::kAction|p::kHold,0xFFFFFFFFu}) {
        entries[0].buttons=invalid;
        Check(!d::detail::ReviveActionMask(entries,0xFFFFFFFFu,others,present) && others==0,"owned entry must be exactly ACTION, never other native controls");
    }
    entries[0].buttons=p::kAction;
    for (unsigned i=0;i<8;++i) entries[i]={i+1,p::kAction};
    Check(d::detail::ReviveActionMask(entries,p::kAction,others,present) && !present && others==p::kAction,"full coherent table proves own absence, never successful installation");
    for (unsigned ownSlot=0;ownSlot<8;++ownSlot) {
        for (unsigned bit=0;bit<32;++bit) {
            for (auto& entry : entries) entry={0,0xFFFFFFFFu};
            entries[ownSlot]={d::kReviveActionHash,p::kAction};
            const unsigned otherSlot=(ownSlot+1)%8;
            const std::uint32_t restriction=1u<<bit;
            entries[otherSlot]={1,restriction};
            Check(d::detail::ReviveActionMask(entries,p::kAction|restriction,others,present) && present && others==restriction,
                  "every occupied table slot and independent native restriction bit stays authoritative");
        }
    }
    d::detail::RegenCeiling recovery{1000,5658,true};
    Check(d::detail::RegenTarget(recovery,1001,5660,true)==5658 && recovery.active,"ordinary native regen is capped during recovery");
    Check(d::detail::RegenTarget(recovery,1100,5000,true)==5000 && recovery.life==5000,"lower enemy damage stays lower rather than being undone");
    Check(d::detail::RegenTarget(recovery,4999,5100,true)==5000,"last millisecond of recovery remains protected");
    Check(d::detail::RegenTarget(recovery,5000,5100,true)==5100 && !recovery.active,"exact four-second deadline releases ordinary regen");
    recovery={2000,4000,true};
    Check(d::detail::RegenTarget(recovery,1999,4500,true)==4500 && !recovery.active,"clock reversal ends lease rather than extending it");
    recovery={2000,4000,true};
    Check(d::detail::RegenTarget(recovery,2001,4500,false)==4500 && !recovery.active,"down and revive cycles release old recovery ceiling");
    for(float invalid : {0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        recovery={2000,4000,true};
        d::detail::RegenTarget(recovery,2001,invalid,true);
        Check(!recovery.active,"dead or invalid native health retires recovery ownership");
        recovery={2000,invalid,true};
        Check(d::detail::RegenTarget(recovery,2001,4500,true)==4500 && !recovery.active,"invalid ceiling cannot authorize health writes");
    }
    // A second accepted positive hit restarts the deadline with its lower value.
    recovery={1000,5658,true};
    Check(d::detail::RegenTarget(recovery,4500,6600,true)==5658,"previous shot ceiling gives the correct next-shot base");
    recovery={4500,4716,true};
    Check(d::detail::RegenTarget(recovery,5000,6600,true)==4716,"new accepted shot extends recovery from its own timestamp");
    Check(d::detail::RegenTarget(recovery,8500,6600,true)==6600 && !recovery.active,"restarted deadline still has a hard bound");
    float burstLife=6600;
    recovery={};
    for(unsigned shot=0;shot<7;++shot) {
        const auto at=1000+shot*100;
        const float before=d::detail::RegenTarget(recovery,at,6600,true);
        burstLife=before-942;
        Check(burstLife>0,"seven accepted 942-damage hits accumulate despite intervening native regeneration");
        recovery={at,burstLife,true};
    }
    Check(d::detail::RegenTarget(recovery,1700,6600,true)==6 && 942>=burstLife,"eighth ordinary hit is lethal against actual6600 owner rather than only its6000 replica");
    recovery={};
    Check(d::detail::RegenTarget(recovery,100,6600,true)==6600 && !recovery.active,"cleared context does not suppress new-scene health");
    d::detail::PadScope ps{true,true,true,true,false,false,true,71,71,1000,1000,10,10,false,false};
    Check(!d::detail::PadRefusal(ps),"fresh outer exact-caller owner callback admits observation outside all input windows");
    for(unsigned gate=0;gate<14;++gate) {
        auto bad=ps;
        switch(gate) {
        case 0:bad.freshUpdate=false;break;
        case 1:bad.outerUpdate=false;break;
        case 2:bad.exactCaller=false;break;
        case 3:bad.verified=false;break;
        case 4:bad.stopped=true;break;
        case 5:bad.failed=true;break;
        case 6:bad.armed=false;break;
        case 7:bad.ownerThread=0;break;
        case 8:bad.thread=72;break;
        case 9:bad.now=bad.poll+d::detail::kScopeLeaseMs+1;break;
        case 10:bad.now=bad.poll-1;break;
        case 11:bad.sequenceBefore=bad.sequenceAfter=11;break;
        case 12:bad.sequenceAfter=12;break;
        default:bad.batchBefore=true;break;
        }
        Check(d::detail::PadRefusal(bad)!=nullptr,"every missing natural owner/phase qualification independently refuses movement write");
    }
    ps.batchAfter=true;
    Check(d::detail::PadRefusal(ps)!=nullptr,"batch starting during coherent owner reads refuses cap"); ps.batchAfter=false;
    const float cap=d::detail::kDownedInputMagnitude;
    Check(cap>.1f && cap<1.f,"deliberate cap remains above exact natural sampler deadzone");
    for(unsigned frame=0;frame<100;++frame) {
        auto plan=d::detail::PlanPadMagnitude(1.f,true,true);
        Check(plan.valid && plan.write && plan.magnitude==cap,"each new full input frame receives one fixed cap without compounding");
    }
    for(float current : {0.f,.01f,.099f,.1f,cap*.5f,cap}) {
        const auto plan=d::detail::PlanPadMagnitude(current,true,true);
        Check(plan.valid && !plan.write && plan.magnitude==current,"released stick, deadzone and smaller analog magnitude preserve retail result");
    }
    for(float current : {0.f,.1f,cap,1.f}) {
        const auto alive=d::detail::PlanPadMagnitude(current,false,false);
        const auto pending=d::detail::PlanPadMagnitude(current,true,false);
        Check(alive.valid && !alive.write && alive.magnitude==current,"fresh healthy/revived update keeps unrestricted current input");
        Check(pending.valid && !pending.write && pending.magnitude==current,"controls pending cannot cap movement before prone/mask confirmation");
    }
    for(float invalid : {-1.f,1.001f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        const auto plan=d::detail::PlanPadMagnitude(invalid,true,true);
        Check(!plan.valid && !plan.write,"invalid fresh derived magnitude refuses rather than guessing or clamping");
    }
    // Model three real cycles with held input -> down -> revived current release.
    // A cached old1.0 is never a restore source; the next natural update owns it.
    d::detail::Cycle pc{};
    for(unsigned round=0;round<3;++round) {
        Check(d::detail::Enter(pc,round==1?2:1),"new input-cap cycle enters exact damage/sleep generation");
        Check(d::detail::PlanPadMagnitude(1.f,pc.down,true).magnitude==cap,"active down cycle caps new held movement");
        Check(d::detail::Revive(pc,pc.generation),"matching teammate grant ends input-cap cycle");
        const auto released=d::detail::PlanPadMagnitude(0.f,pc.down,false);
        Check(released.valid && !released.write && released.magnitude==0.f,"revive with released real input cannot resurrect cached held movement");
        Check(d::detail::PlanPadMagnitude(1.f,pc.down,false).magnitude==1.f,"next healthy held input demonstrates full native magnitude restored");
    }
    std::printf("%u native downed cycle, input and health checks; no game calls\n",checks);
}