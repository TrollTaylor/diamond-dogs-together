-- Executes lua/coop/damage_lifecycle.lua (a TppMain fragment) with simulated
-- damage control, online status and original termination. No game calls.
-- Usage: lua51 damage_lifecycle_lua_check.lua <damage_lifecycle.lua>
local checks=0
local function check(v,why) checks=checks+1; assert(v,tostring(checks)..': '..(why or 'check failed')) end
local function packed(...) return {n=select('#',...),...} end
local function fixture(options)
    options=options or {}
    local events={}
    local function record(name) events[#events+1]=name end
    local env={e={}}
    if not options.noTerminate then
        env.e.OnTerminate=function(...)
            record('terminate')
            check(select('#',...)==3 and select(1,...)==1 and select(2,...)==nil and select(3,...)==3,'original arguments')
            if options.yieldTerminate then coroutine.yield('terminating') end
            if options.errorTerminate then error(options.errorTerminate) end
            return 7,nil,9
        end
    end
    if not options.noState then
        env.Tpp={MgsCoop={hostSessionCreated=true,clientPrepared=true,clientJoinRequested=true,
            damageRulesActive=true,onlineInitializationOwned=options.online}}
    end
    if not options.noDamage then
        env.MgsCoopDamage={SetActive=function(value)
            check(value==false,'damage rules are only switched off')
            record('damage-off')
            if options.damageError then error(options.damageError) end
            return options.restored
        end}
    end
    env.TppGameStatus={Reset=function(group,name)
        check(group=='Mission' and name=='S_IS_ONLINE','online flag reset')
        record('online-reset')
        if options.resetError then error(options.resetError) end
    end}
    setmetatable(env,{__index=_G})
    local chunk=assert(loadfile(arg[1])); setfenv(chunk,env); chunk()
    return env,events
end
local function terminate(env)
    local r=packed(env.e.OnTerminate(1,nil,3))
    check(r.n==3 and r[1]==7 and r[2]==nil and r[3]==9,'original returns preserved')
end
local function cleared(state)
    return state.hostSessionCreated==false and state.clientPrepared==false and state.clientJoinRequested==false
end
-- Normal teardown: rules restored, owned online flag reset, then the original.
local env,events=fixture({restored=true,online=true})
terminate(env)
local state=env.Tpp.MgsCoop
check(table.concat(events,',')=='damage-off,online-reset,terminate','teardown order')
check(cleared(state) and state.damageRulesActive==false and state.onlineInitializationOwned==false,'state cleared')
check(state.sessionCleanupError==nil,'no cleanup error')
-- An unowned online flag is left alone.
env,events=fixture({restored=true,online=false})
terminate(env)
check(table.concat(events,',')=='damage-off,terminate','unowned online flag untouched')
-- A refused restoration keeps damageRulesActive set but still terminates.
for _,restored in ipairs({false,nil,1,'yes'}) do
    env,events=fixture({restored=restored})
    terminate(env)
    check(env.Tpp.MgsCoop.damageRulesActive==true and cleared(env.Tpp.MgsCoop),'refused restoration reported')
    check(events[#events]=='terminate','original still runs')
end
-- A raising restoration is recorded, not propagated.
env,events=fixture({damageError='restore failed',online=true})
terminate(env)
state=env.Tpp.MgsCoop
check(state.damageRulesActive==true and tostring(state.sessionCleanupError):find('restore failed',1,true)~=nil,'restoration error recorded')
check(table.concat(events,',')=='damage-off,online-reset,terminate','teardown continues after restoration error')
-- A raising online reset keeps ownership and records the failure.
env,events=fixture({restored=true,online=true,resetError='reset failed'})
terminate(env)
state=env.Tpp.MgsCoop
check(state.onlineInitializationOwned==true and tostring(state.sessionCleanupError):find('reset failed',1,true)~=nil,'reset error recorded')
check(state.damageRulesActive==false and events[#events]=='terminate','original still runs after reset error')
-- Missing damage control or state table: remaining cleanup and the original still run.
env,events=fixture({noDamage=true,online=true})
terminate(env)
check(cleared(env.Tpp.MgsCoop) and table.concat(events,',')=='online-reset,terminate','no damage control')
check(env.Tpp.MgsCoop.damageRulesActive==true,'flag untouched without damage control')
env,events=fixture({noState=true,restored=true})
terminate(env)
check(table.concat(events,',')=='damage-off,terminate','no state table')
-- No original termination: the fragment installs nothing.
env,events=fixture({noTerminate=true})
check(env.e.OnTerminate==nil and #events==0,'absent original left absent')
-- Original errors keep their identity after cleanup.
local token={}
env,events=fixture({restored=true,errorTerminate=token})
local ok,err=pcall(env.e.OnTerminate,1,nil,3)
check(not ok and err==token and table.concat(events,',')=='damage-off,terminate','original error identity')
-- Original yields are preserved.
env,events=fixture({restored=true,yieldTerminate=true})
local co=coroutine.create(function() return env.e.OnTerminate(1,nil,3) end)
local resumed=packed(coroutine.resume(co))
check(resumed[1]==true and resumed[2]=='terminating' and coroutine.status(co)=='suspended','yield preserved')
resumed=packed(coroutine.resume(co))
check(resumed.n==4 and resumed[1]==true and resumed[2]==7 and resumed[3]==nil and resumed[4]==9,'returns after yield')
print(checks..' damage lifecycle checks: teardown order, refused/raising restoration, online ownership, missing APIs, errors and yields; no game calls')
