-- Host/guest recording fixture for world_buddy.lua. No game, network, or saves.
local file = assert(arg[1], "world_buddy.lua required")
local limit=assert(io.open(file,"rb"));local bytes=limit:read("*a");limit:close()
assert(#bytes:gsub("\r\n","\n")<=16000 and #bytes:gsub("\r?\n","\r\n")<=16000,"world buddy literal fits LF/CRLF ceiling")
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ": " .. why, 2) end end
local function vec(x,y,z) return {GetX=function()return x end,GetY=function()return y end,GetZ=function()return z end} end
local function F(host, vectorKind)
  local c = {time=1000, active=true, demo=false, block=true, buddy=2, id=9728, pos=vec(1,2,3), calls={},tx={},logs={}}
  local env = setmetatable({}, {__index=_G})
  env.e = {}
  env.vars = {missionCode=30010,playerRotY=45}
  env.BuddyType = {NONE=0,HORSE=1,DOG=2,QUIET=3,WALKER_GEAR=4}
  if vectorKind == "table" then env.Vector3=setmetatable({}, {__call=function(_,x,y,z)return vec(x,y,z)end})
  elseif vectorKind == "userdata" then env.Vector3=newproxy(true);getmetatable(env.Vector3).__call=function(_,x,y,z)return vec(x,y,z)end
  elseif vectorKind == "missing" then env.Vector3=nil
  elseif vectorKind == "bad" then env.Vector3=42
  elseif vectorKind == "error" then env.Vector3=function()error("constructor fixture")end
  elseif vectorKind == "unreadable" then env.Vector3=function()return {}end
  else env.Vector3=vec end
  env.DemoDaemon={IsDemoPlaying=function()return c.demo end}
  env.GameObject={NULL_ID=65535,GetGameObjectIdByIndex=function(t,i)
    check(t=="TppBuddyDog2" and i==0,"only dog instance zero is queried");if c.idError then error("fixture id error") end;return c.id end}
  env.TppBuddy2BlockController={GetActiveBuddyType=function()return c.buddy end,IsBlockActive=function()return c.block end,
    CallBuddy=function(k,p,y)c.calls[#c.calls+1]={name="CallBuddy",kind=k,pos=p,yaw=y};return c.callResult end,
    RequestMoveToPosition=function(k,p,y)c.calls[#c.calls+1]={name="LuaAiStayAndSnarl",kind=k,pos=p,yaw=y};return c.moveResult end}
  local W={active=true,paired=true,eff=3,host=host,mission=30010,epoch=42,MISSIONS={[30010]={},[30020]={},[30050]={}},
    OP={},api={},bindHooks={},startHooks={},stopHooks={},pairedHooks={},unpairedHooks={},handlers={}}
  env.e.MgsCoopWorld=W
  W.get=function(t,k)return t and t[k]end
  W.now=function()return c.time end
  W.tick32=function(n)return n%4294967296 end
  W.inGame=function()return c.active end
  W.log=function(s)c.logs[#c.logs+1]=s end
  W.tx=function(op,...)c.tx[#c.tx+1]={op,...};return c.txResult~=false end
  W.api={null=65535,send=function(id,cmd)
    c.calls[#c.calls+1]={name=cmd.id,id=id,cmd=cmd}
    if cmd.id=="GetPosition" then error("unsupported Dog GetPosition must never be called") end
    if cmd.id=="SetWarpPosition" then check(cmd.pos and not cmd.position,"retail dog warp uses pos");return end
    if cmd.id=="LuaAiStayAndSnarl" then
      if c.aiError then error("fixture Dog target error") end
      if cmd.position then
        check(id==9728 and cmd.useMarker==false and (cmd.dash==0 or cmd.dash==1) and cmd.warp==0 and cmd.ignoreMarker==true,"held command uses exact Dog0/typed fields and never command warps")
        check(cmd.look and cmd.look.GetX,"look is a position Vector3")
      else check(cmd.look==nil and cmd.warp==nil and cmd.ignoreMarker==nil,"clear uses omitted position only") end
      return c.aiResult
    end
    error("unqualified game command " .. tostring(cmd.id)) end}
  W.receipt=function(source)
    local m,e,id,mode=source:match("^return 'MGS_COOP_BUDDY_POSE_V1' %-%- (%d+) (%d+) (%d+) (%d+)$")
    check(m and tonumber(m)==W.mission and tonumber(e)==(mode=="1" and W.buddy.s.ownedEpoch or W.epoch) and tonumber(id)==9728 and (mode=="0" or mode=="1"),"receipt has fixed integer grammar and exact mission/epoch/Dog0")
    c.calls[#c.calls+1]={name="NativePose",source=source,mode=tonumber(mode)}
    if c.positionError then error("fixture native receipt failure") end
    if c.nativeReply ~= nil then return c.nativeReply end
    if c.pos == nil then return {ready=false,reason="dog_unrealized"} end
    local ok,x,y,z=pcall(function()return c.pos:GetX(),c.pos:GetY(),c.pos:GetZ()end)
    if not ok then return {ready=true,mission=W.mission,epoch=tonumber(e),id=9728,pos={}} end
    return {ready=true,mission=W.mission,epoch=tonumber(e),id=9728,pos={math.floor(x*1000+0.5),math.floor(y*1000+0.5),math.floor(z*1000+0.5)}}
  end
  setfenv(assert(loadfile(file)),env)()
  for _,f in ipairs(W.bindHooks) do f(W.api,function(t,n,k,opt)return t and t[k]end,function(t,n,k,opt)return t and t[k]end) end
  c.W,c.B,c.env=W,W.buddy,env
  function c:tick(at)if at then self.time=at end;self.B.tick(self.time)end
  function c:rx(v)self.W.handlers[39](v,self.time)end
  function c:sample(seq,k,p,x,y,z,age)self:rx({seq or 1,k or 1,p==nil and 1 or p,x or 4000,y or 2000,z or 3000,100,123,age or 0})end
  function c:count(name)local n=0;for _,v in ipairs(self.calls)do if v.name==name then n=n+1 end end;return n end
  function c:unpair()self.W.paired=false;for _,f in ipairs(self.W.unpairedHooks)do f(1)end end
  function c:stop()for _,f in ipairs(self.W.stopHooks)do f(true)end;self.W.active=false end
  return c
end
local h=F(true);h:tick();check(#h.tx==1 and h.tx[1][1]==39,"host sends companion state")
check(h.tx[1][2]==1 and h.tx[1][3]==1 and h.tx[1][4]==1,"host emits dog/present/seq")
check(h.tx[1][5]==1000 and h.tx[1][6]==2000 and h.tx[1][7]==3000,"host sends actual dog pose")
check(h:count("CallBuddy")==0 and h:count("LuaAiStayAndSnarl")==0 and h:count("SetWarpPosition")==0,"host normal buddy behavior unchanged")
h:tick(1100);check(#h.tx==1,"host cadence bounded")
h.buddy=1;h:tick(1250);check(h.tx[2][3]==2 and h.tx[2][4]==0,"horse announced without fabricated pose or spawn")
h.buddy=4;h:tick(1500);check(h.tx[3][3]==3,"walker kind distinct")
h.buddy=3;h:tick(1750);check(h.tx[4][3]==4,"Quiet kind distinct")
h.buddy=0;h:tick(2000);check(h.tx[5][3]==0,"no buddy distinct")
h=F(true);h.pos=nil;h:tick();check(h.tx[1][3]==1 and h.tx[1][4]==0,"unreadable dog never fakes presence")
h=F(true);h.id=65535;h:tick();check(h.tx[1][4]==0,"unallocated dog not present")
h=F(true);h.pos=vec(0/0,0,0);h:tick();check(h.tx[1][4]==0,"nonfinite host pose rejected")
h=F(true);h.txResult=false;h:tick();check(h.B.s.seq==0,"rejected transmit does not spend sequence")
local g=F(false);g:sample();g:tick();check(g:count("LuaAiStayAndSnarl")==1,"guest submits exact persistent Dog target")
check(g:count("CallBuddy")==0 and g:count("SetWarpPosition")==0,"near dog does not respawn or teleport")
check(g.B.s.owned and g.B.s.original==2,"guest dog behavior borrowed")
check(g.tx[1][1]==40 and g.tx[1][4]==0 and g.tx[1][5]==300,"guest reports actual initial error")
g:tick(1100);check(g:count("LuaAiStayAndSnarl")==1,"move commands bounded at 250ms")
g:tick(1250);check(g:count("LuaAiStayAndSnarl")==2,"ongoing target navigates natively")
g.pos=vec(4,2,3);g:sample(2);g:tick(1500);check(g:count("LuaAiStayAndSnarl")==3,"arrived dog keeps the host target held")
g:sample(3,1,1,25000);g:tick(1750);check(g:count("SetWarpPosition")==1,"distant recovery uses actual retail dog warp")
g=F(false);g.buddy=0;g:sample();g:tick();check(g:count("CallBuddy")==1,"guest without buddy requests one dog")
check(g.calls[1].kind==2,"requested buddy is dog")
g:tick(1500);check(g:count("CallBuddy")==1,"singleton request not repeatedly reissued")
g.buddy=2;g.pos=vec(4,2,3);g:sample(2);g:tick();check(g.B.s.original==0 and g.B.s.pending==nil,"loaded dog retains original no-buddy state")
g:unpair();check(g:count("CallBuddy")==2 and g.calls[#g.calls].kind==0,"lost link removes only owned dog")
for _,k in ipairs({1,3,4})do
 g=F(false);g.buddy=k;g:sample();g:tick();check(g:count("CallBuddy")==0 and g:count("LuaAiStayAndSnarl")==0,"existing unrelated buddy is not commandeered")
 check(g.tx[1][4]==2,"unrelated buddy refusal reported")
end
g=F(false);g.buddy=0;g.callResult=false;g:sample();g:tick();check(g:count("CallBuddy")==1 and not g.B.s.owned,"refused dog load not owned")
g:tick(1100);check(g:count("CallBuddy")==1,"refused load backoff")
g=F(false);g.buddy=0;g:sample();g:tick();g.time=6000;g:sample(2);g:tick();check(g.B.s.reason=="dog_load_timeout" and g:count("CallBuddy")==1,"missing allocation gives bounded timeout")
g=F(false);g:sample();g:tick();g:unpair();check(g:count("CallBuddy")==1 and g.calls[#g.calls].kind==2,"borrowed dog returns to original local AI")
check(not g.B.s.owned and not g.B.s.rx,"unpair clears target and ownership")
g=F(false);g.pos=vec(11.5,348.25,-7.75);g:sample(1,1,1,14000,348250,-7750);g:tick();g:unpair()
local released=g.calls[#g.calls]
check(released.name=="CallBuddy" and released.kind==2,"borrowed original dog restores through stock call")
check(released.pos:GetX()==11.5 and released.pos:GetY()==348.25 and released.pos:GetZ()==-7.75,"original dog release preserves actual xyz including nonzero terrain height")
check(math.abs(released.yaw-math.pi/4)<0.000001,"original dog release preserves separate radian heading")
g=F(false);g:sample();g:tick();g.buddy=1;g:unpair();check(g:count("CallBuddy")==0,"user-changed buddy never deleted during release")
g=F(false);g:sample();g:tick();g.id=9729;g:unpair();check(g:count("CallBuddy")==0,"reallocated dog never modified during release")
g=F(false);g:sample();g:tick();g:tick(2001);check(g.B.s.reason=="expired" and g:count("CallBuddy")==1,"sample expiry releases guest AI")
g=F(false);g:sample(1,1,1,4000,2000,3000,1001);g:tick();check(g:count("LuaAiStayAndSnarl")==0,"queued old sample never moves dog")
g=F(false);g:sample(1);g:sample(1,1,1,5000);check(g.B.s.rx.x==4,"duplicate sample rejected")
g:sample(65535,1,1,5000);check(g.B.s.rx.x==4,"old wrapped sequence rejected")
g.B.s.rxSeq=65535;g:sample(1,1,1,6000);check(g.B.s.rx.x==6,"sequence rollover admitted")
g=F(false);g:sample();g:rx({2,1,1,0/0,0,0,0,0,0});check(g.B.s.rxSeq==1,"nonfinite payload leaves sequence intact")
g:rx({2,1,1,0,0,0,3143,0,0});check(g.B.s.rxSeq==1,"invalid heading rejected")
g:rx({2,1,1,0,0,0,0,0,65536});check(g.B.s.rxSeq==1,"invalid queue age rejected")
g:rx({2,1,1,0,0,0,0,0});check(g.B.s.rxSeq==1,"missing native age rejected")
for _,k in ipairs({0,2,3,4})do
 g=F(false);g:sample(1,k);g:tick();check(g:count("CallBuddy")==0 and g:count("LuaAiStayAndSnarl")==0,"unsupported buddy kind never falsely spawned")
 check(type(g.tx[1][4])=="number","refusal result is numeric on wire")
end
for _,guard in ipairs({"demo","block","active","paired","mission","level","binding"})do
 g=F(false);g:sample()
 if guard=="demo"then g.demo=true elseif guard=="block"then g.block=false elseif guard=="active"then g.active=false
 elseif guard=="paired"then g.W.paired=false elseif guard=="mission"then g.env.vars.missionCode=10020
 elseif guard=="level"then g.W.eff=2 elseif guard=="binding"then g.B.bound=nil end
 g:tick();check(g:count("LuaAiStayAndSnarl")==0 and g:count("SetWarpPosition")==0 and g:count("CallBuddy")==0,"unsafe context inert: "..guard)
end
g=F(false);g:sample();g:tick();g.demo=true;g:unpair();check(g.B.s.owned and g:count("CallBuddy")==0,"release defers during demo")
g.demo=false;g:tick(1250);check(not g.B.s.owned and g:count("CallBuddy")==1,"deferred release runs after demo")
g=F(false);g:sample();g:tick();g:stop();check(not g.B.s.owned and not g.B.s.rx,"mission stop clears borrowed state")
g=F(false);g:sample();g:tick();for _,f in ipairs(g.W.pairedHooks)do f(true)end;check(not g.B.s.rx and g.B.s.rxSeq==nil,"new mission epoch clears old targets")
h=F(true);h:tick();h.W.handlers[40]({1,1,0,30});check(h.B.s.remoteError==30,"host reads guest error")
h.W.handlers[40]({65535,1,0,1});check(h.B.s.remoteError==30,"old acknowledgement does not change status")
g=F(false);g:sample();g.aiError=true;g:tick()
check(g.B.disabled and g.W.active,"companion API errors disable only companion sync")
check(g.logs[#g.logs]:find("scope=buddy_only",1,true),"companion failure reported without world shutdown")
for _,f in ipairs(g.W.startHooks)do f()end;check(not g.B.disabled,"new mission may qualify companion again")
g=F(false);g.buddy=0;g:sample();g:tick();g.callResult=false;g:unpair()
check(g.B.s.owned and g.B.s.releaseOnly and g:count("CallBuddy")==2,"pending native request cancellation retains ownership on refusal")
g:tick(1100);check(g:count("CallBuddy")==2,"release retries are bounded")
g.buddy=2;g.callResult=true;g:tick(1250)
check(not g.B.s.owned and not g.B.s.pending and g.calls[#g.calls].kind==0,"late allocated dog is cleaned after link loss")
g=F(false);g:sample();g:tick();g.callResult=false;g:unpair()
check(g.B.s.owned,"native release refusal retains ownership")
g.callResult=true;g:tick(1250);check(not g.B.s.owned,"native release retries after its transition")
g=F(false);g:sample();g:tick();g.callResult=false;for _,f in ipairs(g.W.pairedHooks)do f(true)end
check(g.B.s.releaseOnly and g.B.s.owned and not g.B.s.rx,"epoch reset preserves only deferred native cleanup")
g:sample(1);g.callResult=true;g:tick(1250)
check(not g.B.s.releaseOnly and g.B.s.rxSeq==1,"new epoch proceeds after native cleanup")
g=F(false);g:sample();g.aiResult=false;g:tick()
check(g.B.s.reason=="target_refused" and g.tx[1][4]==2,"refused native move is reported")
g=F(false);g.buddy=0;g:sample();g:tick();g.buddy=1;g:unpair()
check(not g.B.s.pending and not g.B.s.owned and g:count("CallBuddy")==1,"user's different buddy clears pending metadata without a new call")
h=F(true);h:tick();local status=h.B.status()
check(status:find("buddy:H",1,true) and status:find("kdog",1,true),"status reports current native dog kind")
check(status:find("H=1.00,2.00,3.00@0",1,true) and status:find("v1",1,true),"host status labels actual transmitted pose and version")
g=F(false);g:sample();g:tick();g.pos=vec(3.5,2,3);status=g.B.status()
check(status:find("H=4.00,2.00,3.00@0",1,true) and status:find("L=3.50,2.00,3.00@0",1,true),"guest status distinguishes host sample and current local readback")
check(status:find("e50",1,true),"status recomputes current actual readback error")
check(g:count("LuaAiStayAndSnarl")==1 and g:count("CallBuddy")==0,"status never modifies native buddy")
g.demo=true;g.time=1250;g.pos=vec(90,0,0);status=g.B.status()
check(status:find("a0",1,true) and status:find("L=3.50,2.00,3.00@250",1,true),"unsafe status uses explicitly aged last readback")
check(not status:find("90.00",1,true),"unsafe status never invents a refreshed pose")
g=F(false);g.B.bound=nil;status=g.B.status()
check(status:find("kunknown",1,true) and status:find("L=?@-1",1,true),"missing companion API reports unread status")
g=F(false);g.demo=true;g.time=1000000;g.B.s.rxSeq=65535;g.B.s.reason=string.rep("r",100)
g.B.s.hostPos={x=-1000000,y=-1000000,z=-1000000,at=0}
g.B.s.localPos={x=-1000000,y=-1000000,z=-1000000,at=0}
status=g.B.status()
check(#status<=160,"maximum bounded status fits native debug note without truncating actual local position")
check(status:find("L=-1000000.00,-1000000.00,-1000000.00@99999+",1,true),"bounded status retains complete local readback and explicit old age")
-- Actual retail constructor shapes must qualify without actor side effects.
for _, shape in ipairs({"table", "userdata"}) do
 h=F(true,shape);check(h.B.bound,"callable "..shape.." constructor qualifies")
 check(#h.calls==0,"constructor probe sends no game object or buddy commands")
 h:tick();check(#h.tx==1 and h.tx[1][4]==1,"host publishes actual dog with "..shape.." Vector3")
 g=F(false,shape);g:sample();g:tick();check(g:count("LuaAiStayAndSnarl")==1 and not g.B.disabled,"guest navigates with "..shape.." Vector3")
 g:unpair();check(not g.B.s.owned,"borrowed dog releases with "..shape.." Vector3")
end
for _,shape in ipairs({"missing", "bad", "error", "unreadable"}) do
 g=F(false,shape);g:sample();g:tick();check(not g.B.bound and #g.calls==0 and #g.tx==0,"unusable constructor inert: "..shape)
 check(g.B.status():find(shape=="missing" and "missing_Vector3" or shape=="unreadable" and "vector_unreadable" or "vector_not_callable",1,true),"constructor refusal visible in bounded status")
end
for _,guard in ipairs({"demo","block","active","mission","world"})do
 g=F(false)
 if guard=="demo"then g.demo=true elseif guard=="block"then g.block=false elseif guard=="active"then g.active=false
 elseif guard=="mission"then g.env.vars.missionCode=10020 else g.W.active=false end
 local reason=({demo="demo_active",block="buddy_block_inactive",active="not_in_game",mission="mission_mismatch",world="world_inactive"})[guard]
 check(g.B.status():find(reason,1,true),"exact context refusal visible: "..guard)
end
-- Cleanup survives full stop during a native pause; no follow/traffic is resumed.
g=F(false);g:sample();g:tick();g.demo=true;g:stop()
check(g.B.s.owned and g.B.s.releaseOnly and not g.W.active,"full disable retains only pending borrowed cleanup")
local traffic=#g.tx;local moves=g:count("LuaAiStayAndSnarl")
check(g.B.cleanupPending()==false and g:count("CallBuddy")==0,"inactive cleanup waits through demo")
g.demo=false;g:tick(1250);check(g.B.s.owned,"inactive normal tick never takes cleanup authority")
check(g.B.cleanupPending()==true and not g.B.s.owned and g:count("CallBuddy")==1,"inactive cleanup releases exact borrowed dog")
check(#g.tx==traffic and g:count("LuaAiStayAndSnarl")==moves+1 and not g.B.s.rx,"inactive cleanup only clears the old Dog command and sends no packets")
g=F(false);g:sample();g:tick();g.callResult=false;g:stop();g.time=1100
check(not g.B.cleanupPending() and g:count("CallBuddy")==1,"inactive native release backoff enforced")
g.time=1250;g.callResult=true;check(g.B.cleanupPending() and g:count("CallBuddy")==2,"inactive refused release later completes")
g=F(false);g:sample();g:tick();g.demo=true;g:stop();g.demo=false;g.W.mission=30020;g.env.vars.missionCode=30020
check(g.B.cleanupPending() and not g.B.s.owned and g:count("CallBuddy")==0,"old mission ownership never changes new mission dog")
g=F(false);g:sample();g:tick();g.demo=true;g:stop();g.demo=false;g.buddy=1
check(g.B.cleanupPending() and g:count("CallBuddy")==0,"inactive cleanup preserves user's different buddy")
g=F(false);g:sample();g:tick();g.demo=true;g:stop();g.demo=false;g.id=9729
check(g.B.cleanupPending() and g:count("CallBuddy")==0,"inactive cleanup preserves reallocated actor")
g=F(false);g:sample();g:tick();check(g.B.cleanupPending() and g.B.s.owned and g:count("CallBuddy")==0,"normal paired ownership unaffected by cleanup hook")
-- Probe diagnostics preserve actual IDs/types and never fabricate coordinates.
h=F(true);h.pos=nil;status=h.B.status()
check(status:find("rqn9728/-/n_real",1,true),"nil position distinguished from failed ID")
check(h.B.s.probe.id==9728 and h.B.s.probe.idType=="number" and h.B.s.probe.positionType=="nil","actual read return types cached")
check(status:find("L=?@-1",1,true) and not h.B.disabled,"unsupported read keeps no fabricated position and remains diagnosable")
h=F(true);h.id=65535;status=h.B.status()
check(status:find("rqn65535/-/idbad",1,true) and h:count("GetPosition")==0,"null ID blocks command and reports actual value")
h=F(true);h.id="9728";status=h.B.status()
check(status:find("rqs?/-/idbad",1,true),"wrong ID return type diagnosed without coercion")
h=F(true);h.idError=true;status=h.B.status()
check(status:find("rq-?/-/iderr",1,true),"ID API exception diagnosed locally")
h=F(true);h.positionError=true;status=h.B.status()
check(status:find("rqn9728/-/nerr",1,true),"command API exception distinct from nil return")
h=F(true);h.pos={};status=h.B.status()
check(status:find("rqn9728/t/reply",1,true),"vector method failure diagnosed")
h=F(true);h.pos=vec(0/0,0,0);status=h.B.status()
check(status:find("rqn9728/t/pos",1,true),"nonfinite vector diagnosed")
h=F(true);h:tick();h.pos=nil;h.time=1250;status=h.B.status()
check(status:find("L=1.00,2.00,3.00@250",1,true),"failed read preserves explicitly aged previous actual pose")
local before=#h.logs;h.B.status();check(#h.logs==before,"repeated read failure log bounded")
h.time=6250;h.B.status();check(#h.logs==before+1,"persistent read failure retried and bounded log updated")
h.pos=vec(9,348.25,-7);status=h.B.status()
check(h.B.s.probe.reason=="ok" and status:find("L=9.00,348.25,-7.00@0",1,true),"successful actual read clears failed diagnostic")
h=F(true);h:tick();h.B.s.hostPos={x=-1000000,y=-1000000,z=-1000000,at=0};h.B.s.localPos={x=-1000000,y=-1000000,z=-1000000,at=0};h.time=1000000;h.pos={};status=h.B.status()
check(#status<=160 and status:find("L=-1000000.00",1,true),"failed read diagnostic and worst actual poses fit native note")
-- Fixed native read receipt rejects stale or forged fields without follow.
for _,bad in ipairs({"mission","epoch","id","ready","fractional","range","shape","literal","exception"}) do
 h=F(true)
 local r={ready=true,mission=30010,epoch=42,id=9728,pos={1000,2000,3000}}
 if bad=="mission" then r.mission=30020 elseif bad=="epoch" then r.epoch=43 elseif bad=="id" then r.id=9729
 elseif bad=="ready" then r.ready=1 elseif bad=="fractional" then r.pos[2]=2.5 elseif bad=="range" then r.pos[2]=1000000001
 elseif bad=="shape" then r.pos={1000,2000,3000,4} elseif bad=="literal" then r="MGS_COOP_BUDDY_POSE_V1"
 else h.positionError=true end
 h.nativeReply=r;h:tick()
 check(h.tx[1][4]==0 and not h.B.s.localPos,"bad native receipt never declares actual Dog presence: "..bad)
 check(h:count("GetPosition")==0,"unsupported engine position query removed: "..bad)
end
h=F(true);h.W.epoch=0;h:tick();check(h:count("NativePose")==0 and h.tx[1][4]==0,"zero epoch cannot request native read")
g=F(false);g:sample();g:tick();g:unpair();check(g.calls[#g.calls-2].name=="NativePose" and g.calls[#g.calls-2].mode==1,"borrowed cleanup selects read-only cleanup scope")
h=F(true);h:tick();check(h.calls[1].name=="NativePose" and h.calls[1].mode==0,"normal host sample selects paired scope")
g=F(false);g:sample();g:tick();g.demo=true;g:stop();g.demo=false;g.W.epoch=0
check(g.B.cleanupPending() and not g.B.s.owned,"cleanup uses retained borrowed epoch after world epoch is cleared")
check(g.calls[#g.calls-2].source:find("30010 42 9728 1",1,true),"cleared world epoch never fabricates a new cleanup scope")
-- A target stays held while host is stationary; local-follow is never re-enabled in the deadband.
g=F(false);g.pos=vec(4,2,3);g:sample();g:tick()
local held=g.calls[#g.calls]
check(held.name=="LuaAiStayAndSnarl" and held.cmd.position:GetX()==4 and held.cmd.dash==0,"stationary host gets a held walking target, not local follow")
g:sample(2);g:tick(1250);check(g:count("LuaAiStayAndSnarl")==2,"stationary target is refreshed at bounded cadence")
g:unpair();check(g.calls[#g.calls-1].name=="LuaAiStayAndSnarl" and g.calls[#g.calls-1].cmd.position==nil,"release clears held AI before restoring original Dog")
check(g.calls[#g.calls].name=="CallBuddy" and not g.B.s.aiHeld,"original native behavior restored after exact clear")
g=F(false);g:sample();g:tick();g.aiResult=false;g:unpair()
check(g.B.s.owned and g.B.s.aiHeld and g:count("CallBuddy")==0,"refused held-target clear retains ownership for retry")
g.aiResult=nil;g:tick(1250);check(not g.B.s.owned and g:count("CallBuddy")==1,"deferred held-target clear completes before restoring Dog")
g=F(false);g:sample();g.aiError=true;g:tick()
check(g.B.disabled and g.B.s.owned and g.B.s.aiHeld and g.B.s.releaseOnly,"command exception preserves only owned cleanup while disabling follow")
local packets=#g.tx;g.aiError=false;g:tick(1250)
check(g.B.disabled and not g.B.s.owned and g:count("CallBuddy")==1 and #g.tx==packets,"disabled follower retries exact cleanup without follow or packets")
-- A ride clears the native held target before giving up Dog cleanup ownership.
-- It never calls the selector itself or re-arms follow during the handoff.
g=F(false);g:sample();g:tick()
local before=g:count("CallBuddy")
check(g.B.yieldForRide()==true and g.B.s.rideYield and not g.B.s.owned and g.B.s.yieldedOwned and not g.B.disabled and g:count("CallBuddy")==before,"ride yield clears the target then drops ownership without a selector call")
check(g.calls[#g.calls].name=="LuaAiStayAndSnarl" and g.calls[#g.calls].cmd.position==nil and g.B.s.yieldedOwned.aiHeld==nil,"exact omitted-position clear precedes the ownership snapshot")
g:sample(2);g:tick(1250)
check(g:count("CallBuddy")==before and not g.B.s.owned and g:count("LuaAiStayAndSnarl")==2 and not g.B.s.aiHeld,"yielded Dog does not re-arm native follow while horse loading begins")
g.buddy=1;g:sample(3);g:tick(1500)
check(g:count("CallBuddy")==before and not g.B.s.owned and not g.B.disabled and g.B.s.rideYield and g.tx[#g.tx][4]==0,"horse selector stays yielded and the host dog is still acknowledged")
g.B.keepRideYield();check(not g.B.s.yieldedOwned and g.B.s.rideYield,"accepted ride forgets the cleanup snapshot")
g.buddy=2;g:sample(4);g:tick(1750)
check(not g.B.s.owned and not g.B.s.aiHeld and g:count("CallBuddy")==before and g:count("LuaAiStayAndSnarl")==2,"accepted ride never re-arms an unowned Dog when native type flickers")
g=F(false);g:sample();g:tick();g.B.yieldForRide();local calls=g:count("CallBuddy");g:unpair()
check(g:count("CallBuddy")==calls and not g.B.s.owned,"yielding the selector does not dismiss the dog")
g=F(false);g.buddy=0;g:sample();g:tick()
check(g.B.s.owned and g.B.s.original==0 and g.B.s.pending and g:count("CallBuddy")==1,"pending dog load is the borrowed selector")
check(g.B.yieldForRide()==true and not g.B.s.owned and not g.B.s.pending)
check(g.B.resumeFollow()==true and g.B.s.owned and g.B.s.pending and g.B.s.original==0 and not g.B.s.rideYield and g:count("CallBuddy")==1,"refused ride restores the pending dog without another selector call")
g.buddy=2;g.pos=vec(4,2,3);g:sample(2);g:tick(1250)
check(g.B.yieldForRide()==true and not g.B.s.owned)
check(g.B.resumeFollow()==true and g.B.s.owned and g.B.s.original==0 and not g.B.s.rideYield,"refused ride restores the realized borrowed dog")
g:unpair();check(g.calls[#g.calls].name=="CallBuddy" and g.calls[#g.calls].kind==0,"restored borrow still dismisses only that dog")
g=F(false);g:sample();g:tick();g.B.yieldForRide();g.buddy=1
check(g.B.resumeFollow()==true and not g.B.s.owned and g:count("CallBuddy")==0,"resume after the ride lands does not despawn or recall the dog")
-- A failed handoff keeps cleanup ownership, and a refused Horse request can
-- resume native Dog following from the genuinely cleared target state.
for _,fault in ipairs({"pose","id","clearFalse","clearError","demo","epoch","mission"})do
 g=F(false);g:sample();g:tick();local ai=g:count("LuaAiStayAndSnarl")
 if fault=="pose"then g.nativeReply={ready=false,reason="dog_changed"}
 elseif fault=="id"then g.id=65535 elseif fault=="clearFalse"then g.aiResult=false
 elseif fault=="clearError"then g.aiError=true elseif fault=="demo"then g.demo=true
 elseif fault=="epoch"then g.W.epoch=43 else g.W.mission=30020 end
 check(g.B.yieldForRide()==false and g.B.s.owned and g.B.s.aiHeld and not g.B.s.rideYield and not g.B.s.yieldedOwned,"unqualified/failed clear retains borrowed ownership: "..fault)
 check(g:count("CallBuddy")==0,"refused yield never invokes the selector: "..fault)
 check(g:count("LuaAiStayAndSnarl")==ai+((fault=="clearFalse"or fault=="clearError")and 1 or 0),"only a qualified Dog clear can be submitted: "..fault)
end
g=F(false);g:sample();g:tick();g.B.yieldForRide();g.B.resumeFollow()
check(g.B.s.owned and not g.B.s.aiHeld and not g.B.s.rideYield,"refused Horse call restores actual cleared Dog ownership")
g:sample(2);g:tick(1250)
check(g.B.s.aiHeld and g:count("LuaAiStayAndSnarl")==3 and g.calls[#g.calls].cmd.position~=nil,"next fresh host sample re-arms the native Dog target")
h=F(true);h.B.s.rideYield=true;h:tick()
check(h.tx[1][3]==1 and h.tx[1][4]==1 and h:count("CallBuddy")==0 and not h.B.disabled,"host dog follow still publishes while a guest ride holds its own selector")
-- Optional composition exercises the real debug helper against the real
-- follower, while only native engine boundaries remain recorded fixtures.
if arg[2]then
 local function debugGuest()
  local c=F(false);c:sample();c:tick()
  local env,W,D=c.env,c.W,{mission=30010}
  c.debugLogs={}
  env.PlayerStatus={ON_HORSE=8};env.PlayerInfo={OrCheckStatus=function(a)return false end}
  local dogId=W.api.buddyId
  W.api.buddyId=function(t,i)if t=="TppHorse2"then check(i==0,"composed horse lookup is zero only");return 6656 end;return dogId(t,i)end
  env.GameObject.DoesGameObjectExistWithTypeName=function(t)check(t=="TppHorse2","composed exact Horse class query");return c.buddy==1 end
  W.api.buddyCall=function(k,p,y)
   check(k==1 and not c.B.s.owned and not c.B.s.aiHeld and c.B.s.rideYield,"stock Horse request follows the native Dog clear and selector yield")
   check(c.calls[#c.calls].name=="LuaAiStayAndSnarl" and c.calls[#c.calls].cmd.position==nil,"native held-target clear is the last mutation before horse selection")
   c.calls[#c.calls+1]={name="CallBuddy",kind=k,pos=p,yaw=y}
   return c.callResult~=false
  end
  D.api={get=function(t,k)return t and t[k]end,call=function(t,k,...)return t[k](...)end,
   integer=function(n,l,h)return type(n)=="number"and n==math.floor(n)and n>=l and n<=h end,
   finite=function(n)return type(n)=="number"and n==n and math.abs(n)<1e15 end,
   pose=function()return{100,340,200,45}end,
   note=function(id,ok,msg)c.debugLogs[#c.debugLogs+1]={id,ok,msg}end}
  env.e.MgsCoopDebug=D;setfenv(assert(loadfile(arg[2])),env)()
  c.D=D;return c
 end
 g=debugGuest();local ok,msg=g.D.buddy.run({"horse_spawn"},17)
 check(ok and msg:find("submitted_horse;",1,true) and g:count("CallBuddy")==1 and g:count("LuaAiStayAndSnarl")==2,"F9 clears the held Dog then requests Horse once")
 g:sample(2);g:tick(1250)
 check(g:count("LuaAiStayAndSnarl")==2 and not g.B.s.aiHeld,"still-selected Dog cannot re-arm while the actual debug request loads")
 g.buddy=1;g.D.pending.frames=1;g.D.pending.poll()
 check(not g.D.pending and g.debugLogs[1][2] and g.debugLogs[1][3]:find("arrival=unverified",1,true),"composed selected type still does not assert horse arrival")
 g=debugGuest();g.callResult=false;ok,msg=g.D.buddy.run({"horse_spawn"},18)
 check(not ok and msg=="horse_call_refused" and g.B.s.owned and not g.B.s.aiHeld and not g.B.s.rideYield,"real helper refusal restores the cleared borrow")
 g:sample(2);g:tick(1250)
 check(g.B.s.aiHeld and g:count("LuaAiStayAndSnarl")==3,"helper refusal re-arms normal follow on the next fresh sample")
 for _,fault in ipairs({"pose","clearFalse","clearError"})do
  g=debugGuest();if fault=="pose"then g.nativeReply={ready=false,reason="dog_changed"}elseif fault=="clearFalse"then g.aiResult=false else g.aiError=true end
  ok,msg=g.D.buddy.run({"horse_spawn"},19)
  check(not ok and msg=="dog_borrowed" and g:count("CallBuddy")==0 and g.B.s.owned and g.B.s.aiHeld and not g.D.pending,"real helper never calls Horse when native Dog cleanup refuses: "..fault)
 end
end
print("world_buddy_lua_check: " .. checks .. " checks passed")
