-- Run the actual module to Mission.Start, then stop before unrelated game work.
local checks=0
local function check(v) checks=checks+1; assert(v,checks) end
local stop={}
local function run(path,code,result,ready,loadFails)
    local events={}
    local phases=0
    local function record(name) return function() events[#events+1]=name end end
    local env={
        Tpp={MgsCoop={playerPackageSelected=ready,archiveDirectReadSucceeded=ready},SetGameStatus=record("disable")},
        TppScriptVars={},ScriptBlock={},
        Mission={Start=function() events[#events+1]="start"; error(stop) end},
        TppWeather={OnEndMissionPrepareFunction=record("weather")},
        TppClock={Stop=record("clock")},
        TppUI={FADE_SPEED={FADE_MOMENT=0},FadeOut=record("fade")},
        TppSave={WaitingAllEnqueuedSaveOnStartMission=record("save-wait")},
        TppPause={RegisterPause=record("pause")},
        TppMission={DisableInGameFlag=record("disable-flag"),IsFOBMission=function() return code==50050 end,
                    SetFOBMissionFlag=record("fob")},
        TppGameStatus={Set=record("online"),Reset=record("offline")},vars={missionCode=code},
    }
    env.loadstring=function(source)
        if source:find("return 'MGS_COOP_PHASE_V1' -- ",1,true)==1 then
            check(source=="return 'MGS_COOP_PHASE_V1' -- OnAllocate:enter:"..tostring(code))
            phases=phases+1
            return function() end
        end
        check(source=="return 'MGS_COOP_HOST_CREATE_V1'")
        events[#events+1]="host"
        if loadFails then return nil,"fixture compile failure" end
        return function() return result end
    end
    setmetatable(env,{__index=_G})
    local chunk=assert(loadfile(path));setfenv(chunk,env)
    local mod=chunk()
    local ok,err=pcall(mod.OnAllocate,{})
    check(not ok and err==stop)
    check(phases==(path==arg[2] and 1 or 0))
    return table.concat(events,","),env.Tpp.MgsCoop.hostSessionCreated,env.Tpp.MgsCoop.clientPrepared
end
for _,code in ipairs({10010,10020,30050,50050}) do
    local original=run(arg[1],code,false,true)
    local patched,created=run(arg[2],code,false,true)
    check(not created)
    check(patched==original:gsub(",start$",code==30050 and ",host,start" or ",start"))
    local untouched=run(arg[2],code,true,false)
    check(untouched==original)
end
local original=run(arg[1],30050,false,true)
local success,created=run(arg[2],30050,true,true)
check(created==true and success==original:gsub(",start$",",host,online,start"))
for _,returned in ipairs({false,"unhandled receipt",1}) do
    local events,created=run(arg[2],30050,returned,true)
    check(not created and events==original:gsub(",start$",",host,start"))
end
local failed,created=run(arg[2],30050,false,true,true)
check(not created and failed==original:gsub(",start$",",host,start"))
local client,hostCreated,clientPrepared=run(arg[2],30050,"client",true)
check(not hostCreated and clientPrepared==true and client==original:gsub(",start$",",host,online,start"))
print(checks.." actual-module checks: original pre-start order, exact mission/gates, true-only online transition, failed/unhandled request; no game calls")
