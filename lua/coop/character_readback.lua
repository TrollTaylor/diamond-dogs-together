-- MGS co-op character readback (original). Appended to the fingerprinted
-- TppMain before its return, inside the session_lifecycle wrapper. Defines the
-- log-only lab readback once (Tpp.MgsCoop.CharacterReadback; per-mission state
-- comes from the spliced character chunk: characterPoll, characterStatus),
-- starts it after the original OnMissionGameStart returns (the player object
-- exists from then on), polls it after each OnUpdate and stops it at
-- OnTerminate entry, so nothing reads the player during loading or teardown.
-- Original arguments, results, errors and yields pass through unchanged.
-- Player.GetPlayerType (0x9d30b0) has no null checks: it runs only while
-- started (not title mode) and for indices below the count of a ready native
-- sample taken just before (PLAYERS receipt, whose raw values keep OCELOT 5 /
-- QUIET 6, which GetPlayerType reads as 0); otherwise i0/i1 are "na". verdict
-- compares the expected type with vars.playerType and the local raw type.
do
    local R = {}
    local function state() return Tpp and type(Tpp.MgsCoop) == "table" and Tpp.MgsCoop or nil end
    local function read(get)
        local ok, value = pcall(get)
        if ok then return value end
    end
    local function receipt(source)
        local chunk = loadstring(source)
        if type(chunk) ~= "function" then return nil end
        return chunk()
    end
    local function log(text)
        local clean = string.gsub(text, "[^%w_=:,.;%-]", "_")
        loadstring("return 'MGS_COOP_CHARACTER_READBACK_V1' -- " .. string.sub(clean, 1, 192))
    end
    local function live(coop)
        local poll = coop and coop.characterPoll
        return type(poll) == "table" and poll.started == true and
            read(function() return gvars.ini_isTitleMode end) ~= true
    end
    function R.Snapshot()
        local coop = state()
        local players, limit, raw, values = "stopped", "na", {"na", "na"}, {"na", "na"}
        if live(coop) then
            local ready, total, raw0, raw1 = receipt("return 'MGS_COOP_CHARACTER_PLAYERS_V1'")
            players = ready == false and tostring(total) or "native_unavailable"
            if ready == true and type(total) == "number" then
                local sample = {raw0, raw1}
                players, limit = "ready", tostring(total)
                for i = 0, math.min(math.floor(total), 2) - 1 do
                    raw[i + 1] = tostring(sample[i + 1])
                    local ok, value = pcall(function() return Player.GetPlayerType{instanceIndex = i} end)
                    values[i + 1] = ok and tostring(value) or "err"
                end
            end
        end
        local d = coop and coop.characterStatus
        local expect = type(d) == "table" and d.expect or nil
        local index = read(function() return PlayerInfo.GetLocalPlayerIndex() end)
        local current = read(function() return vars.playerType end)
        local own = (index == 0 or index == 1) and raw[index + 1] or "na"
        local verdict = "keep"
        if expect ~= nil then
            verdict = (current ~= expect and "not_applied") or (own == "na" and "vars_applied") or
                (own ~= tostring(expect) and "native_pending") or "applied"
        end
        return "verdict=" .. verdict .. ";expect=" .. tostring(expect or "na") .. ";local=" .. tostring(index) ..
            ";vars=" .. tostring(current) .. ";parts=" .. tostring(read(function() return vars.playerPartsType end)) ..
            ";camo=" .. tostring(read(function() return vars.playerCamoType end)) .. ";temp=" ..
            (read(function() return gvars.ply_isUsingTempPlayerType end) == true and "1" or "0") ..
            ";players=" .. players .. ";limit=" .. limit .. ";i0=" .. values[1] .. ";i1=" .. values[2] ..
            ";raw0=" .. raw[1] .. ";raw1=" .. raw[2] .. ";quiet_ocelot_read_as=0"
    end
    function R.Readback(tag)
        local body = R.Snapshot()
        local coop = state()
        if coop then coop.characterReadback = body end
        log(tostring(tag or "manual") .. ";" .. body)
        return body
    end
    -- Rate-limited (every 60 calls), change-only, at most 64 lines per
    -- mission; only while enabled by the character chunk and started.
    function R.Poll(tag)
        local coop = state()
        local poll = coop and coop.characterPoll
        if type(poll) ~= "table" or poll.enabled ~= true or not live(coop) then return false end
        if tag == nil then
            poll.frames = poll.frames + 1
            if poll.frames < 60 then return false end
        end
        poll.frames = 0
        if poll.reports >= 64 then return false end
        local body = R.Snapshot()
        if tag == nil and body == poll.last then return false end
        poll.last, poll.reports = body, poll.reports + 1
        coop.characterReadback = body
        log((tag or "change") .. ";" .. body)
        return true
    end
    function R.Started()
        local coop = state()
        local poll = coop and coop.characterPoll
        if type(poll) ~= "table" or poll.enabled ~= true then return false end
        poll.started, poll.frames = true, 0
        return R.Poll("start")
    end
    function R.Stopped()
        local coop = state()
        local poll = coop and coop.characterPoll
        if type(poll) == "table" then poll.started = false end
    end
    local coop = state()
    if coop then coop.CharacterReadback = R end

    local start, update, terminate = e.OnMissionGameStart, e.OnUpdate, e.OnTerminate
    local function call(name)
        if type(R[name]) == "function" then pcall(R[name]) end
    end
    local function started(...) call("Started"); return ... end
    local function updated(...) call("Poll"); return ... end
    if type(start) == "function" then
        e.OnMissionGameStart = function(...) return started(start(...)) end
    end
    if type(update) == "function" then
        e.OnUpdate = function(...) return updated(update(...)) end
    end
    if type(terminate) == "function" then
        e.OnTerminate = function(...) call("Stopped"); return terminate(...) end
    end
end
