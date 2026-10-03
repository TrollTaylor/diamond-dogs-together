-- Adapted from Dynamite TppMain.OnTerminate and Nt disconnect messages,
-- c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
-- Changes: exact owned-session permission, deferred loss handling outside
-- message dispatch and bounded rule-restoration retries; no reconnect or
-- repeated native session-close request.
-- Installed last, outside the diagnostic trace and all other mission wrappers.
-- vars.missionCode can already name the destination when OnTerminate runs.
-- Retain the scene accepted at allocation until its termination has returned.
local mgsCoopSceneMission = nil
do
    local message, update, terminate = e.OnMessage, e.OnUpdate, e.OnTerminate
    local hostLost, memberLeft = false, false
    local cleanupPending, cleanupFrames, cleanupAttempts = false, 0, 0
    local nt = Fox.StrCode32("Nt")
    local disconnect = Fox.StrCode32("SessionDisconnectFromHost")
    local deleted = Fox.StrCode32("SessionDeleteMember")
    local function receipt(source)
        local call = loadstring(source)
        return call and call() == true
    end
    local function coop()
        return mgsCoopSceneMission == 30050 and Tpp and type(Tpp.MgsCoop) == "table"
    end
    local function restoreRules()
        -- Keep native and Lua restoration independent: either may fail while
        -- the other succeeds. A cleanup retry never closes the session again.
        local nativeOk, nativeRestored = pcall(receipt, "return 'MGS_COOP_RULES_RESTORE_V1'")
        local luaOk, luaRestored = true, Tpp.MgsCoop.damageRulesActive ~= true
        if type(MgsCoopDamage) == "table" and type(MgsCoopDamage.SetActive) == "function" then
            luaOk, luaRestored = pcall(MgsCoopDamage.SetActive, false)
        end
        Tpp.MgsCoop.damageRulesActive = not (luaOk and luaRestored == true)
        cleanupPending = not (nativeOk and nativeRestored == true and luaOk and luaRestored == true)
        Tpp.MgsCoop.sessionCleanupPending = cleanupPending
        Tpp.MgsCoop.sessionCleanupRetriesExhausted = cleanupPending and cleanupAttempts >= 8
        if not nativeOk then Tpp.MgsCoop.sessionCleanupError = tostring(nativeRestored)
        elseif not luaOk then Tpp.MgsCoop.sessionCleanupError = tostring(luaRestored) end
    end
    if type(message) == "function" then
        e.OnMessage = function(context, category, event, ...)
            if coop() and category == nt then
                if event == disconnect and Tpp.MgsCoop.clientPrepared == true then hostLost = true end
                if event == deleted and Tpp.MgsCoop.hostSessionCreated == true then memberLeft = true end
            end
            return message(context, category, event, ...)
        end
    end
    local function handleLoss()
        if not coop() then hostLost, memberLeft = false, false; return end
        if memberLeft then
            memberLeft = false
            receipt("return 'MGS_COOP_TARGET_CLEAR_V1'")
            Tpp.MgsCoop.peerDisconnectObserved = true
            -- Keep the host running and accepting. A leave notification does
            -- not authorize closing the host or invent a replacement peer.
        end
        if hostLost then
            if receipt("return 'MGS_COOP_SESSION_LOST_V1'") then
                Tpp.MgsCoop.clientPrepared = false
                Tpp.MgsCoop.clientJoinRequested = false
                Tpp.MgsCoop.sessionLost = true
                cleanupPending, cleanupFrames, cleanupAttempts = true, 0, 0
                restoreRules()
            end
            hostLost = false
        end
        if cleanupPending and cleanupAttempts < 8 then
            cleanupFrames = cleanupFrames + 1
            if cleanupFrames >= 60 then
                cleanupFrames, cleanupAttempts = 0, cleanupAttempts + 1
                restoreRules()
            end
        end
    end
    local function updated(...)
        local ok, failure = pcall(handleLoss)
        if not ok and coop() then Tpp.MgsCoop.sessionCleanupError = tostring(failure) end
        return ...
    end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    local function close()
        if not coop() then return end
        local state = Tpp.MgsCoop
        if type(TppNetworkUtil) == "table" and type(TppNetworkUtil.CloseSession) == "function"
            and type(TppNetworkUtil.SessionDisconnectPreparingMembers) == "function" then
            local nativeClose = TppNetworkUtil.CloseSession
            local nativePreparing = TppNetworkUtil.SessionDisconnectPreparingMembers
            if not receipt("return 'MGS_COOP_SESSION_CLOSE_V1'") then return end
            local prepared, prepareFailure = pcall(nativePreparing)
            state.sessionPreparingDisconnectReturned = prepared
            if not prepared or not receipt("return 'MGS_COOP_PREPARING_DISCONNECT_RETURNED_V1'") then
                local restored = receipt("return 'MGS_COOP_SESSION_CLOSE_FAILED_V1'")
                state.sessionCloseBranchRestored = restored
                state.sessionCleanupPending = state.sessionCleanupPending or not restored
                state.sessionCloseReturned = false
                state.sessionCleanupError = prepared and "Native close context or branch preparation was refused" or tostring(prepareFailure)
                return
            end
            local ok, failure = pcall(nativeClose)
            -- Restore the native branch before any result-table writes or
            -- error formatting. Retain the pre-call table if globals change.
            local restored
            if ok then restored = receipt("return 'MGS_COOP_SESSION_CLOSE_RETURNED_V1'")
            else restored = receipt("return 'MGS_COOP_SESSION_CLOSE_FAILED_V1'") end
            state.sessionCloseBranchRestored = restored
            state.sessionCleanupPending = state.sessionCleanupPending or not restored
            state.sessionCloseReturned = ok
            if not ok then state.sessionCleanupError = tostring(failure) end
        end
    end
    local function terminated(...)
        local ok, failure = pcall(close)
        if not ok and coop() then Tpp.MgsCoop.sessionCleanupError = tostring(failure) end
        return ...
    end
    if type(terminate) == "function" then
        e.OnTerminate = function(...)
            hostLost, memberLeft = false, false
            -- This signal is mandatory even after diagnostic receipts are
            -- exhausted, and even if an earlier helper cleared Lua co-op flags.
            -- Acknowledgement means entry was recorded, not cleanup completed.
            local acknowledged, result = pcall(receipt, "return 'MGS_COOP_MISSION_TERMINATING_V1'")
            if coop() then
                Tpp.MgsCoop.sessionTerminationAcknowledged = acknowledged and result == true
                if not acknowledged then Tpp.MgsCoop.sessionCleanupError = tostring(result)
                elseif result ~= true then Tpp.MgsCoop.sessionCleanupError = "Native termination signal was not acknowledged" end
                -- Retry before the original handler can destroy scene systems.
                -- Preserve its execution even if our restoration fails.
                local ok, failure = pcall(restoreRules)
                if not ok then Tpp.MgsCoop.sessionCleanupError = tostring(failure) end
            end
            return terminated(terminate(...))
        end
    end
