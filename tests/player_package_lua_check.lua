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
    env.diag, env.states = {}, {}
    env.loadstring = function(s)
        if s:find("MGS_COOP_PACK_STATE_V1", 1, true) then env.states[#env.states+1]=s
        elseif s:find("MGS_COOP_PACK_SELECTED_V1", 1, true) then env.receipts[#env.receipts+1]=s else env.diag[#env.diag+1]=s end
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
-- Mother Base layouts (docs/player-package-selection.md "Mother Base layouts"):
-- AddFOBLayoutPack adds f30050_ly%03d.fpk right after f30050.fpk. ly001..ly003
-- select; any other layout, a second one, a misplaced one or one in another
-- mission's list refuses with the list unchanged. A fresh module instance
-- keeps the receipt caps independent of the checks above.
local lp, lenv = module(arg[2], true)
local mb, afgh, mafr = lenv.Tpp.MgsCoop.partnerMissions[30050], lenv.Tpp.MgsCoop.partnerMissions[30010], lenv.Tpp.MgsCoop.partnerMissions[30020]
local common = {"/Assets/tpp/pack/location/mtbs/pack_common/mtbs_script.fpk", "/Assets/tpp/pack/mission2/common/mis_com_helicopter.fpk",
    "/Assets/tpp/pack/mission2/common/mis_com_mtbs.fpk", "/Assets/tpp/pack/mission2/common/mis_com_dd_soldier_wait.fpk"}
local function layout(n) return string.format("/Assets/tpp/pack/mission2/free/f30050/f30050_ly%03d.fpk", n) end
local function concat(a, b) local r = {} for _, v in ipairs(a) do r[#r+1] = v end for _, v in ipairs(b) do r[#r+1] = v end return r end
local function makeIn(mod, code, paths)
    return mod.MakeMissionPackList(code, function() for _, path in ipairs(paths) do mod.AddMissionPack(path) end end)
end
local function refusedWith(code, reason, scene, paths)
    local before = #lenv.diag
    local list = makeIn(lp, code, paths)
    check(#list == #paths + 2)
    for i, path in ipairs(paths) do check(list[i] == path) end
    check(lenv.Tpp.MgsCoop.playerPackageSelected == false and lenv.Tpp.MgsCoop.playerPackageMission == nil)
    check(lenv.Tpp.MgsCoop.IsPartnerBodyMission(code) == false)
    local receipt = lenv.diag[before + 1] or ""
    check(receipt:find("MGS_COOP_PACK_REFUSED_V1' -- mission=" .. code .. "; reason=" .. reason .. "; scene=" .. scene .. "; path=", 1, true) ~= nil)
    check(#receipt < 256)
end
-- The verbatim real-save list of run 39845163 (six entries; the fixture adds
-- the two coloring packs of a missing TppColoringSystem).
local receiptsBefore = #lenv.receipts
local real = concat(common, {mb.original, layout(3)})
local list = makeIn(lp, 30050, real)
check(#list == 9 and list[5] == mb.replacement and list[6] == layout(3) and list[9] == mb.controller)
for i = 1, 4 do check(list[i] == common[i]) end
check(list[7] == "/Assets/tpp/pack/fova/mecha/all/mfv_scol_c11.fpk" and list[8] == "/Assets/tpp/pack/fova/mecha/all/mfv_scol_c07.fpk")
check(lenv.Tpp.MgsCoop.IsPartnerBodyMission(30050) == true)
check(#lenv.receipts == receiptsBefore + 1)
check(lenv.receipts[#lenv.receipts] == "return 'MGS_COOP_PACK_SELECTED_V1' -- mission=30050; single_controller=true; layout=f30050_ly003")
check(#lenv.diag == 0)  -- a selection logs no list and no refusal
for _, n in ipairs({1, 2}) do
    list = makeIn(lp, 30050, concat(common, {mb.original, layout(n)}))
    check(list[5] == mb.replacement and list[6] == layout(n) and list[#list] == mb.controller and #list == 9)
    check(lenv.receipts[#lenv.receipts]:find("; layout=f30050_ly00" .. n, 1, true) ~= nil)
    check(lenv.Tpp.MgsCoop.IsPartnerBodyMission(30050) == true)
end
-- No layout (the pre-layout shape) still selects, with layout=none.
list = makeIn(lp, 30050, concat(common, {mb.original}))
check(#list == 8 and list[5] == mb.replacement and list[8] == mb.controller)
check(lenv.receipts[#lenv.receipts]:find("; layout=none", 1, true) ~= nil)
check(#lenv.diag == 0)
for _, n in ipairs({0, 4, 500, 999}) do refusedWith(30050, "layout_unknown", "layout", concat(common, {mb.original, layout(n)})) end
refusedWith(30050, "layout_count2", "layout", concat(common, {mb.original, layout(1), layout(3)}))
refusedWith(30050, "layout_order", "layout", concat(common, {layout(3), mb.original}))
refusedWith(30050, "layout_order", "layout", {common[1], mb.original, common[4], layout(3)})
refusedWith(30010, "layout_mission", "layout", {afgh.original, layout(3)})
refusedWith(30020, "layout_mission", "layout", {mafr.original, layout(3)})
-- The demo stage and the battle-gear hangars keep their own controller: refused.
refusedWith(30050, "foreign_or_variant", "demo_stage", concat(common, {"/Assets/tpp/pack/mission2/free/f30050/f30051.fpk"}))
for level = 0, 5 do
    refusedWith(30050, "foreign_or_variant", "hangar",
        concat(common, {string.format("/Assets/tpp/pack/mission2/free/f30050/f30050_hanger_btg%02d.fpk", level)}))
end
refusedWith(30010, "foreign_or_variant", "story", {"/Assets/tpp/pack/mission2/free/f30010/f30011.fpk"})
refusedWith(30010, "foreign_or_variant", "other", {afgh.original, mb.controller})
-- Lists are logged only after a refusal: every PACK_LIST group follows a
-- PACK_REFUSED line, at most 6 lists of at most 96 lines.
local lists, prior = 0, ""
for _, line in ipairs(lenv.diag) do
    if line:find("MGS_COOP_PACK_LIST_V1", 1, true) then
        check(prior:find("MGS_COOP_PACK_REFUSED_V1", 1, true) or prior:find("MGS_COOP_PACK_LIST_V1", 1, true))
        if line:find("; i=1; ", 1, true) then lists = lists + 1; check(prior:find("MGS_COOP_PACK_REFUSED_V1", 1, true)) end
        check(#line < 256)
    end
    prior = line
end
check(lists == 6)
for _ = 1, 30 do makeIn(lp, 30050, concat(common, {mb.original, layout(7)})) end
local refusedLines = 0
for _, line in ipairs(lenv.diag) do if line:find("MGS_COOP_PACK_REFUSED_V1", 1, true) then refusedLines = refusedLines + 1 end end
check(refusedLines == 24)
for _ = 1, 20 do makeIn(lp, 30050, real) end
check(#lenv.receipts == 16)
-- Compact state must still change after both diagnostic caps are exhausted.
-- In particular, a refused rebuild followed by a successful rebuild before
-- mission start must clear the refusal even when SELECTED logs have stopped.
local function lastState(wire) return wire.states[#wire.states] or "" end
local function isState(wire, code, result, scene)
    local expected = "return 'MGS_COOP_PACK_STATE_V1' -- mission=" .. code .. "; result=" .. result .. "; scene=" .. (scene or "none")
    return lastState(wire) == expected and #expected <= 96
end
local diagCount = #lenv.diag
local selectedCount = #lenv.receipts
local stateCount = #lenv.states
list = makeIn(lp, 30050, concat(common, {mb.original, layout(7)}))
check(isState(lenv, 30050, "refused", "layout") and not lenv.Tpp.MgsCoop.IsPartnerBodyMission(30050))
check(#lenv.diag == diagCount and #lenv.receipts == selectedCount and #lenv.states == stateCount + 2)
list = makeIn(lp, 30050, real)
check(isState(lenv, 30050, "selected") and lenv.Tpp.MgsCoop.IsPartnerBodyMission(30050))
check(#lenv.diag == diagCount and #lenv.receipts == selectedCount and #lenv.states == stateCount + 4)
-- Clear precedes the original callback, so a throwing rebuild and unsupported
-- mission cannot inherit any earlier accepted/refused result.
makeIn(lp, 30050, {"/Assets/tpp/pack/mission2/free/f30050/f30051.fpk"})
check(isState(lenv, 30050, "refused", "demo_stage"))
local sawClear = false
ok, err = pcall(lp.MakeMissionPackList, 30050, function()
    sawClear = isState(lenv, 30050, "clear")
    error(errorToken)
end)
check(not ok and err == errorToken and sawClear and isState(lenv, 30050, "clear"))
check(not lenv.Tpp.MgsCoop.IsPartnerBodyMission(30050))
makeIn(lp, 40050, {})
check(isState(lenv, 40050, "clear"))
lenv.Tpp.MgsCoop.archiveDirectReadSucceeded = false
makeIn(lp, 30050, real)
check(isState(lenv, 30050, "clear") and not lenv.Tpp.MgsCoop.IsPartnerBodyMission(30050))
lenv.Tpp.MgsCoop.archiveDirectReadSucceeded = true
makeIn(lp, 30020, {mafr.original})
check(isState(lenv, 30020, "selected") and lenv.Tpp.MgsCoop.IsPartnerBodyMission(30020))
for _, wire in ipairs(lenv.states) do check(#wire <= 96) end
-- Optional: the real TppMissionList.GetMissionPackagePath (arg[3], the pinned
-- 3c250e19 source) through the patched module, with bounded game stubs.
local missionChecks = 0
if arg[3] then
    local scene = {stage = false, hangar = false, level = 0, coloring = {}}
    local packs = setmetatable({
        MTBS_SCRIPT = common[1], HELICOPTER = common[2], MTBS_MISSION_AREA = common[3], DD_SOLDIER_WAIT = common[4],
        AFGH_SCRIPT = "/Assets/tpp/pack/location/afgh/pack_common/afgh_script.fpk",
        MAFR_SCRIPT = "/Assets/tpp/pack/location/mafr/pack_common/mafr_script.fpk",
        AFGH_MISSION_AREA = "/Assets/tpp/pack/mission2/common/mis_com_afgh.fpk", MAFR_MISSION_AREA = "/Assets/tpp/pack/mission2/common/mis_com_mafr.fpk",
        AFGH_DECOY = "/Assets/tpp/pack/collectible/decoy/decoy_svs.fpk", MAFR_DECOY = "/Assets/tpp/pack/collectible/decoy/decoy_pf.fpk",
        ORDER_BOX = "/Assets/tpp/pack/mission2/common/mis_com_order_box.fpk"},
        {__index = function(_, k) return "/Assets/tpp/pack/mission2/common/fixture_" .. tostring(k) .. ".fpk" end})
    local menv = {
        gvars = {pck_missionPackLabelName = "default", f30050_missionPackIndex = -1, s10240_isPlayedFuneralDemo = false},
        vars = {mbLayoutCode = 3, mbClusterId = 0},
        Fox = {StrCode32 = function(s) return s end},
        Tpp = {IsTypeFunc = function(v) return type(v) == "function" end, IsTypeString = function(v) return type(v) == "string" end,
            IsTypeTable = function(v) return type(v) == "table" end, MgsCoop = newCoop(true)},
        TppDefine = {LOCATION_ID = setmetatable({}, {__index = function(_, k) return k end}), MISSION_COMMON_PACK = packs,
            MISSION_ENUM = {}, CLUSTER_DEFINE = {Develop = 1}},
        TppLocation = {GetLocationName = function() return scene.location end, SetBuddyBlock = function() end},
        TppDemo = {SetNextMBDemo = function() end, CanUpdateMBDemo = function() return false end, UpdateMBDemo = function() end,
            GetMBDemoName = function() return nil end, IsQuestStart = function() return false end, IsSortieMBDemo = function() return false end,
            IsUseMBDemoStage = function() return scene.stage end, IsBattleHangerDemo = function() return scene.hangar end},
        TppStory = {GetBattleGearDevelopLevel = function() return scene.level end, HueyHasKantokuGrass = function() return false end},
        TppEneFova = {AddUniquePackage = function() end, AddUniqueSettingPackage = function() end},
        TppHostage2 = {SetHostageType = function() end},
        EnemyFova = {INVALID_FOVA_VALUE = 65535},
        TppSoldierFace = {SetUseBlackDdFova = function() end},
        TppColoringSystem = {GetAdditionalColoringPackFilePaths = function() return scene.coloring end},
        TppMission = {IsHardMission = function() return false end},
        Mission = {}, receipts = {}, diag = {}, states = {},
    }
    menv.loadstring = function(s)
        if s:find("MGS_COOP_PACK_STATE_V1", 1, true) then menv.states[#menv.states+1] = s
        elseif s:find("MGS_COOP_PACK_SELECTED_V1", 1, true) then menv.receipts[#menv.receipts+1] = s else menv.diag[#menv.diag+1] = s end
        return loadstring(s)
    end
    setmetatable(menv, {__index = _G})
    local packChunk = assert(loadfile(arg[2])); setfenv(packChunk, menv); menv.TppPackList = packChunk()
    local listChunk = assert(loadfile(arg[3])); setfenv(listChunk, menv)
    local missionList = listChunk()
    check(type(missionList) == "table" and type(missionList.GetMissionPackagePath) == "function")
    local coop = menv.Tpp.MgsCoop
    local function run(code, location, label)
        scene.location = location
        menv.gvars.pck_missionPackLabelName = label or "default"
        missionChecks = missionChecks + 1
        return missionList.GetMissionPackagePath(code)
    end
    local function selected(code)
        return coop.playerPackageSelected == true and coop.playerPackageMission == code and coop.IsPartnerBodyMission(code) == true
    end
    local function lastDiag() return menv.diag[#menv.diag] or "" end
    for n = 1, 3 do
        menv.vars.mbLayoutCode = n
        local got = run(30050, "mtbs")
        check(selected(30050) and #got == 7 and got[5] == mb.replacement and got[6] == layout(n) and got[7] == mb.controller)
        for i = 1, 4 do check(got[i] == common[i]) end
        check(menv.receipts[#menv.receipts]:find("; layout=f30050_ly00" .. n, 1, true) ~= nil and menv.gvars.f30050_missionPackIndex == 0)
    end
    check(#menv.diag == 0)
    -- An unreadable layout seed (0) is refused rather than guessed.
    menv.vars.mbLayoutCode = 0
    local got = run(30050, "mtbs")
    check(not selected(30050) and got[5] == mb.original and got[6] == layout(0) and #got == 6 and lastDiag():find("MGS_COOP_PACK_LIST_V1", 1, true) ~= nil)
    menv.vars.mbLayoutCode = 3
    -- DemoStage (label or a queued demo-stage demo) and BattleHanger 0..5 keep their controller.
    got = run(30050, "mtbs", "DemoStage")
    check(not selected(30050) and got[#got] ~= mb.controller and got[4] == "/Assets/tpp/pack/mission2/free/f30050/f30051.fpk")
    scene.stage = true
    got = run(30050, "mtbs")
    check(not selected(30050) and got[4] == "/Assets/tpp/pack/mission2/free/f30050/f30051.fpk" and menv.gvars.pck_missionPackLabelName == "DemoStage")
    scene.stage = false
    for level = 0, 5 do
        scene.level = level
        got = run(30050, "mtbs", "BattleHanger")
        check(not selected(30050) and got[4] == string.format("/Assets/tpp/pack/mission2/free/f30050/f30050_hanger_btg%02d.fpk", level))
        check(menv.gvars.f30050_missionPackIndex == 2)
    end
    scene.hangar = true
    got = run(30050, "mtbs")
    check(not selected(30050) and got[4]:find("_hanger_btg05.fpk", 1, true) ~= nil)
    scene.hangar = false
    -- After the demo, the AfterDemo reload rebuilds the normal list: the partner returns.
    got = run(30050, "mtbs", "AfterDemo")
    check(selected(30050) and got[6] == layout(3) and got[7] == mb.controller)
    -- Afghanistan: the normal pack selects, the Volgin recovery demo refuses.
    got = run(30010, "afgh")
    check(selected(30010) and got[6] == afgh.replacement and got[#got] == afgh.controller)
    got = run(30010, "afgh", "recoverVolginDemo")
    check(not selected(30010) and got[6] == "/Assets/tpp/pack/mission2/free/f30010/f30011.fpk" and got[#got] ~= afgh.controller)
    -- Africa: nothing in the normal list (coloring packs included) can refuse.
    scene.coloring = {"/Assets/tpp/pack/fova/mecha/all/mfv_scol_c11.fpk", "/Assets/tpp/pack/fova/mecha/all/mfv_scol_c07.fpk"}
    local diagBefore = #menv.diag
    got = run(30020, "mafr")
    check(selected(30020) and got[6] == mafr.replacement and got[#got] == mafr.controller and #got == 9 and #menv.diag == diagBefore)
    check(menv.receipts[#menv.receipts] == "return 'MGS_COOP_PACK_SELECTED_V1' -- mission=30020; single_controller=true; layout=none")
    -- A mission without a partner body leaves no selection behind.
    run(40050, "mtbs")
    check(coop.playerPackageSelected == false and coop.playerPackageMission == nil and isState(menv, 40050, "clear"))
end
print(checks.." Lua checks ("..missionChecks.." real TppMissionList requests): all three free-roam controllers, Mother Base layouts ly001..ly003, unchanged demo/hangar/story/ACC/layout refusals, duplicate/cross-location ownership, missing archive, reloads, errors and bounded receipts/lists; no engine allocation")
