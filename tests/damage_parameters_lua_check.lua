-- Executes lua/coop/damage_parameters.lua (and optionally the composed
-- DamageParameterTables.lua) against simulated TppDamage and native receipts.
-- Usage: lua51 damage_parameters_lua_check.lua <damage_parameters.lua>
--            [<original DamageParameterTables.lua> <composed DamageParameterTables.lua>
--             [<Dynamite c90f252 standalone/DamageParameterTables.lua>]]
-- No game calls. Native replies and ReloadDamageParameter are simulated.
local checks=0
local function check(v,why) checks=checks+1; assert(v,tostring(checks)..': '..(why or 'check failed')) end
local function read(path)
    local file=assert(io.open(path,'rb')); local text=file:read('*a'); file:close(); return text
end
local function deepEqual(a,b)
    if type(a)~='table' or type(b)~='table' then return a==b end
    for k,v in pairs(a) do if not deepEqual(v,b[k]) then return false end end
    for k in pairs(b) do if a[k]==nil then return false end end
    return true
end
local function copy(value)
    if type(value)~='table' then return value end
    local out={}
    for k,v in pairs(value) do out[k]=copy(v) end
    return out
end
local DEFAULT="local CLEAR_IGNORE_DEFENDER_FLAG = false"
local EXPERIMENT="local CLEAR_IGNORE_DEFENDER_FLAG = true"
local function experiment(source)
    local _,count=source:gsub(DEFAULT,DEFAULT)
    check(count==1,'exactly one opt-in constant, defaulting to false')
    local flipped,replaced=source:gsub(DEFAULT,EXPERIMENT)
    check(replaced==1,'experiment substitution')
    return flipped
