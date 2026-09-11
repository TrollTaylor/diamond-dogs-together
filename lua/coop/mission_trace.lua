-- Project-authored, bounded lifecycle observation. No new game API calls.
do
    local receipts = 0
    local function emit(name, phase)
        if receipts >= 128 or type(loadstring) ~= "function" then return end
        receipts = receipts + 1
        local mission = vars and vars.missionCode
        if type(mission) ~= "number" then mission = -1 end
        loadstring("return 'MGS_COOP_PHASE_V1' -- " .. name .. ":" .. phase .. ":" .. tostring(mission))
    end
    for _, name in ipairs({"OnAllocate", "OnInitialize", "OnEnterMissionPrepare",
        "OnTextureLoadingWaitStart", "OnMissionCanStart", "OnMissionGameStart",
        "OnReload", "OnTerminate", "EnableBlackLoading", "DisableBlackLoading"}) do
        local original = e[name]
        if type(original) == "function" then
            local function returned(...)
                pcall(emit, name, "return")
                return ...
            end
            e[name] = function(...)
                -- Diagnostic failure must not replace the mission's own call,
                -- results or exception. Operational teardown is a separate signal.
                pcall(emit, name, "enter")
                -- Preserve errors, coroutine yields and all return values.
                return returned(original(...))
            end
        end
    end
end
