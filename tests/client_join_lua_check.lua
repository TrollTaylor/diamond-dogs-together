-- Exercises the current manual guest wrapper with simulated native receipts.
-- No MGSV, Steam, key injection or real session is involved.
local checks=0
local function check(value,why)
    checks=checks+1
    assert(value,tostring(checks)..': '..(why or 'check failed'))
end
local READY="return 'MGS_COOP_CLIENT_READY_V1'"
local POLL="return 'MGS_COOP_CLIENT_POLL_V1'"
local RESET="return 'MGS_COOP_CLIENT_RESET_POLL_V1'"
local IDLE='MGS_COOP_CLIENT_POLL_V1'
local RESET_IDLE='MGS_COOP_CLIENT_RESET_POLL_V1'
local sentinel={}
local function fixture(options)
    options=options or {}
    local events,notices={},{}
    local controls={mode=options.mode or 'manual',result=IDLE,resetResult=RESET_IDLE,ready=0,polls=0,resets=0,timeoutCalls=0}
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
        check(source==READY or source==POLL or source==RESET,'only current ready/poll/reset receipts')
        if source==READY then controls.ready=controls.ready+1; events[#events+1]='ready'
        elseif source==POLL then controls.polls=controls.polls+1; events[#events+1]='poll'
        else controls.resets=controls.resets+1 end -- idle reset polls are not recorded in events
        if controls.compileThrow then error(sentinel) end
        if controls.compileNil then return nil end
        if source==RESET and controls.resetThrow then error(sentinel) end
        return function()
            if controls.executeThrow then error(sentinel) end
            if source==READY then return controls.mode
            elseif source==POLL then return controls.result
            else return controls.resetResult end
        end
    end
    env.TppUiCommand={AnnounceLogView=function(text)
        if controls.uiThrow then error(sentinel) end
        notices[#notices+1]=text
    end}
    -- The binding exists in this exe, but it lazily allocates a session through
    -- GetMainSession; the guest wrapper must never call it.
    env.TppNetworkUtil={SetTimeOut=function() controls.timeoutCalls=controls.timeoutCalls+1 end}
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
check(c.timeoutCalls==0,'SetTimeOut is never called (lazy session getter)')
for i=1,5 do call(env,'OnUpdate') end
check(c.polls==5 and env.Tpp.MgsCoop.clientJoinRequested~=true,'idle input does not submit')
check(c.resets==5,'reset is polled every update')
c.result='submitted'; call(env,'OnUpdate'); call(env,'OnUpdate')
check(c.polls==6,'submitted attempt stops polling')
check(env.Tpp.MgsCoop.clientJoinRequested==true and env.Tpp.MgsCoop.clientJoinStatus=='submitted','submitted, not established')
check(env.Tpp.MgsCoop.hostSessionCreated==nil and env.Tpp.MgsCoop.sessionStatus==nil,'guest never invents host or connection')
check(#notices==2,'one submission announcement')
check(c.resets==7,'reset keeps polling after a consumed join')

-- Reset re-arm: the native observer returns 'rearm'; the guest resumes waiting
-- and a subsequent submitted poll works, so retries no longer need a restart.
c.resetResult='rearm'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='waiting' and env.Tpp.MgsCoop.clientJoinRequested==false,'reset re-arms the guest')
check(#notices==3,'reset announces once')
check(env.Tpp.MgsCoop.sessionLost==nil and env.Tpp.MgsCoop.clientPrepared==true,'re-arm never invents session state')
c.resetResult=RESET_IDLE; c.result='submitted'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='submitted' and env.Tpp.MgsCoop.clientJoinRequested==true,'join works again after reset')
check(c.timeoutCalls==0,'no SetTimeOut after a re-armed join either')

-- A failed attempt re-armed by a reset can be retried.
env,c,events,notices=fixture(); call(env)
c.result='failed'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='failed','synchronous failure reported')
local polls=c.polls; call(env,'OnUpdate')
check(c.polls==polls,'failed attempt stops polling until reset')
c.resetResult='rearm'; call(env,'OnUpdate'); c.resetResult=RESET_IDLE
check(env.Tpp.MgsCoop.clientJoinStatus=='waiting','failed attempt re-armed')
c.result='submitted'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='submitted' and c.polls==polls+1,'retry after reset submits')

-- A refused reset never returns a re-arm literal: the result stands.
env,c,events,notices=fixture(); call(env)
c.result='failed'; call(env,'OnUpdate')
local noticeCount=#notices
c.resetResult='refused'; call(env,'OnUpdate'); call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='failed' and #notices==noticeCount,'unrecognized reset result is ignored')
c.result='submitted'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='failed','no join without a re-arm')

