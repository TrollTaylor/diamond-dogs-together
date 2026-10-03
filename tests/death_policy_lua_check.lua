-- Executes lua/coop/death_policy.lua (and optionally the composed TppMission.lua
-- written by death_policy_patch_check) against simulated game globals.
-- Usage: lua51 death_policy_lua_check.lua <death_policy.lua>
--            [<original TppMission.lua> <composed TppMission.lua>]
-- No game calls. MgsCoopRespawn and the original handler are simulated.
local checks=0
local function check(v,why) checks=checks+1; assert(v,tostring(checks)..': '..(why or 'check failed')) end
local function read(path)
    local file=assert(io.open(path,'rb')); local text=file:read('*a'); file:close(); return text
end
local function packed(...) return {n=select('#',...),...} end

-- 1. Fragment semantics on a simulated module table `e`.
local fragment=read(arg[1])
local function run(original)
    local calls={}
    local env={e={}}
    if original~=false then
        env.e.OnPlayerDead=original or function(...)
            calls[#calls+1]=packed(...)
            return 'original',select('#',...)
        end
    end
    setmetatable(env,{__index=_G})
    local chunk=assert(loadstring(fragment,'=death_policy'))
    setfenv(chunk,env)
    chunk()
    return env,calls
end
-- Without a respawn controller the original handler gets every argument and
-- its results are returned unchanged.
local env,calls=run()
local result=packed(env.e.OnPlayerDead(1,'Dead',nil,'extra'))
check(#calls==1 and calls[1].n==4 and calls[1][1]==1 and calls[1][2]=='Dead' and calls[1][3]==nil and calls[1][4]=='extra',
    'original receives all arguments without a controller')
check(result.n==2 and result[1]=='original' and result[2]==4,'original results returned')
-- Malformed controllers fall back to the original handler.
for _,controller in ipairs({1,'x',{},{Handle=1},{Handle={}}}) do
    env,calls=run()
    env.MgsCoopRespawn=controller
    check(env.e.OnPlayerDead(0,'Dead')=='original' and #calls==1,'malformed controller falls back')
end
-- A controller that handles the death suppresses the original handler and
-- receives index, reason and a fallback bound to the original call.
env,calls=run()
local seen
env.MgsCoopRespawn={Handle=function(index,reason,fallback)
    seen=packed(index,reason,fallback); return true
end}
result=packed(env.e.OnPlayerDead(1,'Dead','extra'))
check(result.n==0 and #calls==0,'handled death suppresses the original')
check(seen.n==3 and seen[1]==1 and seen[2]=='Dead' and type(seen[3])=='function','controller arguments')
check(seen[3]()=='original' and #calls==1 and calls[1].n==2 and calls[1][1]==1 and calls[1][2]=='Dead',
    'fallback calls the original with index and reason')
-- The controller may run the fallback itself; the original runs once.
env,calls=run()
env.MgsCoopRespawn={Handle=function(_,_,fallback) fallback(); return true end}
env.e.OnPlayerDead(0,'FallDeath')
check(#calls==1 and calls[1][2]=='FallDeath','fallback inside a handled death runs once')
-- A controller that declines, returns nil, or fails falls back.
for _,handle in ipairs({function() return false end,function() end,function() error('boom') end,
    function() error({}) end}) do
    env,calls=run()
    env.MgsCoopRespawn={Handle=handle}
    result=packed(env.e.OnPlayerDead(0,'Dead','extra'))
    check(#calls==1 and calls[1].n==3 and result[1]=='original','declined or failed controller falls back')
end
-- Errors from the original handler still propagate with their identity.
local token={}
env,calls=run(function() error(token) end)
local ok,err=pcall(env.e.OnPlayerDead,0,'Dead')
check(not ok and err==token,'original error identity')
-- A module without OnPlayerDead is left unchanged.
env=run(false)
check(env.e.OnPlayerDead==nil,'missing handler stays missing')
check(rawget(env,'original')==nil,'no globals leaked')
print(checks..' death policy checks: argument forwarding, controller handling, fallback, refusals and errors; no game calls')

-- 2. Optional: the actual composed TppMission.lua against the extracted original.
if arg[2] and arg[3] then
    local before=checks
    local originalSource=read(arg[2])
    local composedSource=read(arg[3])
    check(#originalSource==104102,'pinned original size')
    -- Permissive engine stubs: unknown globals resolve to callable, indexable
    -- placeholders so the module top level runs. MgsCoopRespawn stays absent
    -- unless a case sets it.
    local function stub()
        local t={}
        return setmetatable(t,{__index=function(s,k) local v=stub(); rawset(s,k,v); return v end,
            __call=function() return stub() end})
    end
    local function moduleEnv(hostCalls)
        local env={TppNetworkUtil={IsHost=function() hostCalls[#hostCalls+1]=true; return false end},
            Tpp=setmetatable({StrCode32Table=function(t) return t end},{__index=function(s,k)
                local v=stub(); rawset(s,k,v); return v end})}
        return setmetatable(env,{__index=function(s,k)
            if _G[k]~=nil then return _G[k] end
            if k=='MgsCoopRespawn' then return nil end
            local v=stub(); rawset(s,k,v); return v
        end})
    end
    local plainCalls,composedCalls={},{}
    local plain=assert(loadstring(originalSource,'@/Assets/tpp/script/lib/TppMission.lua'))
    setfenv(plain,moduleEnv(plainCalls))
    local plainModule=plain()
    local composedEnv=moduleEnv(composedCalls)
    local composed=assert(loadstring(composedSource,'@/Assets/tpp/script/lib/TppMission.lua'))
    setfenv(composed,composedEnv)
    local module=composed()
    check(type(plainModule)=='table' and type(module)=='table','both sources load and return their module')
    check(#plainCalls==0 and #composedCalls==0,'loading calls no handler')
    check(rawget(composedEnv,'e')==nil and rawget(composedEnv,'original')==nil,'fragment binds the module local e')
    for key,value in pairs(plainModule) do check(type(module[key])==type(value),'same module field types: '..tostring(key)) end
    for key in pairs(module) do check(plainModule[key]~=nil,'no extra module fields: '..tostring(key)) end
    -- OnPlayerDead is the fragment wrapper (defined after the original lines)
    -- around the module's own handler.
    local originalLines=select(2,originalSource:gsub('\n','\n'))+1
    local wrapper=module.OnPlayerDead
    check(debug.getinfo(wrapper,'S').linedefined>=originalLines,'OnPlayerDead is the spliced wrapper')
    local inner
    for index=1,16 do
        local upName,upValue=debug.getupvalue(wrapper,index)
        if upName==nil then break end
        if upName=='original' then inner=upValue end
    end
    check(type(inner)=='function' and debug.getinfo(inner,'S').linedefined==debug.getinfo(plainModule.OnPlayerDead,'S').linedefined,
        'wrapper forwards to the module handler')
    -- Without a controller the original handler runs (its first statement asks
    -- TppNetworkUtil.IsHost, simulated as false so it returns early).
    check(module.OnPlayerDead(0,'Dead')==nil and #composedCalls==1,'original handler runs without a controller')
    composedEnv.MgsCoopRespawn={Handle=function() return true end}
    module.OnPlayerDead(0,'Dead')
    check(#composedCalls==1,'handled death skips the original handler')
    composedEnv.MgsCoopRespawn={Handle=function() return false end}
    module.OnPlayerDead(0,'Dead')
    check(#composedCalls==2,'declined death runs the original handler')
    composedEnv.MgsCoopRespawn=nil
    -- The Player Dead message, built later by Messages(), uses the wrapper.
    local messages=module.Messages()
    local deadFunc
    for _,entry in ipairs(type(messages)=='table' and type(messages.Player)=='table' and messages.Player or {}) do
        if type(entry)=='table' and entry.msg=='Dead' then deadFunc=entry.func end
    end
    check(deadFunc~=nil and rawequal(deadFunc,wrapper),'Player Dead message dispatches to the wrapper')
    print((checks-before)..' composed TppMission checks: module unchanged apart from the OnPlayerDead wrapper, which the Dead message uses')
end
