-- Detached fixtures for normal-release stock horse tests; no game or saves.
local helper,menu=assert(arg[1]),assert(arg[2])
local checks=0
local function check(v,why)checks=checks+1;if not v then error(checks..": "..why,2)end end
local function slurp(path)local f=assert(io.open(path,"rb"));local s=f:read("*a");f:close();return s end
local source=slurp(helper)
check(#source:gsub("\r\n","\n")<=16000 and #source:gsub("\r?\n","\r\n")<=16000,"literal under limit in both checkout modes")
check(not source:find("SendCommand",1,true) and not source:find("GetGameObjectIdByIndex",1,true) and not source:find("GetGameObjectIdIsRiddenToLocal",1,true),"no per-instance, unsafe capacity or unqualified rider call")
local function F()
 local c={calls=0,reads=0,kind=2,exists=false,mounted=false,time=1000,logs={},countReads=0}
 local env=setmetatable({},{__index=_G})
 local function vec(x,y,z)
  local p={x=x,y=y,z=z};function p:GetX()return self.x end;function p:GetY()return self.y end;function p:GetZ()return self.z end;return p
 end
 env.PlayerStatus={ON_HORSE=8}
 env.PlayerInfo={OrCheckStatus=function(a)check(#a==1 and a[1]==8,"only native horse posture is read");c.reads=c.reads+1;return c.mounted end}
 env.GameObject={DoesGameObjectExistWithTypeName=function(t)check(t=="TppHorse2" or t=="TppWalkerGear2","exact horse / walker class existence query");c.reads=c.reads+1;if t=="TppWalkerGear2" then return c.walkerExists==true end;return c.exists end}
 local B={s={}}
 B.guard=function(active)c.guards=(c.guards or 0)+1;check(active==true,"public actions use existing live buddy guard");if c.guardError then error("fixture")end;return not c.refuse,c.refuse end
 local W={active=true,mission=30010,api={},buddy=B,now=function()return c.time end}
 W.api.buddyHorse=1
 W.api.buddyWalker=4
 W.api.buddyId=function(t,i)
  if t=="TppWalkerGear2" then check(i==0 and c.kind==4 and c.walkerExists==true,"walker actor-zero lookup only after typed native availability");return c.walkerActor0 or 22528 end
  check(t=="TppHorse2"and i==0 and c.kind==1 and c.exists==true,"actor-zero lookup only after typed native availability");c.idReads=(c.idReads or 0)+1;return c.actor0 or 6656 end
 W.api.buddyType=function()c.reads=c.reads+1;return c.kind end
 W.api.buddyVector=function(x,y,z)if c.vectorError then error("fixture")end;if c.badVector then return vec(x+1,y,z)end;return vec(x,y,z)end
 W.api.buddyCall=function(t,p,yaw)
  check(t==(c.expectType or 1),"exact HORSE enum1 (WALKER_GEAR 4 for walker_spawn)");c.calls=c.calls+1;c.args={t,p,yaw}
  if c.callError then error("fixture")end
  if c.callResult==false then return false end
  if c.immediate then c.kind,c.exists=1,true end
  if c.forceNil then return end
  return c.callResult==nil and true or c.callResult
 end
 local D={mission=30010}
 D.api={get=function(t,k)return t and t[k]end,call=function(t,k,...)return assert(t[k])(...)end,
  integer=function(n,l,h)return type(n)=="number"and n==math.floor(n)and n>=l and n<=h end,
  finite=function(n)return type(n)=="number"and n==n and math.abs(n)<1e15 end,
  pose=function()if c.poseError then error("fixture")end;return c.pose or {10,20,30,-60}end,
  note=function(id,ok,message)c.logs[#c.logs+1]={id,ok,message}end}
 env.e={MgsCoopDebug=D,MgsCoopWorld=W}
 setfenv(assert(loadstring(source)),env)()
 c.W,c.B,c.D,c.env,c.S=W,B,D,env,D.buddy
 function c:run(cmd)return self.S.run({cmd},17)end
 function c:poll(n)
  for i=1,n or 1 do local q=self.D.pending;if q then q.frames=q.frames+1;q.poll()end end
 end
 return c
end
local c=F();local ok,msg=c:run("horse_status")
check(ok and msg=="horse;selected=dog;available=0;actor0=65535;localMounted=0;nativeCount=unverified;remoteRider=unverified;arrival=unverified;retry=Recall" and c.calls==0,"stock status is read-only and counts are not invented")
c=F();c.kind,c.exists,c.mounted=1,true,true
ok,msg=c:run("horse_status");check(ok and msg:find("localMounted=1",1,true)and msg:find("actor0=6656",1,true)and msg:find("arrival=mounted;retry=none",1,true)and #msg<=160 and c.calls==0,"mounted posture status requires actual horse-zero ID and is read-only")
ok,msg=c:run("horse_spawn");check(not ok and msg=="horse_already_mounted"and c.calls==0,"current ridden horse is never replaced")
c=F();ok,msg=c:run("horse_spawn")
check(ok and msg=="submitted_horse;scope=local;stock_loader;arrival=unverified;close_F9;mount=normal_action"and c.calls==1 and c.D.pending and c.D.pending.kind=="spawn","one stock selection request; existing pending enum; no force mounting")
local p,yaw=c.args[2],c.args[3]
check(math.abs(p:GetX()-12)<1e-8 and p:GetY()==20 and math.abs(p:GetZ()-(30+2*math.sqrt(3)))<1e-8 and math.abs(yaw+math.pi/3)<1e-8,"four metres to right, actual Vector3 and radians")
c:poll(10);check(c.D.pending and c.calls==1,"loading wait never repeats selector")
c.kind,c.exists=1,true;c:poll()
check(not c.D.pending and c.logs[#c.logs][2]and c.logs[#c.logs][3]:find("horse_selected;",1,true)and c.logs[#c.logs][3]:find("arrival=unverified;retry=Recall",1,true) and #c.logs[#c.logs][3]<=160,"selected type is not nearby arrival; retry remains visible within receipt limit")
ok,msg=c:run("horse_spawn");check(not ok and msg=="horse_cooldown"and c.calls==1,"repeated click bounded")
c.time=3000;ok,msg=c:run("horse_spawn");check(ok and c.calls==2 and msg:find("submitted_horse_recall;",1,true),"cooldown boundary recalls the selected horse through the same stock loader")
c=F();c:run("horse_spawn");c:poll(600)
check(not c.D.pending and not c.logs[#c.logs][2]and c.logs[#c.logs][3]:find("horse_load_timeout",1,true)and c.calls==1,"bounded native-loading timeout")
c=F();c:run("horse_spawn");c.refuse="buddy_block_inactive";c:poll(50)
check(c.D.pending and #c.logs==0 and c.calls==1,"expected stock block swap waits safely")
c.refuse=nil;c.kind,c.exists=1,true;c:poll();check(not c.D.pending and c.logs[1][2],"allocation after block reload finishes")
c=F();c:run("horse_spawn");c.refuse="world_inactive";c:poll()
check(not c.D.pending and not c.logs[1][2]and c.logs[1][3]=="world_inactive","world retirement aborts without another native call")
c=F();c:run("horse_spawn");c.env.e.MgsCoopWorld={};c:poll()
check(not c.D.pending and not c.logs[1][2]and c.calls==1,"world replacement never acts on cached owner")
for _,why in ipairs({"buddy_block_inactive","not_in_game","demo_active","mission_mismatch","world_inactive"})do
 c=F();c.refuse=why;ok,msg=c:run("horse_spawn")
 check(not ok and msg==why and c.calls==0 and c.reads==0,"refused guard has no reads or mutations "..why)
end
for _,fault in ipairs({"binding","vectorError","badVector","clock","downed","cleanup"})do
 c=F()
 if fault=="binding"then c.W.api.buddyHorse=3 elseif fault=="clock"then c.time=0/0
 elseif fault=="downed"then c.W.downed={maskOwned=true} elseif fault=="cleanup"then c.W.downed={nativePending=true}
 else c[fault]=true end
 ok,msg=c:run("horse_spawn");check(not ok and c.calls==0 and not c.D.pending,"invalid preparation cannot call horse "..fault)
end
for _,fault in ipairs({"kind","availability","posture","actor"})do
 c=F();if fault=="kind"then c.kind=9 elseif fault=="availability"then c.exists=1 elseif fault=="actor"then c.kind,c.exists,c.actor0=1,true,6657 else c.mounted=0 end
 ok,msg=c:run("horse_status");check(not ok and c.calls==0,"malformed registered result refuses "..fault)
end
for _,accepted in ipairs({true,false})do
 c=F();c.B.s.owned=true
 c.B.yieldForRide=function()c.yields=(c.yields or 0)+1;c.B.s.owned=false;return true end
 c.B.keepRideYield=function()c.keeps=(c.keeps or 0)+1;return true end
 c.B.resumeFollow=function()c.resumes=(c.resumes or 0)+1;c.B.s.owned=true;return true end
 c.callResult=accepted;ok,msg=c:run("horse_spawn")
 check(c.yields==1 and c.calls==1,"borrowed host dog yields selector once")
 check(accepted and ok and c.keeps==1 and not c.resumes or not accepted and not ok and c.resumes==1 and c.B.s.owned,"selector success keeps yield; refusal restores borrow")
end
for _,bad in ipairs({0,"true",{}})do
 c=F();c.callResult=bad;ok,msg=c:run("horse_spawn")
 check(not ok and msg=="horse_call_refused" and c.calls==1 and not c.D.pending,"non-boolean loader results never establish acceptance")
end
c=F();c.forceNil=true;ok,msg=c:run("horse_spawn")
check(not ok and msg=="horse_call_refused" and c.calls==1 and not c.D.pending,"nil is not the retail bool receipt")
c=F();c.B.s.owned=true;ok,msg=c:run("horse_spawn")
check(not ok and msg=="dog_borrowed"and c.calls==0,"old companion build without yield safely refuses")
c=F();c.B.s.owned=true;c.B.yieldForRide=function()return true end;ok,msg=c:run("horse_spawn")
check(not ok and msg=="dog_borrowed"and c.calls==0,"still-owned dog cannot be clobbered")
c=F();ok,msg=c.S.run({"horse_spawn","2"},1);check(not ok and msg=="horse_args"and c.calls==0,"no count or arbitrary parameters accepted")
-- D-Walker lab lever (docs/mounts.md): the same stock selector with WALKER_GEAR, read-only status, no mount.
c=F();ok,msg=c:run("walker_status")
check(ok and msg=="walker;selected=dog;available=0;actor0=65535;arrival=unverified" and c.calls==0,"walker status is read-only")
c=F();c.expectType=4;ok,msg=c:run("walker_spawn")
check(ok and msg=="submitted_walker;scope=local;stock_loader;arrival=unverified;mount=normal_action" and c.calls==1 and c.args[1]==4 and
 c.D.pending and c.D.pending.kind=="spawn","one stock WALKER_GEAR selection request")
p,yaw=c.args[2],c.args[3]
check(math.abs(p:GetX()-12)<1e-8 and p:GetY()==20 and math.abs(p:GetZ()-(30+2*math.sqrt(3)))<1e-8 and math.abs(yaw+math.pi/3)<1e-8,"walker four metres to the right")
c:poll(10);check(c.D.pending and c.calls==1,"walker loading wait never repeats the selector")
c.kind,c.walkerExists=4,true;c:poll()
check(not c.D.pending and c.logs[#c.logs][2] and c.logs[#c.logs][3]=="walker_selected;walker;selected=walker;available=1;actor0=22528;arrival=unverified","walker selected, arrival unverified")
ok,msg=c:run("walker_spawn");check(not ok and msg=="walker_cooldown" and c.calls==1,"walker click bounded")
c=F();c.expectType=4;c:run("walker_spawn");c:poll(600)
check(not c.D.pending and not c.logs[#c.logs][2] and c.logs[#c.logs][3]:find("walker_load_timeout",1,true) and c.calls==1,"bounded walker loading timeout")
c=F();c.refuse="buddy_block_inactive";ok,msg=c:run("walker_spawn")
check(not ok and msg=="walker_block_inactive" and c.calls==0 and c.reads==0,"no walker deploy without an active buddy block")
c=F();c.kind,c.walkerExists,c.walkerActor0=4,true,22529;ok,msg=c:run("walker_status")
check(not ok and msg=="walker_actor0;id=22529" and c.calls==0,"an unexpected walker actor refuses")
c=F();c.expectType=4;c.B.s.owned=true;ok,msg=c:run("walker_spawn")
check(not ok and msg=="dog_borrowed" and c.calls==0,"walker: a borrowed dog without yield refuses")
c=F();ok,msg=c.S.run({"walker_spawn","2"},1);check(not ok and msg=="horse_args" and c.calls==0,"walker: no parameters accepted")
local raw=slurp(menu)
check(raw:find('{"player", "spawn", "buddy", "vehicle"}',1,true)and raw:find('a[1] == "horse_status"',1,true),"normal debug route and read-only mounted exception composed")
check(raw:find('{"enemy", "player", "spawn", "buddy", "vehicle"}',1,true),"stop clears new helper metadata")
print("debug_buddy_lua_check: "..checks.." checks PASS")
