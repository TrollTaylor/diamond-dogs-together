-- Original isolated recording fixture. No game/launch/network/save access.
local file=assert(arg[1],"scratch world_buddy_ride.lua required")
local checks=0
local function check(v,s)checks=checks+1;if not v then error(checks..": "..s,2)end end
local function vec(x,y,z)return{GetX=function()return x end,GetY=function()return y end,GetZ=function()return z end}end
local function F(k)
 local c={kind=k or 1,time=1000,generation=9,cycle=2,active=true,block=true,demo=false,admitted=true,commands=true,allocated=2,realized=2,mask=3,calls={},nativeN=0,rider=65535,pending=65535,state=0}
 local env=setmetatable({e={},vars={missionCode=30010,playerPosX=100,playerPosY=348.25,playerPosZ=-7.75,playerRotY=45}},{__index=_G})
 local W={mission=30010,epoch=42,active=true,api={},startHooks={},stopHooks={}};env.e.MgsCoopWorld=W
 W.buddy={s={},guard=function(active)
  check(active==true,"every public read requires active buddy context")
  if not W.active or not c.active then return false,"world_inactive"end
  if c.demo then return false,"demo_active"end
  if not c.block then return false,"buddy_block_inactive"end
  if env.vars.missionCode~=W.mission then return false,"mission_mismatch"end
  return true
 end}
 W.get=function(t,key)return t and t[key]end;W.now=function()return c.time end
 W.api={buddyDog=2,buddyHorse=1,buddyWalker=4,buddyNone=0,
  buddyType=function()c.calls[#c.calls+1]={name="kind"};if c.kindError then error("fixture kind")end;return c.kind==2 and 4 or c.kind==1 and 1 or c.kind==3 and 2 or 0 end,
  buddyVector=vec,
  buddyId=function(class,index)
   check(c.allocated==2 and c.realized==2 and c.mask==3,"index1 only after actual allocated and realized2")
   check(index==1 and class==(c.kind==2 and "TppWalkerGear2"or"TppHorse2"),"exact current class index1 only")
   c.calls[#c.calls+1]={name="id",class=class,index=index}
   if c.onId then c.onId()end
   return c.id or(c.kind==2 and 22529 or 6657)
  end,
  buddyCall=function(which,pos,yaw)
   c.calls[#c.calls+1]={name="CallBuddy",kind=which,pos=pos,yaw=yaw}
   if c.callError then error("fixture selection")end
   return c.callResult
  end,
  send=function(id,cmd)
   c.calls[#c.calls+1]={name=cmd.id,id=id,cmd=cmd}
   if c.sendError then error("fixture send")end
   if cmd.id=="GetMaxInstanceCount"then check(c.kind==2 and type(id)=="table"and id.type=="TppWalkerGear2","class count only qualified Walker");return c.classCount or c.allocated end
   check(c.allocated==2 and c.realized==2 and c.mask==3,"per-instance command only realized2")
   local secondId=c.kind==2 and 22529 or 6657
   local firstId=c.kind==2 and 22528 or 6656
   check(id==secondId or (c.allowAssigned and (id==firstId or id==secondId)),"only the assigned realized body is touched")
   if cmd.id=="GetPosition"then check(c.kind==2,"no guessed Horse GetPosition");return c.position or vec(104,348.25,-7.75)end
   if cmd.id=="IsPlayerRiding"then check(c.kind==2,"no guessed Horse riding query");if c.riding~=nil then return c.riding end;return c.rider~=65535 and c.state==1 end
   if cmd.id=="SetCallHorse"then
    check(c.kind==1 and cmd.startPosition==cmd.goalPosition and cmd.pos==nil,"exact proven Horse fields")
   elseif cmd.id=="SetPosition"then check(c.kind==2 and cmd.pos and cmd.rotY and cmd.position==nil,"exact proven Walker fields")
   else error("unqualified command "..tostring(cmd.id))end
   if c.onSend then c.onSend(cmd)end
   return c.sendResult
  end}
 W.receipt=function(source)
  local mission,epoch,kind=source:match("^return 'MGS_COOP_BUDDY_CAPACITY_V1' %-%- (%d+) (%d+) (%d+)$")
  check(mission and tonumber(mission)==W.mission and tonumber(epoch)==W.epoch and tonumber(kind)>=0 and tonumber(kind)<=2,"fixed three-uint receipt contains no owner/pointer/count input")
  local k=tonumber(kind);c.nativeN=c.nativeN+1;c.calls[#c.calls+1]={name="receipt",kind=k}
  if c.receiptError then error("fixture receipt")end
  local r={ready=true,admitted=c.admitted,commands=c.commands,profile="buddy_capacity_two_v1",mission=W.mission,epoch=W.epoch,kind=k,generation=c.generation,cycle=c.cycle}
  if k~=0 then
   r.allocated,r.realized,r.mask,r.actors=c.allocated,c.realized,c.mask,{}
   for i=1,c.allocated do
    r.actors[i]={id=(k==2 and 22528 or 6656)+i-1,rider=i==2 and c.rider or 65535,occupied=i==2 and(c.occupied~=nil and c.occupied or(c.rider~=65535 and c.state==1))or false,realized=math.floor(c.mask/2^(i-1))%2==1}
    if i==2 and c.occupied~=nil then r.actors[i].occupied=c.occupied end
    if k==1 then r.actors[i].pending=i==2 and c.pending or 65535 else r.actors[i].state=i==2 and c.state or 0 end
   end
  end
  if c.corrupt then local altered=c.corrupt(r,c.nativeN);if altered~=nil then return altered end end
  return r
 end
 setfenv(assert(loadfile(file)),env)();c.W,c.R,c.env=W,W.buddyRide,env
 function c:count(name)local n=0;for _,v in ipairs(self.calls)do if v.name==name then n=n+1 end end;return n end
 function c:mutations()return self:count("CallBuddy")+self:count("SetPosition")+self:count("SetCallHorse")end
 function c:restart()for _,f in ipairs(W.startHooks)do f()end end
 return c
end
local h=F(1);check(h.R.census():find("id6657",1,true),"Horse reports distinct actual second id")
check(h:count("GetPosition")==0 and h:count("GetMaxInstanceCount")==0 and h:mutations()==0,"Horse census does not issue unsupported commands or mutate")
check(h.R.placeSecond()=="horse_place_submitted","qualified unused second Horse placement submits")
local cmd=h.calls[#h.calls].cmd
check(cmd.startPosition:GetX()==104 and cmd.startPosition:GetY()==348.25 and cmd.startPosition:GetZ()==-7.75,"fixed4m position preserves y/z terrain")
check(h.R.placeSecond()=="place_cooldown"and h:count("SetCallHorse")==1,"Horse requests bounded2s")
h.time=3000;check(h.R.placeSecond()=="horse_place_submitted"and h:count("SetCallHorse")==2,"inclusive placement cooldown")
local w=F(2);check(w.R.census():find("id22529",1,true),"Walker distinct actual second id")
check(w:count("GetPosition")==1 and w:count("GetMaxInstanceCount")==1 and w:count("IsPlayerRiding")==1 and w:mutations()==0,"Walker census uses proven read commands only")
check(w.R.placeSecond()=="walker_place_submitted","qualified unused second Walker placement submits")
cmd=w.calls[#w.calls].cmd;check(cmd.pos:GetY()==348.25 and math.abs(cmd.rotY-math.pi/4)<0.000001,"Walker uses exact Vector pos and radians")
for _,kind in ipairs({1,2})do
 for _,fault in ipairs({"inactive","demo","block","mission","missing_api","missing_guard","guard_error","missing_receipt"})do
  local c=F(kind)
  if fault=="inactive"then c.W.active=false elseif fault=="demo"then c.demo=true elseif fault=="block"then c.block=false
  elseif fault=="mission"then c.env.vars.missionCode=10020 elseif fault=="missing_api"then c.W.api=nil elseif fault=="missing_guard"then c.W.buddy.guard=nil
  elseif fault=="guard_error"then c.W.buddy.guard=function()error("fixture guard")end else c.W.receipt=nil end
  local ok,result=pcall(c.R.census);check(ok and type(result)=="string","pre-bind census safely refuses "..fault)
  ok,result=pcall(c.R.placeSecond);check(ok and type(result)=="string","pre-bind placement safely refuses "..fault)
  check(c:count("kind")==0 and c:count("receipt")==0 and c:mutations()==0,"no native buddy access before guard "..fault)
 end
 local c=F(kind);c.W.api.buddyType=nil;check(c.R.census()=="select_ride_first"and c:mutations()==0,"missing kind callable safely refuses")
 c=F(kind);c.kindError=true;check(c.R.placeSecond()=="select_ride_first"and c:mutations()==0,"throwing kind safely refuses")
 for _,fault in ipairs({"not_admitted","bad_profile","bad_generation","bad_cycle","wrong_kind","wrong_mission","wrong_epoch","bad_mask","bad_count","bad_realized","bad_id","bad_actor_type","bad_rider","bad_occupied","missing_commands","missing_actor","receipt_error"})do
  c=F(kind)
  if fault=="receipt_error"then c.receiptError=true else c.corrupt=function(r)
   if fault=="not_admitted"then r.admitted=false elseif fault=="bad_profile"then r.profile="normal"elseif fault=="bad_generation"then r.generation=0
   elseif fault=="bad_cycle"then r.cycle=0 elseif fault=="wrong_kind"then r.kind=0 elseif fault=="wrong_mission"then r.mission=30020
   elseif fault=="wrong_epoch"then r.epoch=43 elseif fault=="bad_mask"then r.mask=4 elseif fault=="bad_count"then r.allocated=3
   elseif fault=="bad_realized"then r.realized=1 elseif fault=="bad_id"then r.actors[2].id=6656 elseif fault=="bad_actor_type"then r.actors[2]=false
   elseif fault=="bad_rider"then r.actors[2].rider=65536 elseif fault=="bad_occupied"then r.actors[2].occupied=1
   elseif fault=="missing_commands"then r.commands=nil else r.actors[2]=nil end
   return r end end
  check(c.R.placeSecond()~=(kind==1 and"horse_place_submitted"or"walker_place_submitted"),"invalid receipt refused "..fault)
  check(c:count("id")==0 and c:mutations()==0,"invalid receipt never queries index1 or mutates "..fault)
 end
 c=F(kind);c.allocated=1;c.realized=1;c.mask=1;check(c.R.census():find("allocated1",1,true),"singleton actual capacity reported")
 check(c.R.placeSecond()=="second_unrealized"and c:count("id")==0 and c:mutations()==0,"singleton never queried as second")
 c=F(kind);c.realized=1;c.mask=1;check(c.R.placeSecond()=="second_unrealized"and c:count("id")==0 and c:mutations()==0,"allocated-but-unrealized second untouched")
 for _,fault in ipairs({"generation","cycle","count","rider","commands"})do
  c=F(kind);c.corrupt=function(r,n)if n>=2 then if fault=="generation"then r.generation=10 elseif fault=="cycle"then r.cycle=3 elseif fault=="count"then r.allocated=1;r.realized=1;r.mask=1;r.actors[2]=nil elseif fault=="rider"then r.actors[2].rider=1 else r.commands=false end end;return r end
  check(c.R.placeSecond()~=(kind==1 and"horse_place_submitted"or"walker_place_submitted")and c:mutations()==0,"changing natural owner refuses "..fault)
 end
 c=F(kind);c.id=65535;check(c.R.placeSecond()=="second_id"and c:mutations()==0,"native declaration not enough when public id unavailable")
 c=F(kind);c.onId=function()c.cycle=c.cycle+1 end;check(c.R.placeSecond()=="owner_changed"and c:mutations()==0,"owner rechecked after id read")
 c=F(kind);c.rider=1;check(c.R.placeSecond()=="second_busy"and c:mutations()==0,"actual rider protects second")
 c=F(kind);c.commands=false;check(type(c.R.census())=="string"and c:mutations()==0,"read-only census still admitted without command permission")
 check(c.R.placeSecond()=="command_admission"and c:mutations()==0,"read census admission cannot authorize mutation")
 for _,fault in ipairs({"nan_pos","huge_pos","get_error","missing_get","vector_error","vector_nil","vector_unreadable","bad_vector","missing_send","clock_error","missing_clock"})do
  c=F(kind)
  if fault=="nan_pos"then c.env.vars.playerPosX=0/0 elseif fault=="huge_pos"then c.env.vars.playerPosX=1000000
  elseif fault=="get_error"then c.W.get=function()error("fixture get")end elseif fault=="missing_get"then c.W.get=nil
  elseif fault=="vector_error"then c.W.api.buddyVector=function()error("fixture vector")end elseif fault=="vector_nil"then c.W.api.buddyVector=function()return nil end
  elseif fault=="vector_unreadable"then c.W.api.buddyVector=function()return{}end elseif fault=="bad_vector"then c.W.api.buddyVector=function()return vec(0,0,0)end
  elseif fault=="missing_send"then c.W.api.send=nil elseif fault=="clock_error"then c.W.now=function()error("fixture clock")end else c.W.now=nil end
  local ok,result=pcall(c.R.placeSecond);check(ok and type(result)=="string"and c:mutations()==0,"invalid local binding/pose refuses "..fault)
 end
 c=F(kind);c.sendResult=false;check(c.R.placeSecond()=="place_refused"and c:mutations()==1,"stock explicit refusal reported as refusal")
 c=F(kind);c.sendError=true;check(c.R.placeSecond()=="place_refused"and c:mutations()==1,"stock exception contained")
 c.R.last=string.rep("x",999);check(#c.R.status()==160,"status bounded160")
 for _,f in ipairs(c.W.stopHooks)do f()end;check(c.R.status()=="stopped"and c.R.nextPlace==0,"stop retires pending cooldown")
 c:restart();check(c.R.status()=="idle"and c.R.nextSelect==0,"fresh mission resets experiment status")
end
for _,state in ipairs({1,2,255})do local c=F(2);c.state=state;check(c.R.placeSecond()=="second_busy"and c:mutations()==0,"nonzero Walker state prevents placement without invented pending")end
local c=F(2);c.state=1;c.rider=1;c.occupied=false;check(c.R.placeSecond()=="bad_actor"and c:count("id")==0,"Walker occupied inconsistent with actual rider/state refused")
c=F(1);c.pending=1;check(c.R.placeSecond()=="second_busy"and c:mutations()==0,"pending Horse rider prevents placement")
for _,which in ipairs({"dog","horse","walker","none"})do
 c=F(1);check(c.R.select(which)=="selection_submitted_"..which,"fixed stock selection allowed "..which)
 cmd=c.calls[#c.calls];check(cmd.name=="CallBuddy"and cmd.pos:GetY()==348.25 and math.abs(cmd.yaw-math.pi/4)<0.000001,"selection proper stock position/heading")
 check(c.R.select(which)=="selection_cooldown"and c:count("CallBuddy")==1,"selection bounded1s")
end
c=F();check(c.R.select("quiet")=="selection_invalid"and #c.calls==0,"unapproved fixed option refused")
c=F();c.W.buddy.s.owned=true;check(c.R.select("horse")=="dog_borrowed"and c:mutations()==0,"owned Dog without a selector yield is not clobbered")
c=F();c.W.buddy.s.owned=true;check(c.R.select("walker")=="dog_borrowed"and c:mutations()==0,"owned Dog without a selector yield is not clobbered by walker")
c=F();c.W.buddy.s.owned=true;check(c.R.select("dog")=="dog_borrowed"and c:mutations()==0 and c.W.buddy.s.owned,"dog reselection cannot replace borrowed follow")
c=F();c.W.buddy.s.owned=true;check(c.R.select("none")=="dog_borrowed"and c:mutations()==0,"dismiss cannot clear borrowed follow")
local function armYield(c)
 c.W.buddy.yieldForRide=function()
  c.yields=(c.yields or 0)+1
  if c.yieldRefuse then return false end
  if c.yieldError then error("yield") end
  c.W.buddy.s.owned=nil
  c.W.buddy.s.rideYield=true
  return true
 end
 c.W.buddy.resumeFollow=function()c.resumes=(c.resumes or 0)+1;c.W.buddy.s.owned=true;c.W.buddy.s.rideYield=nil;return true end
 c.W.buddy.keepRideYield=function()c.keeps=(c.keeps or 0)+1;return true end
end
c=F();c.W.buddy.s.owned=true;armYield(c)
check(c.R.select("horse")=="selection_submitted_horse" and c.yields==1 and c.keeps==1 and not c.resumes,"horse selection submits while dog follow is active")
check(not c.W.buddy.s.owned and c:count("CallBuddy")==1 and c.calls[#c.calls].kind==1,"horse call is the only selector mutation and does not dismiss the dog")
c=F();c.W.buddy.s.owned=true;armYield(c)
check(c.R.select("walker")=="selection_submitted_walker" and c:count("CallBuddy")==1 and c.calls[#c.calls].kind==4,"walker selection submits while dog follow is active")
c=F();c.W.buddy.s.owned=true;armYield(c);c.yieldRefuse=true
check(c.R.select("horse")=="dog_borrowed" and c:mutations()==0 and c.W.buddy.s.owned,"refused yield does not call the selector")
c=F();c.W.buddy.s.owned=true;armYield(c);c.yieldError=true
check(c.R.select("horse")=="dog_borrowed" and c:mutations()==0,"yield exception does not call the selector")
c=F();c.W.buddy.s.owned=true;c.W.buddy.yieldForRide=function()return true end
check(c.R.select("horse")=="dog_borrowed" and c:mutations()==0 and c.W.buddy.s.owned,"yield that keeps ownership cannot call")
c=F();c.W.buddy.s.owned=true;armYield(c);c.callResult=false
check(c.R.select("horse")=="selection_refused" and c.resumes==1 and not c.keeps and c.W.buddy.s.owned and c:count("CallBuddy")==1,"failed horse call restores follow ownership")
c=F();c.W.buddy.s.owned=true;armYield(c);c.callError=true
check(c.R.select("horse")=="selection_refused" and c.resumes==1 and c.W.buddy.s.owned,"horse call exception restores follow ownership")
c=F();c.commands=false;check(c.R.select("horse")=="command_admission"and c:mutations()==0,"selection requires distinct command admission")
c=F();c.corrupt=function(r,n)if n==2 then r.cycle=r.cycle+1 end;return r end;check(c.R.select("horse")=="owner_changed"and c:mutations()==0,"selection rechecks actual cycle")
c=F();c.callResult=false;check(c.R.select("horse")=="selection_refused","stock selector false preserved")
c=F();c.callError=true;check(c.R.select("horse")=="selection_refused","stock selector exception contained")
for _,case in ipairs({{"horse","buddyHorse",255},{"walker","buddyWalker",3},{"dog","buddyDog",1},{"none","buddyNone",2}})do
 c=F();c.W.api[case[2]]=case[3];check(c.R.select(case[1])=="selection_binding"and c:mutations()==0,"exact retail enum mismatch cannot call unrelated buddy "..case[1])
end
c=F();check(c.R.select("none")=="selection_submitted_none","remove selection permitted with active block")
c.block=false;c.time=2000;check(c.R.select("horse")=="buddy_block_inactive"and c:count("CallBuddy")==1,"inactive block after NONE safely needs normal menu bootstrap")
 c=F(1);c.corrupt=function(r)r.pack="03";return r end;check(c.R.census():find("pack03",1,true)and c:mutations()==0,"named retained horse pack is reported and does not mount")
c=F(1);c.corrupt=function(r)r.pack="../00";return r end;check(c.R.placeSecond()=="bad_pack"and c:count("id")==0 and c:mutations()==0,"unsafe pack token cannot authorize the second body")
c=F(2);c.corrupt=function(r)r.pack=1;return r end;check(c.R.census()=="bad_pack"and c:mutations()==0,"non-string pack token refused")
 c=F(2);c.classCount=1;check(c.R.census()=="walker_count_changed"and c:count("GetPosition")==0,"actual class count changed stops per-instance command")
c=F(2);c.position={};check(c.R.census()=="walker_position"and c:mutations()==0,"unreadable actual Walker position refused")
c=F(2);c.riding=true;check(c.R.census()=="walker_rider_changed"and c:mutations()==0,"public rider read disagreement refused")
local function rideEnv(kind,host,pack)
 local c=F(kind);c.W.host=host;c.allowAssigned=true
 c.corrupt=function(r) r.pack=pack or (kind==2 and "00" or "03");return r end
 return c
end
local h,g,err=rideEnv(1,true).R.assign(65535,65535,6656,6657)
check(h==0 and g==1 and err==nil,"unset riders put the host on actor0 and the guest on actor1")
h,g,err=rideEnv(1,true).R.assign(6657,65535,6656,6657)
check(h==1 and g==0 and err==nil,"an existing host ride id inverts the guest onto the other body")
h,g,err=rideEnv(1,true).R.assign(6656,6656,6656,6657)
check(h==nil and g==nil and err=="same_actor","both riders cannot resolve to the same actor")
h,g,err=rideEnv(1,true).R.assign(1,65535,6656,6657)
check(h==nil and err=="ride_id_unknown","an unrelated ride id is not a body")
local c=rideEnv(1,true)
check(c.R.ride()=="ride_ready_6656" and c.R.mounted==false and c:count("SetCallHorse")==1 and c.calls[#c.calls].id==6656,"two horse bodies admit the host's own actor")
c=rideEnv(1,false)
check(c.R.ride()=="ride_ready_6657" and c.calls[#c.calls].id==6657 and c.R.mounted==false,"two horse bodies admit the guest's own actor")
c=rideEnv(2,true)
local hostWalk=c
check(hostWalk.R.ride()=="ride_ready_22528" and hostWalk.R.mounted==false and hostWalk:count("SetPosition")==2,"two walker bodies admit the host's own actor")
local function placed(fx,id)
 for i=#fx.calls,1,-1 do if fx.calls[i].name=="SetPosition" and fx.calls[i].id==id then return fx.calls[i].cmd.pos end end
end
check(placed(hostWalk,22528)[1]==102 and placed(hostWalk,22528)[2]==348.25 and placed(hostWalk,22529)[1]==140,"host parks 22529 and puts 22528 beside the player")
c=rideEnv(2,false)
check(c.R.ride()=="ride_ready_22529" and placed(c,22529)[1]==102 and placed(c,22528)[1]==140 and c.R.mounted==false,"guest parks 22528 and puts 22529 beside the player")
-- Private Walker placement may not relocate the other rider's body.
for _,host in ipairs({true,false})do
 local other=host and 2 or 1
 local ownId=host and 22528 or 22529
 for _,fault in ipairs({"occupied","rider","state"})do
  local occupied=rideEnv(2,host)
  occupied.corrupt=function(r)
   r.pack="00";local a=r.actors[other]
   if fault=="occupied"then a.rider=7;a.state=1;a.occupied=true
   elseif fault=="rider"then a.rider=7;a.state=0;a.occupied=false
   else a.rider=65535;a.state=2;a.occupied=false end
   return r
  end
  check(occupied.R.ride()=="other_ride_busy", "private Walker refuses other "..fault.." body, host="..tostring(host))
  check(occupied:mutations()==0 and occupied:count("SetPosition")==0 and placed(occupied,ownId)==nil,"busy other Walker is untouched and cannot trigger own placement "..fault)
 end
 for _,fault in ipairs({"occupied","rider","state","cycle","generation"})do
  local changed=rideEnv(2,host)
  changed.corrupt=function(r,n)
   r.pack="00"
   if n>=2 then
    local a=r.actors[other]
    if fault=="occupied"then a.rider=7;a.state=1;a.occupied=true
    elseif fault=="rider"then a.rider=7
    elseif fault=="state"then a.state=2
    elseif fault=="cycle"then r.cycle=r.cycle+1 else r.generation=r.generation+1 end
   end
   return r
  end
  local why=changed.R.ride()
  check(why=="actor_changed" or why=="owner_changed", "private Walker double read refuses changing other "..fault)
  check(changed:mutations()==0 and changed:count("SetPosition")==0,"changing other Walker is untouched before either placement "..fault)
 end
 local yielded=rideEnv(2,host)
 yielded.onSend=function()
  yielded.corrupt=function(r)r.pack="00";local a=r.actors[other];a.rider=7;a.state=1;a.occupied=true;return r end
 end
 check(yielded.R.ride()=="actor_changed" and yielded:count("SetPosition")==1,"native rider change after parking blocks the next Walker placement")
 check(placed(yielded,ownId)==nil,"own Walker stays untouched when the first command changes the native other actor")
end
c=F(1);c.W.host=true;c.allocated=1;c.realized=1;c.mask=1;c.R.mounted=true
check(c.R.ride()=="ride_capacity" and c:mutations()==0 and c.R.mounted==false,"one body refuses and cannot leave rideMounted set")
c=rideEnv(1,true);c.realized=1;c.mask=1;c.R.mounted=true
check(c.R.ride()=="ride_capacity" and c:mutations()==0 and c.R.mounted==false,"allocated-but-unrealized second refuses before any ride call")
c=F(1);c.W.host=true;c.allowAssigned=true;c.R.mounted=true
check(c.R.ride()=="ride_pack" and c:mutations()==0 and c.R.mounted==false,"a census without the save pack sends no ride call")
c=rideEnv(1,true);c.corrupt=function(r) r.pack="unknown";return r end
check(c.R.ride()=="ride_pack" and c:mutations()==0 and not c.R.mounted,"pack_unknown sends no ride call")
c=rideEnv(1,true);c.admitted=false
check(c.R.ride()=="not_admitted" and c:mutations()==0 and not c.R.mounted,"a refused census sends no ride call")
c=rideEnv(1,true);c.corrupt=function(r) r.pack="03";r.actors[1].rider=4;r.actors[1].occupied=true;r.actors[2].rider=4;r.actors[2].occupied=true;r.actors[2].pending=65535;return r end
check(c.R.ride()=="same_actor" and c:mutations()==0 and not c.R.mounted,"one rider on both bodies is refused")
c=rideEnv(1,true);c.sendResult=false;c.R.mounted=true
check(c.R.ride()=="ride_call_refused" and c:count("SetCallHorse")==1 and c.R.mounted==false,"a failed mount does not stick rideMounted")
c=rideEnv(1,false);c.sendError=true;c.R.mounted=true
check(c.R.ride()=="ride_call_refused" and c.R.mounted==false,"a mount exception does not stick rideMounted")
c=rideEnv(1,true)
check(c.R.census():find("id6656",1,true) and c.R.census():find("id6657",1,true) and c.R.census():find("localMounted0",1,true) and c.R.census():find("peerWireRide255",1,true),"horse census names both bodies and stays unmounted without a remote ride")
local function occupy(kind,seat,remote)
 local c=F(kind)
 if kind==2 and seat==2 then c.rider=0; c.state=1; c.occupied=true end
 c.corrupt=function(r)
  r.pack=kind==2 and "00" or "03"; if remote~=nil then r.remoteRide=remote end
  r.actors[seat].rider=0; r.actors[seat].occupied=true
  if kind==2 then r.actors[seat].state=1 end
  return r
 end
 return c
end
local firstHorse=occupy(1,1,3).R.census()
check(firstHorse:find("id6656 rider0 occupied1",1,true) and firstHorse:find("id6657 rider65535 pending65535 occupied0",1,true),"peer horse heartbeat leaves the other native horse empty")
check(firstHorse:find("peerWireRide3",1,true),"peer horse wire kind is separately labeled without claiming a native attachment")
check(occupy(1,1,nil).R.census():find("id6657 rider65535 pending65535 occupied0",1,true),"no remote ride leaves the other horse empty")
check(occupy(1,1,2).R.census():find("id6657 rider65535 pending65535 occupied0",1,true),"a walker heartbeat leaves the other native horse empty")
local secondHorse=occupy(1,2,3).R.census()
check(secondHorse:find("id6656 rider65535 occupied0",1,true) and secondHorse:find("id6657 rider0 pending65535 occupied1",1,true),"the mirrored horse census preserves only its actual local rider")
local secondWalker=occupy(2,2,2).R.census()
check(secondWalker:find("id22528 rider65535 occupied0",1,true) and secondWalker:find("id22529 rider0 state1 occupied1",1,true),"a vehicle heartbeat does not fill the other native walker")
check(occupy(2,2,3).R.census():find("id22528 rider65535 occupied0",1,true),"a horse heartbeat leaves the other native walker empty")
for _,kind in ipairs({1,2})do
 for _,wireKind in ipairs({0,1,2,3,4,5,255})do
  local empty=F(kind)
  empty.corrupt=function(r)r.pack=kind==2 and "00" or "03";r.remoteRide=wireKind;r.rideMounted=false;return r end
  local out=empty.R.census()
  local firstId,secondId=kind==2 and 22528 or 6656,kind==2 and 22529 or 6657
  check(out:find("id"..firstId.." rider65535 occupied0",1,true),"wire kind "..wireKind.." preserves first empty native body kind "..kind)
  check(out:find("id"..secondId.." rider65535 "..(kind==2 and "state0" or "pending65535").." occupied0",1,true),"wire kind "..wireKind.." preserves second empty native body kind "..kind)
  check(out:find("localMounted0 peerWireRide"..wireKind,1,true) and empty:mutations()==0,"native mount status and peer wire kind stay separate and read-only kind "..kind)
 end
end
local one=F(1);one.allocated=1;one.realized=1;one.mask=1;one.corrupt=function(r) r.remoteRide=3;return r end
check(one.R.census():find("allocated1",1,true) and not one.R.census():find("rider1",1,true),"one body still refuses instead of inventing the remote rider")
print("buddy ride helper "..checks.." checks PASS")