end

-- Project-authored execution handoff. Compilation does not identify the Lua
-- execution thread: loading can run on a worker before gameplay resumes on the
-- main thread. Keep the original callbacks outside pcall so yields, exceptions
-- and all return values retain the game's behavior.
do
    local allocate, initialize, start, terminate = e.OnAllocate, e.OnInitialize, e.OnMissionGameStart, e.OnTerminate
    local allocationAccepted = false
    local initializationAccepted = false
    local function receipt(source)
        local call = loadstring(source)
        return call and call() == true
    end
    local function signal(source, field)
        local ok, accepted = pcall(receipt, source)
        -- Optional feedback must not prevent the original callback or change
        -- whether the native allocation return signal is sent.
        pcall(function()
            local state = Tpp and Tpp.MgsCoop
            if type(state) == "table" then state[field] = ok and accepted == true end
        end)
        return ok and accepted == true
    end
    local function allocated(...)
        if allocationAccepted then
            signal("return 'MGS_COOP_ALLOCATION_RETURNED_V1'", "allocationReturned")
            allocationAccepted = false
        end
        return ...
    end
    local function terminated(...)
        if signal("return 'MGS_COOP_TERMINATION_RETURNED_V1'", "terminationReturned") then
            mgsCoopSceneMission = nil
        end
        return ...
    end
    local function initialized(...)
        if initializationAccepted then
            signal("return 'MGS_COOP_INITIALIZATION_RETURNED_V1'", "initializationReturned")
            initializationAccepted = false
        end
        return ...
    end
    if type(allocate) == "function" and type(initialize) == "function" and type(start) == "function" and type(terminate) == "function" then
        e.OnAllocate = function(...)
            allocationAccepted = signal("return 'MGS_COOP_ALLOCATION_ENTER_V1'", "allocationEntered")
            if allocationAccepted then
                local ok, mission = pcall(function() return vars and vars.missionCode end)
                mgsCoopSceneMission = ok and type(mission) == "number" and mission or nil
            end
            return allocated(allocate(...))
        end
        e.OnInitialize = function(...)
            initializationAccepted = signal("return 'MGS_COOP_INITIALIZATION_ENTER_V1'", "initializationEntered")
            return initialized(initialize(...))
        end
        e.OnMissionGameStart = function(...)
            signal("return 'MGS_COOP_RUNTIME_ENTER_V1'", "nativeRuntimeReady")
            return start(...)
        end
        e.OnTerminate = function(...)
            -- The existing inner wrapper restores rules and requests owned
            -- close before this return signal releases the mission lifecycle.
            return terminated(terminate(...))
        end
    end
end
