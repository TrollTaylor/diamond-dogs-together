-- Supply drops synced (r1101; lua/coop/world_supply.lua, docs/world-sync-protocol.md section 25): a local supply-box
-- request seen by the native observer goes to the partner as SUPPLY 0x45 kind 1 (drop); the partner's copy requests the
-- same box at the same aim point (Player.RequestToSupplyCbox inside a mirror window, so it is never sent back); a box
-- that is gone on one copy (collected or vanished) is sent as kind 2 (gone) and the other copy removes its own with the
-- lab remove lever. Native answers are faked here: MGS_COOP_SUPPLY_V1 poll / mirror / remove.
local path=assert(arg[1]);local f=assert(io.open(path,"rb"));local src=f:read("*a");f:close()
local checks=0
local function check(v,why)checks=checks+1;if not v then error(checks..": "..why,2)end end
check(#src:gsub("\r\n","\n")<=32000,"the module fits the 32000 LF-byte module limit")
local OP=69
local function F(o)
 o=o or {}
 local c={tx={},logs={},notes={},calls={},bullets={},receipts={},now=0,mirror=false,
  nat={on=o.on~=false and 1 or 0,st=0,fl=0,last=0,ev={}}}
 local env=setmetatable({},{__index=_G});env._G=env
 env.vars={missionCode=o.mission or 30010,playerPosX=o.px or 10,playerPosY=o.py or 300,playerPosZ=o.pz or -20}
 -- As in the game (run c66d418b: step=mirror_skip;reason=missing_api): Vector3 is a callable object, not a function.
 env.Vector3=setmetatable({},{__call=function(_,x,y,z)return {x=x,y=y,z=z}end})
 env.SupplyCbox={DROP_TYPE_AMMO=3}
 function c:push(e)
  self.nat.last=self.nat.last+1;e.seq=self.nat.last;self.nat.ev[#self.nat.ev+1]=e;return e.seq
 end
 -- {seq,kind,mirror,drop,equip,parts,x,y,z,level,wait,flags,aim,caller,state}
 function c:request(x,y,z,o2)
  o2=o2 or {}
  return self:push({0,1,self.mirror and 1 or 0,o2.drop or 3,o2.equip or 5,o2.parts or 0,x,y,z,o2.level or 1,o2.wait or 7,o2.flags or 6,
   o2.aim or 65535,0x15C3ED9,0})
 end
 function c:reset()return self:push({0,2,self.mirror and 1 or 0,0,0,0,0,0,0,0,0,0,65535,0x15CB885,self.nat.st})end
 env.Player={RequestToSupplyCbox=function(t)
  c.calls[#c.calls+1]=t
  if c.nat.on==1 then c:request(math.floor(t.aimPosition.x*1000+0.5),math.floor(t.aimPosition.y*1000+0.5),math.floor(t.aimPosition.z*1000+0.5),
   {drop=t.dropType,equip=t.equipId,parts=t.partsType,level=t.level,wait=t.waitLevel})end
 end}
 env.TppSupportRequest={RequestDropBullet=function(t)c.bullets[#c.bullets+1]=t end}
 env.TppUiCommand={AnnounceLogView=function()end}
 local W={host=o.host~=false,mode=o.mode or 1,paired=o.paired~=false,epoch=5,OP={},handlers={},startHooks={},stopHooks={},
  pairedHooks={},unpairedHooks={},observers={},mission=o.mission or 30010,isStarted=true,active=true}
 W.role=W.host and 1 or 2
 W.api={str32=function(s)return "h:"..s end}
 function W.log(s)c.logs[#c.logs+1]=s end
 function W.now()return c.now end
 function W.inGame()return c.inGame~=false end
 function W.note(code,a,b)c.notes[#c.notes+1]={code,a,b};return true end
 function W.tx(op,...)c.tx[#c.tx+1]={op,...};return c.txOk~=false end
 function W.get(t,k)if t==nil then return nil end;return t[k]end
 function W.tick()c.baseTicks=(c.baseTicks or 0)+1 end
 function W.observe(class,msg)c.observed=(c.observed or 0)+1 end
 function W.receipt(text)
  c.receipts[#c.receipts+1]=text
  local cmd,arg=text:match("^return 'MGS_COOP_SUPPLY_V1' %-%- (%a+) (%d+)$")
  if not cmd then return nil end
  local n=c.nat
  if cmd=="poll" then
   if n.on~=1 then return {on=0}end
   local ev={}
   for _,e in ipairs(n.ev)do if e.seq>tonumber(arg)then local r={};for i=1,15 do r[i]=e[i]end;r[1]=e.seq;ev[#ev+1]=r end end
   return {on=1,inst=1,box=1,st=n.st,fl=n.fl,last=n.last,lost=0,ev=ev}
  elseif cmd=="mirror" then c.mirror=arg=="1";return {ok=1}
  elseif cmd=="remove" then c.removed=(c.removed or 0)+1;return c.removeReply or {ok=1,st=n.st}end
 end
 local P={handles={heal=true},run=function(a)return true,"base:"..a[1]end}
 local D={player=P,api={get=W.get}}
 env.e={MgsCoopWorld=W,MgsCoopDebug=(o.debug~=false) and D or nil}
 setfenv(assert(loadstring(src)),env)()
 c.env,c.W,c.S,c.P,c.D=env,W,W.supply,P,D
 function c:tick(n)for _=1,n or 1 do self.now=self.now+250;self.W.tick()end end
 function c:has(list,text)for _,v in ipairs(list)do if tostring(v):find(text,1,true)then return true end end;return false end
 function c:rx(...)return self.W.handlers[OP]({...})end
 return c
end
-- Loading.
local c=F()
check(type(c.S)=="table" and c.W.OP.SUPPLY==OP and type(c.W.handlers[OP])=="function","W.supply, OP.SUPPLY 69 and its handler")
c:tick();check(c.baseTicks==1,"the previous W.tick still runs")
check(c:has(c.receipts,"return 'MGS_COOP_SUPPLY_V1' -- poll 0"),"the first poll asks for every event")
-- Key off on this copy: nothing is sent or requested.
c=F({on=false});c:tick(4);check(#c.tx==0 and not c:has(c.logs,"step=drop_local"),"key off: no SUPPLY sent")
c:rx(1,1,1,1,3,5,0,1000,2000,3000,1,7,6,0);check(#c.calls==0 and c:has(c.logs,"step=rx_off"),"key off: a partner's drop is not mirrored (logged)")
-- (1) The host's own drop goes to the guest as kind 1 with its aim point, drop type, equip, parts, level, wait and flags.
c=F();c:request(-1234567,321000,5000,{drop=3,equip=5,parts=0,level=1,wait=7,flags=6});c:tick()
check(#c.tx==1,"one SUPPLY for the host's drop")
local t=c.tx[1]
check(t[1]==OP and t[2]==1 and t[3]==1 and t[4]==1 and t[5]==1 and t[6]==3 and t[7]==5 and t[8]==0 and t[9]==-1234567 and t[10]==321000 and
 t[11]==5000 and t[12]==1 and t[13]==7 and t[14]==6 and t[15]==0,"SUPPLY kind 1: seq, origin host, oseq 1, the request's fields, reason 0")
check(c:has(c.logs,"event=supply;step=drop_local;origin=1;oseq=1;drop=3;equip=5;parts=0;x_mm=-1234567;y_mm=321000;z_mm=5000;sent=1"),"logged")
check(c.S.cur and c.S.cur.origin==1 and c.S.cur.oseq==1,"the box is this copy's current box")
c:tick(3);check(#c.tx==1,"an event is sent once")
-- An aim object (or no aim point) sends this player's own position instead.
c=F({px=11.5,py=301,pz=-21});c:request(0,0,0,{aim=4096});c:tick()
check(c.tx[1][9]==11500 and c.tx[1][10]==301000 and c.tx[1][11]==-21000 and c:has(c.logs,"aim=player"),"aim object: the caller's position")
-- Unpaired: logged, not sent; the box is still this copy's.
c=F({paired=false});c:request(1000,2000,3000);c:tick()
check(#c.tx==0 and c:has(c.logs,"step=drop_local") and c:has(c.logs,"sent=0"),"unpaired: not sent")
-- (2) The guest mirrors the host's drop inside a mirror window and never sends it back.
c=F({host=false});c:rx(9,1,1,4,3,5,0,-1234567,321000,5000,1,7,6,0)
check(#c.calls==1,"one Player.RequestToSupplyCbox")
local q=c.calls[1]
check(q.dropType==3 and q.equipId==5 and q.partsType==0 and q.level==1 and q.waitLevel==7 and q.callHeliSound==true,"the host's request fields")
check(math.abs(q.aimPosition.x+1234.567)<1e-6 and math.abs(q.aimPosition.y-321)<1e-6 and math.abs(q.aimPosition.z-5)<1e-6,"the same aim point in m")
check(c.receipts[#c.receipts-1]:find("mirror 1",1,true) and c.receipts[#c.receipts]:find("mirror 0",1,true) and not c.mirror,
 "the request ran inside the mirror window, closed after")
check(c.S.cur and c.S.cur.origin==1 and c.S.cur.oseq==4,"the guest's current box is the host's box 4")
check(#c.notes==1 and c.notes[1][1]==21 and c.notes[1][2]==1,"note 21 a=1: the partner's drop toast")
check(c:has(c.logs,"event=supply;step=mirror;origin=1;oseq=4;ok=1"),"logged")
c:tick();check(#c.tx==0 and c:has(c.logs,"step=mirror_seen"),"the mirror's own request is seen and never sent back")
-- A repeated drop (same origin and box) is mirrored once.
c:rx(10,1,1,4,3,5,0,-1234567,321000,5000,1,7,6,0);check(#c.calls==1,"a duplicate drop is not requested twice")
-- A Vector3 that fails is caught inside the request: logged ok=0, the mirror window closed.
c=F({host=false});c.env.Vector3=setmetatable({},{__call=function()error("bad vector")end});c:rx(9,1,1,4,3,5,0,1000,2000,3000,1,7,6,0)
check(c:has(c.logs,"event=supply;step=mirror;origin=1;oseq=4;ok=0") and not c.mirror and #c.calls==0,"a failing Vector3: ok=0, window closed")
-- Echo guard: a request seen outside the mirror window within 3 s of a mirror and within 1 m of its aim point is the
-- mirror's own (should the game ever run it outside Lua's call): never sent back, so two copies never ping-pong.
c=F({host=false});c:rx(9,1,1,4,3,5,0,-1234567,321000,5000,1,7,6,0);c:request(-1234300,321100,5600);c:tick()
check(#c.tx==0 and c:has(c.logs,"event=supply;step=echo;") and c.S.cur and c.S.cur.oseq==4,"an echo of the mirror is not sent")
c:request(-1200000,321000,5000);c:tick()
check(#c.tx==1 and c.tx[1][3]==1 and c.tx[1][4]==2,"a request 34 m away right after is this player's own: sent")
c=F({host=false});c:rx(9,1,1,4,3,5,0,-1234567,321000,5000,1,7,6,0);c:tick(13);c:request(-1234567,321000,5000);c:tick()
check(#c.tx==1 and c.tx[1][4]==2,"the same spot more than 3 s after the mirror: this player's own drop, sent")
-- (3) The guest collects the host's box: live, then reset outside the request grace -> kind 2 gone, reason 1 near.
c=F({host=false,px=-1234.5,py=321,pz=5});c:rx(9,1,1,4,3,5,0,-1234567,321000,5000,1,7,6,0);c:tick()
c.nat.st=14;c:tick(12)
c.nat.st=0;c:reset();c:tick()
check(#c.tx==1 and c.tx[1][3]==2 and c.tx[1][4]==1 and c.tx[1][5]==4 and c.tx[1][15]==1,"gone: kind 2, the host's box 4, reason 1 near")
check(c.S.cur==nil and c:has(c.logs,"step=gone_local;origin=1;oseq=4;reason=1"),"the box is gone here")
-- A reset inside the request grace (the game's own reset of the old box) is not a gone.
c=F();c:request(1000,2000,3000);c:tick();c:reset();c:tick()
check(#c.tx==1 and c.S.cur and c.S.cur.oseq==1,"a reset right after the request is the request's own")
-- A box that was live and reads state 0 later (no reset event) is gone too; far from the player: reason 2.
c=F({px=900,py=0,pz=900});c:request(1000,2000,3000);c:tick();c.nat.st=14;c:tick(12);c.nat.st=0;c:tick()
check(#c.tx==2 and c.tx[2][3]==2 and c.tx[2][15]==2,"state 0 after live: gone, reason 2 far")
-- A new drop replaces the current box: no gone for the old one.
c=F();c:request(1000,2000,3000);c:tick();c.nat.st=14;c:tick(12);c:reset();c:request(5000,2000,3000);c:tick()
check(#c.tx==2 and c.tx[2][3]==1 and c.tx[2][5]==2,"a replacing drop sends kind 1 for the new box only")
-- (4) The host removes its copy when the guest took the host's box.
c=F();c:request(1000,2000,3000);c:tick();c.nat.st=14;c:tick(12)
c:rx(3,2,1,1,0,0,0,0,0,0,0,0,0,1)
check(c.removed==1 and c.S.cur==nil,"the host's own box is removed")
check(c:has(c.logs,"step=remove;origin=1;oseq=1;ok=1"),"logged")
check(c.notes[#c.notes][1]==21 and c.notes[#c.notes][2]==2,"note 21 a=2: the partner took it")
c:reset();c:tick();check(#c.tx==1 and c:has(c.logs,"step=removed"),"the lever's own reset is never sent back")
-- A gone for another box removes nothing.
c=F();c:request(1000,2000,3000);c:tick();c:rx(3,2,1,7,0,0,0,0,0,0,0,0,0,1)
check(not c.removed and c.S.cur and c:has(c.logs,"step=gone_unknown"),"gone for another box: nothing removed")
-- A refused remove is logged; it changed nothing on this screen, so no toast.
c=F();c:request(1000,2000,3000);c:tick();c.removeReply={ok=0,why="no_box"};c:rx(3,2,1,1,0,0,0,0,0,0,0,0,0,1)
check(c:has(c.logs,"ok=0;why=no_box"),"a refused remove is logged")
check(#c.notes==0,"a refused remove shows no toast")
-- The game's own end of a delivered box (run ddf44cee: every AMMO / FULTON box goes state 8 landed -> 14 delivered, then
-- its fade timer resets it 2 s later, on both copies at once): step=delivered, nothing sent, no toast on the partner.
c=F();c:request(1000,2000,3000);c:tick();c.nat.st=8;c:tick(12);c.nat.st=14;c:tick();c:reset();c.nat.st=0;c:tick()
check(#c.tx==1 and c.S.cur==nil and c:has(c.logs,"event=supply;step=delivered;origin=1;oseq=1;"),"a reset at state 14: delivered, not sent")
c:tick(4);check(#c.tx==1 and not c:has(c.logs,"step=gone_local"),"and no gone later (state 0)")
c=F({host=false});c:rx(9,1,1,4,3,5,0,1000,2000,3000,1,7,6,0);c:tick();c.nat.st=8;c:tick(12);c.nat.st=14;c:tick();c:reset();c.nat.st=0;c:tick()
check(#c.tx==0 and c.S.cur==nil and c:has(c.logs,"event=supply;step=delivered;origin=1;oseq=4;"),"the mirrored box's delivery: not sent either")
-- Bad values refuse.
c=F({host=false});c:rx(9,1,1,4,3,5,0,"x",321000,5000,1,7,6,0);check(#c.calls==0,"a bad value is never requested")
c:rx(9,3,1,4,3,5,0,1,2,3,1,7,6,0);check(#c.calls==0,"an unknown kind is ignored")
-- Not in game: a partner's drop is not requested.
c=F({host=false});c.inGame=false;c:rx(9,1,1,4,3,5,0,1000,2000,3000,1,7,6,0);check(#c.calls==0 and c:has(c.logs,"reason=not_in_game"),"not in game")
-- A mission stop forgets the box.
c=F();c:request(1000,2000,3000);c:tick();for _,h in ipairs(c.W.stopHooks)do h(true)end
check(c.S.cur==nil,"stop forgets the box")
-- Census (lab only): every message for a while after a box event, capped.
c=F();c:request(1000,2000,3000);c:tick();c.W.observe(11,22,33,44,55,66)
check(c.observed==1 and c:has(c.logs,"step=msg;class=11;msg=22;a0=33;a1=44;a2=55;a3=66"),"lab census logs and passes on")
c=F({mode=2});c:request(1000,2000,3000);c:tick();c.W.observe(11,22,33,44,55,66)
check(c.observed==1 and not c:has(c.logs,"step=msg"),"no census in play")
-- The stock messages (Player OnPickUpSupplyCbox / OnComeOutSupplyCbox; the game's s10030_sequence.lua): this player took
-- the synced box -> kind 2 reason 1 (collected) at once, whatever the box state or the distance; lab and play.
c=F({host=false,mode=2,px=900,py=0,pz=900});c:rx(9,1,1,4,3,5,0,-1234567,321000,5000,1,7,6,0);c:tick()
c.W.observe("h:Player","h:OnComeOutSupplyCbox",1,2)
check(#c.tx==0 and c:has(c.logs,"event=supply;step=come_out;") and c.S.cur,"OnComeOutSupplyCbox is logged, the box stays")
c.W.observe("h:Player","h:OnPickUpSupplyCbox",1,2)
check(#c.tx==1 and c.tx[1][3]==2 and c.tx[1][4]==1 and c.tx[1][5]==4 and c.tx[1][15]==1 and c.S.cur==nil,
 "OnPickUpSupplyCbox: gone at once, the host's box 4, reason 1 collected (far from the aim point too)")
check(c:has(c.logs,"event=supply;step=pickup;") and c:has(c.logs,"step=gone_local;origin=1;oseq=4;reason=1;why=pickup"),"logged")
check(c.observed==2,"the stock messages still reach the previous observer")
c.nat.st=0;c:reset();c:tick(12);check(#c.tx==1,"the box's own reset after the pickup sends nothing more")
c=F();c.W.observe("h:Player","h:OnPickUpSupplyCbox",1,2)
check(#c.tx==0 and c:has(c.logs,"event=supply;step=pickup;") and c:has(c.logs,"cur=none"),"a pickup with no synced box: logged only")
c=F();c:request(1000,2000,3000);c:tick();c.W.observe("h:GameObject","h:OnPickUpSupplyCbox",1,2)
check(#c.tx==1 and c.S.cur and not c:has(c.logs,"step=pickup"),"the same message name in another class: ignored")
c=F({on=false});c:tick();c.W.observe("h:Player","h:OnPickUpSupplyCbox",1,2);check(#c.tx==0,"key off: a pickup sends nothing")
-- Debug commands (the player module's dispatch, wrapped on the first tick).
c=F();c:tick()
check(c.P.handles.supply==true,"the supply command is registered")
local ok,msg=c.P.run({"supply","state"},1);check(ok and msg:find("on=1",1,true) and msg:find("box=",1,true),"supply state")
ok,msg=c.P.run({"supply","drop"},1)
check(ok and #c.calls==1 and c.calls[1].dropType==3 and math.abs(c.calls[1].aimPosition.x-10)<1e-6 and msg:find("submitted",1,true),
 "supply drop: an ammo box at this player's position")
ok,msg=c.P.run({"supply","bullet"},1);check(ok and #c.bullets==1 and c.bullets[1].pos.x==10,"supply bullet: the support request path")
ok,msg=c.P.run({"supply","remove"},1);check(ok and c.removed==1,"supply remove: the lab lever on this copy")
ok,msg=c.P.run({"heal"},1);check(ok and msg=="base:heal","other commands still reach the player module")
ok,msg=c.P.run({"supply","x"},1);check(not ok,"bad supply arguments refuse")
c=F({debug=false});c:tick();check(true,"no debug menu: inert")
print("world_supply_lua_check: "..checks.." checks")
