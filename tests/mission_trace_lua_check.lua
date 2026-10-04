-- Exercise the original trace fragment's forwarding semantics, including yields.
local calls, messages, checks = {}, {}, 0
local function check(value) checks=checks+1; assert(value, checks) end
local env = {vars={missionCode=30050}, e={}}
setmetatable(env,{__index=_G})
env.loadstring=function(source) messages[#messages+1]=source; return loadstring(source) end
env.e.OnAllocate=function(...)
    calls[#calls+1]={n=select('#',...),...}
    coroutine.yield('loading')
    return nil, 'allocated', nil
end
local errorToken={}
env.e.OnTerminate=function(value) check(value==errorToken); error(errorToken) end
env.e.DisableBlackLoading=function() return 'clear' end
local fragment=assert(loadfile(arg[1])); setfenv(fragment,env); fragment()
local co=coroutine.create(function() return env.e.OnAllocate(nil,7,nil) end)
local ok,value=coroutine.resume(co)
check(ok and value=='loading' and #messages==1)
check(calls[1].n==3 and calls[1][1]==nil and calls[1][2]==7 and calls[1][3]==nil)
local function packed(...) return {n=select('#',...),...} end
local result=packed(coroutine.resume(co))
check(result.n==4 and result[1]==true and result[2]==nil and result[3]=='allocated' and result[4]==nil)
check(#messages==2 and messages[1]:find('OnAllocate:enter:30050',1,true)~=nil)
check(messages[2]:find('OnAllocate:return:30050',1,true)~=nil)
ok,value=pcall(env.e.OnTerminate,errorToken)
check(not ok and value==errorToken and #messages==3)
env.vars.missionCode=40050
check(env.e.DisableBlackLoading()=='clear')
check(messages[4]:find('DisableBlackLoading:enter:40050',1,true)~=nil)
check(messages[5]:find('DisableBlackLoading:return:40050',1,true)~=nil)
for i=1,200 do check(env.e.DisableBlackLoading()=='clear') end
check(#messages==128)
check(env.e.OnInitialize==nil)
-- Apply the same fragment to the actual extracted module and forward OnTerminate.
local actualEnv={Tpp={ApendArray=function() end, IsTypeFunc=function(v) return type(v)=='function' end},
    TppScriptVars={}, ScriptBlock={}, Mission={}, vars={missionCode=30050}, loadstring=env.loadstring}
setmetatable(actualEnv,{__index=_G})
local actual=assert(loadfile(arg[2])); setfenv(actual,actualEnv)
actualEnv.e=actual()
local trace=assert(loadfile(arg[1])); setfenv(trace,actualEnv); trace()
local terminated=0
actualEnv.e.OnTerminate({sequence={OnTerminate=function() terminated=terminated+1 end}})
check(terminated==1)
print(checks..' trace checks: arguments, nil returns, yields, errors, mission labels, bounded receipts and actual-module termination')
-- Optional arg[3]: the composed TppMain-trace.lua written by
-- mission_trace_patch_check (fragment spliced before the module's final
-- `return e`, so it binds the module's local e instead of a global).
if arg[3] then
    local before=checks
    local function moduleEnv(received)
        local moduleGlobals={Tpp={ApendArray=function() end, IsTypeFunc=function(v) return type(v)=='function' end},
            TppScriptVars={}, ScriptBlock={}, Mission={}, vars={missionCode=30050},
            loadstring=function(source) received[#received+1]=source; return loadstring(source) end}
        return setmetatable(moduleGlobals,{__index=_G})
    end
    local plainReceived,received={},{}
    local plain=assert(loadfile(arg[2])); setfenv(plain,moduleEnv(plainReceived))
    local plainModule=plain()
    local composedEnv=moduleEnv(received)
    local composed=assert(loadfile(arg[3])); setfenv(composed,composedEnv)
    local module=composed()
    check(type(module)=='table' and #received==0 and #plainReceived==0)
    check(rawget(composedEnv,'e')==nil)
    local file=assert(io.open(arg[2],'rb')); local originalText=file:read('*a'); file:close()
    local originalLines=select(2,originalText:gsub('\n','\n'))+1
    for key,value in pairs(plainModule) do check(type(module[key])==type(value)) end
    for key in pairs(module) do check(plainModule[key]~=nil) end
    -- Each traced lifecycle function is a fragment wrapper (defined after the
    -- original module lines) whose `original` upvalue is the module function.
    local wrapped=0
    for _,name in ipairs({"OnAllocate","OnInitialize","OnEnterMissionPrepare","OnTextureLoadingWaitStart",
        "OnMissionCanStart","OnMissionGameStart","OnReload","OnTerminate","EnableBlackLoading","DisableBlackLoading"}) do
        local plainFunction=plainModule[name]
        if type(plainFunction)=='function' then
            local wrapper=module[name]
            check(debug.getinfo(wrapper,'S').linedefined>=originalLines)
            local inner
            for index=1,16 do
                local upName,upValue=debug.getupvalue(wrapper,index)
                if upName==nil then break end
                if upName=='original' then inner=upValue end
            end
            check(type(inner)=='function' and
                debug.getinfo(inner,'S').linedefined==debug.getinfo(plainFunction,'S').linedefined)
            wrapped=wrapped+1
        else
            check(module[name]==nil)
        end
    end
    check(wrapped>=3)
    terminated=0
    module.OnTerminate({sequence={OnTerminate=function() terminated=terminated+1 end}})
    check(terminated==1 and #received==2)
    check(received[1]=="return 'MGS_COOP_PHASE_V1' -- OnTerminate:enter:30050")
    check(received[2]=="return 'MGS_COOP_PHASE_V1' -- OnTerminate:return:30050")
    print((checks-before)..' composed TppMain-trace checks: '..wrapped..' lifecycle wrappers bound to the module, receipts and termination forwarding')
end
