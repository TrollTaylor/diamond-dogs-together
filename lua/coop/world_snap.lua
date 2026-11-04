-- MGS co-op world sync, enemy sync M4 (original; docs/world-sync-design.md 7.10): the owner's position stream of the
-- guards near the other player (SOLDIER_SNAP 75, a datagram: 10 Hz within 30 m of that player or in an ALERT CP, 5 Hz
-- to 60 m) and the receiver's soft follow: one DRIVE_V1 per guard and update for up to 8 guards, the native stepping
-- this copy toward the owner's newest two samples, 100 ms behind (at rest under 1.5 m, at most 0.4 m plus the owner's
-- speed every 100 ms, the whole way beyond 8 m). The owner is the host, or the guest for the guards it owns (M3,
-- W.ownerOn). Only with soldier_snap (CFG r[19] bit 4, W.snapOn) and follow. While a guard is driven the M1 / M3
-- corrections leave him alone (world_place_guest.lua, W.snapDriving).
-- M6 (docs/world-sync-design.md 7.12; soldier_body, CFG r[19] bit 16, W.bodyOn): a lying body that was carried and dropped
-- is placed on the other copy too (a kind-3 placement with flag 8, either side reporting the bodies near its own player),
-- and a held-up guard raises his hands on the other copy (SetForceHoldup).
-- M5 (docs/world-sync-design.md 7.11; soldier_route, CFG r[19] bit 8, W.routeOn): the host reads the route each of its
-- guards walks (GetGameObjectIdUsingRoute on every route of the mission's route sets, 6 per update) and tells the guest
-- when a guard near it walks another one (ENEMY_DEBUG kind 2 flag 16: route only, no placement); the guest's copy takes
-- it (SetSneakRoute without a point: the engine's default 0xFFFF, the dispatcher's own, joins where he stands) unless it
-- is already on it (IsOnRoute), and again right after each correction lands.
-- M7 probe (docs/world-sync-design.md 7.13; soldier_puppet, CFG r[19] bit 32, W.puppetOn; the plan's own scope for now:
-- probes behind a switch): on the guest, the copies of the host's guards it drives become puppets (SetPuppet true and
-- restrict notice: they neither move by themselves nor notice this player) driven every update (DRIVE_V1 mode 1);
-- outside 60 m or off NORMAL they are set back. Without soldier_perceive the host's AI does not perceive the guest's body
-- (the target filter at 0x1325629 compares the target with the local player only), so the probe's puppets never fight.
-- M7 brain (soldier_brain with soldier_perceive, CFG r[19] bit 64, W.brainOn): each guard's owner (the host, or the guest
-- for the guards it owns: M3) is its only brain. Native soldier_authority (MGS_COOP_SOLDIER_AUTH_V1 bits, sent here)
-- drops the other copy's hits on a player and forwards the owner's hits on the replica as SOLDIER_HIT
-- (world_friendly.lua). The other copy of a calm guard is a puppet (SetPuppet: it never notices or fights; it stands in its
-- idle pose (run ef7e9088) and only the M4 stream moves it; M1 corrections refuse it as disabled_local;
-- restrict notice did not hold the copies back: run 5fb44105, 89 hits on a restrict-noticed guest in 30 s); it is set free
-- (its own AI fights, its damage counted only by the owner) while its CP is in ALERT, for 8 s after its owner's copy hit
-- this player (SOLDIER_HIT: W.soldierEngaged, which also makes it discover this player), and for 8 s after this player
-- damaged it (so a shot guard still reacts and its CP raises the alarm).
-- M7 step 1 (soldier_perceive, CFG r[19] bit 256, W.perceiveOn; docs/world-sync-design.md 7.13): on the host each guard
-- the M1 scan samples perceives one player, the nearer: the guest's body (target 1) once the guest is at least 3 m nearer
-- than the host, the host (0) once the host is at least 3 m nearer; a choice holds 4 s (native soldier_perceive answers
-- the offense index getter per soldier: MGS_COOP_SOLDIER_PERCEIVE_V1).
do
    local W = e.MgsCoopWorld
    local P = type(W) == "table" and W.place
    if type(P) == "table" and type(W.enemy) == "table" then
        local S, OP = {}, W.OP
        W.snap = S
        OP.SOLDIER_SNAP = 75
        local fmt, floor, min, sqrt = string.format, math.floor, math.min, math.sqrt
        local pos, flat = P.pos, P.flat
        local function L(...) W.log(fmt(...)) end
        function S.reset()
            S.slot, S.tx, S.dv, S.rr, S.dr, S.statAt, S.bd, S.br, S.hu = nil, {}, {}, 0, 0, nil, {}, 0, {}
            S.rt = {names = nil, i = 0, cur = {}, want = {}}
            S.n = {tx = 0, calls = 0, steps = 0, hard = 0, rest = 0, stale = 0, refused = 0, sink = 0}
        end
        S.reset()
        W.startHooks[#W.startHooks + 1] = S.reset
        W.unpairedHooks[#W.unpairedHooks + 1] = function()
            if S.brainRelease then pcall(S.brainRelease, "unpaired") end
            S.tx, S.dv, S.slot, S.rt.cur, S.rt.want = {}, {}, nil, {}, {}
            for h in pairs(S.pp or {}) do
                local en = W.ent[h]
                if en and type(en.id) == "number" then S.puppet(h, en, false) end
            end
            S.pp = nil
        end
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
                        local mode = W.puppetOn and not W.host and S.puppet(h, en, true) and 1 or 0
                        local r = W.receipt(fmt(mode == 1 and "return 'MGS_COOP_WORLD_DRIVE_V1' -- %d %d %d %d %d 1" or
                            "return 'MGS_COOP_WORLD_DRIVE_V1' -- %d %d %d %d %d", slot, en.id, x, y, z))
                        if type(r) == "table" and r.refused then
                            if r.refused == "stale" then S.n.stale, dv.back = S.n.stale + 1, now + 500
                            else S.n.refused, dv.back = S.n.refused + 1, now + 2000 end
                        elseif type(r) == "table" then
                            dv.ok, dv.err = now, r.e
                            -- Enemy gaps (place_sink): a driven copy 1.5 m under its owner for 2 s (r.v: its y minus the
                            -- target's, cm; the stream's y is the owner's GetPosition y - 800 + LIFT) is event=drive_sink.
                            local off = type(r.v) == "number" and r.v * 10 - (800 - P.LIFT) or nil
                            if off and off < -1500 then
                                dv.lowAt = dv.lowAt or now
                                if now - dv.lowAt >= 2000 and not dv.sunk then
                                    dv.sunk, S.n.sink = true, S.n.sink + 1
                                    L("event=drive_sink;ent=%.0f;off_m=%.2f;e_cm=%d", h, off / 1000, r.e or -1)
                                end
                            else dv.lowAt, dv.sunk = nil, nil end
                            if r.s == 2 then S.n.hard = S.n.hard + 1
                            elseif r.s == 1 then S.n.steps = S.n.steps + 1
                            else S.n.rest = S.n.rest + 1 end
                        end
                    end
                end
            end
        end
        -- M7 probe: one guard on (true) or off as a puppet; logged once per change.
        function S.puppet(h, en, on)
            S.pp = S.pp or {}
            if (S.pp[h] == true) == on then return on end
            local ok1 = pcall(W.api.send, en.id, {id = "SetPuppet", enabled = on})
            local ok2 = pcall(W.api.send, en.id, {id = "SetRestrictNotice", enabled = on})
            S.pp[h] = on or nil
            L("event=puppet;ent=%.0f;on=%d;ok=%d", h, on and 1 or 0, ok1 and ok2 and 1 or 0)
            return on
        end
        -- Puppets leaving the bubble (60 m, a state change, the stream gone stale) are set back, 2 per update.
        function S.puppetsOff(now)
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            if type(px) ~= "number" or not S.pp then return end
            local n = 0
            for h in pairs(S.pp) do
                local en, dv = W.ent[h], S.dv[h]
                local x, _, z = nil, nil, nil
                if en and type(en.id) == "number" then x, _, z = pos(en.id) end
                -- A stale stream sets a puppet back only after 3 s without a drive result (run ef7e9088: 619 on/off
                -- toggles from 500 ms stale back-offs).
                if not x or P.state(en) ~= 0 or flat(x, z, px * 1000, pz * 1000) > 60000 or not W.puppetOn or
                    (dv and dv.back and now < dv.back and now - (dv.ok or 0) >= 3000) then
                    if en and type(en.id) == "number" then S.puppet(h, en, false) else S.pp[h] = nil end
                    n = n + 1
                    if n >= 2 then return end
                end
            end
        end
        -- The M1 / M3 corrections leave a guard alone while the stream drives him (a result in the last second).
        function W.snapDriving(h, now)
            local dv = S.dv and S.dv[h]
            return dv ~= nil and dv.ok ~= nil and (now or W.now()) - dv.ok < 1000
        end
        -- M6 bodies: at most 2 reads per update of the lying bodies (sleep .. dead) within 60 m of this player; one at rest
        -- (under 0.2 m in 1.5 s) more than 1.5 m from where it last lay was carried here: the other copy places its body
        -- there (host: ENEMY_DEBUG kind 3 flag 8; guest: ENEMY_PLACE flag 8), 0.2 m over where this one lies.
        function S.bodies(now)
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            local list = W.entOrder
            if type(px) ~= "number" or type(pz) ~= "number" or type(list) ~= "table" then return end
            px, pz = px * 1000, pz * 1000
            local n, reads, scans = #list, 0, 0
            while reads < 2 and scans < 32 and scans < n do
                S.br, scans = S.br % n + 1, scans + 1
                local h = list[S.br]
                local en, b = W.ent[h], S.bd[h]
                local st = en and type(en.id) == "number" and P.state(en)
                if st and st >= 2 and st <= 5 and (not b or now - b.at >= 500) then
                    reads = reads + 1
                    local x, y, z = pos(en.id)
                    if x and (x ~= 0 or z ~= 0) and flat(x, z, px, pz) <= 60000 then
                        if not b then b = {x = x, z = z, restX = x, restZ = z, still = now}; S.bd[h] = b end
                        b.at = now
                        if flat(x, z, b.x, b.z) >= 200 then b.x, b.z, b.still = x, z, now
                        elseif now - b.still >= 1500 and flat(x, z, b.restX, b.restZ) > 1500 then
                            b.restX, b.restZ = x, z
                            local ok
                            if W.host then ok = P.announce(3, h, x, y + 200, z, 0, 8, 0)
                            else
                                P.ownSeq = (P.ownSeq or 0) % 65535 + 1
                                ok = W.tx(OP.ENEMY_PLACE, P.ownSeq, h, 3, x, y + 200, z, 0, 8, 0, en.id % 65536)
                            end
                            L("event=body_tx;ent=%.0f;x=%d;z=%d;sent=%d", h, x, z, ok and 1 or 0)
                        end
                    end
                elseif not st or st < 2 or st > 5 then S.bd[h] = nil end
            end
        end
        -- The other copy's body placement (flag 8, world_place_guest.lua P.sample): this copy's body (lying here too)
        -- more than 1 m off goes there through the same FIFO and native placement (kind 3; P.check lets a dead one go).
        function P.body(c, en)
            if not W.bodyOn then return end
            local st = P.state(en)
            local x, _, z = pos(en.id)
            if not (st and st >= 2 and st <= 5 and x) then return end
            local d = flat(x, z, c.x, c.z)
            local fix = d > 1000 and #P.q < 16 and not (P.cur and P.cur.h == c.h)
            L("event=place_body;seq=%d;ent=%.0f;div_m=%.1f;state=%d;fix=%d", c.seq, c.h, d / 1000, st, fix and 1 or 0)
            if fix then
                c.body = true
                P.q[#P.q + 1] = c
            end
            if S.bd[c.h] then S.bd[c.h].restX, S.bd[c.h].restZ = c.x, c.z end
        end
        -- M6 hold-ups: a hold-up applied here (W1 state 1) also raises this copy's hands (SetForceHoldup, handler 0x135D0B2).
        local applyOne = W.enemy.applyOne
        W.enemy.applyOne = function(h, st, ver, src, now, x, z)
            local r = applyOne(h, st, ver, src, now, x, z)
            local en = W.ent[h]
            if W.bodyOn and st == 1 and r ~= "defer" and r ~= "held" and en and type(en.id) == "number" and P.state(en) == 1 and
                not (S.hu and S.hu[h]) then
                S.hu = S.hu or {}
                S.hu[h] = true
                local ok = pcall(W.api.send, en.id, {id = "SetForceHoldup"})
                L("event=holdup_show;ent=%.0f;ok=%d", h, ok and 1 or 0)
            elseif st ~= 1 and S.hu then S.hu[h] = nil end
            return r
        end
        -- M5 routes, the host: 6 routes per update; a guard's route changing while he is near the guest goes out.
        function S.routes(now)
            local rt = S.rt
            if not rt.names then
                rt.names = {}
                if P.route then P.route(0) end
                for _, name in pairs(P.routes or {}) do rt.names[#rt.names + 1] = name end
                table.sort(rt.names)
            end
            local n = #rt.names
            if n == 0 then return end
            local qx, qz = P.partnerXZ(now)
            for _ = 1, min(6, n) do
                rt.i = rt.i % n + 1
                local name = rt.names[rt.i]
                local ok, ids = pcall(W.api.send, {type = "TppSoldier2"}, {id = "GetGameObjectIdUsingRoute", route = name})
                if ok and type(ids) == "number" then ids = {ids} end
                if ok and type(ids) == "table" then
                    rt.reads = (rt.reads or 0) + 1
                    for _, id in ipairs(ids) do
                        local h = W.byId and W.byId[id]
                        local en = h and W.ent[h]
                        if en and rt.cur[h] ~= name then
                            rt.cur[h] = name
                            local x, _, z = pos(en.id)
                            if qx and x and flat(x, z, qx, qz) <= 60000 and S.mine(h) and P.state(en) == 0 and
                                P.announce(2, h, 0, 0, 0, 0, 16, W.h(name)) then
                                rt.sent = (rt.sent or 0) + 1
                                L("event=route_tx;ent=%.0f;route=%s", h, name)
                            end
                        end
                    end
                end
            end
        end
        -- M5 routes, the guest: the host copy's route (kind 2 flag 16, world_place_guest.lua) for this copy.
        function P.routeSync(c)
            if W.host or not W.routeOn then return end
            local en, name = W.ent[c.h], P.route and P.route(c.route)
            if not (en and type(en.id) == "number" and name) then return end
            S.rt.want[c.h] = name
            S.routeApply(c.h, en, name, "rx")
        end
        function S.routeApply(h, en, name, why)
            if P.state(en) ~= 0 then return end
            local okOn, on = pcall(W.api.send, en.id, {id = "IsOnRoute", route = name})
            if okOn and on == true and why == "rx" then return end
            local ok = pcall(W.api.send, en.id, {id = "SetSneakRoute", route = name})
            S.rt.applied = (S.rt.applied or 0) + 1
            L("event=route_apply;ent=%.0f;route=%s;why=%s;on=%s;ok=%d", h, name, why, tostring(okOn and on), ok and 1 or 0)
        end
        -- A correction or drive landing re-joins the host copy's route where the copy now stands.
        local done = P.done
        P.done = function(c, why)
            done(c, why)
            local name = not why and c.kind == 3 and not c.host and W.routeOn and S.rt.want[c.h]
            local en = name and W.ent[c.h]
            if en and type(en.id) == "number" then S.routeApply(c.h, en, name, "landed") end
        end
        -- M7 brain ------------------------------------------------------------------------------------------------------
        S.bn = {auth = {}, gate = {}, i = 0, n = {set = 0, lift = 0, engage = 0}}
        W.startHooks[#W.startHooks + 1] = function() S.bn = {auth = {}, gate = {}, i = 0, n = {set = 0, lift = 0, engage = 0}} end
        local function auth(text)
            local ok, r = pcall(W.receipt, "return 'MGS_COOP_SOLDIER_AUTH_V1' -- " .. text)
            return ok and r or nil
        end
        -- The other copy is this guard's brain: on the host a guard the guest owns (M3), on the guest every guard but those.
        function S.remote(h)
            if W.host then return W.ownerOn == true and P.own ~= nil and P.own[h] ~= nil and P.own[h].o == 1 end
            return not (W.ownerOn == true and P.owned ~= nil and P.owned[h] == true)
        end
        local function debugIgnored(en)
            local m = W.get(W.get(e, "MgsCoopDebug"), "enemy")
            return type(m) == "table" and type(m.ignored) == "table" and m.ignored[en.name] == true
        end
        local function setRestrict(h, en, g, want, why)
            pcall(W.api.send, en.id, {id = "SetPuppet", enabled = want})
            g.restricted = want
            S.bn.n[want and "set" or "lift"] = S.bn.n[want and "set" or "lift"] + 1
            L("event=brain_gate;ent=%.0f;puppet=%d;why=%s", h, want and 1 or 0, why)
        end
        -- This player damaged a guard: that copy is free for 8 s (it reacts; its CP raises the alarm).
        if type(W.observers) == "table" then
            W.observers[#W.observers + 1] = function(kind, h, cls)
                if not (W.brainOn and kind == "Damage" and cls == "local" and h) then return end
                local g = S.bn.gate[h]
                if not g then g = {}; S.bn.gate[h] = g end
                g.free = W.now() + 8000
                local en = W.ent[h]
                if g.restricted and en and type(en.id) == "number" then setRestrict(h, en, g, false, "damaged") end
            end
        end
        -- SOLDIER_HIT from the owner's copy of guard `hash` (world_friendly.lua): this copy engages this player.
        function W.soldierEngaged(hash, now)
            if not W.brainOn then return end
            local en = W.ent[hash]
            local g = S.bn.gate[hash]
            if not g then g = {}; S.bn.gate[hash] = g end
            local first = not (g.until_ and now < g.until_)
            g.until_ = now + 8000
            if first then S.bn.n.engage = S.bn.n.engage + 1 end
            if en and type(en.id) == "number" and P.state(en) == 0 and not debugIgnored(en) then
                if g.restricted then setRestrict(hash, en, g, false, "engaged") end
                if first then pcall(W.api.send, en.id, {id = "SetDiscoveryPlayer"}) end
            end
        end
        -- Every restrict notice this module set is lifted (brain off, unpaired, mission end).
        function S.brainRelease(why)
            for h, g in pairs(S.bn.gate) do
                local en = W.ent[h]
                if g.restricted and en and type(en.id) == "number" and not debugIgnored(en) then setRestrict(h, en, g, false, why) end
            end
            S.bn.gate = {}
        end
        function S.brainTick(now)
            if not S.bn.cleared then
                if auth("clear") ~= nil then S.bn.cleared = true end
            end
            local list = W.entOrder
            local n = type(list) == "table" and #list or 0
            for _ = 1, min(8, n) do
                S.bn.i = S.bn.i % n + 1
                local h = list[S.bn.i]
                local en = W.ent[h]
                if en and type(en.id) == "number" then
                    local remote = S.remote(h)
                    if S.bn.auth[h] ~= remote and auth(fmt("%d %d", en.id % 512, remote and 1 or 0)) == 1 then S.bn.auth[h] = remote end
                    local g = S.bn.gate[h]
                    if not g then g = {}; S.bn.gate[h] = g end
                    local engaged = g.until_ ~= nil and now < g.until_
                    local cp = W.cp and W.cp[en.cp]
                    local alert = cp ~= nil and cp.localPhase == 3
                    local free = engaged or alert or (g.free ~= nil and now < g.free)
                    if P.state(en) == 0 and not debugIgnored(en) then
                        local want = remote and not free
                        if want ~= (g.restricted == true) then
                            setRestrict(h, en, g, want, want and "remote" or
                                (not remote and "owned" or engaged and "engaged" or alert and "alert" or "damaged"))
                        end
                    elseif g.restricted and P.state(en) ~= 0 then
                        g.restricted = nil  -- a downed guard: the puppet flag no longer matters; set again when NORMAL
                    end
                end
            end
            if not S.bn.statAt or now - S.bn.statAt >= 10000 then
                S.bn.statAt = now
                local ok, en, rc = pcall(W.receipt, "return 'MGS_COOP_SOLDIER_AUTH_V1' -- status")
                local restricted, engaged = 0, 0
                for _, g in pairs(S.bn.gate) do
                    if g.restricted then restricted = restricted + 1 end
                    if g.until_ and now < g.until_ then engaged = engaged + 1 end
                end
                L("event=brain_stats;native=%s;remote=%s;restricted=%d;engaged=%d;set=%d;lift=%d;engages=%d", ok and tostring(en) or "?",
                    ok and tostring(rc) or "?", restricted, engaged, S.bn.n.set, S.bn.n.lift, S.bn.n.engage)
            end
        end
        -- M7 step 1: per-soldier perception targets (host) ---------------------------------------------------------
        S.pc = {t = {}, at = {}, sent = 0}
        W.startHooks[#W.startHooks + 1] = function() S.pc = {t = {}, at = {}, sent = 0} end
        local function perceive(text)
            local ok, r = pcall(W.receipt, "return 'MGS_COOP_SOLDIER_PERCEIVE_V1' -- " .. text)
            return ok and r or nil
        end
        -- From P.scan (world_place.lua): dh / dq = squared mm to the host's player / the guest's (nil when unknown).
        function S.pick(cur, dh, dq, heldMs)
            local want = cur or 0
            if not dq then want = 0
            else
                local gap = sqrt(dh) - sqrt(dq)
                if gap >= 3000 then want = 1 elseif gap <= -3000 then want = 0 end
            end
            if want == cur or (cur ~= nil and heldMs < 4000) then return cur end
            return want
        end
        function P.perceiveScan(h, en, dh, dq, now)
            if not (W.perceiveOn and W.host and W.paired and type(en.id) == "number") then return end
            local c = S.pc
            if not c.cleared then
                if perceive("clear") == nil then return end
                c.cleared = true
            end
            local cur = c.t[h]
            local want = S.pick(cur, dh, dq, now - (c.at[h] or -1e9))
            if want ~= cur and perceive(fmt("%d %d", en.id % 512, want)) == 1 then
                c.t[h], c.at[h], c.sent = want, now, c.sent + 1
                L("event=perceive;ent=%.0f;idx=%d;target=%d;dh_m=%.0f;dq_m=%s", h, en.id % 512, want, sqrt(dh) / 1000,
                    dq and fmt("%.0f", sqrt(dq) / 1000) or "na")
            end
            if not c.statAt or now - c.statAt >= 10000 then
                c.statAt = now
                local ok, on, t0, t1, auto = pcall(W.receipt, "return 'MGS_COOP_SOLDIER_PERCEIVE_V1' -- status")
                L("event=perceive_stats;native=%s;t0=%s;t1=%s;auto=%s;sent=%d", ok and tostring(on) or "?", ok and tostring(t0) or "?",
                    ok and tostring(t1) or "?", ok and tostring(auto) or "?", c.sent)
            end
        end
        local tick = P.tick
        P.tick = function(now)
            tick(now)
            if W.brainOn and W.followOn == true and W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true then
                S.brainTick(now)
            elseif not W.brainOn and next(S.bn.gate) ~= nil then
                S.brainRelease("brain_off")
            end
            if W.routeOn and W.host and W.followOn == true and W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true then
                S.routes(now)
            end
            if W.bodyOn and W.followOn == true and W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true then S.bodies(now) end
            if not S.ready() then return end
            S.produce(now)
            S.drive(now)
            if S.pp then S.puppetsOff(now) end
            if not S.statAt or now - S.statAt >= 10000 then
                S.statAt = now
                local c = S.n
                L("event=snap_stats;tx=%d;calls=%d;steps=%d;hard=%d;rest=%d;stale=%d;refused=%d;sink=%d", c.tx, c.calls, c.steps,
                    c.hard, c.rest, c.stale, c.refused, c.sink)
            end
        end
    end
end
