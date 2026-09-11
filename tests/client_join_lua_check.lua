local checks=0
local function check(v) checks=checks+1; assert(v,checks) end
local function fixture(prepared, mission, result, fails, original)
    local events={}
    local env={e={},vars={missionCode=mission},Tpp={MgsCoop={clientPrepared=prepared}},
        loadstring=function(source)
            check(source=="return 'MGS_COOP_CLIENT_JOIN_V1'")
            events[#events+1]="request"
            if fails then return nil end
            return function() return result end
        end}
    env.e.OnMissionGameStart=original or function(...)
        events[#events+1]="start"
        check(select('#',...)==3)
        check(select(1,...)==7 and select(2,...)==nil and select(3,...)==9)
        return 1,nil,3
    end
    setmetatable(env,{__index=_G})
    local chunk=assert(loadfile(arg[1])); setfenv(chunk,env); chunk()
    return env,events
end
local function call(env)
    local function result(...) check(select('#',...)==3); check(select(1,...)==1 and select(2,...)==nil and select(3,...)==3) end
    result(env.e.OnMissionGameStart(7,nil,9))
end
local env,events=fixture(true,30050,true)
call(env); call(env)
check(table.concat(events,',')=='start,request,start')
check(env.Tpp.MgsCoop.clientJoinRequested==true and env.Tpp.MgsCoop.hostSessionCreated==nil)
for _,args in ipairs({{false,30050,true},{true,10010,true},{true,30050,false},{true,30050,'unhandled'},{true,30050,1},{true,30050,true,true}}) do
    local e,v=fixture(unpack(args)); call(e)
    check(e.Tpp.MgsCoop.clientJoinRequested~=true)
    check(table.concat(v,',')==(args[1] and args[2]==30050 and 'start,request' or 'start'))
end
local sentinel={}
env,events=fixture(true,30050,true,false,function() error(sentinel) end)
local ok,err=pcall(env.e.OnMissionGameStart)
check(not ok and err==sentinel and #events==0)
env,events=fixture(true,30050,true,false,function() coroutine.yield('loading'); return 1,nil,3 end)
local co=coroutine.create(function() call(env) end)
local ok,value=coroutine.resume(co)
check(ok and value=='loading' and #events==0)
check(coroutine.resume(co)); check(table.concat(events,',')=='request')
print(checks..' client lifecycle checks: post-start order, single request, host exclusion, failed/unhandled requests, nil returns, errors and yields; no game calls')
