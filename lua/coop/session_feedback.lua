-- Project-authored session feedback. Native state is not proof of player control.
do
    local start, update, terminate = e.OnMissionGameStart, e.OnUpdate, e.OnTerminate
    local active, frames, pendingPolls, pendingNotice = false, 0, 0, false
    local previous
    local messages = {
        host_ready = "Co-op: host is accepting connections. The guest can press Ctrl+F8.",
        host_not_accepting = "Co-op: host session exists, but is not accepting connections.",
        host_peer_present = "Co-op: a guest is present in the session.",
        client_established = "Co-op: host connection established.",
        session_error = "Co-op: the game reported a session error. Restart before another attempt.",
        unavailable = "Co-op: session status is currently unavailable.",
    }
    -- A guest can retry: the native Ctrl+F9 reset re-arms the join when the
    -- game released the failed session; it refuses while a live session
    -- remains, and then only a restart helps. Messages fit one log line.
    local guestMessages = {
        session_error = "Co-op: session error. Press Ctrl+F9 to reset, or restart the game.",
    }
    local pendingMessage = "Co-op: still connecting. Ctrl+F9 resets; restart the game if it does not."
    local function announce(text)
        if type(TppUiCommand) == "table" and type(TppUiCommand.AnnounceLogView) == "function" then
            pcall(TppUiCommand.AnnounceLogView, text)
        end
    end
    -- The loading/session adaptations own player creation. A roster change
    -- cannot establish that a player object is ready for commands. In particular,
    -- index 1 is the guest's local player, so an unconditional Warp here moves
    -- that player's own actor. Realize has not been verified for TppPlayer2.
    local function poll()
        if not active or vars.missionCode ~= 30050 or type(Tpp.MgsCoop) ~= "table" then return end
        local state = Tpp.MgsCoop
        if state.sessionLost == true then active = false; return end
        local hosting = state.hostSessionCreated == true
        if not hosting and state.clientJoinRequested ~= true then
            -- Between guest attempts (a failed request, or a reset that
            -- re-armed the join): the next request starts with fresh feedback,
            -- so a repeated status and the pending reminder are announced again.
            previous, pendingPolls, pendingNotice, frames = nil, 0, false, 0
            return
        end
        frames = frames + 1
        if frames < 60 then return end
        frames = 0
        local call = loadstring("return 'MGS_COOP_SESSION_STATUS_V1'")
        local status = call and call()
        if status ~= "client_connecting" and messages[status] == nil then return end
        state.sessionStatus = status
        if previous ~= status then
            previous = status
            local text = (not hosting and guestMessages[status]) or messages[status]
            if text then announce(text) end
        end
        if status == "client_connecting" then
            pendingPolls = pendingPolls + 1
            if pendingPolls >= 30 and not pendingNotice then
                pendingNotice = true
                announce(pendingMessage)
            end
        else pendingPolls = 0 end
    end
    local function started(...) active = true; return ... end
    local function updated(...)
        local ok, failure = pcall(poll)
        if not ok then
            active = false
            if type(Tpp.MgsCoop) == "table" then Tpp.MgsCoop.sessionFeedbackError = tostring(failure) end
        end
        return ...
    end
    if type(start) == "function" then e.OnMissionGameStart = function(...) return started(start(...)) end end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    if type(terminate) == "function" then e.OnTerminate = function(...) active = false; return terminate(...) end end
end
