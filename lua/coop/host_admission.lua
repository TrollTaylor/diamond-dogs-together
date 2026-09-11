-- Adapted from Dynamite mission OnEnter SessionEnableAccept(true) and mission
-- termination SessionEnableAccept(false), pinned c90f252f8200765f66c005d6bf4357fda0c0d98e.
-- GPLv3 with Commons Clause 1.0; third_party/dynamite/LICENSE.
-- Open only after this host's original start function returns, on the engine
-- thread, with a native ownership receipt. Close before original termination.
do
    local accepting = false
    local function authorize()
        if type(loadstring) ~= "function" then return false end
        local request = loadstring("return 'MGS_COOP_HOST_ADMISSION_V1'")
        return request and request() == true
    end
    local function observe()
        loadstring("return 'MGS_COOP_HOST_ADMISSION_OBSERVED_V1'")
    end
    local function canCall()
        return type(TppNetworkUtil) == "table"
            and type(TppNetworkUtil.SessionEnableAccept) == "function"
    end
    local start = e.OnMissionGameStart
    local terminate = e.OnTerminate
    if type(start) == "function" and type(terminate) == "function" then
        local function started(...)
            if not accepting and vars and vars.missionCode == 30050
                and Tpp and type(Tpp.MgsCoop) == "table"
                and Tpp.MgsCoop.hostSessionCreated == true
                and Tpp.MgsCoop.equipmentPreparationReturned == true and canCall() and authorize() then
                TppNetworkUtil.SessionEnableAccept(true)
                accepting = true
                observe()
            end
            return ...
        end
        e.OnMissionGameStart = function(...)
            -- Preserve nil return values, errors and coroutine yields.
            return started(start(...))
        end
        e.OnTerminate = function(...)
            if accepting then
                accepting = false
                if canCall() and authorize() then
                    TppNetworkUtil.SessionEnableAccept(false)
                    observe()
                end
            end
            if type(loadstring) == "function" then
                loadstring("return 'MGS_COOP_HOST_RELEASE_V1'")
            end
            return terminate(...)
        end
    end
end
