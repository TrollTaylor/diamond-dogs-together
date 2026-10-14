#define main baseline_driver_checks
#include "partner_driver_tests.cpp"
#undef main
#include "vehicle_mirror_state.hpp"
namespace {
bool currentJob=false,currentAttachment=false;
bool VehicleJob(void*,const pp::State&,const mgscoop::partner_input::Arm& arm) noexcept {
 return currentJob&&arm.enabled&&arm.native&&!arm.neutral;
}
bool VehicleAttachment(void*,const pp::State&) noexcept{return currentAttachment;}
std::vector<mgscoop::partner_net::ReliableOrigin> originQueue;
unsigned routed=0;std::uint64_t acceptedAt=0,incarnation=0;
bool OriginDrain(void* context,std::uint8_t channel,std::uint8_t* out,std::size_t cap,std::size_t& size,
 mgscoop::partner_net::ReliableOrigin& origin) noexcept {
 if(!HookDrainReliable(context,channel,out,cap,size))return false;
 if(originQueue.empty())return false;origin=originQueue.front();originQueue.erase(originQueue.begin());return true;
}
bool VehicleReceive(void*,const mgscoop::partner_net::ReliableOrigin& origin,const std::uint8_t* bytes,std::size_t size) noexcept {
 if(size!=32||bytes[0]!='V')return false;++routed;acceptedAt=origin.acceptedTick;incarnation=origin.incarnation;return true;
}
void TestVehicleJobRetention(){
 namespace pn=mgscoop::partner_native;
 NativeSession s;Check(s.Playing()>=0,"vehicle: normal foot clips precede native admission");
 Check(s.Until(pn::Mode::entering,3000)&&s.Until(pn::Mode::active,6000),"vehicle: ordinary native placement/Arm/deferral admission");
 s.hooks.native_vehicle_job=VehicleJob;s.hooks.native_vehicle_attached=VehicleAttachment;
 const auto gen=s.driver.mission.last_arm.gen;
 const auto arms=s.fake.arms.size(),clears=s.fake.Writes("clear");
 const auto inputSends=s.fake.inputs.size();
 s.driver.settings.nride=mgscoop::PartnerNativeRide::hide;
 s.driver.ride_rx.have=true;s.driver.ride_rx.epoch=s.fake.link_session;
 s.driver.ride_rx.packet.kind=mgscoop::partner_ride_wire::Kind::vehicle;s.driver.ride_rx.received_ms=s.now;
 currentJob=true;currentAttachment=false;
 Check(s.N(),"fresh genuine intent retains job before actual attachment");
 Check(s.driver.mission.native_defer_armed&&s.driver.mission.input_armed&&s.driver.mission.last_arm.gen==gen&&
  s.fake.arms.size()==arms&&s.fake.Writes("clear")==clears,"legacy ride-kind cannot disarm/withdraw/clear pending native job");
 const auto slots=s.fake.Writes("slot"),xforms=s.fake.Calls("xform:");
 currentAttachment=true;s.fake.link_live=false;s.fake.latest.age_ms=50000;s.fake.sample.valid=false;
 for(unsigned k=0;k<8;++k)Check(s.N(),"current native phase6 cleanup ticks survive source expiry");
 Check(s.driver.mission.input_armed&&s.driver.mission.native_defer_armed&&s.driver.mission.last_arm.gen==gen&&
  s.fake.arms.size()==arms,"current cleanup retains exact Arm without freshening/republication");
 Check(s.fake.Writes("slot")==slots&&s.fake.Calls("xform:")==xforms&&!(s.reply.op&op::warp),"actual matching attachment suppresses all foot placement paths");
 Check(s.fake.inputs.size()>inputSends,"local input transmission continues while peer drives or exits");
 currentJob=false;currentAttachment=false;s.fake.link_live=true;s.fake.latest.age_ms=5;s.fake.sample.valid=true;
 s.driver.ride_rx.packet.kind=mgscoop::partner_ride_wire::Kind::foot;s.driver.ride_rx.received_ms=s.now;
 s.driver.settings.nride=mgscoop::PartnerNativeRide::off;
 Check(s.N()&&!s.driver.mission.vehicle_job,"native retirement releases special job retention");
 Check(s.driver.recon_place_request==mgscoop::partner_recon::PlaceReason::none||s.driver.mission.recon_active,
  "foot reconciliation resumes after actual native exit");
}
void TestAuthenticatedVehicleRouting(){
 NativeSession s;s.hooks.drain_reliable_origin=OriginDrain;s.hooks.receive_vehicle=VehicleReceive;
 mgscoop::vehicle_mirror_state::Receipt r{};r.event=mgscoop::vehicle_mirror_state::Event::mounted;r.name={};
 r.role=1;r.mission=30050;r.epoch=2;r.sequence=1;r.mount=1;r.phase=3;r.seat=0;
 unsigned char bytes[32]{};Check(mgscoop::vehicle_mirror_state::Encode(r,bytes,32),"V fixture exact logical packet");
 s.fake.channel2.emplace_back(bytes,bytes+32);
 mgscoop::partner_net::ReliableOrigin origin{};origin.incarnation=99;origin.session=s.fake.link_session;origin.acceptedTick=1234;origin.message=44;
 origin.localRole=0;origin.peerRole=1;originQueue.push_back(origin);
 // An unrelated existing caps packet follows on the same queue.
 s.fake.channel2.push_back(CapsBytes(7,s.fake.link_session,mgscoop::partner_caps_wire::Feature::InputSender,
   mgscoop::partner_native_net::Drive::native,"ok"));origin.message=45;originQueue.push_back(origin);
 Check(s.N()&&routed==1&&acceptedAt==1234&&incarnation==99,"router passes original insertion clock/incarnation without reconstructing LinkInfo");
 Check(s.driver.caps_rx.have&&s.driver.caps_rx.caps.revision==7,"unrelated channel2 handler preserved with origin-aware dequeue");
 s.hooks.drain_reliable_origin=nullptr;s.fake.channel2.emplace_back(bytes,bytes+32);
 Check(s.N()&&routed==1,"bare legacy dequeue cannot authorize vehicle message");
}
}
int main(){
 const auto before=checks;TestVehicleJobRetention();TestAuthenticatedVehicleRouting();
 std::printf("%u detached actual driver wiring checks passed; no game\n",checks-before);return 0;
}
