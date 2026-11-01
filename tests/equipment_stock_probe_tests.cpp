#include "mgscoop/equipment_stock_probe.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
namespace {
unsigned checks=0;
void Check(bool value) { ++checks; if(!value) { std::fprintf(stderr,"stock probe check %u failed\n",checks); std::exit(1); } }
mgscoop::equipment_stock_probe::Context Ready() {
    mgscoop::equipment_stock_probe::Context c;
    c.enabled=c.allocation_owner=c.mission_read=c.offline_owner_gates=true;
    c.groups_authorized=c.groups_returned=c.preflight_ok=true; c.mission=30010;
    return c;
}
}
int main() {
    using namespace mgscoop::equipment_stock_probe;
    Authorization a; auto c=Ready();
    Check(std::strcmp(a.Try(c),"not_allocation_owner")==0);
    a.Begin(30010); c.enabled=false;
    Check(std::strcmp(a.Try(c),"disabled")==0 && !a.Attempted());
    c=Ready(); c.allocation_owner=false;
    Check(std::strcmp(a.Try(c),"not_allocation_owner")==0 && !a.Attempted());
    c=Ready(); c.mission_read=false;
    Check(std::strcmp(a.Try(c),"allocation_mission")==0 && !a.Attempted());
    c=Ready(); c.mission=30020;
    Check(std::strcmp(a.Try(c),"allocation_mission")==0 && !a.Attempted());
    for(auto field : {&Context::offline_owner_gates,&Context::groups_authorized,&Context::groups_returned,&Context::preflight_ok}) {
        a.Begin(30010); c=Ready(); c.*field=false;
        Check(a.Try(c)!=nullptr && a.Attempted());
        Check(!a.Receipt(true,30010,true,true) && !a.QueuedFor(30010));
        c=Ready(); Check(std::strcmp(a.Try(c),"already_attempted")==0);
    }
    a.Begin(30010); c=Ready(); Check(a.Try(c)==nullptr && a.Attempted());
    Check(!a.Receipt(false,30010,true,true) && !a.QueuedFor(30010));
    Check(!a.Receipt(true,30020,true,true) && !a.QueuedFor(30010));
    Check(a.Receipt(true,30010,true,true) && a.QueuedFor(30010) && !a.QueuedFor(30020));
    Check(!a.Receipt(true,30010,true,true));
    a.SealAllocation(); Check(a.QueuedFor(30010) && std::strcmp(a.Try(c),"not_allocation_owner")==0);
    a.Begin(30010); Check(!a.QueuedFor(30010) && !a.Attempted());
    Check(a.Try(c)==nullptr); Check(!a.Receipt(true,30010,false,true) && !a.QueuedFor(30010));
    Check(!a.Receipt(true,30010,true,true));
    a.Begin(30010); Check(a.Try(c)==nullptr); Check(!a.Receipt(true,30010,true,false) && !a.QueuedFor(30010));
    a.Begin(30010); Check(a.Try(c)==nullptr); a.End();
    Check(!a.Receipt(true,30010,true,true) && !a.QueuedFor(30010) && !a.Attempted());
    a.Begin(30020); c.mission=30020; Check(std::strcmp(a.Try(c),"allocation_mission")==0 && !a.Attempted());
    // The generalized preload's authorization (W2): any supported mission of
    // the allocation; no manifest or no qualified id spends nothing.
    GeneralAuthorization g; GeneralContext gc{};
    const auto general_ready=[]() {
        GeneralContext x{};
        x.enabled=x.allocation_owner=x.mission_read=x.supported=x.owner_gates=true;
        x.groups_authorized=x.groups_returned=x.manifest=x.preflight_ok=true; x.ids=3; x.mission=30020;
        return x;
    };
    gc=general_ready(); Check(std::strcmp(g.Try(gc),"not_allocation_owner")==0);
    g.Begin(30020);
    gc=general_ready(); gc.enabled=false; Check(std::strcmp(g.Try(gc),"disabled")==0 && !g.Attempted());
    gc=general_ready(); gc.supported=false; Check(std::strcmp(g.Try(gc),"allocation_mission")==0 && !g.Attempted());
    gc=general_ready(); gc.mission=30010; Check(std::strcmp(g.Try(gc),"allocation_mission")==0 && !g.Attempted());
    gc=general_ready(); gc.manifest=false; Check(std::strcmp(g.Try(gc),"no_manifest")==0 && !g.Attempted());
    gc=general_ready(); gc.ids=0; Check(std::strcmp(g.Try(gc),"no_ids")==0 && !g.Attempted());
    for(auto field : {&GeneralContext::owner_gates,&GeneralContext::groups_authorized,&GeneralContext::groups_returned,&GeneralContext::preflight_ok}) {
        g.Begin(30020); gc=general_ready(); gc.*field=false;
        Check(g.Try(gc)!=nullptr && g.Attempted() && !g.Receipt(true,30020,true,true) && !g.QueuedFor(30020));
        gc=general_ready(); Check(std::strcmp(g.Try(gc),"already_attempted")==0);
    }
    g.Begin(30050); gc=general_ready(); gc.mission=30050; Check(g.Try(gc)==nullptr && g.Attempted());
    Check(!g.Receipt(true,30020,true,true) && g.Receipt(true,30050,true,true) && g.QueuedFor(30050) && !g.Receipt(true,30050,true,true));
    g.SealAllocation(); Check(std::strcmp(g.Try(gc),"not_allocation_owner")==0 && g.QueuedFor(30050));
    g.Begin(30050); Check(!g.QueuedFor(30050) && g.Try(gc)==nullptr && !g.Receipt(true,30050,true,false) && !g.QueuedFor(30050));
    g.End(); Check(!g.Attempted() && !g.Receipt(true,30050,true,true));
    // Two-PC test 2026-10-23 item 2: after a handoff the peer's first loadout is
    // this copy's own: its own manifest predicts the preload when none arrived.
    Check(ChoosePreloadSource(true,false,false)==PreloadSource::remote && ChoosePreloadSource(true,true,true)==PreloadSource::remote);
    Check(ChoosePreloadSource(false,true,true)==PreloadSource::predicted);
    Check(ChoosePreloadSource(false,false,true)==PreloadSource::none && ChoosePreloadSource(false,true,false)==PreloadSource::none &&
          ChoosePreloadSource(false,false,false)==PreloadSource::none);
    Check(std::strcmp(PreloadSourceName(PreloadSource::predicted),"predicted")==0 &&
          std::strcmp(PreloadSourceName(PreloadSource::remote),"remote")==0 && std::strcmp(PreloadSourceName(PreloadSource::none),"none")==0);
    // A predicted source is a manifest for the one attempt of the allocation (no_manifest no longer refuses it).
    g.Begin(30050); gc=general_ready(); gc.mission=30050;
    gc.manifest=ChoosePreloadSource(false,true,true)!=PreloadSource::none;
    Check(g.Try(gc)==nullptr && g.Attempted());
    {
        // The late manifest's cover of the allocation's ids (base 0 is no gun).
        const std::uint16_t remote[3]={0,46,9}, ids[2]={46,9}, other[1]={32};
        unsigned wanted=9;
        Check(PreloadCover(remote,ids,2,wanted)==2 && wanted==2);
        Check(PreloadCover(remote,other,1,wanted)==0 && wanted==2);
        Check(PreloadCover(remote,nullptr,0,wanted)==0 && wanted==2);
        const std::uint16_t none[3]={0,0,0};
        Check(PreloadCover(none,ids,2,wanted)==0 && wanted==0);
    }
    {
        // fb1031 gear: the partner paired after this mission's allocation
        // (the friend's log: refused link_not_connected, then no_resident).
        LateGearInput in{};
        in.admitted=in.manifest=in.gameplay=in.missing=true;
        Check(DecideLateGear(in)==LateGear::next_load);  // no mid-mission load: tell the viewer once
        in.midload=true;
        Check(DecideLateGear(in)==LateGear::request);
        in.attempted=true;
        Check(DecideLateGear(in)==LateGear::next_load);  // refused or failed: the next load
        in.returned=true;
        Check(DecideLateGear(in)==LateGear::loading);    // the writer re-plans
        LateGearInput quiet=in; quiet.attempted=quiet.returned=false;
        quiet.covered=true; Check(DecideLateGear(quiet)==LateGear::none);   // the allocation preloaded them
        quiet.covered=false; quiet.missing=false; Check(DecideLateGear(quiet)==LateGear::none); // resident anyway
        quiet.missing=true; quiet.admitted=false; Check(DecideLateGear(quiet)==LateGear::none);
        quiet.admitted=true; quiet.manifest=false; Check(DecideLateGear(quiet)==LateGear::none);
        quiet.manifest=true; quiet.gameplay=false; Check(DecideLateGear(quiet)==LateGear::none);
        Check(std::strcmp(LateGearName(LateGear::request),"request")==0 && std::strcmp(LateGearName(LateGear::next_load),"next_load")==0 &&
              std::strcmp(LateGearName(LateGear::loading),"loading")==0 && std::strcmp(LateGearName(LateGear::none),"none")==0);

        MidLoadAuthorization m;
        Check(std::strcmp(m.Try(true,30050,7,LateGear::request,true),"mission")==0 && !m.Attempted());
        m.Track(30050,7);
        Check(std::strcmp(m.Try(false,30050,7,LateGear::request,true),"not_runtime_owner")==0);
        Check(std::strcmp(m.Try(true,30010,7,LateGear::request,true),"mission")==0);
        Check(std::strcmp(m.Try(true,30050,8,LateGear::request,true),"mission")==0);
        Check(std::strcmp(m.Try(true,30050,7,LateGear::next_load,true),"not_needed")==0 && !m.Attempted());
        Check(!m.Receipt(true,30050,true));                       // nothing permitted yet
        Check(m.Try(true,30050,7,LateGear::request,true)==nullptr && m.Attempted() && !m.Returned());
        Check(std::strcmp(m.Try(true,30050,7,LateGear::request,true),"already_attempted")==0);
        Check(!m.Receipt(false,30050,true) && !m.Receipt(true,30010,true));
        Check(m.Receipt(true,30050,true) && m.Returned());
        Check(!m.Receipt(true,30050,true));                       // one receipt per permit
        m.Track(30050,7);                                         // the same pair: kept
        Check(m.Attempted() && m.Returned());
        m.Track(30050,8);                                         // a new pair asks once more
        Check(!m.Attempted() && !m.Returned());
        Check(std::strcmp(m.Try(true,30050,8,LateGear::request,false),"midload_preflight")==0 && m.Attempted() &&
              !m.Receipt(true,30050,true) && !m.Returned());
        // The note: once per mission and link session, only for next_load.
        Check(!m.TakeNote(LateGear::loading) && !m.TakeNote(LateGear::request));
        Check(m.TakeNote(LateGear::next_load) && !m.TakeNote(LateGear::next_load));
        m.End(); Check(!m.Attempted());
        m.Track(30050,8); Check(m.TakeNote(LateGear::next_load));
        // A failed public call: returned stays false.
        m.Track(30020,9);
        Check(m.Try(true,30020,9,LateGear::request,true)==nullptr && !m.Receipt(true,30020,false) && !m.Returned());

        std::uint32_t seen=0;
        Check(!ResidencyRenewal(seen,0) && seen==0);
        Check(ResidencyRenewal(seen,1) && seen==1 && !ResidencyRenewal(seen,1));
        Check(ResidencyRenewal(seen,2) && seen==2);

        // Covered: the allocation had this partner's manifest, or its ids cover every non-zero base.
        const std::uint16_t remote[3]={0,46,9}, both[2]={46,9}, one[1]={46};
        Check(LateGearCovered(false,remote,both,2));                // preloaded (the friend's 60839 reload)
        Check(!LateGearCovered(false,remote,one,1));                // partial, no manifest at the allocation
        Check(!LateGearCovered(false,remote,nullptr,0));            // refused link_not_connected (55903)
        Check(LateGearCovered(true,remote,one,1));                  // had the manifest: 9 was dropped there (no note)
        Check(LateGearCovered(true,remote,nullptr,0));              // had it, preflight refused: the next load fails alike
        const std::uint16_t none[3]={0,0,0};
        Check(!LateGearCovered(false,none,nullptr,0));              // nothing wanted: the writer decides (missing stays false)
    }
    std::printf("equipment_stock_probe_tests: %u checks passed; native_calls=0\n",checks);
}
