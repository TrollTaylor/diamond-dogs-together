-- Pure late-travel policy fixture; no game or native calls.
local path=assert(arg[1],"world_goto.lua path")
local count=0
local function check(v,s) count=count+1;assert(v,s) end
local function setup()
 local c={now=0,session=17,link=1,ready=false,paired=false,seq="loading",calls=0,ack=0,logs={}}
 local W={mode=1,host=true,paired=false,t0=-3000,follow={enabled=true},OP={GOTO=1,GOTO_ACK=2},handlers={},api={str32=function()return 19 end}}
 e={MgsCoopWorld=W};vars={missionCode=40050};gvars={mis_nextMissionCodeForEmergency=0};svars={mis_isDefiniteMissionClear=true}
 TppSequence={GetCurrentSequenceName=function()return c.seq end};TppClock={DEPLOY_TIME={CURRENT=1}}
 TppMission={IsHelicopterSpace=function()return true end,SelectNextMissionHeliStartRoute=function()c.calls=c.calls+1 end,
 OnEndMissionPreparation=function()c.calls=c.calls+1 end,ReserveMissionClearOnRideOnHelicopter=function()c.calls=c.calls+1 end}
 TppDemo={SetNextMBDemo=function()c.calls=c.calls+1 end};TppDefine={MISSION_CLEAR_TYPE={RIDE_ON_HELICOPTER=1},SYS_MISSION_ID={MTBS_HELI=40050}}
 f30050_sequence={ReserveMissionClear=function()c.calls=c.calls+1 end}
 W.cfg=function()return {3,1,1,c.link,7,0,1,c.session}end
 W.get=function(t,k)return t and t[k]end;W.inGame=function()return c.ready end
 W.tx=function()c.ack=c.ack+1 end;W.log=function(s)c.logs[#c.logs+1]=s end
 assert(loadfile(path))();c.W=W
 function c.arm(step,now)W.handlers[1]({step,30020,0},now)end
 function c.tick(now)W.paired=c.paired;W.go.tick(now)end
 return c
end
for _,step in ipairs{1,2}do
 local c=setup();c.arm(step,100);c.tick(101);local a=c.ack
 c.ready=true;c.paired=true;c.seq="Seq_Game_MainGame";c.tick(100+(step==1 and 30000 or 60000))
 check(c.calls==0 and c.W.gt==nil,"expired request never calls vanilla even once newly ready")
 check(c.ack==a,"expiry emits no uncorrelated ACK that could cancel newer native work")
 check(c.logs[#c.logs]:find("reason=timeout",1,true),"bounded cancellation receipt")
 c.tick(999999);check(c.calls==0,"nil cancelled request safe")
end
local c=setup();c.arm(2,0);c.ready=true;c.seq="Seq_Game_MainGame";c.tick(59999)
check(c.calls==0 and c.W.gt~=nil,"late unpaired ACC remains pending inside lease")
c.paired=true;c.tick(60000);check(c.calls==0 and c.W.gt==nil,"late pair cannot deploy after native wait deadline")
for _,change in ipairs{"session","down","unknown","clock"}do
 c=setup();c.arm(2,100);local old=c.W.gt
 if change=="session" then c.session=18 elseif change=="down" then c.link=0 elseif change=="unknown"then c.session=0 end
 c.ready=true;c.paired=true;c.seq="Seq_Game_MainGame";c.tick(change=="clock" and 99 or 200)
 check(c.calls==0 and c.W.gt==nil and c.ack==0,"invalid lease/clock cancels without game calls or ACK: "..change)
end
c=setup();c.arm(2,0);local old=c.W.gt;c.arm(2,59999);c.ready=true;c.paired=true;c.seq="Seq_Game_MainGame";c.tick(60000)
check(c.calls==2 and c.W.gt~=old,"newer goto retained with independent deadline")
c.tick(60001);check(c.W.gt==nil and c.ack==1,"normal reserved ACK retained")
c=setup();vars.missionCode=30050;c.arm(1,0);c.ready=true;c.tick(29999);check(c.calls==1,"MB reserve before deadline preserved")
c.tick(30000);check(c.W.gt==nil and c.ack==0,"expired readback cannot ACK newer native request")
c=setup();c.W.cfg=nil;c.arm(2,0);c.ready=true;c.paired=true;c.seq="Seq_Game_MainGame";c.tick(1)
check(c.calls==2,"legacy metadata-less fixture behavior preserved before deadline")
print("world_goto_lua_check: "..count.." checks passed")
