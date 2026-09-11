-- Adapted from Dynamite's DeadInFOB ten-second respawn timer and native request,
-- c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
-- Changes: owned two-member context, local-only request, duplicate suppression,
-- deferred request outside message dispatch, cancellation and death fallback.
do
    local control = {}
    local ready, blocked, pending = false, false, nil
    local frames, serial = 0, 0
    local timerName, confirmName = "MgsCoop_RespawnWait", "MgsCoop_RespawnConfirm"
    local playerCategory, timerCategory = Fox.StrCode32("Player"), Fox.StrCode32("Timer")
    local deadEvent, finishEvent = Fox.StrCode32("DeadInFOB"), Fox.StrCode32("Finish")
    local timerHash, confirmHash = Fox.StrCode32(timerName), Fox.StrCode32(confirmName)
    local function state(value, failure)
        if Tpp and type(Tpp.MgsCoop) == "table" then
            Tpp.MgsCoop.respawnState = value
            if failure then Tpp.MgsCoop.respawnError = tostring(failure) end
        end
    end
    local function announce(text)
        if TppUiCommand and type(TppUiCommand.AnnounceLogView) == "function" then
            pcall(TppUiCommand.AnnounceLogView, text)
        end
    end
    local function nativeRule(enabled)
        local source = enabled and "return 'MGS_COOP_RESPAWN_ENABLE_V1'" or "return 'MGS_COOP_RESPAWN_DISABLE_V1'"
        local call = loadstring(source)
        return call and call() == true
    end
    local function stopTimers()
        if GkEventTimerManager and type(GkEventTimerManager.Stop) == "function" then
            pcall(GkEventTimerManager.Stop, timerName)
            pcall(GkEventTimerManager.Stop, confirmName)
        end
        local ok, restored = pcall(nativeRule, false)
        if Tpp and type(Tpp.MgsCoop) == "table" then
            Tpp.MgsCoop.respawnNativeRestorePending = not (ok and restored == true)
        end
    end
    local function context()
        if not ready or vars.missionCode ~= 30050 or not Tpp or type(Tpp.MgsCoop) ~= "table"
            or Tpp.MgsCoop.sessionLost then return end
        if not Tpp.MgsCoop.hostSessionCreated and not Tpp.MgsCoop.clientPrepared then return end
        local query = loadstring("return 'MGS_COOP_RESPAWN_CONTEXT_V1'")
        local result = query and query()
        if type(result) ~= "table" or not PlayerInfo or type(PlayerInfo.GetLocalPlayerIndex) ~= "function"
            or result.localIndex ~= PlayerInfo.GetLocalPlayerIndex() then return end
        return result
    end
    local function fail(reason)
        local previous = pending
        pending, blocked = nil, true
        stopTimers()
        state("failed", reason)
        -- This callback is the saved original death handler, not this wrapper.
        if previous and previous.fallback then previous.fallback() end
    end
    function control.Handle(index, reason, fallback)
        if blocked or (index ~= 0 and index ~= 1) then return false end
        local current = context()
        if not current then return false end
        if index ~= current.localIndex then return true end
        if pending then return true end
        if not Player or type(Player.RequestToRespawn) ~= "function" or not GkEventTimerManager
            or type(GkEventTimerManager.Start) ~= "function" or type(GkEventTimerManager.Stop) ~= "function" then return false end
        local nativeOk, enabled = pcall(nativeRule, true)
        if not nativeOk or enabled ~= true then state("native_rule_unavailable"); return false end
        serial = serial + 1
        timerName, confirmName = "MgsCoop_RespawnWait_" .. serial, "MgsCoop_RespawnConfirm_" .. serial
        timerHash, confirmHash = Fox.StrCode32(timerName), Fox.StrCode32(confirmName)
        pending = { index=index, fallback=fallback, due=false, requested=false, expired=false, missingSamples=0 }
        local ok, failure = pcall(GkEventTimerManager.Start, timerName, 10)
        if not ok then stopTimers(); pending=nil; state("timer_failed", failure); return false end
        state("waiting")
        announce("Respawn in 10 seconds")
        return true
    end
    _G.MgsCoopRespawn = control
    local function process()
        if not pending then return end
        frames = frames + 1
        if frames % 15 ~= 0 then return end
        local current = context()
        if not current then
            -- A changing publication bank can invalidate one read without a
            -- disconnect. Allow several subsequent observations to recover.
            pending.missingSamples = pending.missingSamples + 1
            if pending.missingSamples >= 8 then fail("Co-op session unavailable") end
            return
        end
        pending.missingSamples = 0
        if current.localIndex ~= pending.index then fail("Local player identity changed"); return end
        -- The event can precede publication of DEAD; do not cancel during the
        -- countdown because the first published sample still says alive.
        if not pending.requested and not pending.due then return end
        local dead = current.localIndex == 0 and current.dead0 or current.localIndex == 1 and current.dead1
        if not dead then
            stopTimers()
            pending, blocked = nil, false
            state("alive_observed")
            return
        end
        if pending.expired then fail("Respawn did not clear the death state"); return end
        if pending.due and not pending.requested then
            if not nativeRule(true) then fail("Native respawn rule unavailable"); return end
            pending.due, pending.requested = false, true
            -- Follow the upstream HUD reset only for an authorized local respawn.
            if TppUiStatusManager and type(TppUiStatusManager.ClearStatus) == "function" then
                for _, name in ipairs({"AnnounceLog", "EquipPanel", "HeadMarker", "WorldMarker"}) do
                    pcall(TppUiStatusManager.ClearStatus, name)
                end
            end
            local ok, failure = pcall(Player.RequestToRespawn)
            if not ok then fail(failure); return end
            state("request_returned")
            announce("Respawning")
            local timerOk, timerFailure = pcall(GkEventTimerManager.Start, confirmName, 15)
            if not timerOk then
                -- Do not issue another request after a partial transition.
                state("request_returned", timerFailure)
            end
        end
    end
    local originalMessage, originalUpdate = e.OnMessage, e.OnUpdate
    local originalStart, originalTerminate = e.OnMissionGameStart, e.OnTerminate
    if type(originalMessage) == "function" then
        e.OnMessage = function(owner, category, event, sender, ...)
            if ready then
                if category == timerCategory and event == finishEvent and pending then
                    if sender == timerHash then pending.due = true end
                    if sender == confirmHash then pending.expired = true end
                elseif category == playerCategory and event == deadEvent then
                    -- Arming a timer is allowed here; the actual native respawn
                    -- call is deferred until the normal update has returned.
                    local ok, failure = pcall(control.Handle, sender, nil, function()
                        if TppMission and type(TppMission.OnPlayerDead) == "function" then TppMission.OnPlayerDead(sender) end
                    end)
                    if not ok then state("event_failed", failure) end
                end
            end
            return originalMessage(owner, category, event, sender, ...)
        end
    end
    local function updated(...)
        local ok, failure = pcall(process)
        if not ok then
            local fallbackOk, fallbackFailure = pcall(fail, failure)
            if not fallbackOk then state("fallback_failed", fallbackFailure) end
        end
        return ...
    end
    if type(originalUpdate) == "function" then e.OnUpdate = function(...) return updated(originalUpdate(...)) end end
    local function started(...)
        ready, blocked, frames = true, false, 0
        return ...
    end
    if type(originalStart) == "function" then e.OnMissionGameStart = function(...) return started(originalStart(...)) end end
    if type(originalTerminate) == "function" then
        e.OnTerminate = function(...)
            ready = false
            if pending then stopTimers() end
            pending = nil
            return originalTerminate(...)
        end
    end
end
