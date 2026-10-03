-- Exercises the current manual guest wrapper with simulated native receipts.
-- No MGSV, Steam, key injection or real session is involved.
local checks=0
local function check(value,why)
    checks=checks+1
    assert(value,tostring(checks)..': '..(why or 'check failed'))
end
local READY="return 'MGS_COOP_CLIENT_READY_V1'"
local POLL="return 'MGS_COOP_CLIENT_POLL_V1'"
local IDLE='MGS_COOP_CLIENT_POLL_V1'
local sentinel={}
local function fixture(options)
    options=options or {}
    local events,notices={},{}
    local controls={mode=options.mode or 'manual',result=IDLE,ready=0,polls=0}
    local env={e={},vars={missionCode=30050},Tpp={MgsCoop={clientPrepared=true,equipmentPreparationReturned=true}}}
    for _,name in ipairs({'OnMissionGameStart','OnUpdate','OnTerminate'}) do
        env.e[name]=function(...)
            events[#events+1]=name
            check(select('#',...)==3,'original argument count')
            check(select(1,...)==7 and select(2,...)==nil and select(3,...)==9,'original arguments')
            if options.errorAt==name then error(sentinel) end
            if options.yieldAt==name then coroutine.yield('original yield') end
            return 1,nil,3
        end
    end
    env.loadstring=function(source)
        check(source==READY or source==POLL,'only current ready/poll receipts')
        if source==READY then controls.ready=controls.ready+1; events[#events+1]='ready'
        else controls.polls=controls.polls+1; events[#events+1]='poll' end
        if controls.compileThrow then error(sentinel) end
        if controls.compileNil then return nil end
        return function()
            if controls.executeThrow then error(sentinel) end
            return source==READY and controls.mode or controls.result
        end
    end
    env.TppUiCommand={AnnounceLogView=function(text)
        if controls.uiThrow then error(sentinel) end
        notices[#notices+1]=text
    end}
    setmetatable(env,{__index=_G})
    local chunk=assert(loadfile(arg[1])); setfenv(chunk,env); chunk()
    return env,controls,events,notices
end
local function call(env,name)
    local function returns(...)
        check(select('#',...)==3,'original return count')
        check(select(1,...)==1 and select(2,...)==nil and select(3,...)==3,'original return values')
    end
    returns(env.e[name or 'OnMissionGameStart'](7,nil,9))
end
local env,c,events,notices=fixture()
call(env); call(env)
check(c.ready==1 and c.polls==0,'startup never requests a connection')
check(table.concat(events,',')=='OnMissionGameStart,ready,OnMissionGameStart','post-start readiness order')
check(env.Tpp.MgsCoop.clientJoinStatus=='waiting' and #notices==1,'one waiting announcement')
for i=1,5 do call(env,'OnUpdate') end
check(c.polls==5 and env.Tpp.MgsCoop.clientJoinRequested~=true,'idle input does not submit')
c.result='submitted'; call(env,'OnUpdate'); call(env,'OnUpdate')
check(c.polls==6,'submitted attempt stops polling')
check(env.Tpp.MgsCoop.clientJoinRequested==true and env.Tpp.MgsCoop.clientJoinStatus=='submitted','submitted, not established')
check(env.Tpp.MgsCoop.hostSessionCreated==nil and env.Tpp.MgsCoop.sessionStatus==nil,'guest never invents host or connection')
check(#notices==2,'one submission announcement')

for _,mode in ipairs({'diagnostic_manual','manual'}) do
    for _,result in ipairs({'diagnostic_passed','diagnostic_failed','submitted','failed','blocked','unexpected',false}) do
        env,c,events,notices=fixture({mode=mode})
        call(env)
        check(c.polls==0,'both modes wait for explicit input')
        c.result=result; call(env,'OnUpdate'); call(env,'OnUpdate')
        local diagnostic=mode=='diagnostic_manual'
        local status=diagnostic and (result=='diagnostic_passed' and 'diagnostic_passed' or 'diagnostic_failed')
            or (result=='submitted' and 'submitted' or 'failed')
        check(env.Tpp.MgsCoop.clientJoinStatus==status,'mode-specific result')
        check(env.Tpp.MgsCoop.clientJoinRequested==(not diagnostic and result=='submitted'),'diagnostics never submit')
        check(c.polls==1 and #notices==2,'single attempt and result announcement')
    end
end
for _,change in ipairs({'unprepared','equipment','mission','lost','absent'}) do
    for _,afterStart in ipairs({false,true}) do
        env,c=fixture()
        if afterStart then call(env) end
        if change=='unprepared' then env.Tpp.MgsCoop.clientPrepared=false
        elseif change=='equipment' then env.Tpp.MgsCoop.equipmentPreparationReturned=false
        elseif change=='mission' then env.vars.missionCode=10010
        elseif change=='lost' then env.Tpp.MgsCoop.sessionLost=true
        else env.Tpp.MgsCoop=nil end
        if not afterStart then call(env) end
        c.result='submitted'; call(env,'OnUpdate')
        check(c.ready==(afterStart and 1 or 0) and c.polls==0,'ineligible guest never polls')
    end
end
for _,mode in ipairs({'blocked','unhandled','diagnostic'}) do
    env,c=fixture({mode=mode}); call(env); call(env,'OnUpdate')
    check(c.polls==0 and env.Tpp.MgsCoop.clientJoinStatus=='failed','unsupported/old mode does not join')
end
for _,failure in ipairs({'compileThrow','compileNil','executeThrow'}) do
    for _,afterStart in ipairs({false,true}) do
        env,c=fixture()
        if afterStart then call(env) end
        c[failure]=true
        if not afterStart then call(env) else call(env,'OnUpdate') end
        local polls=c.polls
        call(env,'OnUpdate')
        check(c.polls==polls and env.Tpp.MgsCoop.clientJoinStatus=='failed','receipt failures stop attempt')
        check(env.Tpp.MgsCoop.clientJoinRequested~=true,'receipt failure never submits')
    end
end
env,c=fixture(); c.uiThrow=true; call(env); c.result='submitted'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinRequested==true,'optional notification failure does not block joining')
env,c=fixture(); call(env); call(env,'OnTerminate'); c.result='submitted'; call(env,'OnUpdate')
check(c.polls==0,'termination disarms waiting input')

for _,name in ipairs({'OnMissionGameStart','OnUpdate','OnTerminate'}) do
    env,c=fixture({errorAt=name})
    if name~='OnMissionGameStart' then call(env) end
    local readyBefore,pollsBefore=c.ready,c.polls
    local ok,err=pcall(function() call(env,name) end)
    check(not ok and err==sentinel,'original error propagated unchanged')
    check(c.ready==readyBefore and c.polls==pollsBefore,'no receipt after original error')
    env,c=fixture({yieldAt=name})
    if name~='OnMissionGameStart' then call(env) end
    readyBefore,pollsBefore=c.ready,c.polls
    local co=coroutine.create(function() call(env,name) end)
    local resumed,value=coroutine.resume(co)
    check(resumed and value=='original yield','original yield crosses wrapper')
    check(c.ready==readyBefore and c.polls==pollsBefore,'no receipt before original resumes')
    check(coroutine.resume(co),'original resumes successfully')
end
print(checks..' manual guest Lua checks passed: ordering, idle polling, one attempt, diagnostic separation, eligibility, termination, failures, original returns/errors/yields; no game or Steam calls')
