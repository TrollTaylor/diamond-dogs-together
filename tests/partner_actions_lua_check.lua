local checks = 0
local function check(v, why) checks = checks+1; assert(v, tostring(checks)..": "..why) end
local env = {e = {}}
setmetatable(env, {__index = _G})
local chunk = assert(loadfile(arg[1])); setfenv(chunk, env); chunk()
local A = env.e.MgsCoopPartnerActions
check(A and A.enabled == false and A.s == nil, "producer default off")
local function observe(t, fire, reload, mission, equip, stance)
    return A.observe(true, true, mission or 30050, equip or 873, stance or 0, t, fire or false, reload or false, 0)
end
observe(100, true, true); check(A.s.cursor == 0, "initial held state baselines")
for t = 101,200 do observe(t, true, true) end
check(A.s.cursor == 0, "hold does not count bullets/reloads")
observe(210); observe(220, true); observe(230); observe(240, true); observe(250, true, true)
check(A.s.fireCount == 2 and A.s.reloadCount == 1, "raw state pulses under100ms remain distinct")
check(A.s.history[1].tick == 220 and A.s.history[2].tick == 240 and A.s.history[3].kind == 2, "original event time and mixed order")
local g = A.s.generation
observe(260,true,true,30050,874); check(A.s.generation == g+1 and A.s.cursor == 0, "equip cancellation")
observe(270,true,true,30050,874,1); check(A.s.generation == g+2 and A.s.cursor == 0, "stance cancellation")
observe(280,true,true,30020,874,1); check(A.s.generation == g+3, "mission cancellation")
observe(270,true,true,30020,874,1); check(A.s.generation == g+4, "clock backwards cancellation")
A.reset(); observe(300); check(A.s.generation == g+5, "explicit reset increments next generation")
for i=1,10 do observe(300+i*20,true); observe(310+i*20) end
check(#A.s.history == 8 and A.s.cursor == 10 and A.s.history[1].token == 3 and A.s.history[8].token == 10, "bounded retained history")
local source = A.receipt()
local values = {}; for value in source:gmatch("%-%- (.*)$") do for n in value:gmatch("%d+") do values[#values+1]=tonumber(n) end end
check(#values == 34 and values[10] == 8 and values[11] == 3, "strict receipt 10+3*count integers")
A.s.cursor, A.s.fireCount, A.s.reloadCount = 4294967295, 4294967295, 0
observe(600,true); check(A.s.cursor == 0 and A.s.history[8].token == 0, "u32 event identity rollover")
source = A.receipt(); check(not source:find("e%+") and not source:find("%-[1-9]"), "unsigned receipt integers never signed or exponent")
A.reset(); observe(4294967290); observe(10,true); check(A.s.cursor == 1, "tick rollover forward")
check(A.observe(false,true,30050,873,0,20,false,false,0) == nil and A.s == nil, "disabled cancels")
for _, bad in ipairs({-1,4294967296,0/0,math.huge}) do check(A.observe(true,true,30050,873,0,bad,false,false,0) == nil, "invalid clock rejected") end
check(A.observe(true,true,30050,873,3,20,false,false,0) == nil, "invalid stance rejected")
check(A.observe(true,true,30050,873,0,20,false,false,4) == nil, "locomotion flags rejected in gun companion")

local fired, reload, aim, now = false,false,false,1
env.vars = {missionCode = 30050}; env.PlayerStatus = {GUN_FIRE=7,GUN_FIRE_SUPPRESSOR=8}
env.PlayerInfo = {OrCheckStatus=function(s) return s[1]==7 and fired or false end}
env.Time = {GetRawElapsedTimeSinceStartUp=function() return now end}
env.PlayerPad = {HOLD=1,RELOAD=2}; env.PlayerVars = {scannedButtonsDirect=0}
env.bit = {band=function(a,b) if b==1 then return a%2 else return a>=2 and 2 or 0 end end}
check(A.sample(true,873,0) == nil, "retail binding sample still off by default")
A.enabled=true; A.sample(true,873,0)
fired=true; now=1.01; env.PlayerVars.scannedButtonsDirect=3; A.sample(true,873,0)
check(A.s.fireCount==1 and A.s.reloadCount==1 and A.s.flags==11, "verified raw public state and requested pad provenance")
fired=false; now=1.02; env.PlayerVars.scannedButtonsDirect=0; A.sample(true,873,0)
fired=true; now=1.03; A.sample(true,873,0); check(A.s.fireCount==2, "raw fire rise not old latched status")
if arg[2] then
    local file = assert(io.open(arg[2], "wb")); file:write(A.receipt()); file:close()
end
env.PlayerInfo.OrCheckStatus = function() error("unavailable") end
check(A.sample(true,873,0) == nil and A.s == nil, "missing/error binding cancels instead of fabricating edge")
print("PASS "..checks.." Lua action producer checks (mock public bindings only)")
