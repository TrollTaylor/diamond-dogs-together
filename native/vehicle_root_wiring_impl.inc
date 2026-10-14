// Included inside the observer's existing namespace, after native birth equality.
// Every Lua lookup occurs on the ordinary Lua update; native hooks only read cache.
namespace vw=mgscoop::vehicle_root_wiring;
namespace vm=mgscoop::coop_vehicle_mirror;
namespace vi=mgscoop::partner_input_hook;
SRWLOCK vehicle_names_lock=SRWLOCK_INIT;
vw::Cache vehicle_names{};
bool VehicleScene(mgscoop::vehicle_mirror_state::Link& out,const mgscoop::partner_net::ReliableOrigin& origin) noexcept {
 out={};unsigned short mission=0;const auto first=mgscoop::world_link::GetStatus();
 if(!PartnerSleepPrivate()||!partner_on||!partner_gate_ok||mission_terminating||gameplay_restore_pending||
  !first.configured||!first.paired||!first.link_up||first.effective<3||!ReadMission(mission)||mission!=partner_mission||
  !vw::Link(origin,mission,first.epoch,out))return false;
 const auto late=mgscoop::world_link::GetStatus();unsigned short finalMission=0;
 return late.configured&&late.paired&&late.link_up&&late.effective>=3&&late.epoch==first.epoch&&
  ReadMission(finalMission)&&finalMission==mission;
}
bool VehicleLink(mgscoop::vehicle_mirror_state::Link& out) noexcept {
 mgscoop::partner_net::ReliableOrigin a{},b{};
 return mgscoop::partner_net::CurrentReliableOrigin(a)&&VehicleScene(out,a)&&
  mgscoop::partner_net::CurrentReliableOrigin(b)&&vw::SameOrigin(a,b);
}
bool VehicleColdIdentity(vw::Identity& out) noexcept {
 out={};if(!OnRuntimeThread()||!PartnerSleepPrivate()||mission_terminating||gameplay_restore_pending)return false;
 mgscoop::vehicle_mirror_state::Link link{};if(!VehicleLink(link))return false;
 vi::NativePoseBirthCensus a{},b{};pp::State current{};
 if(!vi::ReadNativePoseBirthCensus(a)||pp::ReadState(partner_addresses,current)!=pp::Refusal::none||
  current.P!=a.player||current.Q!=a.data||current.S!=a.state||current.local||current.sync||current.count!=2||
  current.ids[0]!=0||current.ids[1]!=1||current.parts[0]!=3||current.parts[1]!=3||
  !pp::Realized(current,0)||!pp::Realized(current,1)||!vi::ReadNativePoseBirthCensus(b)||!SameColdBirthCensus(a,b))return false;
 out=vw::FromBirth(b,link.mission,link.epoch);return vw::Valid(out);
}
bool VehicleResolve(mgscoop::vehicle_mirror_state::Name name,std::uint16_t& id) noexcept {
 id=0xffff;vw::Cache cache{};AcquireSRWLockShared(&vehicle_names_lock);cache=vehicle_names;ReleaseSRWLockShared(&vehicle_names_lock);
 const auto now=GetTickCount64();
 if(vi::NativePoseOwnedWindow()){
  vi::NativePoseJobContext current{};mgscoop::vehicle_mirror_state::Link link{};std::size_t at=0;
  if(!vi::ReadNativePoseOwnedJobContext(current)||!VehicleLink(link)||
   !vw::OwnedCache(cache,current,link.mission,link.epoch,now)||!vw::Index(name,at)||cache.ids[at]==0xffff)return false;
  id=cache.ids[at];return true;
 }
 vw::Identity current{};return VehicleColdIdentity(current)&&vw::Resolve(cache,current,name,now,id);
}
bool VehicleSend(const mgscoop::vehicle_mirror_state::Link& expected,const unsigned char* bytes,std::size_t size) noexcept {
 mgscoop::partner_net::ReliableOrigin origin{};mgscoop::vehicle_mirror_state::Link current{};
 if(!mgscoop::partner_net::CurrentReliableOrigin(origin)||!VehicleScene(current,origin)||
  !mgscoop::vehicle_mirror_state::Same(current,expected))return false;
 const auto result=mgscoop::partner_net::SendReliableForOrigin(origin,2,bytes,size,mgscoop::link::ReliableMode::append);
 return result==mgscoop::link::ReliableResult::queued||result==mgscoop::link::ReliableResult::replaced;
}
bool PartnerDrainReliableOrigin(void*,std::uint8_t channel,std::uint8_t* out,std::size_t cap,std::size_t& size,
 mgscoop::partner_net::ReliableOrigin& origin) noexcept {
 return mgscoop::partner_net::DrainReliableAuthenticated(channel,out,cap,size,origin);
}
bool PartnerReceiveVehicle(void*,const mgscoop::partner_net::ReliableOrigin& origin,const std::uint8_t* bytes,std::size_t size) noexcept {
 // Payload and origin came from one queue operation. Current identity is only
 // compared; it is never substituted for that captured message origin.
 mgscoop::partner_net::ReliableOrigin current{};mgscoop::vehicle_mirror_state::Link captured{};
 return vehicle_wiring_prepared.load()&&mgscoop::partner_net::CurrentReliableOrigin(current)&&vw::SameOrigin(origin,current)&&
  VehicleScene(captured,origin)&&vm::ReceiveAuthenticated(captured,bytes,size,origin.acceptedTick);
}
bool PartnerVehicleAttached(void*,const pp::State& st) noexcept {
 if(!PartnerSleepPrivate()||!OnRuntimeThread()||mission_terminating||gameplay_restore_pending)return false;
 vm::ReplicaRide ride{};vi::NativePoseBirthCensus a{},b{};
 return vm::ReadReplicaRide(ride)&&ride.attached&&vi::ReadNativePoseBirthCensus(a)&&
  a.player==st.P&&a.data==st.Q&&a.state==st.S&&a.player==ride.player&&a.block[1]==ride.block&&a.birth[1]==ride.birth&&
  a.generation==ride.lease.generation&&a.sceneEpoch==ride.lease.sceneEpoch&&
  vi::ReadNativePoseBirthCensus(b)&&SameColdBirthCensus(a,b);
}
bool PartnerVehicleJob(void*,const pp::State& st,const mgscoop::partner_input::Arm& arm) noexcept {
 if(!PartnerSleepPrivate()||!OnRuntimeThread()||mission_terminating||gameplay_restore_pending||
  !arm.enabled||!arm.native||arm.holdlog||arm.neutral||!arm.gen||!arm.rates_epoch)return false;
 vm::NativeDemand a{},b{};
 return vm::ReadNativeDemand(a)&&a.player==st.P&&a.data==st.Q&&a.state==st.S&&a.player==arm.P&&a.block==arm.blk1&&
  a.generation==arm.gen&&a.sceneEpoch==arm.rates_epoch&&a.pool==arm.pool1&&vm::ReadNativeDemand(b)&&
  b.player==a.player&&b.data==a.data&&b.state==a.state&&b.block==a.block&&b.birth==a.birth&&
  b.generation==a.generation&&b.sceneEpoch==a.sceneEpoch&&b.pool==a.pool&&b.retained==a.retained&&b.lease.serial==a.lease.serial;
}
bool VehicleMirrorPrepare() noexcept {
 if(vehicle_wiring_prepared.load())return true;
 const auto pose=vi::NativePoseWindowStatus();
 if(!PartnerSleepPrivate()||!OnRuntimeThread()||mission_terminating||gameplay_restore_pending||!offline_enabled||!partner_on||!partner_gate_ok||!game_base||!game_span||
  !pose.installed||pose.required||pose.held||pose.faulted||!EnsurePartnerSleepConfigured()||!vehicle_pose_observer.load())return false;
 // Install all immutable native providers before opting into required windows.
 if(!psleep::Enable(false)||!mgscoop::coop_passenger::Install(game_base,game_span,true,Log)||
  !vm::Install(game_base,game_span,true,{VehicleLink,VehicleResolve,VehicleSend,mgscoop::vehicle_native_protection::ReadOriginal},Log)||
  !mgscoop::vehicle_native_protection::Install(game_base,game_span,true,{vm::ReadUIEvent9Receipt,
    [](const char* why) noexcept{mgscoop::worker_scenario::Refuse(why);}}))return false;
 vehicle_wiring_prepared.store(true);return true;
}
bool VehicleMirrorEnable(bool on) noexcept {
 if(!PartnerSleepPrivate()||!OnRuntimeThread()||!vehicle_wiring_prepared.load())return false;
 if(!on){const bool ok=vm::SetEnabled(false);if(ok)vehicle_wiring_enabled.store(false);return ok;}
 const auto pose=vi::NativePoseWindowStatus();vw::Identity identity{};
 if(!pose.installed||!pose.required||pose.held||pose.faulted||pose.windows<2||
  !VehicleColdIdentity(identity)||psleep::SuppressForcedStance()||mgscoop::coop_carry::PartnerCarryHold()||
  !psleep::Enable(false)||!vm::SetEnabled(true))return false;
 vehicle_wiring_enabled.store(true);return true;
}
bool SelectVehicleNamesReceipt(const char* text,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
 if(!text)return false;
 const bool query=size==sizeof(vw::query)-1&&!std::memcmp(text,vw::query,size);
 const bool publication=size>=sizeof(vw::publish)-1&&!std::memcmp(text,vw::publish,sizeof(vw::publish)-1);
 if(!query&&!publication)return false;
 static constexpr char no[]="return false";static char answer[64]{};
 replacement={no,sizeof(no)-1};world_quiet=true;
 vw::Identity current{};
 if(!vehicle_wiring_prepared.load()||!VehicleColdIdentity(current))return true;
 AcquireSRWLockExclusive(&vehicle_names_lock);
 if(query){const auto nonce=vw::Begin(vehicle_names,current,GetTickCount64());
  if(nonce&&SUCCEEDED(StringCchPrintfA(answer,ARRAYSIZE(answer),"return %lu",static_cast<unsigned long>(nonce))))
   replacement={answer,std::strlen(answer)};
 }else{
  std::uint32_t nonce=0;std::uint16_t ids[vw::names]{};
  if(vw::Parse(text,size,nonce,ids)&&vw::Accept(vehicle_names,current,nonce,ids,GetTickCount64())){
   static constexpr char yes[]="return true";replacement={yes,sizeof(yes)-1};
  }else vehicle_names.pending=0;
 }
 ReleaseSRWLockExclusive(&vehicle_names_lock);return true;
}
