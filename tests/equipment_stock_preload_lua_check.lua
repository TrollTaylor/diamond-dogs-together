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
-- The generalized preload (V2): any partner-body mission, the ids native qualified.
local function fixture2(options)
    options = options or {}
    local state = {equipmentPreparationMode="offline_allocation_probe", equipmentPreparationReturned=true,
        playerPackageSelected=true, playerPackageMission=30020, archiveDirectReadSucceeded=true,
        IsPartnerBodyMission=function(code) return code==30010 or code==30020 or code==30050 end}
    local localPlayer = {missionCode=30020}
    local calls, queries, receipts, v1 = 0,0,0,0
    local supplied, events = nil,{}
    local env = {Tpp={MgsCoop=state}, vars=setmetatable({}, {__index=localPlayer,
        __newindex=function() error("vars write forbidden") end}), TppEquip={EQP_WP_East_sr_020=46}, math=math}
    env.TppEquip.RequestLoadToEquipMissionBlock=function(ids)
        calls=calls+1; supplied=ids; events[#events+1]="public"
        if options.publicError then error("public load failure") end
    end
    env.loadstring=function(source)
        if source=="return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_QUERY_V1'" then
            return function() v1=v1+1; return false end
        end
        if source=="return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_QUERY_V2'" then
            return function()
                queries=queries+1; events[#events+1]="query"
                if options.queryError then error("query failure") end
                if options.reply~=nil then return options.reply end
                return {343,46,9}
            end
        end
        check(source=="return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_RETURNED_V2'"
            or source=="return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_FAILED_V2'")
        return function()
            receipts=receipts+1; events[#events+1]="receipt"
            return not options.verifyRefused and not options.publicError
        end
    end
    if options.edit then options.edit(env, state, localPlayer) end
    setmetatable(env,{__index=_G})
    local chunk=assert(loadfile(arg[1])); setfenv(chunk,env)
    local ok=pcall(chunk); check(ok)
    return state, calls, queries, receipts, supplied, events, v1
end
local s2,c2,q2,r2,ids2,ev2,v1=fixture2()
check(c2==1 and q2==1 and r2==1 and v1==0)
check(type(ids2)=="table" and #ids2==3 and ids2[1]==343 and ids2[2]==46 and ids2[3]==9)
check(s2.equipPreloadAttempted and s2.equipPreloadReturned and s2.equipPreloadVerified and s2.equipPreloadMission==30020)
check(s2.equipPreloadIds[1]==343 and #s2.equipPreloadIds==3 and s2.stockPreloadAttempted==nil)
check(table.concat(ev2,",")=="query,public,receipt")
-- In 30010 the stock46 query runs too (native refuses it in the generalized setting).
s2,c2,q2,r2,ids2,ev2,v1=fixture2({edit=function(e,s,p) p.missionCode=30010; s.playerPackageMission=30010 end})
check(c2==1 and q2==1 and r2==1 and v1==1 and s2.stockPreloadAttempted==nil)
for _,reply in ipairs({false, {}, {1,2,3,4}, {0}, {560}, {1.5}, {"9"}, "return"}) do
    s2,c2,q2,r2=fixture2({reply=reply})
    check(c2==0 and q2==1 and r2==0 and s2.equipPreloadAttempted==nil)
end
s2,c2,q2,r2=fixture2({queryError=true}); check(c2==0 and q2==1 and r2==0)
for _,edit in ipairs({
    function(e,s) s.equipmentPreparationMode="native_session" end,
    function(e,s) s.equipmentPreparationReturned=false end,
    function(e,s) s.IsPartnerBodyMission=nil end,
    function(e,s,p) p.missionCode=40010; s.playerPackageMission=40010 end,
    function(e,s) s.playerPackageMission=30010 end,
    function(e,s) s.archiveDirectReadSucceeded=false end,
    function(e,s) s.hostSessionCreated=true end,
    function(e) e.TppEquip.RequestLoadToEquipMissionBlock=nil end,
    function(e) e.loadstring=false end,
}) do
    s2,c2,q2,r2=fixture2({edit=edit}); check(c2==0 and q2==0 and r2==0 and s2.equipPreloadAttempted==nil)
end
s2,c2,q2,r2=fixture2({publicError=true})
check(c2==1 and r2==1 and s2.equipPreloadReturned==false and s2.equipPreloadVerified==false
    and s2.equipPreloadError:find("public load failure",1,true))
s2,c2,q2,r2=fixture2({verifyRefused=true}); check(c2==1 and r2==1 and s2.equipPreloadReturned and not s2.equipPreloadVerified)
print("equipment_stock_preload_lua_check: "..checks.." checks passed; game_calls=0")
