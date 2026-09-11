-- Client timing adapted from Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e;
-- GPLv3 + Commons Clause 1.0. Manual input/feedback are project-authored.
-- Steam identities remain native; submitted is not a connected-player result.
do
    local start, update, terminate = e.OnMissionGameStart, e.OnUpdate, e.OnTerminate
    local startedOnce, waiting = false, false
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
        if mode == "diagnostic" then
            receipt("return 'MGS_COOP_CLIENT_JOIN_V1'")
            Tpp.MgsCoop.clientJoinStatus = "diagnostic"
        elseif mode == "manual" then
            waiting = true
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
        Tpp.MgsCoop.clientJoinRequested = result == "submitted"
        Tpp.MgsCoop.clientJoinStatus = result == "submitted" and "submitted" or "failed"
        announce(result == "submitted" and "Co-op: join request sent. Waiting for connection."
            or "Co-op: join could not start. Restart the game before trying again.")
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
    local function updated(...) protected(poll); return ... end
    if type(start) == "function" then e.OnMissionGameStart = function(...) return started(start(...)) end end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    if type(terminate) == "function" then
        e.OnTerminate = function(...) waiting = false; return terminate(...) end
    end
end
