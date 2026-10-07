-- Actual equipment fragment with a default-off native authorization fixture.
-- Simulated allocation only: no native code, assets, session or game writes.
local checks = 0
local function check(value) checks = checks + 1; assert(value, checks) end
local function fixture(options)
    options = options or {}
    local events, native = {}, {attempted=false, phase="allocate"}
    local localPlayer = {index=0, weapon=873, slot=1, ammo=10, online=false, synchronizer=nil}
    local state = {hostSessionCreated=options.host == true, clientPrepared=options.client == true}
    state.playerPackageSelected = options.package ~= false
    state.playerPackageMission = options.packageMission or options.mission or 30010
    state.archiveDirectReadSucceeded = options.directRead ~= false
    state.IsPartnerBodyMission = function(mission)
        return options.supported ~= false and (mission == 30010 or mission == 30020 or mission == 30050)
    end
    local env = {vars={missionCode=options.mission or 30010}, mvars={ply_equipMissionBlockGroupSize=1777721},
        Tpp={MgsCoop=state}, Player=localPlayer}
    env.TppSystemUtility = {GetCurrentGameMode=function()
        if options.modeError then error("mode read fixture error") end
        return options.mode or "TPP"
    end}
    env.TppGameStatus = {IsSet=function(scope, flag)
        check(scope == "Mission" and flag == "S_IS_ONLINE")
        if options.onlineError then error("online read fixture error") end
        if options.onlineUnknown then return nil end
        return options.online == true
    end}
    if options.noModeApi then env.TppSystemUtility = nil end
    if options.noStatusApi then env.TppGameStatus = nil end
    setmetatable(env, {__index=_G})
    env.loadstring = function(source)
        if source == "return 'MGS_COOP_OFFLINE_EQUIPMENT_GROUPS_PREPLAYER_V2'" then
            events[#events+1] = "authorize"
            if options.queryCompileFailure then return nil end
            return function()
                if options.queryError then error("authorization fixture error") end
                if options.queryValue ~= nil then return options.queryValue end
                if options.enabled ~= true or native.phase ~= "allocate" then return false end
                if native.attempted then return false end
                native.attempted = true
                return options.stateOk ~= false and options.saves ~= false
            end
        elseif source == "return 'MGS_COOP_EQUIPMENT_GROUPS_RETURNED_V1'" then
            return function() events[#events+1] = "returned" end
        elseif source == "return 'MGS_COOP_EQUIPMENT_GROUPS_FAILED_V1'" then
            return function() events[#events+1] = "failed" end
        end
        error("Unexpected receipt: " .. source)
    end
    env.TppPlayer = {SetEquipMissionBlockGroupSize=function()
        events[#events+1] = "local_group"
        if options.firstError then error("local group fixture error") end
    end}
    env.TppEquip = {CreateEquipGhostBlockGroups=function(parameters)
        check(type(parameters) == "table" and parameters.ghostCount == 1)
        local keys=0; for _ in pairs(parameters) do keys=keys+1 end; check(keys == 1)
        events[#events+1] = "ghost_group"
        if options.secondError then error("ghost group fixture error") end
    end}
    if options.noPlayerApi then env.TppPlayer = nil end
    if options.noGhostApi then env.TppEquip.CreateEquipGhostBlockGroups = nil end
    local function run()
        local chunk = assert(loadfile(arg[1])); setfenv(chunk, env); chunk()
        -- The exact-source composition leaves these original calls after the
        -- optional insertion. A refusal or protected error must reach them.
        events[#events+1] = "vanilla_loading"
        events[#events+1] = "vanilla_instances"
        events[#events+1] = "vanilla_activation"
        check(localPlayer.index == 0 and localPlayer.weapon == 873 and localPlayer.slot == 1)
        check(localPlayer.ammo == 10 and localPlayer.online == false and localPlayer.synchronizer == nil)
        check(state.hostSessionCreated == (options.host == true) and state.clientPrepared == (options.client == true))
    end
    return env, state, events, native, run
end
local env, state, events, native, run = fixture({enabled=true})
run()
check(table.concat(events, ",") == "authorize,local_group,ghost_group,returned,vanilla_loading,vanilla_instances,vanilla_activation")
check(native.attempted and state.equipmentPreparationAttempted == true and state.equipmentPreparationReturned == true)
check(state.equipmentPreparationMode == "offline_allocation_probe" and state.equipmentPreparationError == nil)
run()
check(table.concat(events, ",") == "authorize,local_group,ghost_group,returned,vanilla_loading,vanilla_instances,vanilla_activation,authorize,vanilla_loading,vanilla_instances,vanilla_activation")
check(state.equipmentPreparationReturned == true) -- A repeated refusal cannot erase the first result.
native.phase = "ended"; run()
check(events[#events-3] == "authorize")
native.phase = "allocate"; native.attempted = false; run()
check(events[#events-3] == "returned" and state.equipmentPreparationReturned == true)

for _,options in ipairs({{}, {enabled=false}, {enabled=true,stateOk=false}, {enabled=true,saves=false},
    {enabled=true,queryCompileFailure=true}, {enabled=true,queryError=true},
    {enabled=true,queryValue=1}, {enabled=true,queryValue="true"}, {enabled=true,supported=false},
    {enabled=true,mission=40050}, {enabled=true,mission=30011},
    {enabled=true,package=false}, {enabled=true,packageMission=30020}, {enabled=true,directRead=false},
    {enabled=true,mode="MGO"}, {enabled=true,mode="unknown"}, {enabled=true,noModeApi=true},
    {enabled=true,modeError=true}, {enabled=true,online=true}, {enabled=true,onlineUnknown=true},
    {enabled=true,onlineError=true}, {enabled=true,noStatusApi=true}}) do
    env, state, events, native, run = fixture(options); run()
    check(state.equipmentPreparationAttempted == nil and state.equipmentPreparationReturned == nil)
    for _,event in ipairs(events) do check(event ~= "local_group" and event ~= "ghost_group") end
end
-- Failed native read consumes the correct allocation's attempt, including if
-- a later simulated player read becomes valid. It is not a retry path.
local options = {enabled=true,stateOk=false}
env, state, events, native, run = fixture(options); run(); options.stateOk=true; run()
check(native.attempted)
for _,event in ipairs(events) do check(event ~= "local_group" and event ~= "ghost_group") end

for _,value in ipairs({0, -1, math.huge, -math.huge, "1777721", false, 0/0}) do
    env, state, events, native, run = fixture({enabled=true})
    env.mvars.ply_equipMissionBlockGroupSize = value; run()
    check(native.attempted and state.equipmentPreparationAttempted == true and state.equipmentPreparationReturned == false)
    check(type(state.equipmentPreparationError) == "string")
    check(table.concat(events, ",") == "authorize,failed,vanilla_loading,vanilla_instances,vanilla_activation")
end
for _,missing in ipairs({"size", "mvars"}) do
    env, state, events, native, run = fixture({enabled=true})
    if missing == "size" then env.mvars.ply_equipMissionBlockGroupSize = nil else env.mvars = nil end
    run(); check(state.equipmentPreparationReturned == false and events[2] == "failed")
end
for _,options in ipairs({{enabled=true,noPlayerApi=true}, {enabled=true,noGhostApi=true},
    {enabled=true,firstError=true}, {enabled=true,secondError=true}}) do
    env, state, events, native, run = fixture(options); run()
    check(native.attempted and state.equipmentPreparationReturned == false and type(state.equipmentPreparationError) == "string")
    check(events[#events-3] == "failed")
    if options.firstError then check(events[2] == "local_group" and events[3] == "failed") end
    if options.secondError then check(events[2] == "local_group" and events[3] == "ghost_group") end
    local before = #events; run(); check(#events == before+4)
end
-- Existing native-session preparation never asks for offline authorization.
-- Its established behavior is retained even without the new offline mvars gate.
for _,options in ipairs({{host=true}, {client=true}, {host=true,client=true}}) do
    env, state, events, native, run = fixture(options); env.mvars=nil; run()
    check(table.concat(events, ",") == "local_group,ghost_group,returned,vanilla_loading,vanilla_instances,vanilla_activation")
    check(not native.attempted and state.equipmentPreparationReturned == true and state.equipmentPreparationMode == "native_session")
end
print(checks .. " equipment allocation Lua checks passed (simulated lifecycle; no game calls)")
