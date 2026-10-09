-- Execute the actual original/patched module with bounded game API fixtures.
local controller = "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk"
local mission = "/Assets/tpp/pack/mission2/free/f30050/f30050.fpk"
local replacement = "/Assets/tpp/pack/mission2/free/f30050/mgs_coop_f30050.fpk"
local bootstrapPath = (arg[0]:match('^(.*[/\\])') or '') .. '../lua/coop/bootstrap.lua'
local function newCoop(mounted)
    local bootstrapEnv = {e = {}, loadstring = function() return function() end end}
    setmetatable(bootstrapEnv, {__index = _G})
    local chunk = assert(loadfile(bootstrapPath)); setfenv(chunk, bootstrapEnv); chunk()
    local coop = bootstrapEnv.e.MgsCoop
    coop.archiveDirectReadSucceeded = mounted
    return coop
end
local function module(path, mounted)
    local env = { Tpp = {
        IsTypeFunc = function(v) return type(v) == "function" end,
        IsTypeString = function(v) return type(v) == "string" end,
        MgsCoop = newCoop(mounted),
    }, receipts = {}, gvars = {pck_missionPackLabelName=0},
       Fox = {StrCode32=function() return 1 end} }
    env.diag = {}
    env.loadstring = function(s)
        if s:find("MGS_COOP_PACK_SELECTED_V1", 1, true) then env.receipts[#env.receipts+1]=s else env.diag[#env.diag+1]=s end
        return loadstring(s)
    end
    setmetatable(env, {__index=_G})
    local chunk=assert(loadfile(path)); setfenv(chunk,env)
    local mod=chunk()
    return mod, env
end
local original = module(arg[1],true)
local patched, env = module(arg[2],true)
local checks=0
local function check(v) checks=checks+1; assert(v,checks) end
for _,code in ipairs({10010,10020,10151,30010,30011,30020,30050,30150,40010,40020,40050,50050}) do
    local capability = env.Tpp.MgsCoop.partnerMissions[code]
    local source = capability and capability.original or mission
    local calls=0
    local function run(mod)
        return mod.MakeMissionPackList(code,function(n)
            check(n==code); calls=calls+1; mod.AddMissionPack(source)
        end)
    end
    local a,b=run(original),run(patched)
    check(calls==2 and b==patched.missionPackList)
    check(#b==#a+(capability and 1 or 0))
    for i,v in ipairs(a) do check(b[i]==(capability and v==source and capability.replacement or v)) end
    if capability then check(b[#b]==capability.controller) end
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
-- All normal free-roam packs replace exactly one controller owner. Reject
-- variants, duplicate desired controllers and cross-location owners unchanged.
for _, code in ipairs({30010,30020,30050}) do
    local cap = env.Tpp.MgsCoop.partnerMissions[code]
    local otherCode = code == 30010 and 30020 or 30010
    local other = env.Tpp.MgsCoop.partnerMissions[otherCode]
    local function make(paths)
        return patched.MakeMissionPackList(code,function()
            for _,path in ipairs(paths) do patched.AddMissionPack(path) end
        end)
    end
    local list=make({cap.original})
    check(#list==4 and list[1]==cap.replacement and list[4]==cap.controller)
    check(env.Tpp.MgsCoop.IsPartnerBodyMission(code)==true)
    check(env.Tpp.MgsCoop.IsPartnerBodyMission(otherCode)==false)
    list=make({cap.original,cap.controller})
    check(#list==4 and list[1]==cap.replacement and list[2]==cap.controller)
    for _,paths in ipairs({{cap.original,cap.original},{cap.replacement},{cap.original,cap.replacement},
                            {cap.original,cap.controller,cap.controller},{cap.original,other.original},
                            {cap.original,other.replacement},
                            {cap.original,"/Assets/tpp/pack/mission2/free/f"..code.."/f"..(code+1)..".fpk"}}) do
        list=make(paths)
        check(#list==#paths+2)
        for i,path in ipairs(paths) do check(list[i]==path) end
        check(env.Tpp.MgsCoop.IsPartnerBodyMission(code)==false)
    end
    if cap.controller ~= other.controller then
        list=make({cap.original,other.controller})
        check(#list==4 and list[1]==cap.original and env.Tpp.MgsCoop.IsPartnerBodyMission(code)==false)
    end
    env.Tpp.MgsCoop.archiveDirectReadSucceeded=false
    list=make({cap.original})
    check(#list==3 and list[1]==cap.original and env.Tpp.MgsCoop.IsPartnerBodyMission(code)==false)
    env.Tpp.MgsCoop.archiveDirectReadSucceeded=true
end
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
print(checks.." Lua checks: all three free-roam controllers, unchanged demo/hangar/story/ACC lists, duplicate/cross-location ownership, missing archive, reloads, errors and bounded receipts; no engine allocation")
