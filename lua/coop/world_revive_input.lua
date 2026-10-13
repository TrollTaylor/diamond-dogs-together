-- Own-name use-button mask: native pickup/CQC never runs during a qualified revive hold.
do
    local W, floor, fmt = e.MgsCoopWorld, math.floor, string.format
    local D, R = W.downed, {}
    local NAME, HASH = "MgsCoopRevive", 1912501439
    local function valid(n, lo, hi)
        return type(n) == "number" and n == floor(n) and n >= lo and n <= hi
    end
    local function moment(n)
        return type(n) == "number" and n == n and n >= 0 and n <= 9007199254740991
    end
    local function player(method)
        local alive, f = W.get(Player, "IsInstanceValid"), W.get(Player, method)
        if type(alive) ~= "function" or type(f) ~= "function" then return nil end
        local ok, yes = pcall(alive)
        return ok and yes == true and f or nil
    end
    local function observe()
        if not valid(W.mission, 0, 99999) or not valid(W.epoch, 1, 65535) then return nil end
        local ok, r = pcall(W.receipt, fmt("return 'MGS_COOP_DOWNED_V1' -- %d %d 4 %d", W.mission, W.epoch, HASH))
        return ok and type(r) == "table" and r.ready == true and r or nil
    end
    local function absent(r)
        -- Optional exact-table evidence only; a refused/applied=false call cannot establish absence.
        if r and r.reviveMaskObserved == true and r.reviveMaskPresent == false then
            D.reviveMaskOwned, R.installed, R.nextReset = nil, nil, nil
        end
    end
    local function release(now)
        R.eligible, R.installed = nil, nil
        if not D.reviveMaskOwned then return end
        if R.nextReset and now >= R.nextReset - 100 and now < R.nextReset then return end
        R.nextReset = now + 100
        local f = player("ResetPadMask")
        if f then pcall(f, {settingName = NAME}) end
        -- Successful submission can be queued. Keep retrying our own name until observed absent.
    end
    local function eligible(r, now)
        if not moment(now) or not D.hitEnabled() or not W.inGame() or r.down or r.ready ~= true then return false end
        local other = D.records[3 - W.role]
        if not other or other.ready ~= true or other.down ~= true or not moment(other.at) or
            now < other.at or now - other.at >= 500 or not valid(other.generation, 1, 4294967295) or
            type(r.pos) ~= "table" or type(other.pos) ~= "table" then return false end
        local range = 0
        for i = 1, 3 do
            if not valid(r.pos[i], -1000000000, 1000000000) or
                not valid(other.pos[i], -1000000000, 1000000000) then return false end
            range = range + (r.pos[i] - other.pos[i]) ^ 2
        end
        return range <= 9000000
    end
    function D.revivePoll(r, now)
        R.polled, R.eligible = true, nil
        if not eligible(r, now) then
            release(now)
            if D.reviveMaskOwned and D.hitEnabled() and W.inGame() then absent(observe()) end
            return false
        end
        if R.mission ~= W.mission or R.epoch ~= W.epoch then release(now); R.nextSet = nil end
        R.mission, R.epoch = W.mission, W.epoch
        if not R.installed then
            if R.nextSet and now >= R.nextSet - 100 and now < R.nextSet then return false end
            local f = player("SetPadMask")
            if not f then return false end
            D.reviveMaskOwned, R.nextSet = true, now + 100
            local ok = pcall(f, {settingName = NAME, except = false, buttons = 0x20, sticks = 0, triggers = 0})
            R.installed = ok or nil
            if not ok then return false end
        end
        local seen = observe()
        if not seen or seen.reviveMaskObserved ~= true or seen.reviveMaskPresent ~= true or
            seen.applied ~= true or seen.down ~= false or seen.generation ~= r.generation or
            type(seen.held) ~= "boolean" then R.installed = nil; return false end
        R.eligible = true
        return seen.held
    end
    local prompt = 0
    local function announce(h)
        local state = 0
        if h and h.teammateDown and not h.down then
            state = h.near and (h.inRange and 3 or 2) or 1
        end
        if state == prompt then return end
        prompt = state
        if state == 0 or type(TppUiCommand) ~= "table" or type(TppUiCommand.AnnounceLogView) ~= "function" then return end
        local wake = h.teammateCause == 2
        local text = state == 3 and (wake and "Hold Action (E) to wake your teammate. Pickup is unavailable."
            or "Hold Action (E) to revive your teammate. Pickup is unavailable.")
            or state == 2 and (wake and "Close enough to wake your teammate. Action (E) is not ready. Pickup is unavailable."
            or "Close enough to revive your teammate. Action (E) is not ready. Pickup is unavailable.")
            or (wake and "Teammate asleep. Get closer to wake them. Pickup is unavailable."
            or "Teammate down. Get closer to revive. Pickup is unavailable.")
        pcall(TppUiCommand.AnnounceLogView, text)
    end
    local stop, tick, hud = D.stop, D.tick, D.hud
    function D.stop() release(W.now()); return stop() end
    function D.tick(now)
        R.polled, R.eligible = false, nil
        tick(now)
        announce(D.hud(now))
        if not R.polled then release(now) end
    end
    if type(W.tickHooks) == "table" then
        for i, f in ipairs(W.tickHooks) do if f == tick then W.tickHooks[i] = D.tick end end
    end
    function D.hud(now)
        local h = hud(now)
        if not R.eligible then h.inRange, h.progress = false, 0 end
        return h
    end
    local function cleanup() prompt = 0; release(W.now()) end
    W.startHooks[#W.startHooks + 1] = cleanup
    W.unpairedHooks[#W.unpairedHooks + 1] = cleanup
    W.stopHooks[#W.stopHooks + 1] = function(final) if final then cleanup() end end
    W.pairedHooks[#W.pairedHooks + 1] = function(changed) if changed then cleanup() end end
end
