-- Real lifecycle fragment with a controlled native-receipt boundary.
-- No game calls; checks forwarding, outgoing-scene identity and close ordering.
local checks=0
local function check(v,why) checks=checks+1; assert(v,why or checks) end
local function packed(...) return {n=select('#',...),...} end
local function fixture(options)
    options=options or {}
    local events={}
    local function record(s) events[#events+1]=s end
    local function original(name)
        return function(...)
            record(name)
            check(select('#',...)==3 and select(2,...)==nil,'arguments')
            if options.yieldAt==name then coroutine.yield(name) end
            if options.errorAt==name then error(options.token) end
            return 7,nil,9
        end
    end
    local env={e={OnAllocate=original('allocate'),OnInitialize=original('initialize'),OnMissionGameStart=original('start'),
        OnTerminate=original('terminate'),OnUpdate=original('update'),OnMessage=function() end},
        vars={missionCode=30050},Tpp={MgsCoop={hostSessionCreated=true}},Fox={StrCode32=function(s) return s end}}
    env.loadstring=function(source)
        local name=assert(source:match("^return 'MGS_COOP_(.+)_V1'$"),source)
        return function()
            record(name)
            if name==options.throwReceipt then error('receipt failed') end
            return name~=options.refuseReceipt
        end
    end
    env.MgsCoopDamage={SetActive=function(v) check(v==false); record('damage-off');return true end}
    env.TppNetworkUtil={SessionDisconnectPreparingMembers=function() record('prepare-close') end,
        CloseSession=function() record('close');if options.closeFails then error('close failed') end end}
    setmetatable(env,{__index=_G})
    local chunk=assert(loadfile(arg[1]));setfenv(chunk,env);chunk()
    return env,events
end
local function call(env,name)
    local r=packed(env.e[name](1,nil,3))
    check(r.n==3 and r[1]==7 and r[2]==nil and r[3]==9,'return values')
end
local env,events=fixture()
call(env,'OnAllocate');call(env,'OnInitialize');call(env,'OnMissionGameStart')
env.vars.missionCode=1 -- destination title screen during outgoing termination
call(env,'OnTerminate')
check(table.concat(events,',')==
    'ALLOCATION_ENTER,allocate,ALLOCATION_RETURNED,INITIALIZATION_ENTER,initialize,INITIALIZATION_RETURNED,RUNTIME_ENTER,start,MISSION_TERMINATING,RULES_RESTORE,damage-off,terminate,SESSION_CLOSE,prepare-close,PREPARING_DISCONNECT_RETURNED,close,SESSION_CLOSE_RETURNED,TERMINATION_RETURNED','lifecycle order')
check(env.Tpp.MgsCoop.sessionCloseReturned and env.Tpp.MgsCoop.sessionCloseBranchRestored)
local previous=#events
call(env,'OnTerminate')
check(table.concat(events,',',previous+1)=='MISSION_TERMINATING,terminate,TERMINATION_RETURNED','outgoing identity cleared')
for _,name in ipairs({'allocate','initialize','start','terminate'}) do
    env,events=fixture({yieldAt=name})
    if name~='allocate' then call(env,'OnAllocate') end
    local method=({allocate='OnAllocate',initialize='OnInitialize',start='OnMissionGameStart',terminate='OnTerminate'})[name]
    local co=coroutine.create(function() call(env,method) end)
    local ok,value=coroutine.resume(co)
    check(ok and value==name and coroutine.status(co)=='suspended','yield preserved')
    check(coroutine.resume(co));check(coroutine.status(co)=='dead')
end
for _,name in ipairs({'allocate','initialize','start','terminate'}) do
    local token={}
    env,events=fixture({errorAt=name,token=token})
    if name~='allocate' then call(env,'OnAllocate') end
    local method=({allocate='OnAllocate',initialize='OnInitialize',start='OnMissionGameStart',terminate='OnTerminate'})[name]
    local ok,err=pcall(env.e[method],1,nil,3)
    check(not ok and err==token,'original error identity')
    check(events[#events]==name,'no returned signal after original failure')
end
for _,option in ipairs({{refuseReceipt='ALLOCATION_ENTER'},{throwReceipt='ALLOCATION_ENTER'}}) do
    env,events=fixture(option);call(env,'OnAllocate')
    check(table.concat(events,',')=='ALLOCATION_ENTER,allocate','refused allocation forwards original')
end
env,events=fixture({closeFails=true});call(env,'OnAllocate');env.vars.missionCode=40050;call(env,'OnTerminate')
check(env.Tpp.MgsCoop.sessionCloseReturned==false and env.Tpp.MgsCoop.sessionCloseBranchRestored==true)
check(table.concat(events,','):find('close,SESSION_CLOSE_FAILED,TERMINATION_RETURNED',1,true)~=nil)
env,events=fixture({refuseReceipt='PREPARING_DISCONNECT_RETURNED'});call(env,'OnAllocate');call(env,'OnTerminate')
check(env.Tpp.MgsCoop.sessionCloseReturned==false)
for _,event in ipairs(events) do check(event~='close','no close after refused context') end
print(checks..' lifecycle checks passed; native replies simulated, no game calls')
