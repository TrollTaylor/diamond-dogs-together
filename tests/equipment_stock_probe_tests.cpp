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
    std::printf("equipment_stock_probe_tests: %u checks passed; native_calls=0\n",checks);
}
