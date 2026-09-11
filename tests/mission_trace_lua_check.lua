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
