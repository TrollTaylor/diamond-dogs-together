-- Execute the actual original/patched module with bounded game API fixtures.
local controller = "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk"
local mission = "/Assets/tpp/pack/mission2/free/f30050/f30050.fpk"
local replacement = "/Assets/tpp/pack/mission2/free/f30050/mgs_coop_f30050.fpk"
local function module(path, mounted)
    local env = { Tpp = {
        IsTypeFunc = function(v) return type(v) == "function" end,
        IsTypeString = function(v) return type(v) == "string" end,
        MgsCoop = { archiveDirectReadSucceeded = mounted, gameplayEnabled = false },
    }, receipts = {}, gvars = {pck_missionPackLabelName=0},
       Fox = {StrCode32=function() return 1 end} }
    env.loadstring = function(s) env.receipts[#env.receipts+1]=s; return loadstring(s) end
    setmetatable(env, {__index=_G})
    local chunk=assert(loadfile(path)); setfenv(chunk,env)
    local mod=chunk()
    return mod, env
end
local original = module(arg[1],true)
local patched, env = module(arg[2],true)
local checks=0
local function check(v) checks=checks+1; assert(v,checks) end
for _,code in ipairs({10010,10020,10151,30010,30020,30050,30150,40050,50050}) do
    local calls=0
    local function run(mod)
        return mod.MakeMissionPackList(code,function(n)
            check(n==code); calls=calls+1; mod.AddMissionPack(mission)
        end)
    end
    local a,b=run(original),run(patched)
    check(calls==2 and b==patched.missionPackList)
    check(#b==#a+(code==30050 and 1 or 0))
    for i,v in ipairs(a) do check(b[i]==(code==30050 and v==mission and replacement or v)) end
    if code==30050 then check(b[#b]==controller) end
end
check(env.Tpp.MgsCoop.gameplayEnabled==false)
check(env.Tpp.MgsCoop.playerPackageMission==nil)
check(env.Tpp.MgsCoop.playerPackageSelected==false)
-- Rebuilding a mission list must not leak packages from the previous mission.
local function normal() patched.AddMissionPack(mission) end
check(#patched.MakeMissionPackList(30050,normal)==4)
check(env.Tpp.MgsCoop.playerPackageSelected==true and env.Tpp.MgsCoop.playerPackageMission==30050)
check(#patched.MakeMissionPackList(40050,nil)==2)
check(env.Tpp.MgsCoop.playerPackageSelected==false and env.Tpp.MgsCoop.playerPackageMission==nil)
local duplicate=patched.MakeMissionPackList(30050,function() normal(); patched.AddMissionPack(controller) end)
check(#duplicate==4 and duplicate[1]==replacement and duplicate[2]==controller)
-- A demo/hangar list or absent/ambiguous normal mission must never acquire a
-- second controller or report successful package selection.
for _,paths in ipairs({{}, {"/Assets/tpp/pack/mission2/free/f30050/f30051.fpk"},
                       {"/Assets/tpp/pack/mission2/free/f30050/f30050_hanger_btg01.fpk"},
                       {mission,mission}, {replacement}, {mission,replacement}}) do
    local list=patched.MakeMissionPackList(30050,function()
        for _,path in ipairs(paths) do patched.AddMissionPack(path) end
    end)
    check(#list==#paths+2)
    for i,path in ipairs(paths) do check(list[i]==path) end
    check(env.Tpp.MgsCoop.playerPackageSelected==false and env.Tpp.MgsCoop.playerPackageMission==nil)
end
local errorToken={}
local ok,err=pcall(patched.MakeMissionPackList,30050,function() error(errorToken) end)
check(not ok and err==errorToken)
check(env.Tpp.MgsCoop.playerPackageSelected==false and env.Tpp.MgsCoop.playerPackageMission==nil)
env.Tpp.MgsCoop.archiveDirectReadSucceeded=false
local withoutArchive=patched.MakeMissionPackList(30050,normal)
check(#withoutArchive==3 and withoutArchive[1]==mission)
check(env.Tpp.MgsCoop.playerPackageSelected==false and env.Tpp.MgsCoop.playerPackageMission==nil)
env.Tpp.MgsCoop.archiveDirectReadSucceeded=true
for _,mounted in ipairs({false,true}) do
    local p,e=module(arg[2],mounted)
    if mounted then e.Tpp.MgsCoop=nil end
    check(#p.MakeMissionPackList(30050,nil)==2)
end
for i=1,25 do patched.MakeMissionPackList(30050,normal) end
check(#env.receipts==16)
print(checks.." Lua checks: controller replacement, unchanged demo/hangar lists, other missions, ambiguous ownership, missing archive, reloads, errors and bounded receipts; no engine allocation")
