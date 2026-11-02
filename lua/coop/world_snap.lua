-- MGS co-op world sync, enemy sync M4 (original; docs/world-sync-design.md 7.10): the owner's position stream of the
-- guards near the other player (SOLDIER_SNAP 75, a datagram: 10 Hz within 30 m of that player or in an ALERT CP, 5 Hz
-- to 60 m) and the receiver's soft follow: one DRIVE_V1 per guard and update for up to 8 guards, the native stepping
-- this copy toward the owner's newest two samples, 100 ms behind (at rest under 1.5 m, at most 0.4 m plus the owner's
-- speed every 100 ms, the whole way beyond 8 m). The owner is the host, or the guest for the guards it owns (M3,
-- W.ownerOn). Only with soldier_snap (CFG r[19] bit 4, W.snapOn) and follow. While a guard is driven the M1 / M3
-- corrections leave him alone (world_place_guest.lua, W.snapDriving).
do
    local W = e.MgsCoopWorld
    local P = type(W) == "table" and W.place
    if type(P) == "table" and type(W.enemy) == "table" then
        local S, OP = {}, W.OP
        W.snap = S
        OP.SOLDIER_SNAP = 75
        local fmt, floor, min = string.format, math.floor, math.min
        local pos, flat = P.pos, P.flat
        local function L(...) W.log(fmt(...)) end
        function S.reset()
            S.slot, S.tx, S.dv, S.rr, S.dr, S.statAt = nil, {}, {}, 0, 0, nil
            S.n = {tx = 0, calls = 0, steps = 0, hard = 0, rest = 0, stale = 0, refused = 0}
        end
        S.reset()
        W.startHooks[#W.startHooks + 1] = S.reset
        W.unpairedHooks[#W.unpairedHooks + 1] = function() S.tx, S.dv, S.slot = {}, {}, nil end
        -- A guard's roster slot (his W.entOrder index: the same on both copies once the rosters match).
        function S.slotOf(h)
            if not S.slot then
                S.slot = {}
                for i, k in ipairs(W.entOrder or {}) do S.slot[k] = i end
            end
            return S.slot[h]
        end
        -- This side owns him: the host every guard but the guest's (M3); the guest only those (M3).
        function S.mine(h)
            if W.host then return not (W.ownerOn and P.own and P.own[h] and P.own[h].o == 1) end
            return W.ownerOn == true and P.owned ~= nil and P.owned[h] == true
        end
        function S.ready()
            return W.snapOn and W.followOn == true and W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true
        end
        -- Producer: at most 6 reads per update of the NORMAL guards this side owns within 60 m of the other player.
        function S.produce(now)
            local list = W.entOrder
            local n = type(list) == "table" and #list or 0
            if n == 0 then return end
            local qx, qz = P.partnerXZ(now)
            if not qx then return end
            local reads, scans = 0, 0
            while reads < 6 and scans < 48 and scans < n do
                S.rr, scans = S.rr % n + 1, scans + 1
                local h = list[S.rr]
                local en, t = W.ent[h], S.tx[h]
                if en and type(en.id) == "number" and S.mine(h) and (not t or now - t.at >= t.every) then
                    reads = reads + 1
                    if not t then t = {}; S.tx[h] = t end
                    t.at, t.every = now, 1000
                    if P.state(en) == 0 then
                        local x, y, z = pos(en.id)
                        local d = x and (x ~= 0 or z ~= 0) and flat(x, z, qx, qz)
                        if d and d <= 60000 then
                            local cp = W.cp and W.cp[en.cp]
                            local alert = cp ~= nil and cp.localPhase == 3
                            t.every = (d <= 30000 or alert) and 100 or 200
                            local moved = t.x and flat(x, z, t.x, t.z) or 0
                            local dt = t.x and now - t.sampleAt or 0
                            local speed = dt > 0 and dt < 2000 and floor(min(65535, moved * 1000 / dt)) or 0
                            local yaw = moved >= 50 and floor(math.atan2(x - t.x, z - t.z) * 1000 + 0.5) or (t.yaw or 0)
                            local slot = S.slotOf(h)
                            if slot and slot <= 512 and W.tx(OP.SOLDIER_SNAP, floor(now) % 4294967296, slot, x, y - 800 + P.LIFT, z, yaw,
                                (alert and 1 or 0) + (moved >= 50 and 2 or 0), speed) then
                                S.n.tx = S.n.tx + 1
                            end
                            t.x, t.z, t.yaw, t.sampleAt = x, z, yaw, now
                        end
                    end
                end
            end
        end
        -- Receiver: one DRIVE_V1 for each of up to 8 NORMAL guards the other side owns within 60 m of this player, not
        -- being placed; a stale stream backs a guard off 500 ms, a refusal 2 s.
        function S.drive(now)
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            local list = W.entOrder
            if type(px) ~= "number" or type(pz) ~= "number" or type(list) ~= "table" then return end
            px, pz = px * 1000, pz * 1000
            local n, calls, scans = #list, 0, 0
            while calls < 8 and scans < 48 and scans < n do
                S.dr, scans = S.dr % n + 1, scans + 1
                local h = list[S.dr]
                local en, dv = W.ent[h], S.dv[h]
                if en and type(en.id) == "number" and not S.mine(h) and not (P.cur and P.cur.h == h) and
                    not (dv and dv.back and now < dv.back) and P.state(en) == 0 then
                    local x, y, z = pos(en.id)
                    local slot = S.slotOf(h)
                    if slot and x and (x ~= 0 or z ~= 0) and flat(x, z, px, pz) <= 60000 then
                        calls = calls + 1
                        S.n.calls = S.n.calls + 1
                        if not dv then dv = {}; S.dv[h] = dv end
                        local r = W.receipt(fmt("return 'MGS_COOP_WORLD_DRIVE_V1' -- %d %d %d %d %d", slot, en.id, x, y, z))
                        if type(r) == "table" and r.refused then
                            if r.refused == "stale" then S.n.stale, dv.back = S.n.stale + 1, now + 500
                            else S.n.refused, dv.back = S.n.refused + 1, now + 2000 end
                        elseif type(r) == "table" then
                            dv.ok, dv.err = now, r.e
                            if r.s == 2 then S.n.hard = S.n.hard + 1
                            elseif r.s == 1 then S.n.steps = S.n.steps + 1
                            else S.n.rest = S.n.rest + 1 end
                        end
                    end
                end
            end
        end
        -- The M1 / M3 corrections leave a guard alone while the stream drives him (a result in the last second).
        function W.snapDriving(h, now)
            local dv = S.dv and S.dv[h]
            return dv ~= nil and dv.ok ~= nil and (now or W.now()) - dv.ok < 1000
        end
        local tick = P.tick
        P.tick = function(now)
            tick(now)
            if not S.ready() then return end
            S.produce(now)
            S.drive(now)
            if not S.statAt or now - S.statAt >= 10000 then
                S.statAt = now
                local c = S.n
                L("event=snap_stats;tx=%d;calls=%d;steps=%d;hard=%d;rest=%d;stale=%d;refused=%d", c.tx, c.calls, c.steps, c.hard, c.rest,
                    c.stale, c.refused)
            end
        end
    end
end
