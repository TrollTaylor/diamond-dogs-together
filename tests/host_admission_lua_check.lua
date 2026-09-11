-- Exercise the actual admission fragment against lifecycle and network fixtures.
local checks=0
local function check(value) checks=checks+1; assert(value,checks) end
local function pack(...) return {n=select('#',...),...} end
local function fixture(options)
    options=options or {}
    local events={}
    local env={vars={missionCode=options.mission or 30050},
        Tpp={MgsCoop={hostSessionCreated=options.created~=false}},e={}}
    setmetatable(env,{__index=_G})
    local function event(value) events[#events+1]=value end
    env.loadstring=function(source)
        if source=="return 'MGS_COOP_HOST_ADMISSION_V1'" then
            event('authorize')
            if options.compileFailure then return nil end
            return function() return options.authorized end
        elseif source=="return 'MGS_COOP_HOST_ADMISSION_OBSERVED_V1'" then event('observe')
        elseif source=="return 'MGS_COOP_HOST_RELEASE_V1'" then event('release')
        else error('Unexpected receipt: '..source) end
        return function() end
    end
    if not options.noApi then
        env.TppNetworkUtil={SessionEnableAccept=function(value)
            check(type(value)=='boolean'); event(value and 'open' or 'close')
        end}
    end
    env.e.OnMissionGameStart=function(...)
        local args=pack(...); check(args.n==3 and args[1]==nil and args[2]==7 and args[3]==nil)
        event('start')
        if options.raise then error(options.raise) end
        if options.yieldStart then coroutine.yield('loading') end
        return nil,'started',nil
    end
    env.e.OnTerminate=function(...)
        event('terminate')
        if options.raiseTerminate then error(options.raiseTerminate) end
        return ...
    end
    if options.noTerminate then env.e.OnTerminate=nil end
    local chunk=assert(loadfile(arg[1])); setfenv(chunk,env); chunk()
    return env,events
end
local env,events=fixture({authorized=true})
local values=pack(env.e.OnMissionGameStart(nil,7,nil))
check(values.n==3 and values[1]==nil and values[2]=='started' and values[3]==nil)
check(table.concat(events,',')=='start,authorize,open,observe')
env.e.OnMissionGameStart(nil,7,nil)
check(table.concat(events,',')=='start,authorize,open,observe,start')
values=pack(env.e.OnTerminate(nil,9,nil))
check(values.n==3 and values[2]==9)
check(table.concat(events,',')=='start,authorize,open,observe,start,authorize,close,observe,release,terminate')
env.e.OnTerminate()
check(events[#events]=='terminate' and events[#events-1]=='release')
for _,options in ipairs({{authorized=false},{authorized=1},{authorized='unhandled'},
    {authorized=true,mission=40050},{authorized=true,created=false},
    {authorized=true,noApi=true},{authorized=true,compileFailure=true},
    {authorized=true,noTerminate=true}}) do
    env,events=fixture(options)
    env.e.OnMissionGameStart(nil,7,nil)
    for _,event in ipairs(events) do check(event~='open' and event~='close') end
end
local token={}
env,events=fixture({authorized=true,raise=token})
local ok,err=pcall(env.e.OnMissionGameStart,nil,7,nil)
check(not ok and err==token and table.concat(events,',')=='start')
env,events=fixture({authorized=true,yieldStart=true})
local co=coroutine.create(function() return env.e.OnMissionGameStart(nil,7,nil) end)
ok,err=coroutine.resume(co)
check(ok and err=='loading' and table.concat(events,',')=='start')
values=pack(coroutine.resume(co))
check(values.n==4 and values[1]==true and values[3]=='started')
check(table.concat(events,',')=='start,authorize,open,observe')
env,events=fixture({authorized=true,raiseTerminate=token})
env.e.OnMissionGameStart(nil,7,nil)
ok,err=pcall(env.e.OnTerminate)
check(not ok and err==token)
check(table.concat(events,',')=='start,authorize,open,observe,authorize,close,observe,release,terminate')
print(checks..' admission checks: lifecycle ordering, one open, close/release, unavailable API, refused ownership, nil returns, errors and yields; no game calls')
