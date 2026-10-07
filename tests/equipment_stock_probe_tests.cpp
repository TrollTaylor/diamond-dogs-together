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
    std::printf("equipment_stock_probe_tests: %u checks passed; native_calls=0\n",checks);
}
