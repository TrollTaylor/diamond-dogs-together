-- Cargo protocol/lifecycle regression. No retail process or attachment claim.
local checks=0
local function check(v,why) checks=checks+1;assert(v,checks..': '..why) end
local function fixture(disabled)
 local now,role,mounted,txOk,maskOk=1000,0,false,true,true
 local physical={ready=true,carrying=false,carried=false,localCarried=false,peerCarrying=false}
 local calls,tx,masks,logs={},{},{},{}
 local D={records={
  {at=now,pos={0,0,0},generation=11,down=false},
  {at=now,pos={0,0,0},generation=22,down=false}},sleepTimers={}}
 local W={OP={},handlers={},paired=true,eff=3,mission=30010,epoch=7,role=1,downed=D,startHooks={},unpairedHooks={}}
 W.get=function(t,k)return t and t[k] end
 W.now=function()return now end
 W.tick=function()end
 W.log=function(v)logs[#logs+1]=v end
 W.tx=function(...)tx[#tx+1]={...};return txOk end
 W.receipt=function(text)
  if text:find('MGS_COOP_RIDE_POSE_V1',1,true) then return {ready=true,actors={{realized=true,id=6656,x=0,y=0,z=0,yaw=0}}} end
  if text:find('MGS_COOP_CARGO_V2',1,true) then
   calls[#calls+1]=text
   if type(physical)~='table' then return physical end
   physical.role=role;return physical
  end
 end
 local env=setmetatable({e={MgsCoopWorld=W},vars={playerPosX=0,playerPosY=0,playerPosZ=0},
  PlayerInfo={OrCheckStatus=function(s)return s[1]==1 and mounted or false end},PlayerStatus={ON_HORSE=1,ON_VEHICLE=2},
  PlayerPad={STICK_L=1},PlayerStance={STAND=1,SQUAT=2},Player={
   SetPadMask=function(q)masks[#masks+1]={'set',q};return maskOk end,
   ResetPadMask=function(q)masks[#masks+1]={'reset',q};return true end,
   RequestToSetTargetStance=function()end},
  Tpp={IsVehicle=function()return false end},TppUiCommand={AnnounceLogView=function()end},
  GameObject={SendCommand=function(a,b)calls[#calls+1]=b;return true end}}, {__index=_G})
 local chunk=assert(loadfile(arg[1]));setfenv(chunk,env);chunk()
 local C=W.horseCargo
 if not disabled then assert(C.setTesting(true)) end
 local function tick(t,refresh)
  now=t
  if refresh~=false then for i=1,2 do D.records[i].at=now end end
  W.tick()
 end
 tick(now)
 return {W=W,C=C,D=D,env=env,tx=tx,calls=calls,masks=masks,logs=logs,tick=tick,
  recv=function(v)W.handlers[43](v) end,setRole=function(v)role=v end,
  setMounted=function(v)mounted=v end,setTx=function(v)txOk=v end,setMask=function(v)maskOk=v end,
  setPhysical=function(v)physical=v end,
  time=function(v)now=v end}
end
local function packet(seq,state,x,y,z,yaw)return {seq,state,x or 0,y or 1550,z or -550,yaw or 0} end
local f=fixture()
check(type(f.C)=='table' and f.W.OP.CARGO==43,'cargo binds its existing op')
f.recv(packet(1,2));check(f.C.offer and f.C.offer.at==1000,'valid state2 offer reaches the receiver')
f.time(1100);f.recv(packet(1,2,10));check(f.C.offer.at==1000,'duplicate offer cannot renew its expiry')
f.recv(packet(65535,2,10));check(f.C.offer.x==0,'older sequence is ignored')
f.recv(packet(2,4));check(f.C.rxSeq==1,'undefined state is ignored without consuming sequence')
f.recv(packet(2.5,2));check(f.C.rxSeq==1,'fractional sequence is ignored')
f.recv(packet(2,2,1000000001));check(f.C.rxSeq==1,'out of range position is ignored')
f.recv(packet(2,2,0/0));check(f.C.rxSeq==1,'NaN position is ignored')
f.C.rxSeq=65535;f.recv(packet(1,2,250));check(f.C.offer.x==.25,'sequence wraps at 65535')
f.W.paired=false;f.recv(packet(2,2,500));check(f.C.offer.x==.25,'unpaired packet cannot renew a cached offer')
f.tick(1200);check(f.C.offer==nil and f.C.state=='off','unpair clears even an idle offer')

f=fixture();f.setMounted(true);f.tick(1100)
check(f.tx[#f.tx][3]==2 and f.C.sentOffer,'mounted owner publishes state2 with an actual sent-offer lease')
f.recv(packet(1,3,20000));check(f.C.state=='off','request must match the point the owner just offered')
f.recv(packet(2,3));check(f.C.state=='carrier' and f.C.awake==true,'fresh nearby awake request starts transport')
check(f.C.ownerGeneration==22,'owner binds the current teammate generation')
f.tick(1150);check(f.C.state=='carrier','same driver and teammate remain active')
f.setMounted(false);f.tick(1200)
check(f.C.state=='off','awake carrier transport ends when driver dismounts')
check(f.tx[#f.tx][3]==0,'dismount sends an end packet')
f.recv(packet(2,3));check(f.C.state=='off','duplicate request cannot restart a stopped ride')

f=fixture();f.setMounted(true);f.tick(1100);f.time(2200)
f.recv(packet(1,3));check(f.C.state=='off','expired sent offer cannot admit a rider')
f=fixture();f.setMounted(true);f.setTx(false);f.tick(1100)
check(f.C.sentOffer==nil,'failed offer transmission does not issue an admission lease')
f.recv(packet(1,3));check(f.C.state=='off','request is refused if no offer was sent')
f=fixture();f.setMounted(true);f.tick(1100);f.D.records[2].pos={20000,0,0}
f.recv(packet(1,3));check(f.C.state=='off','current teammate must actually be nearby')

local function boardAwake()
 local g=fixture();g.recv(packet(1,2));g.D.rawCarryHeld=true
 g.D.rawCarryHeldAt=1100;g.tick(1100)
 g.D.rawCarryHeldAt=1900;g.tick(1900)
 g.D.rawCarryHeldAt=2000;g.tick(2000)
 check(g.C.requestAt==2000 and g.tx[#g.tx][3]==3,'holding R sends state3 once')
 g.recv(packet(2,1))
 check(g.C.state=='cargo' and g.C.awake and g.C.maskOwned,'acknowledged awake passenger owns one movement mask')
 return g
end
f=boardAwake();local start=f.C.startAt
f.time(2150);f.recv(packet(2,1));check(f.C.startAt==start,'duplicate active point cannot reset hop-off grace')
check(f.C.status():find('attachment=offset',1,true)~=nil,'debug state explicitly reports offset rather than native seat proof')
f.tick(2200);check(f.C.state=='cargo','fresh active point moves the passenger')
f.tick(3800);check(f.C.state=='off' and not f.C.maskOwned,'stale point releases transport and movement mask')
check(f.masks[#f.masks][1]=='reset','stream loss resets the owned mask')

f=boardAwake();local txBefore=#f.tx;f.recv(packet(3,0))
check(f.C.state=='off' and not f.C.maskOwned,'peer end releases mask and transport')
check(#f.tx==txBefore,'peer end does not create reliable stop ping-pong')
f=boardAwake();f.W.epoch=8;f.tick(2150)
check(f.C.state=='off' and not f.C.maskOwned and not f.C.offer and not f.C.requestAt,'epoch replacement clears attachment, mask and offer lease')
check(f.C.epoch==8,'next context is newly bound')
f=boardAwake();f.D.records[1].down=true;f.tick(2150)
check(f.C.state=='off' and not f.C.maskOwned,'newly downed passenger relinquishes awake movement mask')
f=boardAwake();f.D.records[1].generation=12;f.tick(2150)
check(f.C.state=='off' and not f.C.maskOwned,'body generation replacement cannot continue old attachment')
f=boardAwake();f.C.tick=function()error('fixture fault')end;f.tick(2150)
check(f.C.state=='off' and not f.C.maskOwned,'tick exception runs full transport cleanup')
f=boardAwake();f.W.startHooks[1]()
check(f.C.state=='off' and not f.C.maskOwned and f.C.rxSeq==nil,'mission hook resets state, mask and sequence history')

f=fixture();f.D.records[1].down=true;f.D.records[1].cause=2;f.D.sleepTimers[1]={at=100,generation=11}
f.recv(packet(1,1));check(f.C.state=='cargo' and not f.C.awake,'fresh sleeping teammate may receive cargo without awake request')
f.tick(1200);check(f.D.sleepTimers[1].at==100,'cargo never postpones host-owned sleep deadline')
f.D.records[1].down=false;f.tick(1250);check(f.C.state=='off','natural wake exits sleeping cargo')

f=fixture();f.recv(packet(1,2));f.C.requestAt=1000;f.setMask(false);f.recv(packet(2,1))
check(f.C.state=='off' and not f.C.maskOwned,'refused pad mask prevents awake passenger admission')
f=fixture();f.recv(packet(1,2));f.C.requestAt=1000;f.recv(packet(2,1,20000))
check(f.C.state=='off','first active point cannot teleport passenger from far away')
f=fixture();f.C.offer={x=0,z=0,y=0,at=1100};f.D.rawCarryHeld=true;f.D.rawCarryHeldAt=2000;f.time(1900)
f.tick(1900);check(f.C.hopSince==nil,'future input timestamp cannot count as a current held input')
f=fixture();f.setRole(1);f.D.records[2].down=true;f.D.rawCarryHeld=true;f.D.rawHeld=false
f.D.rawCarryHeldAt=1100;f.tick(1100);f.D.rawCarryHeldAt=1600;f.tick(1600)
check(f.C.state=='off' and f.C.heldSince==nil,'R pickup input cannot masquerade as unheld E loading input')
local function loading()
 local g=fixture();g.setRole(1);g.D.records[2].down=true;g.D.rawHeld=true
 g.D.rawHeldAt=1100;g.tick(1100);g.D.rawHeldAt=1600;g.tick(1600)
 check(g.C.state=='loading','stock local carrier may request put-down near the horse')
 g.setRole(0);return g
end
f=loading();f.setPhysical({ready=true,carrying=true,carried=true,localCarried=false,peerCarrying=false});f.tick(1650)
check(f.C.state=='loading','logical carry role clearing does not imply actual native detach')
f.setPhysical({ready=true,carrying=false,carried=true,localCarried=false,peerCarrying=false});f.tick(1700)
check(f.C.state=='loading','still-carried target delays offset cargo')
f.setPhysical({ready=true,carrying=true,carried=false,localCarried=false,peerCarrying=false});f.tick(1750)
check(f.C.state=='loading','still-carrying local controller delays offset cargo')
f.setPhysical({ready=false,carrying=false,carried=false,localCarried=false,peerCarrying=false});f.tick(1800)
check(f.C.state=='loading','unknown native physical state cannot be treated as detached')
f.setPhysical(0);f.tick(1850)
check(f.C.state=='loading','legacy numeric role reply cannot prove physical detach')
f.setPhysical({ready=true,carrying=false,carried=false,localCarried=false,peerCarrying=false});f.tick(1900)
check(f.C.state=='carrier','only coherent native both-clear admits offset cargo after drop')
f=loading();f.setPhysical(false);f.tick(8000)
check(f.C.state=='off','unknown put-down receipt remains bounded by load timeout')
print('world_horse_cargo_lua_check: '..checks..' checks passed; no game process')

local off=fixture(true)
off.setMounted(true);off.tick(1100);off.recv(packet(1,2))
check(not off.C.testing and #off.tx==0 and not off.C.offer,"normal play never advertises or accepts offset transport")
off.C.setTesting(true);off.tick(1200)
check(off.tx[#off.tx][3]==2,"explicit test opt-in admits the provisional offset protocol")
off.C.setTesting(false)
check(off.C.state=="off" and not off.C.maskOwned and not off.C.testing,"test disable retires owned transport and masks")
