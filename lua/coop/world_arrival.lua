-- Original arrival-prevent (fb1031, docs/world-sync-design.md 9.2): the arrival helicopter watchdog, the start-state log
-- and the support helicopter landing messages. Spliced after world_recovery.lua; uses world_follow's way out (F.arrival:
-- A.heli, A.linked, A.pending and the wrapped RestartMission) and adds A.field (a free-roam restart at the route start).
-- 0.14.5: a guest's arrival helicopter that started at a stored position never flew in, landed or let it off.
do
    local W = e.MgsCoopWorld
    local F = type(W) == "table" and W.follow
    local A = type(F) == "table" and F.arrival
    if type(A) == "table" then
        -- on: the watchdog (default on; the logs always run); wait: still for this long (ms, in game, outside cutscenes);
        -- radius: metres; window: the arrival watch (ms); offFoot: off the helicopter this long ends the watch (ms).
        -- hint: the host's note 15 toast (off until a run shows it).
        local G = {on = true, hint = false, wait = 20000, radius = 1, window = 180000, offFoot = 5000, msgCap = 16}
        W.arrivalWatch = G
        local fmt, floor, sqrt = string.format, math.floor, math.sqrt
        local FIELD = {[30010] = true, [30020] = true, [30050] = true}
        local MSG = {"LandedAtLandingZone", "DescendToLandingZone", "OnPlayerHeliHatchOpen", "PlayerHeliGetOff",
            "LandingFromHeli", "HeliDoorClosed", "RideHelicopter"}
        -- The helicopter worked: it descends to or reached a landing zone, or the player got off.
        local WORKED = {LandedAtLandingZone = true, DescendToLandingZone = true, PlayerHeliGetOff = true, LandingFromHeli = true}
        local function log(s) W.log("event=arrival;" .. s) end
        local function num(v) return type(v) == "number" and v == v and v or nil end
        local function mm(v) v = num(v); return v and floor(v * 1000 + 0.5) or -1 end
        local function flag(v) return v == true and 1 or v == false and 0 or -1 end
        local function linked(role)
            if type(W.cfg) ~= "function" then return false end
            local ok, r = pcall(W.cfg, W.get(vars, "missionCode"), 0)
            return ok and type(r) == "table" and (r[1] or 0) >= 3 and r[2] == role and r[4] == 1 and type(r[8]) == "number" and r[8] > 0
        end
        local function demo()
            local f = W.get(DemoDaemon, "IsDemoPlaying")
            if type(f) ~= "function" then return false end
            local ok, v = pcall(f)
            return ok and v == true
        end
        local function heli()
            local ok, v = pcall(A.heli)
            return ok and v == true
        end
        -- The start state at a free roam or Mother Base start (both roles): two bounded lines.
        function G.startLog()
            local m = W.mission
            if not FIELD[m] then return end
            local name, get = "-", W.get(TppDemo, "GetMBDemoName")
            if m == 30050 and type(get) == "function" then
                local ok, n = pcall(get)
                if ok and n ~= nil then name = string.sub(tostring(n), 1, 40) end
            end
            local p = W.get(gvars, "ply_missionStartPos")
            log(fmt("step=start;m=%d;role=%s;flag=%s;pos=%d,%d,%d;rot=%d;state=%s;used=%d;route=%.0f", m, W.host and "host" or "guest",
                tostring(W.get(vars, "initialPlayerFlag")), mm(W.get(vars, "initialPlayerPosX")), mm(W.get(vars, "initialPlayerPosY")),
                mm(W.get(vars, "initialPlayerPosZ")), mm(W.get(vars, "initialPlayerRotY")), tostring(W.get(gvars, "ply_initialPlayerState")),
                flag(W.get(svars, "ply_isUsedPlayerInitialAction")), num(W.get(gvars, "heli_missionStartRoute")) or -1))
            log(fmt("step=start_pos;use=%d;mpos=%d,%d,%d;mrot=%d;layout=%.0f;cluster=%s;demo=%s;heli=%d",
                flag(W.get(gvars, "ply_useMissionStartPos")), mm(W.get(p, 0)), mm(W.get(p, 1)), mm(W.get(p, 2)),
                mm(W.get(gvars, "ply_missionStartRot")), num(W.get(vars, "mbLayoutCode")) or -1, tostring(W.get(vars, "mbClusterId")),
                name, heli() and 1 or 0))
        end
        -- The support helicopter's landing messages and the player's get-off (names hashed once), G.msgCap lines a mission.
        local hashes
        function G.message(class, msg)
            if not FIELD[W.mission] then return end
            if not hashes then
                local s = W.get(W.api, "str32") or W.get(Fox, "StrCode32")
                if type(s) ~= "function" then return end
                hashes = {}
                for _, name in ipairs(MSG) do
                    local ok, h = pcall(s, name)
                    if ok and h ~= nil then hashes[h] = name end
                end
            end
            local name = hashes[msg]
            if not name then return end
            if WORKED[name] and not G.worked then G.worked = name end
            G.msgs = (G.msgs or 0) + 1
            if G.msgs <= G.msgCap then
                log(fmt("step=heli_msg;m=%s;t_ms=%d;heli=%d", name, W.now() - (G.t0 or 0), heli() and 1 or 0))
            end
        end
        function G.stop(why)
            G.over = true
            log(fmt("step=watch_end;why=%s;m=%d", why, W.mission))
        end
        -- The arrival helicopter that never moves: a linked guest runs the way out (a local restart: on foot at Command
        -- at Mother Base, world_follow A.foot; at the route start in free roam, A.field); a linked host gets a hint.
        function G.fire(now, x, y, z)
            local m, pos = W.mission, fmt("pos=%d,%d,%d", mm(x), mm(y), mm(z))
            if W.host then
                if not linked(1) then return log(fmt("step=watch_end;why=solo;m=%d", m)) end
                log(fmt("step=host_hint;why=static_heli;m=%d;shown=%d;%s", m, G.hint and 1 or 0, pos))
                if G.hint and type(W.note) == "function" then pcall(W.note, 15, m, 1) end
                return
            end
            if not A.linked() then return log(fmt("step=watch_end;why=solo;m=%d", m)) end
            if W.get(svars, "mis_isDefiniteGameOver") == true or W.get(mvars, "mis_isReserveGameOver") == true then
                return log(fmt("step=watch_end;why=game_over;m=%d", m))
            end
            local restart = W.get(TppMission, "RestartMission")
            if type(restart) ~= "function" then return log(fmt("step=watch_end;why=no_restart;m=%d", m)) end
            A.pending = {mission = m, at = now}
            log(fmt("step=unstick;why=static_heli;m=%d;still_ms=%d;%s", m, now - (G.since or now), pos))
            -- The follow token passes the guest suppression; the fade's ExecuteRestartMission passes on A.pending.
            local token = F.token
            F.token = true
            local ok, err = pcall(restart)
            F.token = token
            if not ok then log("step=unstick_error;msg=" .. string.sub(tostring(err), 1, 80)) end
        end
        function G.tick(now)
            local m = W.mission
            if not G.on or not FIELD[m] or G.over then return end
            if G.worked then return G.stop("landed") end
            if now - (G.t0 or now) > G.window then return G.stop("window") end
            if demo() or not W.inGame() then G.since = nil; return end
            if not heli() then
                G.offAt, G.since = G.offAt or now, nil
                if now - G.offAt >= G.offFoot then G.stop("on_foot") end
                return
            end
            G.offAt = nil
            local x, y, z = num(W.get(vars, "playerPosX")), num(W.get(vars, "playerPosY")), num(W.get(vars, "playerPosZ"))
            if not (x and y and z) then return end
            if not G.since then G.since, G.at = now, {x, y, z}; return end
            local dx, dy, dz = x - G.at[1], y - G.at[2], z - G.at[3]
            if sqrt(dx * dx + dy * dy + dz * dz) >= G.radius then return G.stop("moving") end
            if now - G.since < G.wait then return end
            G.over = true
            G.fire(now, x, y, z)
        end
        -- The free-roam way out: vanilla's ride-on reset (no stored start position) at the restart Reserve, so the
        -- helicopter starts at its route start (world_follow A.foot calls it for a restart outside Mother Base).
        function A.field(at)
            local initial, start = W.get(TppPlayer, "ResetInitialPosition"), W.get(TppPlayer, "ResetMissionStartPosition")
            if type(initial) ~= "function" or type(start) ~= "function" then return end
            initial()
            start()
            log(fmt("step=field_route;at=%s;route=%.0f", at, num(W.get(gvars, "heli_missionStartRoute")) or -1))
        end
        W.startHooks[#W.startHooks + 1] = function()
            G.t0, G.since, G.at, G.offAt, G.over, G.worked, G.msgs = W.now(), nil, nil, nil, false, nil, 0
            -- The lab probe's 0.14.5 seeds last until the next free roam or Mother Base arrival.
            if F.arrivedMission and FIELD[F.arrivedMission] then A.legacyPose = nil end
            local ok, err = pcall(G.startLog)
            if not ok then log("step=error;at=start;msg=" .. string.sub(tostring(err), 1, 80)) end
        end
        local tick = F.tick
        F.tick = function(now)
            tick(now)
            local ok, err = pcall(G.tick, now)
            if not ok then log("step=error;at=tick;msg=" .. string.sub(tostring(err), 1, 80)) end
        end
        local message = W.get(e, "OnMessage")
        if type(message) == "function" then
            local function after(class, msg, ...)
                local resend = W.get(W.api, "resend")
                if select("#", ...) == 0 or resend == nil or (...) ~= resend then pcall(G.message, class, msg) end
                return ...
            end
            e.OnMessage = function(s, class, msg, ...) return after(class, msg, message(s, class, msg, ...)) end
        end
        -- Lab probe guest_frozen_heli (id 20, lab_config.hpp; lab mode, guest): the next replayed sortie keeps the
        -- host's start position seeds as 0.14.5 did (A.legacyPose), forcing the frozen arrival the watchdog must undo.
        local P = W.probe
        if type(P) == "table" and type(P.handlers) == "table" and type(P.NAMES) == "table" then
            P.NAMES[20] = "guest_frozen_heli"
            P.handlers.guest_frozen_heli = function()
                local ok = W.mode == 1 and not W.host
                if ok then A.legacyPose = true end
                W.log("event=probe;id=guest_frozen_heli;step=done;result=" .. (ok and "ok" or "refused"))
            end
        end
    end
end
