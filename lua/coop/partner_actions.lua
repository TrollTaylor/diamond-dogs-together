-- Optional action-event producer only. No native playback or callback hooks.
-- Compose before partner_sync and explicitly opt in there; default remains off.
-- Source 1 means observed GUN_FIRE burst onset / requested RELOAD input edge.
-- No verified public per-shot message was found: these are not bullet counts
-- or evidence that a requested reload executed or completed.
do
    local A = {generation = 0, enabled = false}
    e.MgsCoopPartnerActions = A
    local floor, fmt, U32 = math.floor, string.format, 4294967296
    local function integer(v, lo, hi) return type(v) == "number" and v == floor(v) and v >= lo and v <= hi end
    local function get(t, k)
        local ok, v = pcall(function() return t[k] end)
        if ok then return v end
    end
    local function delta(a, b) return (a - b) % U32 end
    local flagsAllowed = {[0]=true, [1]=true, [2]=true, [3]=true, [8]=true, [9]=true, [10]=true, [11]=true}
    function A.reset()
        A.s = nil -- generation survives every lifecycle reset
    end
    local function append(s, kind, tick)
        s.cursor = (s.cursor + 1) % U32
        if kind == 1 then s.fireCount = (s.fireCount + 1) % U32 else s.reloadCount = (s.reloadCount + 1) % U32 end
        local h = s.history
        if #h == 8 then table.remove(h, 1) end
        h[#h + 1] = {token = s.cursor, tick = tick, kind = kind}
    end
    -- Pure boundary, also used by the fixture. First sample baselines held state.
    function A.observe(enabled, eligible, mission, equip, stance, tick, fire, reload, flags)
        if enabled ~= true or eligible ~= true or not integer(mission, 1, U32-1) or not integer(equip, 1, 65535)
            or not integer(stance, 0, 2) or not integer(tick, 0, U32-1) or type(fire) ~= "boolean"
            or type(reload) ~= "boolean" or not flagsAllowed[flags] then A.reset(); return nil end
        local s = A.s
        if not s or s.mission ~= mission or s.equip ~= equip or s.stance ~= stance or delta(tick, s.tick) >= 2147483648 then
            A.generation = (A.generation + 1) % U32
            if A.generation == 0 then A.generation = 1 end
            s = {generation = A.generation, mission = mission, equip = equip, stance = stance, tick = tick,
                fire = fire, reload = reload, flags = flags, fireCount = 0, reloadCount = 0, cursor = 0, history = {}}
            A.s = s
        else
            -- Simultaneous sampled rises have unknown sub-frame ordering.
            if fire and not s.fire then append(s, 1, tick) end
            if reload and not s.reload then append(s, 2, tick) end
            s.tick, s.flags, s.fire, s.reload = tick, flags, fire, reload
        end
        return s
    end
    -- Read the raw public state/input, never the old 100/333 ms latched flags.
    -- Caller supplies the already validated authored gun and gameplay gate.
    function A.sample(eligible, equip, stance)
        if A.enabled ~= true or eligible ~= true then A.reset(); return nil end
        local status = get(PlayerInfo, "OrCheckStatus")
        local clock = get(Time, "GetRawElapsedTimeSinceStartUp")
        local band = get(bit, "band")
        local fireMask, quietMask = get(PlayerStatus, "GUN_FIRE"), get(PlayerStatus, "GUN_FIRE_SUPPRESSOR")
        local reloadMask, holdMask = get(PlayerPad, "RELOAD"), get(PlayerPad, "HOLD")
        local buttons = get(PlayerVars, "scannedButtonsDirect")
        if type(status) ~= "function" or type(clock) ~= "function" or type(band) ~= "function"
            or not integer(fireMask, 0, U32-1) or not integer(quietMask, 0, U32-1)
            or not integer(reloadMask, 1, U32-1) or not integer(holdMask, 1, U32-1)
            or not integer(buttons, 0, U32-1) then A.reset(); return nil end
        local ok, now = pcall(clock)
        if not ok or type(now) ~= "number" or now ~= now or now < 0 or now >= 1e9 then A.reset(); return nil end
        local fOk, fire = pcall(status, {fireMask})
        local qOk, quiet = pcall(status, {quietMask})
        local rOk, reload = pcall(band, buttons, reloadMask)
        local aOk, aim = pcall(band, buttons, holdMask)
        if not fOk or not qOk or not rOk or not aOk or type(fire) ~= "boolean" or type(quiet) ~= "boolean"
            or not integer(reload, 0, U32-1) or not integer(aim, 0, U32-1) then A.reset(); return nil end
        fire, reload, aim = fire or quiet, reload == reloadMask, aim == holdMask
        local flags = (aim and 1 or 0) + (fire and 2 or 0) + (reload and 8 or 0)
        return A.observe(true, true, get(vars, "missionCode"), equip, stance, floor(now * 1000) % U32, fire, reload, flags)
    end
    -- All u32 values use %.0f, since Lua 5.1 %d is signed 32-bit on Windows.
    -- Strict native receipt parser must accept exactly 10 + 3*count integers:
    -- generation mission equip stance tick flags fireCount reloadCount cursor
    -- count, then token tick kind for each retained event. No frame-derived IDs.
    function A.receipt()
        local s = A.s
        if not s then return nil end
        local values = {s.generation, s.mission, s.equip, s.stance, s.tick, s.flags, s.fireCount, s.reloadCount, s.cursor, #s.history}
        for _, ev in ipairs(s.history) do values[#values+1] = ev.token; values[#values+1] = ev.tick; values[#values+1] = ev.kind end
        for i, value in ipairs(values) do values[i] = fmt("%.0f", value) end
        return "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- " .. table.concat(values, " ")
    end
end
