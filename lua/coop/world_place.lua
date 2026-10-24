-- MGS co-op world sync, spawn sync (original; docs/debug-menu.md "Spawn sync"):
-- the host announces each F9 guard placement and per-soldier debug action as
-- ENEMY_DEBUG 34 for the same authored soldier (StrCode32 key, matching roster);
-- the guest applies it to its copy (world_place_guest.lua) and answers
-- ENEMY_DEBUG_ACK 35. Kinds: 1 warp 2 route 3 correct 4 come 5 ignore 6 notice
-- 7 disable 8 enable; flags 1 come, 2 restrict notice. Needs pairing (eff >= 2)
-- and a matching roster, not W1. Placed guards stay in an active area on both
-- PCs (SetOutOfArea false, re-applied after every area update).
do
    local W = e.MgsCoopWorld
    if type(W) == "table" and type(W.enemy) == "table" and type(W.area) == "table" then
        local P, OP, H, E, A = {}, W.OP, W.handlers, W.enemy, W.area
        W.place = P
        OP.ENEMY_DEBUG, OP.ENEMY_DEBUG_ACK = 34, 35
        local fmt, floor, sqrt = string.format, math.floor, math.sqrt
        P.KIND = {"warp", "route", "correct", "come", "ignore", "notice", "disable", "enable"}
        P.WHY = {"not_ready", "roster", "unknown_entity", "unmapped", "terminal_local", "vehicle_local", "disabled_local",
            "native_off", "grant", "pose", "range", "stance", "realize_timeout", "position_timeout", "route_unknown", "context",
            "native_other", "unreadable_local", "busy"}
        local KIND, WHY = P.KIND, P.WHY
        local function L(...) W.log(fmt(...)) end
        -- The wire state of this PC's copy (world_enemy.lua), or nil when unreadable or unbound.
        function P.state(en)
            local ok, st = pcall(E.localState, en)
            return ok and st or nil
        end
        -- Position in mm, or nil.
        function P.pos(id)
            local ok, p = pcall(W.api.send, id, {id = "GetPosition"})
            if not ok or (type(p) ~= "userdata" and type(p) ~= "table") then return nil end
            local good, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end)
            if not good then return nil end
            for _, v in ipairs({x, y, z}) do if type(v) ~= "number" or v ~= v or math.abs(v) > 100000 then return nil end end
            return floor(x * 1000 + 0.5), floor(y * 1000 + 0.5), floor(z * 1000 + 0.5)
        end
        function P.flat(ax, az, bx, bz) return sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
        local pos, flat = P.pos, P.flat
        -- One debug menu history row (MGS_COOP_DEBUG_REMOTE_V1, id 0, never a command).
        function P.remote(ok, text)
            W.receipt("return 'MGS_COOP_DEBUG_REMOTE_V1' -- " .. (ok and "1 " or "0 ") ..
                string.sub((string.gsub(text, "[^%w_=,.;:%-]", "_")), 1, 160))
        end

        function P.reset()
            P.seq, P.out, P.live, P.q, P.cur, P.pin, P.come, P.div, P.last, P.lastAck, P.routes =
                0, {}, {}, {}, nil, {}, {}, {}, nil, nil, nil
            P.patrol = {rr = 0, s = {}, combat = {}}
            P.n = {sent = 0, acked = 0, corrected = 0, refused = 0, applied = 0, failed = 0}
            W.pinned, W.rosterMatch = P.pin, false
        end
        P.reset()
        W.startHooks[#W.startHooks + 1] = P.reset
        if type(W.observers) == "table" then
            W.observers[#W.observers + 1] = function(kind, h)
                if kind == "Damage" and P.patrol then P.patrol.combat[h] = W.now() end
            end
        end
        -- Releases an entry's force-realize; the game call only while its mission is still in
        -- gameplay (as the debug menu's cancel), never during teardown.
        function P.release(c)
            if not (c and c.forced and c.en) then return end
            c.forced = nil
            if W.get(vars, "missionCode") ~= W.mission then return end
            local ok, live = pcall(W.inGame)
            if ok and live == true then pcall(W.api.send, c.en.id, {id = "SetForceRealize", forceRealize = false}) end
        end
        -- Stop (OnTerminate, LEVEL 0): release a pending force-realize, then forget. W1 off
        -- (full=false) keeps everything: spawn sync does not depend on outcome sync.
        W.stopHooks[#W.stopHooks + 1] = function(full)
            if not full then return end
            P.release(P.cur)
            P.q, P.cur, P.live, P.pin = {}, nil, {}, {}
            W.pinned = P.pin
        end
        local paired = H[OP.PAIRED]
        H[OP.PAIRED] = function(v, now)
            W.rosterMatch = v[5] == 1 and W.rosterOk == true
            if paired then return paired(v, now) end
        end
        -- Placed guards: never out of area on this PC (bookkeeping: E.active).
        function P.pinOn(h)
            local en = W.ent[h]
            if not en or not W.api.setOut then return end
            P.pin[h] = true
            pcall(W.api.setOut, {en.name}, false)
        end
        local update = A.update
        function A.update(...)
            update(...)
            for h in pairs(P.pin) do
                local en = W.ent and W.ent[h]
                if en and W.api.setOut then pcall(W.api.setOut, {en.name}, false) end
            end
        end

        -- Host: one ENEMY_DEBUG; seq, or nil and why.
        function P.announce(kind, h, x, y, z, yaw, flags, route)
            if not W.active then return nil, "world_off" end
            if not W.host then return nil, "guest" end
            if not (W.paired and W.eff >= 2) then return nil, "not_paired" end
            if W.rosterMatch ~= true then return nil, "roster" end
            local en = W.ent[h]
            if not en then return nil, "unknown_entity" end
            local seq = P.seq % 65535 + 1
            local go = type(en.id) == "number" and en.id >= 0 and en.id <= 65535 and en.id or 0
            local ok, why = W.tx(OP.ENEMY_DEBUG, seq, h, kind, x, y, z, yaw, flags, route, go)
            if not ok then return nil, "tx_" .. tostring(why) end
            P.seq, P.out[seq] = seq, {h = h, kind = kind}
            P.n.sent = P.n.sent + 1
            if kind ~= 3 then L("event=place_tx;seq=%d;ent=%.0f;kind=%s;flags=%d", seq, h, KIND[kind], flags) end
            return seq
        end
        -- Debug menu (host): announce and return the result tag. Warp: the exact native
        -- request (mm, mrad) and dy = body y minus request y; route: the readback.
        function P.share(kind, h, x, y, z, yaw, flags, route, dy)
            flags = flags or 0
            local seq, why = P.announce(kind, h, x or 0, y or 0, z or 0, yaw or 0, flags, route and W.h(route) or 0)
            if not seq then return "sync=none;why=" .. why end
            if kind <= 2 then P.pinOn(h) end
            local now = W.now()
            if kind == 1 and flags % 2 == 0 then
                P.live[h] = {at = now, next = now + 1000, sent = now, x = x, z = z, yaw = yaw, dy = dy or 0, flags = flags}
            end
            return "sync=world;seq=" .. seq
        end
        -- Host settle window: 60 s after landing while NORMAL, 1 Hz, a correction (kind 3)
        -- when the guard moved 1 m or every 5 s; "come" guards are never corrected.
        function P.hostTick(now)
            for h, l in pairs(P.live) do
                local en = W.ent[h]
                if not en or now - l.at > 60000 or P.state(en) ~= 0 then P.live[h] = nil
                elseif now >= l.next then
                    l.next = now + 1000
                    local x, y, z = pos(en.id)
                    if x then
                        local moved = flat(x, z, l.x, l.z)
                        if l.force or moved >= 1000 or now - l.sent >= 5000 then
                            local yaw = moved >= 300 and floor(math.atan2(x - l.x, z - l.z) * 1000 + 0.5) or l.yaw
                            if P.announce(3, h, x, y - l.dy, z, yaw, l.flags, 0) then
                                l.x, l.z, l.yaw, l.sent, l.force = x, z, yaw, now, nil
                            end
                        end
                    end
                end
            end
            if W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true and P.patrol then
                local list = W.entOrder
                local n = type(list) == "table" and #list or 0
                local hx, hz = (W.get(vars, "playerPosX") or 0) * 1000, (W.get(vars, "playerPosZ") or 0) * 1000
                local pat, scans, reads = P.patrol, 0, 0
                while scans < 8 and reads < 2 and scans < n do
                    pat.rr, scans = pat.rr % n + 1, scans + 1
                    local h = list[pat.rr]
                    local en = W.ent[h]
                    local ps = pat.s[h]
                    if en and not P.live[h] and E.active(en.cp, h) and (not ps or now - ps.sampleAt >= 500) then
                        reads = reads + 1
                        if P.state(en) == 0 then
                            local x, y, z = pos(en.id)
                            if x then
                                local d2 = (x - hx) ^ 2 + (z - hz) ^ 2
                                if d2 <= 3025000000 then
                                    local cp = W.cp and W.cp[en.cp]
                                    local alert = (cp and cp.localPhase == 3) or (pat.combat[h] and now - pat.combat[h] <= 5000)
                                    local flags = alert and 1 or 0
                                    if not ps then
                                        ps = {x = x, z = z, yaw = 0, sent = now, sampleAt = now, alert = alert}
                                        pat.s[h] = ps
                                        P.announce(3, h, x, y - 800, z, 0, flags, 0)
                                    else
                                        ps.sampleAt = now
                                        local moved = flat(x, z, ps.x, ps.z)
                                        local combatNew = alert and not ps.alert
                                        if moved >= 1500 or now - ps.sent >= 3000 or combatNew then
                                            local yaw = moved >= 300 and floor(math.atan2(x - ps.x, z - ps.z) * 1000 + 0.5) or ps.yaw
                                            if P.announce(3, h, x, y - 800, z, yaw, flags, 0) then
                                                ps.x, ps.z, ps.yaw, ps.sent, ps.alert = x, z, yaw, now, alert
                                            end
                                        end
                                    end
                                end
                            end
                        else
                            pat.s[h] = nil
                        end
                    end
                end
            end
        end
        H[OP.ENEMY_DEBUG_ACK] = function(v)
            if not W.host then return end
            local o, ok = P.out[v[1]], v[3] == 0
            P.out[v[1]] = nil
            local kind = o and o.kind or 0
            if not ok then P.n.refused = P.n.refused + 1
            elseif kind == 3 then P.n.corrected = P.n.corrected + 1
            else P.n.acked = P.n.acked + 1 end
            local text = fmt("guest_spawn;seq=%d;ent=%.0f;kind=%s;result=%s;why=%s;err_m=%.2f;frames=%d", v[1], v[2],
                KIND[kind] or "unknown", ok and "applied" or "refused", WHY[v[4]] or "none", v[5] / 100, v[6])
            L("event=place_ack;%s", string.sub(text, 13))
            if kind ~= 3 or not ok then P.lastAck = text; P.remote(ok, text) end
        end
        -- A re-pair in the same epoch re-sends the tracked guards' positions.
        W.pairedHooks[#W.pairedHooks + 1] = function(epochChanged)
            if W.host and not epochChanged then for _, l in pairs(P.live) do l.force, l.next = true, 0 end end
        end
        function P.tick(now)
            if W.host then P.hostTick(now) elseif (W.paired or P.cur) and P.guestTick then P.guestTick(now) end
        end

        -- Debug menu read-only reports (both roles): world_status, spawn_sync.
        function P.report(cmd, D)
            if cmd == "world_status" then
                local area = W.areaState and W.areaState.localArea
                return true, fmt("world;paired=%d;eff=%d;w1=%d;roster=%s;level=%d;area=%s;sent=%d;acked=%d;corrected=%d;refused=%d;" ..
                    "applied=%d;failed=%d", W.paired and 1 or 0, W.eff or 0, W.w1 and 1 or 0,
                    not W.paired and "unknown" or W.rosterMatch and "match" or "mismatch", W.level or 0,
                    area and tostring(area) or W.inferredArea and "inferred" .. W.inferredArea or "none",
                    P.n.sent, P.n.acked, P.n.corrected, P.n.refused, P.n.applied, P.n.failed)
            end
            if W.host then
                if not P.lastAck then return false, "no_guest_ack" end
                return true, P.lastAck
            end
            local c = P.last
            if not c or not P.row then return false, "no_remote_spawn" end
            local text = P.row(c)
            if c.why then return true, text end
            local x, y, z = pos(c.en.id)
            local function m(a, b, d) return x and fmt("%.2f", sqrt((x - a) ^ 2 + (y - b) ^ 2 + (z - d) ^ 2) / 1000) or "unknown" end
            local pp, vx, vy, vz = D and D.partnerPose, W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
            local px = type(pp) == "table" and type(pp[1]) == "number" and type(pp[2]) == "number" and type(pp[3]) == "number"
            local lx = type(vx) == "number" and type(vy) == "number" and type(vz) == "number"
            return true, fmt("%s;partner_m=%s;player_m=%s;goid_match=%d", text, px and m(pp[1], pp[2], pp[3]) or "unknown",
                lx and m(vx * 1000, vy * 1000, vz * 1000) or "unknown", c.go == c.en.id and 1 or 0)
        end
    end
end
