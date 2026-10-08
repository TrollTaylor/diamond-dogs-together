-- MGS co-op world sync, spawn sync on the guest (original; docs/debug-menu.md
-- "Spawn sync"): ENEMY_DEBUG entries wait in a FIFO of 8 and are applied one at
-- a time to this PC's copy of the soldier; every entry ends with an
-- ENEMY_DEBUG_ACK and (except corrections) a debug menu history row. A warp or
-- correct target is the native grant recorded when the message was popped:
-- WARP_V1 names only the seq and this copy's GameObjectId. Corrections (kind 3)
-- re-warp only after two samples over 6 m while NORMAL below ALERT.
do
    local W = e.MgsCoopWorld
    local P = type(W) == "table" and W.place
    if type(P) == "table" then
        local OP, H, E = W.OP, W.handlers, W.enemy
        local fmt, floor, min = string.format, math.floor, math.min
        local KIND, pos, flat = P.KIND, P.pos, P.flat
        local CODE = {}
        for i, w in ipairs(P.WHY) do CODE[w] = i end
        local NATIVE = {disabled = "native_off", contract = "native_off", latched = "native_off", role = "native_off",
            grant = "grant", pose = "pose", range = "range", stance = "stance", mission = "context"}
        local function L(...) W.log(fmt(...)) end
        local function send(id, t) return W.api.send(id, t) end
        local function call(t, k, ...)
            local f = W.get(t, k)
            return type(f) == "function" and pcall(f, ...)
        end
        -- The debug menu's guard table (flags I/P, goto_spawn) on this PC.
        local function menu()
            local m = W.get(W.get(e, "MgsCoopDebug"), "enemy")
            return type(m) == "table" and m or nil
        end

        H[OP.ENEMY_DEBUG] = function(v, now)
            if W.host then return end
            local c = {seq = v[1], h = v[2], kind = v[3], x = v[4], y = v[5], z = v[6], yaw = v[7], flags = v[8], route = v[9],
                go = v[10], at = now or W.now()}
            if c.kind == 3 then return P.sample(c) end
            if #P.q >= 8 then return P.done(c, "busy") end
            P.q[#P.q + 1] = c
        end
        function P.sample(c)
            local en = W.ent[c.h]
            if not en or P.come[c.h] or c.flags % 2 == 1 or type(en.id) ~= "number" then return end
            -- Never while this guard's placement is pending; a queued correction takes the
            -- newest target (and its newer grant) in place.
            if P.cur and P.cur.h == c.h then return end
            for _, q in ipairs(P.q) do
                if q.h == c.h then
                    if q.kind == 3 then q.seq, q.x, q.y, q.z, q.yaw, q.at = c.seq, c.x, c.y, c.z, c.yaw, c.at end
                    return
                end
            end
            local x, _, z = pos(en.id)
            if not x then return end
            local d = flat(x, z, c.x, c.z)
            local div = P.div[c.h] or {n = 0, fixes = 0, at = -1e9}
            P.div[c.h] = div
            div.n = d > 6000 and div.n + 1 or 0
            local cp = W.cp[en.cp]
            local fix = div.n >= 2 and P.state(en) == 0 and not (cp and cp.localPhase >= 3) and div.fixes < 10 and
                c.at - div.at >= 5000 and #P.q < 8
            L("event=place_div;seq=%d;ent=%.0f;div_m=%.1f;n=%d;fix=%d", c.seq, c.h, d / 1000, div.n, fix and 1 or 0)
            if fix then
                div.n, div.fixes, div.at = 0, div.fixes + 1, c.at
                P.q[#P.q + 1] = c
            end
        end
        -- nil when this PC's copy may be changed.
        function P.check(c, en)
            if not (W.paired and W.inGame()) then return "not_ready" end
            if W.rosterMatch ~= true then return "roster" end
            if not en then return "unknown_entity" end
            if type(en.id) ~= "number" or en.id == W.api.null then return "unmapped" end
            local st = P.state(en)
            if st == nil then return "unreadable_local" end
            if E.TERMINAL[st] then return "terminal_local" end
            -- Native placements only, as the host's pick; a route placement re-enables
            -- the guard on both PCs (the retail sequence) and never checks a vehicle.
            if c.kind == 1 or c.kind == 3 then
                local ok, v = pcall(send, en.id, {id = "GetVehicleGameObjectId"})
                if ok and type(v) == "number" and v ~= W.api.null then return "vehicle_local" end
                local off = W.get(TppGameObject, "NPC_STATE_DISABLE")
                local good, s = pcall(send, en.id, {id = "GetStatus"})
                if off ~= nil and good and s == off then return "disabled_local" end
            end
        end
        -- The route name of a wire hash (sneak/caution route sets of every CP).
        function P.route(hash)
            if not P.routes then
                P.routes = {}
                local n = 0
                for _, set in pairs(W.get(mvars, "ene_routeSetsDefine") or {}) do
                    for _, mode in ipairs({"sneak_day", "sneak_night", "caution"}) do
                        for _, group in pairs(type(set) == "table" and type(set[mode]) == "table" and set[mode] or {}) do
                            for _, r in ipairs(type(group) == "table" and group or {}) do
                                if type(r) == "string" and n < 8192 then P.routes[W.h(r)], n = r, n + 1 end
                            end
                        end
                    end
                end
            end
            return P.routes[hash]
        end
        -- why (refused), true (applied now) or nil (pending).
        function P.start(c, now)
            local en = W.ent[c.h]
            local why = P.check(c, en)
            if why then return why end
            local k, m, id = c.kind, menu(), en.id
            c.en, c.t0 = en, now
            if k >= 4 then
                local ok
                if k == 4 then ok = pcall(send, id, {id = "SetDiscoveryPlayer"}); P.come[c.h] = true
                elseif k <= 6 then
                    ok = call(TppEnemy, k == 5 and "SetEnableRestrictNotice" or "SetDisableRestrictNotice", id)
                    if ok and m and type(m.ignored) == "table" then m.ignored[en.name] = k == 5 or nil end
                else ok = call(TppEnemy, k == 7 and "SetDisable" or "SetEnable", id) end
                return ok and true or "native_other"
            end
            if c.flags % 4 >= 2 and call(TppEnemy, "SetEnableRestrictNotice", id) and m and type(m.ignored) == "table" then
                m.ignored[en.name] = true
            elseif k == 1 and c.flags % 2 == 1 and m and type(m.ignored) == "table" and m.ignored[en.name] and
                call(TppEnemy, "SetDisableRestrictNotice", id) then
                m.ignored[en.name] = nil  -- as the host: a "come" warp of an ignoring guard notices again
            end
            if c.flags % 2 == 1 then P.come[c.h] = true end
            P.pinOn(c.h)
            if k == 2 then
                local route = P.route(c.route)
                if not route then return "route_unknown" end
                -- Retail TppDebug's route placement, as on the host.
                local ok = call(TppEnemy, "SetDisable", id) and pcall(send, id, {id = "SetSneakRoute", route = route, point = 0})
                    and pcall(send, id, {id = "SetCautionRoute", route = route, point = 0})
                    and pcall(send, id, {id = "SetAlertRoute", enabled = false, route = "", point = 0})
                call(TppEnemy, "SetEnable", id)
                pcall(send, id, {id = "SetForceRealize"})
                if not ok then return "native_other" end
                c.landed, c.name = 0, route
            end
        end
        function P.step(c, now)
            local id = c.en.id
            if now - c.at > 15000 then return P.done(c, "context") end
            if c.landed then
                if c.frames < c.landed + 2 then return end
                local x, y, z = pos(id)
                if not x then
                    if c.frames > c.landed + 60 then return P.done(c, "position_timeout") end
                    return
                end
                c.err, c.pos = (c.kind ~= 2 or c.x ~= 0 or c.z ~= 0) and flat(x, z, c.x, c.z) or 0, {x, y, z}
                return P.done(c)
            end
            local r = W.receipt(fmt("return 'MGS_COOP_WORLD_WARP_V1' -- %d %d", c.seq, id))
            if type(r) ~= "table" then return P.done(c, "native_off") end
            if r.refused ~= nil then
                c.native = tostring(r.refused)
                return P.done(c, NATIVE[c.native] or "native_other")
            end
            if r.h == 1 then c.landed, c.leaf = c.frames, tostring(r.leaf)
            elseif r.r == -1 and not c.forced then c.forced = true; pcall(send, id, {id = "SetForceRealize"}) end
            if not c.landed and (c.frames >= 150 or now - c.t0 > 10000) then return P.done(c, "realize_timeout") end
        end
        -- Ends one entry: force-realize released, ACK, menu row (not for corrections).
        function P.done(c, why)
            local en = c.en
            P.release(c)
            if P.cur == c then P.cur = nil end
            local m = menu()
            c.why, c.errCm = why, c.err and min(65535, floor(c.err / 10 + 0.5)) or 0
            if why then P.n.failed = P.n.failed + 1
            else
                P.n.applied = P.n.applied + 1
                if c.kind <= 2 then
                    if c.flags % 2 == 1 then pcall(send, en.id, {id = "SetDiscoveryPlayer"}) end
                    if m and type(m.used) == "table" then m.used[en.name] = true end
                    if m and c.pos then m.last = {position = {c.pos[1] / 1000, c.pos[2] / 1000, c.pos[3] / 1000}, mission = W.mission} end
                end
            end
            L("event=place;seq=%d;ent=%.0f;kind=%s;result=%s;why=%s;native=%s;err_m=%.2f;frames=%d", c.seq, c.h, KIND[c.kind],
                why and "refused" or "applied", why or "none", c.native or "none", c.errCm / 100, c.frames or 0)
            if W.paired then
                W.tx(OP.ENEMY_DEBUG_ACK, c.seq, c.h, why and 1 or 0, why and CODE[why] or 0, c.errCm, min(c.frames or 0, 65535))
            end
            if c.kind ~= 3 then
                P.last = c
                P.remote(not why, P.row(c))
            end
        end
        function P.row(c)
            local text = fmt("remote_spawn;seq=%d;ent=%.0f;state=%s;mode=%s;come=%d", c.seq, c.h, c.why and "refused" or "landed",
                KIND[c.kind], c.flags % 2)
            if c.why then return text .. ";why=" .. c.why .. ";native=" .. (c.native or "none") end
            return text .. fmt(";err_m=%.2f", c.errCm / 100)
        end
        function P.guestTick(now)
            local c = P.cur
            if not c then
                c = table.remove(P.q, 1)
                if not c then return end
                P.cur, c.frames = c, 0
                local r = P.start(c, now)
                if r then return P.done(c, r ~= true and r or nil) end
            end
            c.frames = c.frames + 1
            P.step(c, now)
        end
        W.unpairedHooks[#W.unpairedHooks + 1] = function()
            W.rosterMatch = false
            P.out, P.q = {}, {}
            local c = P.cur
            P.release(c)
            P.cur = nil
        end
    end
end
