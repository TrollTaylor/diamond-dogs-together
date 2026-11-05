-- Horse probes (lua/coop/debug_horse.lua; horse plan section 4, P1 P2 P4 P5): lab-only F9 commands on the player module.
-- Nothing runs unless a command starts it; every command is refused outside lab mode (CFG mode 1).
local path=assert(arg[1]);local f=assert(io.open(path,"rb"));local src=f:read("*a");f:close()
local checks=0
local function check(v,why)checks=checks+1;if not v then error(checks..": "..why,2)end end
check(#src:gsub("\r\n","\n")<=16000 and #src:gsub("\r?\n","\r\n")<=16000,"embedded fragment fits both checkout modes")
-- One copy. o.mode (default 1 = lab), o.mounted (false).
local function F(o)
 o=o or {}
 local c={logs={},sends={},masks={},resets={},receipts={},warps={},baseRuns=0,now=1000,mounted=o.mounted==true,
  horse={x=10,y=100,z=20,yaw=0,occupied=false},census=true,rawHeld=false}
 local env=setmetatable({},{__index=_G});env._G=env
 env.vars={missionCode=30010,playerPosX=12,playerPosY=100,playerPosZ=20,playerRotY=0}
 env.mvars={ene_soldierDefine={cp1={"sol_a"}}}
 env.Vector3=function(x,y,z)return {x=x,y=y,z=z,GetX=function(s)return s.x end,GetY=function(s)return s.y end,GetZ=function(s)return s.z end}end
 env.PlayerStatus={ON_HORSE=25}
 env.PlayerInfo={OrCheckStatus=function(s)if s[1]==25 then return c.mounted end;return false end}
 env.PlayerPad={ACTION=0x20}
 env.Player={SetPadMask=function(t)c.masks[#c.masks+1]=t;return true end,ResetPadMask=function(t)c.resets[#c.resets+1]=t;return true end}
 env.GameObject={NULL_ID=65535,GetGameObjectId=function(t,n)if t=="TppSoldier2" and n=="sol_a" then return 1024 end;return 65535 end,
  SendCommand=function(id,cmd)
   c.sends[#c.sends+1]={id=id,cmd=cmd}
   if type(id)=="table" and id.type=="TppHorse2" then
    if cmd.id=="IsCarriedHostage" then return c.hostage==true end
    if cmd.id=="GetCarriedHostage" then return c.hostage and 1 or 0 end
    if cmd.id=="GetRidePlayer" then return c.mounted and 1 or 0 end
    if cmd.id=="SetCallHorse" then if not c.stuck then c.horse.x,c.horse.y,c.horse.z=cmd.startPosition.x,cmd.startPosition.y,cmd.startPosition.z end;return true end
   end
   if type(id)=="table" and id.type=="TppPlayer2" and cmd.id=="Warp" then c.warps[#c.warps+1]={index=id.index,pos=cmd.pos,rotY=cmd.rotY};return true end
   if id==1024 and cmd.id=="GetPosition" then return env.Vector3(c.horse.x,c.horse.y+1.4,c.horse.z-0.6) end
   if id==1024 and cmd.id=="GetLifeStatus" then return 0 end
  end}
 local W={mode=o.mode or 1,paired=true,eff=3,mission=30010,epoch=5,role=1,OP={},handlers={},
  log=function(s)c.logs[#c.logs+1]=s end,now=function()return c.now end}
 W.downed={records={{pos={0,0,0},at=0,generation=1},{pos={15000,100000,25000},at=0,generation=1}}}
 W.api={send=env.GameObject.SendCommand,buddyVector=env.Vector3}
 W.receipt=function(t)
  c.receipts[#c.receipts+1]=t
  if t:find("MGS_COOP_RIDE_POSE_V1",1,true) then
   if not c.census then return {ready=false,reason="scope"} end
   local h=c.horse
   return {ready=true,actors={{id=6656,realized=true,x=math.floor(h.x*1000+.5),y=math.floor(h.y*1000+.5),z=math.floor(h.z*1000+.5),
    yaw=math.floor(h.yaw*1000+.5),occupied=h.occupied,rider=h.occupied and 0 or 65535}}}
  end
  if t:find("MGS_COOP_CARGO_V2",1,true) then return {ready=true,role=0,carrying=false,carried=false,localCarried=false,peerCarrying=false} end
 end
 local P={handles={heal=true},run=function(a)c.baseRuns=c.baseRuns+1;return true,"base:"..a[1]end}
 local D={player=P}
 D.api={get=function(t,k)if t==nil then return nil end;local ok,v=pcall(function()return t[k]end);if ok then return v end end,
  call=function(t,k,...)return assert(t[k])(...)end,
  pose=function()local q=env.vars;return {q.playerPosX,q.playerPosY,q.playerPosZ,q.playerRotY}end}
 env.e={MgsCoopWorld=W,MgsCoopDebug=D,OnUpdate=function()c.updates=(c.updates or 0)+1;return "u" end}
 setfenv(assert(loadstring(src)),env)()
 c.env,c.W,c.D,c.P=env,W,D,P
 function c:run(...)return self.P.run({...},1)end
 function c:tick(n,ms)for _=1,n or 1 do self.now=self.now+(ms or 16);self.W.downed.rawHeld,self.W.downed.rawHeldAt=self.rawHeld,self.now;self.env.e.OnUpdate()end end
 function c:has(text)for _,v in ipairs(self.logs)do if tostring(v):find(text,1,true)then return v end end;return nil end
 function c:calls(name)local n,last=0,nil;for _,s in ipairs(self.sends)do if s.cmd.id==name then n=n+1;last=s end end;return n,last end
 return c
end

-- The chunk joins the player module's dispatch; other commands still reach the base.
local c=F()
check(c.P.handles.horse_probe==true,"horse_probe is routed to the player module")
local ok,msg=c:run("heal")
check(ok and msg=="base:heal" and c.baseRuns==1,"other commands still reach the base dispatch")
check(c.env.e.OnUpdate()=="u","the update chain still returns the original result")
check(type(c.D.horse)=="table" and type(c.D.horse.interp)=="function","D.horse with the pure helpers")

-- Lab only.
local p=F({mode=2})
ok,msg=p:run("horse_probe","status")
check(ok==false and msg=="horse_probe;refused=mode","refused outside lab mode")
ok,msg=p:run("horse_probe","rec","5000")
check(ok==false and #p.receipts==0,"no census read outside lab mode")

-- interp: linear between samples, held at both ends, yaw by the short way.
local H=c.D.horse
local track={{t=0,x=0,y=0,z=0,yaw=3.1},{t=100,x=10,y=2,z=0,yaw=-3.1}}
local x,y,z,yaw=H.interp(track,50)
check(math.abs(x-5)<1e-6 and math.abs(y-1)<1e-6 and z==0,"interp halfway")
check(math.abs(math.abs(yaw)-math.pi)<0.01,"interp turns the short way across pi")
x=H.interp(track,500);check(x==10,"interp holds the last sample")
x=H.interp(track,-5);check(x==0,"interp holds the first sample")
local s,g=H.goal("sg20",1,2,3,0.5)
check(s[1]==1 and g[1]==1 and s[3]==3 and g[3]==3,"start==goal for sg")
s,g=H.goal("yaw20",1,2,3,0)
check(s[1]==1 and math.abs(g[3]-3.02)<1e-9 and g[1]==1,"yaw20 puts the goal 2 cm ahead along the yaw")
s,g=H.goal("chase2",1,2,3,math.pi/2,{x=0,y=2,z=3})
check(s[1]==0 and math.abs(g[1]-4)<1e-9,"chase starts where the horse stands and aims 3 m ahead of the target")
check(H.period("sg4")==250 and H.period("sg10")==100 and H.period("sg20")==50 and H.period("yaw20")==50 and H.period("chase1")==1000 and H.period("chase2")==500,"sender periods")
check(H.period("bogus")==nil,"unknown mode")
local st=H.stats({1,2,3,4,100})
check(st.p50==3 and st.max==100 and st.p95==100,"stats p50/p95/max")

-- rec: one census sample per frame while it runs; done line with the count and path.
c=F()
ok,msg=c:run("horse_probe","rec","1000")
check(ok and msg:find("horse_probe=rec;step=start",1,true),"rec starts")
for i=1,40 do c.horse.x=10+i*0.1;c.horse.occupied=true;c:tick(1,30) end
local done=c:has("horse_probe=rec;step=done;")
check(done and done:find("n=3[0-9];",1,false) and done:find("occ=",1,true),"rec ends on its timer with the sample count")
check(#c.D.horse.track>=30,"track kept for the replay")

-- play refuses while the local player rides, without a track, or an unknown mode.
local m=F({mounted=true})
ok,msg=m:run("horse_probe","play","yaw20","1000")
check(ok==false and msg:find("refused=",1,true),"play refused without a track")
m.D.horse.track={{t=0,x=0,y=100,z=0,yaw=0},{t=1000,x=5,y=100,z=0,yaw=0}}
ok,msg=m:run("horse_probe","play","yaw20","1000")
check(ok==false and msg=="horse_probe;refused=mounted","play refused while mounted")
m.mounted=false
ok,msg=m:run("horse_probe","play","warp99","1000")
check(ok==false and msg=="horse_probe;refused=mode_name","unknown play mode refused")

-- play yaw20: SetCallHorse at 20 Hz on TppHorse2 index 0, goal 2 cm ahead; stats at the end.
c=F()
c.D.horse.track={{t=0,x=0,y=100,z=0,yaw=0},{t=2000,x=0,y=100,z=10,yaw=0}}
ok,msg=c:run("horse_probe","play","yaw20","1000")
check(ok and msg:find("horse_probe=play;step=start;mode=yaw20",1,true),"play starts")
for _=1,70 do c:tick(1,16) end
local n,last=c:calls("SetCallHorse")
check(n>=15 and n<=24,"about 20 Hz for 1 s (got "..n..")")
check(last.id.type=="TppHorse2" and last.id.index==0,"sent to the local horse, index 0")
check(math.abs(last.cmd.goalPosition.z-last.cmd.startPosition.z-0.02)<1e-6,"goal 2 cm ahead")
local stats=c:has("horse_probe=play_stats;mode=yaw20;")
check(stats and stats:find("err_p50_cm=",1,true) and stats:find("leap=0",1,true) and stats:find("sends=",1,true),"play_stats with err and leap")
local before=c:calls("SetCallHorse");c:tick(10,16)
check(c:calls("SetCallHorse")==before,"no sends after the play ends")

-- A horse that rises over the target counts as a leap.
c=F()
c.D.horse.track={{t=0,x=0,y=100,z=0,yaw=0},{t=2000,x=0,y=100,z=10,yaw=0}}
c:run("horse_probe","play","sg20","500")
c.stuck=true
for i=1,40 do c.horse.y=100+(i>10 and i<20 and 0.8 or 0);c:tick(1,16) end
stats=c:has("horse_probe=play_stats;mode=sg20;")
check(stats and stats:find("leap=1;",1,true),"one leap episode counted: "..tostring(stats))

-- leap and yaw are measured against the last placement sent (not the moving recording): a recorded gallop bob that the
-- placed horse does not follow is no leap (run 0059d2b6: identical dy patterns in every mode).
c=F()
local bob={};for i=0,40 do bob[#bob+1]={t=i*50,x=0,y=100+(i%2)*2,z=i*0.1,yaw=0} end
c.D.horse.track=bob
c:run("horse_probe","play","yaw20","1500")
for _=1,100 do c:tick(1,16) end
stats=c:has("horse_probe=play_stats;mode=yaw20;")
check(stats and stats:find("leap=0;",1,true) and stats:find("yaw_p50_deg=0;",1,true),"a bobbing recording placed exactly is no leap: "..tostring(stats))
-- chase2: 2 Hz, start at the horse's own census point.
c=F()
c.D.horse.track={{t=0,x=0,y=100,z=0,yaw=0},{t=4000,x=0,y=100,z=20,yaw=0}}
c.stuck=true
c:run("horse_probe","play","chase2","2000")
for _=1,130 do c:tick(1,16) end
n,last=c:calls("SetCallHorse")
check(n>=3 and n<=5,"chase2 about 2 Hz (got "..n..")")
check(last.cmd.startPosition.x==10 and last.cmd.startPosition.z==20,"chase starts where the horse stands")

-- hold: the ACTION-only mask MgsCoopHorseHold; raw E edges logged; hold 0 resets it.
c=F()
ok,msg=c:run("horse_probe","hold","1")
check(ok and #c.masks==1 and c.masks[1].settingName=="MgsCoopHorseHold" and c.masks[1].buttons==0x20 and c.masks[1].sticks==0 and c.masks[1].except==false,"ACTION-only pad mask")
c.rawHeld=true;c:tick(2,16)
local edge=c:has("horse_probe=raw_e;held=1;")
check(edge and edge:find("dist_cm=200",1,true) and edge:find("mounted=0",1,true),"raw E edge with the horse distance")
c.rawHeld=false;c:tick(1,16)
check(c:has("horse_probe=raw_e;held=0;"),"raw E release edge")
ok,msg=c:run("horse_probe","hold","0")
check(ok and #c.resets==1 and c.resets[1].settingName=="MgsCoopHorseHold","hold 0 resets the mask")
-- The raw E watch outlives hold 0 (an unmasked E is logged too: the poll is live); stop ends it.
c.rawHeld=true;c:tick(2,16)
local n1=0;for _,l in ipairs(c.logs)do if l:find("horse_probe=raw_e;held=1;",1,true)then n1=n1+1 end end
check(n1==2,"raw E still watched after hold 0 (edges "..n1..")")
c:run("horse_probe","stop");c.rawHeld=false;c:tick(2,16)
local n0=0;for _,l in ipairs(c.logs)do if l:find("horse_probe=raw_e;held=0;",1,true)then n0=n0+1 end end
check(n0==1,"stop ends the raw E watch")

-- hostage: the two getters on TppHorse2 index 0 (hash names IsCarriedHostage / GetCarriedHostage).
c=F();c.hostage=true
ok,msg=c:run("horse_probe","hostage")
check(ok and msg:find("horse_probe=hostage;is=true;get=1;",1,true),"hostage reads: "..tostring(msg))

-- watch: horse root, the local player in the horse frame, the nearest guard within 4 m of the horse.
c=F()
ok,msg=c:run("horse_probe","watch","600")
for _=1,45 do c:tick(1,16) end
local w=c:has("horse_probe=watch;t=")
check(w and w:find("p=0,200,0",1,true),"player 2 m to the horse's right: "..tostring(w))
check(w:find("g=-60,0,140",1,true),"guard 0.6 m behind, 1.4 m up: "..tostring(w))
check(c:has("horse_probe=watch;step=done"),"watch ends on its timer")

-- pin: CARGO_V2 hold, body 1 warped every frame to the partner's point raised dy; released at the end.
c=F()
ok,msg=c:run("horse_probe","pin","120","200")
check(ok and msg:find("horse_probe=pin;step=start",1,true),"pin starts")
for _=1,20 do c:tick(1,16) end
local holds,releases=0,0
for _,t in ipairs(c.receipts)do if t=="return 'MGS_COOP_CARGO_V2' -- 1 0 0" then holds=holds+1 elseif t=="return 'MGS_COOP_CARGO_V2' -- 0 0 0" then releases=releases+1 end end
check(holds>=10 and releases==1,"hold while pinned, one release (holds "..holds..")")
check(#c.warps>=10 and c.warps[1].index==1 and math.abs(c.warps[1].pos[2]-101.2)<1e-6 and c.warps[1].pos[1]==15,"body 1 at the partner point raised 1.2 m")
check(c:has("horse_probe=pin;step=end;"),"pin end line")

-- stop ends a running probe and releases what it owns.
c=F()
c:run("horse_probe","hold","1");c:run("horse_probe","pin","50","5000");c:tick(3,16)
ok,msg=c:run("horse_probe","stop")
check(ok and #c.resets==1,"stop releases the mask")
local rel=0;for _,t in ipairs(c.receipts)do if t=="return 'MGS_COOP_CARGO_V2' -- 0 0 0" then rel=rel+1 end end
check(rel==1,"stop releases the cargo hold")
ok,msg=c:run("horse_probe","status")
check(ok and msg:find("horse_probe=status;probe=none;hold=0;",1,true),"status after stop: "..tostring(msg))

-- M2 round 2: cargo_pose sets the victim's body-0 pose on the horse (world_horse_cargo setVictimPose), lab only.
c=F()
local pose
c.W.horseCargo={setVictimPose=function(p,q,b,d,r)pose={p,q,b,d,r};return true end}
ok,msg=c:run("horse_probe","cargo_pose","1700","1300","200","800","2")
check(ok and pose and pose[1]==1700 and pose[2]==1300 and pose[3]==200 and pose[4]==800 and pose[5]==2 and
 msg=="horse_probe=cargo_pose;placed_mm=1700;census_mm=1300;back_mm=200;down_mmps=800;reset=2","cargo_pose sets the victim pose")
ok=c:run("horse_probe","cargo_pose","9000","1300","200","800","2")
check(not ok,"cargo_pose out of range: refused")
ok,msg=c:run("horse_probe","cargo_pose","1300","900","440","1000","4")
check(ok and pose[5]==4,"cargo_pose reset 4: back to the Lua Warp")
c=F({mode=2});c.W.horseCargo={setVictimPose=function()return true end}
ok=c:run("horse_probe","cargo_pose","1700","1300","200","800","2")
check(not ok,"cargo_pose outside the lab: refused")

print("debug_horse_lua_check: "..checks.." checks PASS")