-- Client-check re-arm: the native observer returns 'rearm_diagnostic', and the
-- next passing check is reported as a passed check, never as a failed join.
env,c,events,notices=fixture({mode='diagnostic_manual'}); call(env)
c.result='diagnostic_passed'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='diagnostic_passed','first check passed')
c.resetResult='rearm_diagnostic'; call(env,'OnUpdate'); c.resetResult=RESET_IDLE
check(env.Tpp.MgsCoop.clientJoinStatus=='diagnostic_waiting','diagnostic re-armed')
c.result='diagnostic_passed'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='diagnostic_passed' and env.Tpp.MgsCoop.clientJoinRequested==false,
    'second check reported as a passed check, not a failed join')
check(c.timeoutCalls==0,'diagnostic path never calls SetTimeOut')
-- A diagnostic re-arm after a failed check also stays diagnostic.
c.result='diagnostic_failed'; c.resetResult='rearm_diagnostic'; call(env,'OnUpdate'); c.resetResult=RESET_IDLE
c.result='diagnostic_failed'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='diagnostic_failed' and env.Tpp.MgsCoop.clientJoinRequested==false,'diagnostic failure stays diagnostic')

-- A reset-poll receipt failure (only the RESET receipt throws) must not mark
-- the join failed, stop the join poll or propagate.
env,c=fixture(); call(env)
c.resetThrow=true
call(env,'OnUpdate'); call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='waiting','reset failure does not fail a waiting join')
check(type(env.Tpp.MgsCoop.clientResetError)=='string','reset failure is recorded separately')
check(c.polls==2,'join poll continues while the reset poll fails')
c.result='submitted'; call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='submitted','join still submits while the reset poll fails')
call(env,'OnUpdate')
check(env.Tpp.MgsCoop.clientJoinStatus=='submitted' and env.Tpp.MgsCoop.clientJoinRequested==true,'reset failure after submission keeps the result')

-- No guest mode: the reset receipt is never compiled (host scenes pay nothing).
for _,mode in ipairs({'blocked','unhandled'}) do
    env,c=fixture({mode=mode}); call(env)
    for i=1,3 do call(env,'OnUpdate') end
    check(c.resets==0,'no reset polling without a guest mode')
end
env,c=fixture(); env.Tpp.MgsCoop.clientPrepared=false; call(env)
for i=1,3 do call(env,'OnUpdate') end
check(c.resets==0 and c.ready==0,'no reset polling before the guest is prepared')

for _,mode in ipairs({'diagnostic_manual','manual'}) do
    for _,result in ipairs({'diagnostic_passed','diagnostic_failed','submitted','failed','blocked','unexpected',false}) do
        env,c,events,notices=fixture({mode=mode})
        call(env)
        check(c.polls==0,'both modes wait for explicit input')
        check(c.timeoutCalls==0,'no mode sets a timeout')
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
        local before=c.polls
        call(env,'OnUpdate')
        check(c.polls==before and env.Tpp.MgsCoop.clientJoinStatus=='failed','receipt failures stop attempt')
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
print(checks..' manual guest Lua checks passed: ordering, idle polling, one attempt, no SetTimeOut, reset re-arm (real, failed retry, refused, diagnostic), isolated reset-poll failure, diagnostic separation, eligibility, termination, failures, original returns/errors/yields; no game or Steam calls')
