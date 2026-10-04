-- Exercises lua/coop/session_feedback.lua with simulated native status receipts.
-- No MGSV, Steam or real session is involved.
local checks=0
local function check(value,why)
    checks=checks+1
    assert(value,tostring(checks)..': '..(why or 'check failed'))
end
local STATUS="return 'MGS_COOP_SESSION_STATUS_V1'"
local IDLE='MGS_COOP_SESSION_STATUS_V1'
local sentinel={}
local function fixture(options)
    options=options or {}
    local notices={}
    local controls={status=IDLE,polls=0}
    local env={e={},vars={missionCode=30050},Tpp={MgsCoop={}}}
    for _,name in ipairs({'OnMissionGameStart','OnUpdate','OnTerminate'}) do
        env.e[name]=function(...)
            check(select('#',...)==3,'original argument count')
            check(select(1,...)==7 and select(2,...)==nil and select(3,...)==9,'original arguments')
            if options.errorAt==name then error(sentinel) end
            if options.yieldAt==name then coroutine.yield('original yield') end
            return 1,nil,3
        end
    end
    env.loadstring=function(source)
        check(source==STATUS,'only the session status receipt is compiled')
        controls.polls=controls.polls+1
        if controls.compileThrow then error(sentinel) end
        return function() return controls.status end
    end
    env.TppUiCommand={AnnounceLogView=function(text)
        if controls.uiThrow then error(sentinel) end
        notices[#notices+1]=text
    end}
    setmetatable(env,{__index=_G})
    local chunk=assert(loadfile(arg[1])); setfenv(chunk,env); chunk()
    return env,controls,notices
end
local function call(env,name)
    local function returns(...)
        check(select('#',...)==3,'original return count')
        check(select(1,...)==1 and select(2,...)==nil and select(3,...)==3,'original return values')
    end
    returns(env.e[name or 'OnMissionGameStart'](7,nil,9))
end
-- One native observation happens every 60 updates.
local function poll(env,count)
    for _=1,(count or 1)*60 do call(env,'OnUpdate') end
end
local function last(notices) return notices[#notices] or '' end
local function has(text,part) return string.find(text,part,1,true)~=nil end

-- Host: messages only on status changes.
local env,c,notices=fixture()
env.Tpp.MgsCoop.hostSessionCreated=true
call(env)
for _=1,59 do call(env,'OnUpdate') end
check(c.polls==0,'no observation before 60 updates')
c.status='host_ready'; call(env,'OnUpdate')
check(c.polls==1 and #notices==1 and has(notices[1],'accepting connections') and has(notices[1],'Ctrl+F8'),'host ready announced with the join key')
check(env.Tpp.MgsCoop.sessionStatus=='host_ready','status published')
poll(env,3)
check(#notices==1,'unchanged status is not repeated')
c.status='host_peer_present'; poll(env)
check(#notices==2 and has(last(notices),'guest is present'),'guest presence announced')
c.status='session_error'; poll(env)
check(#notices==3 and has(last(notices),'Restart'),'host session error asks for a restart')
check(not has(last(notices),'Ctrl+F9'),'host is not told to use the guest reset key')
c.status='weird_state'; poll(env)
check(#notices==3 and env.Tpp.MgsCoop.sessionStatus=='session_error','unknown status ignored')

-- Guest: the pending reminder appears once per attempt.
env,c,notices=fixture()
call(env)
poll(env,2)
check(c.polls==0,'no observation before a guest request or host session')
env.Tpp.MgsCoop.clientJoinRequested=true
c.status='client_connecting'
poll(env,29)
check(#notices==0,'connecting has no immediate message')
poll(env)
check(#notices==1 and has(notices[1],'still connecting') and has(notices[1],'Ctrl+F9'),'pending reminder after 30 observations')
poll(env,40)
check(#notices==1,'pending reminder appears once per attempt')
-- Ctrl+F9 re-armed the join (client_join.lua clears clientJoinRequested); a new attempt.
env.Tpp.MgsCoop.clientJoinRequested=false
local polls=c.polls
poll(env,3)
check(c.polls==polls,'no observation between attempts')
env.Tpp.MgsCoop.clientJoinRequested=true
poll(env,30)
check(#notices==2 and has(last(notices),'still connecting'),'second attempt gets its own pending reminder')
c.status='client_established'; poll(env)
check(#notices==3 and has(last(notices),'established'),'established announced')

-- Guest: a repeated status on the next attempt is announced again.
env,c,notices=fixture()
call(env)
env.Tpp.MgsCoop.clientJoinRequested=true
c.status='session_error'; poll(env)
check(#notices==1 and has(notices[1],'Ctrl+F9') and has(notices[1],'restart'),'guest session error offers reset and restart')
poll(env,2)
check(#notices==1,'not repeated within one attempt')
env.Tpp.MgsCoop.clientJoinRequested=false; poll(env)
env.Tpp.MgsCoop.clientJoinRequested=true; poll(env)
check(#notices==2 and has(last(notices),'session error'),'repeated on the next attempt')
c.status='unavailable'; poll(env)
check(#notices==3 and has(last(notices),'unavailable'),'unavailable announced')

-- A host never resets its feedback state (hostSessionCreated stays true).
env,c,notices=fixture()
env.Tpp.MgsCoop.hostSessionCreated=true
call(env)
c.status='host_ready'; poll(env)
env.Tpp.MgsCoop.clientJoinRequested=false; poll(env,2)
check(#notices==1,'host feedback not reset by the guest flag')

-- Stops: other mission, session loss, termination, missing state.
env,c,notices=fixture(); env.Tpp.MgsCoop.hostSessionCreated=true; env.vars.missionCode=10010
call(env); c.status='host_ready'; poll(env)
check(c.polls==0 and #notices==0,'only Mother Base 30050 is observed')
env,c,notices=fixture(); env.Tpp.MgsCoop.hostSessionCreated=true; call(env)
c.status='host_ready'; poll(env)
env.Tpp.MgsCoop.sessionLost=true; polls=c.polls; c.status='host_peer_present'; poll(env,2)
check(c.polls==polls and #notices==1,'session loss stops observation')
env.Tpp.MgsCoop.sessionLost=nil; poll(env,2)
check(c.polls==polls,'stopped until the next mission start')
env,c,notices=fixture(); env.Tpp.MgsCoop.hostSessionCreated=true; call(env)
call(env,'OnTerminate'); c.status='host_ready'; poll(env)
check(c.polls==0,'termination stops observation')
env,c,notices=fixture(); env.Tpp.MgsCoop.hostSessionCreated=true
c.status='host_ready'; poll(env)
check(c.polls==0,'no observation before mission start')
env,c,notices=fixture(); call(env); env.Tpp.MgsCoop=nil; poll(env)
check(c.polls==0,'missing state is ignored')

-- Failures are contained and recorded; original results are unchanged.
env,c,notices=fixture(); env.Tpp.MgsCoop.hostSessionCreated=true; call(env)
c.compileThrow=true; poll(env)
check(type(env.Tpp.MgsCoop.sessionFeedbackError)=='string','poll failure recorded')
c.compileThrow=false; polls=c.polls; poll(env,2)
check(c.polls==polls,'a failed poll stops observation')
env,c,notices=fixture(); env.Tpp.MgsCoop.hostSessionCreated=true; call(env)
c.uiThrow=true; c.status='host_ready'; poll(env)
check(env.Tpp.MgsCoop.sessionStatus=='host_ready' and env.Tpp.MgsCoop.sessionFeedbackError==nil,'announcement failure is harmless')
for _,name in ipairs({'OnMissionGameStart','OnUpdate','OnTerminate'}) do
    env,c=fixture({errorAt=name})
    env.Tpp.MgsCoop.hostSessionCreated=true
    local ok,err=pcall(function() call(env,name) end)
    check(not ok and err==sentinel,'original error propagated unchanged')
    env,c=fixture({yieldAt=name})
    local co=coroutine.create(function() call(env,name) end)
    local resumed,value=coroutine.resume(co)
    check(resumed and value=='original yield','original yield crosses wrapper')
    check(coroutine.resume(co),'original resumes successfully')
end

-- Every announcement fits one announce-log line and names only player keys.
local source=assert(io.open(arg[1],'rb')):read('*a')
local count=0
for text in source:gmatch('"(Co%-op:[^"]*)"') do
    count=count+1
    check(#text<=80,'message fits one line: '..text)
    check(not has(text,'trigger'),'no lab trigger-file jargon: '..text)
end
check(count>=8,'all messages were inspected')
print(checks..' session feedback Lua checks passed: host/guest messages on change, per-attempt pending reminder and repeated status after a reset, guest reset/restart guidance, stop conditions, contained failures, original returns/errors/yields; no game or Steam calls')
