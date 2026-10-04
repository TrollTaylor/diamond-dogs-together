-- Client timing adapted from Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e;
-- GPLv3 + Commons Clause 1.0. Manual input/feedback are project-authored.
-- Steam identities remain native; submitted is not a connected-player result.
-- Dynamite also calls TppNetworkUtil.SetTimeOut(10). The binding exists in this
-- exe (0xA1B6C0), but it calls GetMainSession (0x313780), which allocates and
-- publishes a fallback session object when no session exists, and 0x313A10,
-- which lazily registers per-type entries. Its side effects and its effect on a
-- pending client connection are unverified, so it is deliberately not called.
do
    local start, update, terminate = e.OnMissionGameStart, e.OnUpdate, e.OnTerminate
    local startedOnce, waiting, diagnostic, clientMode = false, false, false, false
    local function receipt(source)
        local call = loadstring(source)
        return call and call()
    end
    local function announce(text)
        if type(TppUiCommand) == "table" and type(TppUiCommand.AnnounceLogView) == "function" then
            pcall(TppUiCommand.AnnounceLogView, text)
        end
    end
    local function eligible()
        return vars.missionCode == 30050 and type(Tpp.MgsCoop) == "table"
            and Tpp.MgsCoop.clientPrepared == true
            and Tpp.MgsCoop.equipmentPreparationReturned == true
            and Tpp.MgsCoop.sessionLost ~= true
    end
    local function begin()
        if startedOnce or not eligible() then return end
        startedOnce = true
        local mode = receipt("return 'MGS_COOP_CLIENT_READY_V1'")
        if mode == "diagnostic_manual" then
            diagnostic, waiting, clientMode = true, true, true
            Tpp.MgsCoop.clientJoinStatus = "diagnostic_waiting"
            announce("Co-op test: press Ctrl+F8 to check guest setup. No connection will be sent.")
        elseif mode == "manual" then
            waiting, clientMode = true, true
            Tpp.MgsCoop.clientJoinStatus = "waiting"
            announce("Co-op: wait for the host to be ready, then press Ctrl+F8 to join.")
        else
            Tpp.MgsCoop.clientJoinStatus = "failed"
            announce("Co-op: guest setup is unavailable. Restart the game before trying again.")
        end
    end
    local function poll()
        if not waiting then return end
        if not eligible() then waiting = false; return end
        local result = receipt("return 'MGS_COOP_CLIENT_POLL_V1'")
        -- An unchanged receipt means no key press; do not cache this chunk,
        -- because the native observer handles each deliberate input poll.
        if result == "MGS_COOP_CLIENT_POLL_V1" then return end
        waiting = false
        if diagnostic then
            Tpp.MgsCoop.clientJoinRequested = false
            Tpp.MgsCoop.clientJoinStatus = result == "diagnostic_passed" and "diagnostic_passed" or "diagnostic_failed"
            announce(result == "diagnostic_passed" and "Co-op test: guest input and setup check passed. No connection sent."
                or "Co-op test: guest setup check failed. Reset (Ctrl+F9) to check again.")
            return
        end
        Tpp.MgsCoop.clientJoinRequested = result == "submitted"
        Tpp.MgsCoop.clientJoinStatus = result == "submitted" and "submitted" or "failed"
        -- Ctrl+F9 re-arms a request that never created a session (the native
        -- reset machine restores and re-arms; lab reset.trigger does the same).
        announce(result == "submitted" and "Co-op: join request sent. Waiting for connection."
            or "Co-op: join could not start. Press Ctrl+F9, then Ctrl+F8 to retry.")
    end
    -- Reset / re-arm poll. The native observer handles Ctrl+F9, reset.trigger
    -- and the lab's automatic triggers. It leaves this literal receipt unchanged
    -- every frame until it has released the previous attempt and re-armed the
    -- join, when it returns 'rearm' (a real join) or 'rearm_diagnostic' (a
    -- client-check run). A refused reset never returns either. It runs on every
    -- update of a guest scene, including after a join was consumed and the
    -- normal join poll has stopped.
    local function resetPoll()
        if not clientMode or vars.missionCode ~= 30050 or type(Tpp.MgsCoop) ~= "table" then return end
        local result = receipt("return 'MGS_COOP_CLIENT_RESET_POLL_V1'")
        if result ~= "rearm" and result ~= "rearm_diagnostic" then return end
        diagnostic, waiting = result == "rearm_diagnostic", true
        Tpp.MgsCoop.clientJoinRequested = false
        if diagnostic then
            Tpp.MgsCoop.clientJoinStatus = "diagnostic_waiting"
            announce("Co-op test: check reset. Press Ctrl+F8 to check guest setup again.")
        else
            Tpp.MgsCoop.clientJoinStatus = "waiting"
            announce("Co-op: join reset. When the host is ready, press Ctrl+F8 to join.")
        end
    end
    local function protected(action)
        local ok, failure = pcall(action)
        if not ok then
            waiting = false
            if type(Tpp.MgsCoop) == "table" then
                Tpp.MgsCoop.clientJoinStatus = "failed"
                Tpp.MgsCoop.clientJoinError = tostring(failure)
            end
        end
    end
    local function started(...) protected(begin); return ... end
    local function updated(...)
        protected(poll)
        -- A reset-poll failure must not mark the join failed or stop updates.
        local ok, failure = pcall(resetPoll)
        if not ok and type(Tpp.MgsCoop) == "table" then Tpp.MgsCoop.clientResetError = tostring(failure) end
        return ...
    end
    if type(start) == "function" then e.OnMissionGameStart = function(...) return started(start(...)) end end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    if type(terminate) == "function" then
        e.OnTerminate = function(...) waiting = false; return terminate(...) end
    end
end
