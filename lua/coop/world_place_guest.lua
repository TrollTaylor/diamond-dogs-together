-- MGS co-op world sync, spawn sync on the guest (original; docs/debug-menu.md
-- "Spawn sync"): ENEMY_DEBUG entries wait in a FIFO of 8 and are applied one at
-- a time to this PC's copy of the soldier; every entry ends with an
-- ENEMY_DEBUG_ACK and (except corrections) a debug menu history row. A warp or
-- correct target is the native grant recorded when the message was popped:
-- WARP_V1 names only the seq and this copy's GameObjectId. Corrections (kind 3)
-- re-warp only after two samples over 6 m while NORMAL below ALERT. With W1 the
-- host's patrol samples are compared with this copy; enemy follow (W.followOn,
-- docs/world-sync-design.md 7.8) uses its own thresholds (P.sample) and FIFO of 16.
do
    local W = e.MgsCoopWorld
    local P = type(W) == "table" and W.place
    if type(P) == "table" then
        local OP, H, E = W.OP, W.handlers, W.enemy
        local fmt, floor, min, max, abs, ceil = string.format, math.floor, math.min, math.max, math.abs, math.ceil
        local KIND, pos, flat = P.KIND, P.pos, P.flat
        local CODE = {}
        for i, w in ipairs(P.WHY) do CODE[w] = i end
        local NATIVE = {disabled = "native_off", contract = "native_off", latched = "native_off", role = "native_off",
            grant = "grant", pose = "pose", range = "range", stance = "stance", mission = "context", far = "far",
            soldier = "latched"}
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
            if #P.q >= (W.followOn == true and 24 or 8) then return P.done(c, "busy") end
            P.q[#P.q + 1] = c
        end
        -- This player's x, z (mm) and camera yaw (degrees; playerRotY when the camera is unreadable), or nil.
        function P.view()
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            if type(px) ~= "number" or type(pz) ~= "number" then return nil end
            local rot, yaw = W.get(vars, "playerCameraRotation"), nil
            if rot ~= nil then
                local ok, v = pcall(function() return rot[1] end)
                if ok and type(v) == "number" and abs(v) <= 36000 then yaw = v end
            end
            yaw = yaw or W.get(vars, "playerRotY")
            return px * 1000, pz * 1000, type(yaw) == "number" and yaw or 0
        end
        -- On this player's screen: within 60 m and inside a 100-degree cone of its camera (forward = sin, cos of the yaw).
        function P.seen(px, pz, yaw, x, z)
            local dx, dz = x - px, z - pz
            local d2 = dx * dx + dz * dz
            if d2 > 3600000000 then return false end
            if d2 < 1000000 then return true end
            local a = (math.deg(math.atan2(dx, dz)) - yaw + 180) % 360 - 180
            return abs(a) <= 50
        end
        -- The place error window (enemy follow): each sample's distance from this copy, one event=place_err line a second
        -- (bins under 1, 2, 3, 5, 8, 15 m and beyond; only guards within 60 m of this player).
        local EDGES = {1000, 2000, 3000, 5000, 8000, 15000}
        function P.winReset() P.win = {n = 0, b = {0, 0, 0, 0, 0, 0, 0}, v = {}, fix = 0} end
        P.winReset()
        function P.err(d, fix)
            local w = P.win
            local k = #EDGES + 1
            for i, e in ipairs(EDGES) do if d < e then k = i; break end end
            w.n, w.b[k] = w.n + 1, w.b[k] + 1
            if #w.v < 256 then w.v[#w.v + 1] = d end
            if fix then w.fix = w.fix + 1 end
        end
        function P.errTick(now)
            if now - (P.errAt or -1e9) < 1000 then return end
            P.errAt = now
            local w = P.win
            if w.n == 0 then return end
            table.sort(w.v)
            local m = #w.v
            L("event=place_err;n=%d;h=%s;p50=%.1f;p95=%.1f;max=%.1f;fix=%d;q=%d", w.n, table.concat(w.b, ","),
                w.v[max(1, ceil(m * 0.5))] / 1000, w.v[max(1, ceil(m * 0.95))] / 1000, w.v[m] / 1000, w.fix, #P.q)
            P.winReset()
        end
        -- One host sample of a guard (kind 3). Enemy follow (W.followOn, free roam): this copy is compared with the host's
        -- sample moved on by the host copy's speed (from its previous sample) for min(age + 100 ms, 400 ms); a guard
        -- off this player's screen is corrected beyond 2.5 m on two samples or 5 m once, one on screen beyond 5 m on two
        -- samples or 8 m once, 1.5 s apart, never within 3 m of this player or when this copy is 250 m from both players;
        -- the first sample after pairing corrects beyond 2.5 m at once. A place-first sample (flag 4) corrects from 1 m at
        -- once, ahead of the FIFO. Mother Base staff: 2 m on two samples or 5 m once, 2.5 s apart; the lab key without
        -- follow (0.14.7): 3.5 m on two samples or 8 m once. The 0.14.7 combat nudge (SetDiscoveryPlayer on this copy
        -- while the host copy fights) pulled the copies apart and is gone: the line stays.
        function P.sample(c)
            local en = W.ent[c.h]
            if not en or P.come[c.h] or type(en.id) ~= "number" then return end
            local prev = P.lastS[c.h]
            P.lastS[c.h] = c
            if not W.w1 and c.flags % 2 == 1 then return end
            local first = c.flags % 8 >= 4
            c.first = first or nil
            if prev and c.at > prev.at and c.at - prev.at <= 4000 then
                local dt = c.at - prev.at
                c.vx, c.vz = max(-10, min(10, (c.x - prev.x) / dt)), max(-10, min(10, (c.z - prev.z) / dt))
            end
            -- Never while this guard's placement is pending; a queued correction takes the
            -- newest target (and its newer grant) in place.
            if P.cur and P.cur.h == c.h then return end
            local div = P.div[c.h] or {n = 0, fixes = 0, at = -1e9, combatAt = -1e9}
            if W.w1 and c.flags % 2 == 1 and c.at - (div.combatAt or -1e9) >= 4000 and P.state(en) == 0 then
                div.combatAt = c.at
                P.div[c.h] = div
                L("event=place_combat;seq=%d;ent=%.0f;nudge=0", c.seq, c.h)
            end
            for i, q in ipairs(P.q) do
                if q.h == c.h then
                    if q.kind == 3 then
                        q.seq, q.x, q.y, q.z, q.yaw, q.flags, q.at = c.seq, c.x, c.y, c.z, c.yaw, c.flags, c.at
                        if first and not q.first then
                            q.first = true
                            table.remove(P.q, i)
                            table.insert(P.q, 1, q)
                        end
                    end
                    return
                end
            end
            local x, _, z = pos(en.id)
            if not x then return end
            local follow, staff = W.w1 and W.followOn == true, W.w1 and W.feature and W.feature.staffPlace
            local tx, tz = c.x, c.z
            if follow and c.vx then
                local ahead = min(max(0, W.now() - c.at) + 100, 400)
                tx, tz = c.x + c.vx * ahead, c.z + c.vz * ahead
            end
            local d = flat(x, z, tx, tz)
            P.div[c.h] = div
            local fix, why, view = false, nil, nil
            local qmax = follow and 16 or 8
            if W.w1 then
                if follow and not staff then
                    local px, pz, yaw = P.view()
                    local seen = px ~= nil and (P.seen(px, pz, yaw, x, z) or P.seen(px, pz, yaw, tx, tz))
                    view = seen and "on" or "off"
                    -- Off screen a hop is not seen: one sample over 2.5 m, 1 s apart (run 3e01f334: a corrected guard
                    -- stands still until his next correction while the host's copy runs on, up to 9 m in 1.7 s with two
                    -- samples); on screen two samples over 5 m or one over 8 m, 1.5 s apart.
                    div.n = d > (seen and 5000 or 2500) and div.n + 1 or 0
                    fix = (seen and (div.n >= 2 or d > 8000) or (not seen and d > 2500)) and c.at - div.at >= (seen and 1500 or 1000)
                    if not div.seen and d > 2500 then fix, why = true, "pair" end
                    if fix and px and (flat(x, z, px, pz) < 3000 or flat(tx, tz, px, pz) < 3000) then fix, why = false, "near_player" end
                    if fix and px and flat(x, z, px, pz) > 250000 then
                        local qx, qz = P.partnerXZ(c.at)
                        if not qx or flat(x, z, qx, qz) > 250000 then fix, why = false, "far" end
                    end
                else
                    -- Mother Base staff (staffPlace): tighter, so both screens show them on the same spot.
                    div.n = d > (staff and 2000 or 3500) and div.n + 1 or 0
                    fix = (div.n >= 2 or d > (staff and 5000 or 8000)) and c.at - div.at >= 2500
                end
                if first and why ~= "near_player" and why ~= "far" then fix, why = d >= 1000, "first" end
                fix = fix and P.state(en) == 0 and #P.q < qmax
            else
                div.n = d > 6000 and div.n + 1 or 0
                local cp = W.cp[en.cp]
                fix = div.n >= 2 and P.state(en) == 0 and not (cp and cp.localPhase >= 3) and div.fixes < 10 and
                    c.at - div.at >= 5000 and #P.q < 8
            end
            div.seen = true
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            local mine = type(px) == "number" and type(pz) == "number" and flat(x, z, px * 1000, pz * 1000) <= 60000
            if follow and mine then P.err(d, fix) end
            -- Follow logs only a correction, a guard 2 m off, a place-first or a first sample (place_err counts the rest).
            if not follow or fix or first or why or d >= 2000 then
                L("event=place_div;seq=%d;ent=%.0f;div_m=%.1f;n=%d;fix=%d;host_xz=%.1f,%.1f;here_xz=%.1f,%.1f%s%s", c.seq, c.h,
                    d / 1000, div.n, fix and 1 or 0, c.x / 1000, c.z / 1000, x / 1000, z / 1000, view and ";view=" .. view or "",
                    why and ";why=" .. why or "")
            end
            if fix then
                div.n, div.fixes, div.at = 0, div.fixes + 1, c.at
                c.used = true
                if first then table.insert(P.q, 1, c) else P.q[#P.q + 1] = c end
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
            if k <= 2 and c.flags % 4 >= 2 and call(TppEnemy, "SetEnableRestrictNotice", id) and m and type(m.ignored) == "table" then
                m.ignored[en.name] = true
            elseif k == 1 and c.flags % 2 == 1 and m and type(m.ignored) == "table" and m.ignored[en.name] and
                call(TppEnemy, "SetDisableRestrictNotice", id) then
                m.ignored[en.name] = nil  -- as the host: a "come" warp of an ignoring guard notices again
            end
            -- Flag 1 means come only for a warp or route placement; on a correction (kind 3) it is the host copy's combat
            -- flag (run f2d2db9b: one combat correction per guard marked him come and his samples were ignored after it).
            if c.kind <= 2 and c.flags % 2 == 1 then P.come[c.h] = true end
            -- Mother Base corrections never pin SetOutOfArea (allCp counts every CP active anyway).
            if not (k == 3 and W.feature and W.feature.staffPlace) then P.pinOn(c.h) end
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
            -- Mother Base staff: never force-realize a staff member this copy has not loaded.
            elseif r.r == -1 and c.kind == 3 and W.feature and W.feature.staffPlace then return P.done(c, "realize_timeout")
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
            if c.first then
                L("event=place_first;step=done;seq=%d;ent=%.0f;result=%s;why=%s;err_m=%.2f;ms=%d", c.seq, c.h,
                    why and "refused" or "applied", why or "none", c.errCm / 100, floor(W.now() - c.at))
            end
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
            if W.followOn == true then P.errTick(now) end
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
        -- Place first (Mother Base staff, staffPlace; every W1 guard with enemy follow): a host outcome of a guard waits until
        -- this copy stands where the host's does: a placement of h queued or running, or the newest sample (at most 2 s
        -- old: the host's place first) more than 1.5 m away while NORMAL is queued now (no 2-sample rule, no cooldown). At
        -- most 4 s at Mother Base, 1.5 s in free roam: then the placement is dropped and the outcome applies anyway.
        function W.placeHold(h, now)
            if W.host or not ((W.feature and W.feature.staffPlace) or W.followOn == true) then return false end
            local limit = W.feature and W.feature.staffPlace and 4000 or 1500
            local c = P.cur
            if c and c.h == h then
                if now - c.at < limit then return true end
                P.done(c, "context")
                return false
            end
            for i, q in ipairs(P.q) do
                if q.h == h then
                    if now - q.at < limit then return true end
                    table.remove(P.q, i)
                    return false
                end
            end
            local s = P.lastS[h]
            if not s or s.used or now - s.at > 2000 then return false end
            s.used = true
            local en = W.ent[h]
            if not en or type(en.id) ~= "number" or P.state(en) ~= 0 then return false end
            local x, _, z = pos(en.id)
            if not x then return false end
            local d = flat(x, z, s.x, s.z)
            local fix = d >= 1500 and #P.q < (W.followOn == true and 16 or 8)
            L("event=place_first;step=hold;seq=%d;ent=%.0f;div_m=%.1f;fix=%d", s.seq, h, d / 1000, fix and 1 or 0)
            if fix then s.first = true; table.insert(P.q, 1, s) end
            return fix
        end
        W.unpairedHooks[#W.unpairedHooks + 1] = function()
            W.rosterMatch = false
            -- P.div too: the first sample of each guard after the next pairing corrects at once (enemy follow).
            P.out, P.q, P.lastS, P.div = {}, {}, {}, {}
            local c = P.cur
            P.release(c)
            P.cur = nil
        end
    end
end