end
-- Shared fixture: TppDamage constants resolve to their names; native receipts
-- answer from options; every Reload call and receipt is recorded in order.
-- Receipts are V1 literals, except the table activation receipt, which is V2
-- and names the loaded table mode; its full text is kept in `active`.
local ACTIVE_PREFIX="return 'MGS_COOP_DAMAGE_TABLE_ACTIVE_V2' -- "
local function run(source,options)
    options=options or {}
    local events,reloads,active={},{},{}
    local damage={ReloadDamageParameter=function(t)
        reloads[#reloads+1]=t; events[#events+1]='reload'
        if options.reloadError and #reloads==options.reloadError then error(options.token) end
    end}
    setmetatable(damage,{__index=function(_,key) return 'TppDamage.'..key end})
    local env={TppDamage=damage,mgsDamage=options.table,vars=options.vars,Tpp=options.Tpp}
    env.loadstring=function(text)
        local name
        if text:sub(1,#ACTIVE_PREFIX)==ACTIVE_PREFIX then
            name='DAMAGE_TABLE_ACTIVE'
            active[#active+1]=text
        else
            name=assert(text:match("^return 'MGS_COOP_([%u%d_]+)_V1'$"),text)
        end
        events[#events+1]=name
        return function()
            if name=='DAMAGE_ON' then return options.nativeOn~=false end
            if name=='DAMAGE_OFF' then return options.nativeOff~=false end
            error('receipt executed: '..name)
        end
    end
    setmetatable(env,{__index=_G})
    local chunk=assert(loadstring(source,'=damage'))
    setfenv(chunk,env)
    local result=chunk()
    return env,events,reloads,result,active
end
local function joined(events,from) return table.concat(events,',',from or 1) end
-- The activation receipt must name the loaded table mode, compile as the same
-- literal chunk the native observer sees, and fit its 240-byte printable copy.
local function checkActive(active,expected,why)
    check(#active==1 and active[1]==ACTIVE_PREFIX..expected,why..': '..tostring(active[1]))
    local compiled=loadstring(active[1])
    check(compiled~=nil and compiled()=='MGS_COOP_DAMAGE_TABLE_ACTIVE_V2',why..': receipt is a literal chunk')
    check(#active[1]<=240 and not active[1]:find('[^\32-\126]'),why..': receipt fits the native log copy')
end

-- 1. Module semantics on a synthetic table.
local function sample()
    return {Other={kept=true},DamageParameter={0,
        {'ATK_A',0,0,0,0,0,0,0,'T','P',0,0,1,7},
        {'ATK_B',0,0,0,0,0,0,0,'T','P',0,0,0,7},
        {'ATK_C',0,0,0,0,0,0,0,'T','P',0,0,1,7},
        {'ATK_D',0,0,0,0,0,0,0,'T','P',0,0,2,7},
        {'ATK_E'}}}
end
local module=read(arg[1])
local env,events,reloads,_,active=run(module,{table=sample()})
local original=env.mgsDamage
check(#reloads==1 and rawequal(reloads[1],original),'module load reloads the original table')
check(env.MgsCoopDamage.changedRows==0,'parity default changes no rows')
check(env.MgsCoopDamage.clearsIgnoreDefender==false,'parity default reported')
check(env.Tpp==nil and joined(events)=='reload','no activation outside a prepared co-op scene')
check(env.MgsCoopDamage.SetActive(true)==true,'activation')
check(joined(events,2)=='DAMAGE_ON,reload,DAMAGE_TABLE_ACTIVE','native first, then table, then receipt')
checkActive(active,'mode=parity;changed_rows=0','parity activation')
local coop=reloads[2]
check(not rawequal(coop,original) and not rawequal(coop.DamageParameter,original.DamageParameter),'separate co-op table')
check(deepEqual(coop,original),'co-op table equals the original under Dynamite parity')
check(rawequal(coop.Other,original.Other),'non-row entries shared')
for i=2,#original.DamageParameter do
    check(not rawequal(coop.DamageParameter[i],original.DamageParameter[i]),'rows copied')
    check(coop.DamageParameter[i][13]==original.DamageParameter[i][13],'field 13 untouched')
end
check(deepEqual(original,sample()),'original table never mutated')
local before=#events
check(env.MgsCoopDamage.SetActive(true)==true,'repeat activation rechecks native')
check(joined(events,before+1)=='DAMAGE_ON' and #reloads==2 and #active==1,'no repeated reload or receipt')
before=#events
check(env.MgsCoopDamage.SetActive(false)==true,'restore')
check(joined(events,before+1)=='DAMAGE_OFF,reload,DAMAGE_TABLE_RESTORED','native off, original table, receipt')
check(rawequal(reloads[3],original),'original table restored')
before=#events
check(env.MgsCoopDamage.SetActive(nil)==true and joined(events,before+1)=='DAMAGE_OFF','repeat restore rechecks native')
check(env.MgsCoopDamage.SetActive(1)==true and #reloads==3,'non-boolean is treated as off')
-- Native refusal leaves the original table loaded.
env,events,reloads,_,active=run(module,{table=sample(),nativeOn=false})
check(env.MgsCoopDamage.SetActive(true)==false and joined(events)=='reload,DAMAGE_ON','refused native rule')
check(#active==0,'no activation receipt after a native refusal')
-- A reload failure restores the native rule and propagates the same error.
local token={}
env,events,reloads=run(module,{table=sample(),reloadError=2,token=token})
local ok,err=pcall(env.MgsCoopDamage.SetActive,true)
check(not ok and err==token,'reload error identity')
check(joined(events)=='reload,DAMAGE_ON,reload,DAMAGE_OFF','native rule restored after reload failure')
before=#events
check(env.MgsCoopDamage.SetActive(false)==true and joined(events,before+1)=='DAMAGE_OFF','still inactive')
-- A failed native restoration is reported while the original table returns.
env,events,reloads=run(module,{table=sample(),nativeOff=false})
env.MgsCoopDamage.SetActive(true)
check(env.MgsCoopDamage.SetActive(false)==false and rawequal(reloads[#reloads],env.mgsDamage),'native off failure reported')
-- Late damage-module loads activate only for a prepared 30050 scene.
for _,case in ipairs({{30050,{hostSessionCreated=true},true},{30050,{clientPrepared=true},true},
    {40050,{hostSessionCreated=true},nil},{30050,{hostSessionCreated=1},nil},{30050,{},nil}}) do
    local state=case[2]
    env,events,reloads=run(module,{table=sample(),vars={missionCode=case[1]},Tpp={MgsCoop=state}})
    check(state.damageRulesActive==case[3],'late activation gate')
    check(#reloads==(case[3] and 2 or 1),'late activation reload count')
end
-- 2. The opt-in experiment path clears only nested field-13 values of 1.
local flipped=experiment(module)
env,events,reloads,_,active=run(flipped,{table=sample()})
check(env.MgsCoopDamage.changedRows==2 and env.MgsCoopDamage.clearsIgnoreDefender==true,'experiment row count')
check(env.MgsCoopDamage.SetActive(true)==true,'experiment activation')
checkActive(active,'mode=clear_field13;changed_rows=2','experiment activation')
coop=reloads[2]
local expected=sample()
expected.DamageParameter[2][13]=0
expected.DamageParameter[4][13]=0
check(deepEqual(coop,expected),'experiment clears exactly the field-13 == 1 rows')
check(deepEqual(env.mgsDamage,sample()),'experiment keeps the original table intact')
print(checks..' damage module checks: Dynamite parity default, opt-in experiment, mode receipts, activation order, refusals, errors and late activation; no game calls')

-- 3. Optional: the actual composed source against the extracted original.
if arg[2] and arg[3] then
    local before=checks
    local originalSource=read(arg[2])
    local composedSource=read(arg[3])
    check(#originalSource==82834,'pinned original size')
    local _,_,base,baseResult=run(originalSource,{})
    check(#base==1 and type(baseResult)=='table','original reloads once and returns its module')
    local vanilla=base[1]
    local rows,flagged=0,0
    for _,row in pairs(vanilla.DamageParameter) do
        if type(row)=='table' then
            rows=rows+1
            if row[13]==1 then flagged=flagged+1 end
        end
    end
    check(rows==475 and flagged==146,'vanilla rows='..rows..' field13==1 rows='..flagged)
    local composedEnv,composedEvents,composed,composedResult,composedActive=run(composedSource,{})
    check(type(composedResult)=='table','composed source returns its module')
    check(#composed==1 and deepEqual(composed[1],vanilla),'composed load reloads the unchanged vanilla table')
    check(composedEnv.MgsCoopDamage.changedRows==0,'composed default changes no rows')
    check(composedEnv.MgsCoopDamage.clearsIgnoreDefender==false,'composed default is Dynamite parity')
    check(composedEnv.MgsCoopDamage.SetActive(true)==true,'composed activation')
    check(joined(composedEvents,2)=='DAMAGE_ON,reload,DAMAGE_TABLE_ACTIVE','composed activation order')
    checkActive(composedActive,'mode=parity;changed_rows=0','composed parity activation')
    check(deepEqual(composed[2],vanilla),'composed co-op table equals vanilla, including all 146 field-13 flags')
    check(composedEnv.MgsCoopDamage.SetActive(false)==true and rawequal(composed[3],composed[1]),'composed restore')
    local state={hostSessionCreated=true}
    local _,_,late=run(composedSource,{vars={missionCode=30050},Tpp={MgsCoop=state}})
    check(state.damageRulesActive==true and #late==2,'composed late activation')
    -- The experiment path in the actual composition clears all 146 flags.
    local experimentEnv,_,experimentReloads,_,experimentActive=run(experiment(composedSource),{})
    check(experimentEnv.MgsCoopDamage.changedRows==146,'experiment clears 146 rows')
    check(experimentEnv.MgsCoopDamage.SetActive(true)==true,'experiment activation')
    checkActive(experimentActive,'mode=clear_field13;changed_rows=146','composed experiment activation')
    local cleared=experimentReloads[2]
    local expectedTable=copy(vanilla)
    for _,row in pairs(expectedTable.DamageParameter) do
        if type(row)=='table' and row[13]==1 then row[13]=0 end
    end
    check(deepEqual(cleared,expectedTable),'experiment differs from vanilla only in field 13')
    check(deepEqual(experimentReloads[1],vanilla),'experiment keeps the vanilla table for restoration')
    print((checks-before)..' composed DamageParameterTables checks: '..rows..' rows, '..flagged..
        ' field-13 flags kept by default and cleared only by the opt-in experiment')
    -- 4. Optional: Dynamite's own table (c90f252) is the parity reference. Its
    -- data equals vanilla and its trailing field-13 loop walks the outer
    -- wrapper, so the table it reloads keeps every flag.
    if arg[4] then
        before=checks
        local _,_,upstream,upstreamResult=run(read(arg[4]),{})
        check(#upstream==1 and type(upstreamResult)=='table','Dynamite table reloads once and returns its module')
        check(deepEqual(upstream[1],vanilla),'Dynamite reloads exactly the vanilla table, all 146 field-13 flags kept')
        check(deepEqual(upstream[1],composed[2]),'default co-op table equals the Dynamite table')
        print((checks-before)..' Dynamite parity checks: the upstream field-13 loop changes no row')
    end
end
