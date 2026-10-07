-- Executes the actual fragment in a host Lua 5.1 fixture; no game or saves.
local checks = 0
local function check(value) checks=checks+1; assert(value, checks) end
local function fixture(options)
    options = options or {}
    local state = {equipmentPreparationMode="offline_allocation_probe", equipmentPreparationReturned=true,
        playerPackageSelected=true, playerPackageMission=30010, archiveDirectReadSucceeded=true}
    local localPlayer = {currentInventorySlot=1, weapons={[0]=871,[1]=873,[2]=875}, missionCode=30010}
    local calls, queries, receipts, attempts, writes = 0,0,0,0,0
    local supplied, events = nil,{}
    local env = {Tpp={MgsCoop=state}, vars=setmetatable({}, {
        __index=localPlayer, __newindex=function() writes=writes+1; error("vars write forbidden") end}),
        TppEquip={EQP_WP_East_sr_020=46}}
    env.TppEquip.RequestLoadToEquipMissionBlock=function(ids)
        calls=calls+1; supplied=ids; events[#events+1]="public"
        check(type(ids)=="table" and #ids==1 and ids[1]==46 and ids[2]==nil)
        if options.publicError then error("public load failure") end
    end
    env.loadstring=function(source)
        if source=="return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_QUERY_V1'" then
            return function()
                queries=queries+1; events[#events+1]="query"
                if options.queryError then error("query failure") end
                if options.disabled or attempts~=0 then return false end
                attempts=attempts+1
                return true
            end
        end
        check(source=="return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_RETURNED_V1'"
            or source=="return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_FAILED_V1'")
        return function()
            receipts=receipts+1; events[#events+1]="receipt"
            if options.receiptError then error("receipt failure") end
            return not options.verifyRefused and not options.publicError
        end
    end
    if options.edit then options.edit(env, state, localPlayer) end
    setmetatable(env,{__index=_G})
    local function run()
        local chunk=assert(loadfile(arg[1])); setfenv(chunk,env)
        local ok,failure=pcall(chunk); check(ok)
        check(writes==0 and localPlayer.currentInventorySlot==1)
        check(localPlayer.weapons[0]==871 and localPlayer.weapons[1]==873 and localPlayer.weapons[2]==875)
        return state, calls, queries, receipts, attempts, supplied, events
    end
    return run, function() attempts=0 end
end

local run,begin=fixture()
local state,calls,queries,receipts,attempts,ids,events=run()
check(calls==1 and queries==1 and receipts==1 and attempts==1)
check(state.stockPreloadAttempted and state.stockPreloadReturned and state.stockPreloadVerified)
check(state.stockPreloadEquipId==46 and state.stockPreloadMission==30010 and state.stockPreloadError==nil)
check(table.concat(events,",")=="query,public,receipt")
state,calls,queries,receipts=run()
check(calls==1 and queries==2 and receipts==1)
begin(); state,calls,queries,receipts=run()
check(calls==2 and queries==3 and receipts==2)

for _,options in ipairs({{disabled=true},{queryError=true}}) do
    run=fixture(options); state,calls,queries,receipts=run()
    check(calls==0 and queries==1 and receipts==0 and state.stockPreloadAttempted==nil)
end
for _,edit in ipairs({
    function(e,s) s.equipmentPreparationMode="native_session" end,
    function(e,s) s.equipmentPreparationReturned=false end,
    function(e,s) s.playerPackageSelected=false end,
    function(e,s) s.playerPackageMission=30020 end,
    function(e,s) s.archiveDirectReadSucceeded=false end,
    function(e,s) s.hostSessionCreated=true end,
    function(e,s) s.clientPrepared=true end,
    function(e,s,p) p.missionCode=30020 end,
    function(e) e.TppEquip.EQP_WP_East_sr_020=794 end,
    function(e) e.TppEquip.RequestLoadToEquipMissionBlock=nil end,
    function(e) e.TppEquip=nil end,
    function(e) e.vars=nil end,
    function(e) e.Tpp=nil end,
    function(e) e.loadstring=false end,
}) do
    run=fixture({edit=edit}); state,calls,queries,receipts=run()
    check(calls==0 and queries==0 and receipts==0 and state.stockPreloadAttempted==nil)
end
run=fixture({publicError=true}); state,calls,queries,receipts,attempts=run()
check(calls==1 and receipts==1 and attempts==1 and state.stockPreloadReturned==false
    and state.stockPreloadVerified==false and state.stockPreloadError:find("public load failure",1,true))
state,calls,queries,receipts=run(); check(calls==1 and queries==2 and receipts==1)
for _,options in ipairs({{verifyRefused=true},{receiptError=true}}) do
    run=fixture(options); state,calls,queries,receipts=run()
    check(calls==1 and receipts==1 and state.stockPreloadReturned and not state.stockPreloadVerified)
end
print("equipment_stock_preload_lua_check: "..checks.." checks passed; game_calls=0")
