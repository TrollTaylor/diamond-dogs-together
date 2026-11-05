-- MGS co-op world sync, spawn sync (original; docs/debug-menu.md "Spawn sync"):
-- the host announces each F9 guard placement and per-soldier debug action as
-- ENEMY_DEBUG 34 for the same authored soldier (StrCode32 key, matching roster);
-- the guest applies it to its copy (world_place_guest.lua) and answers
-- ENEMY_DEBUG_ACK 35. Kinds: 1 warp 2 route 3 correct 4 come 5 ignore 6 notice
-- 7 disable 8 enable; flags 1 come, 2 restrict notice, 4 first (place first).
-- Needs pairing (eff >= 2) and a matching roster, not W1. Placed guards stay in
-- an active area on both PCs (SetOutOfArea false, re-applied after every area
-- update). Enemy follow (W.followOn, CFG r[19], docs/world-sync-design.md 7.8):
-- the host shares where the guards near either player stand (kind 3) in every
-- W1 mission, and where a guard stands right before its own outcome of him.
do
    local W = e.MgsCoopWorld
    if type(W) == "table" and type(W.enemy) == "table" and type(W.area) == "table" then
        local P, OP, H, E, A = {}, W.OP, W.handlers, W.enemy, W.area
        W.place = P
        OP.ENEMY_DEBUG, OP.ENEMY_DEBUG_ACK = 34, 35
        -- Enemy sync M3 (W.ownerOn, docs/world-sync-design.md 7.9): ENEMY_PLACE 71 (guest -> host: a guard the guest owns,
        -- where its copy stands), ENEMY_PLACE_ACK 72, OWNER 73 (host -> guest), ENGAGE 74 (guest -> host).
        OP.ENEMY_PLACE, OP.ENEMY_PLACE_ACK, OP.OWNER, OP.ENGAGE = 71, 72, 73, 74
        local fmt, floor, sqrt, max = string.format, math.floor, math.sqrt, math.max
        P.KIND = {"warp", "route", "correct", "come", "ignore", "notice", "disable", "enable"}
        -- A scan or place-first correction puts the copy's feet this much (mm) over the host copy's (GetPosition y - 800 mm):
        -- placed feet on feet, a copy now and then landed under the ground and fell through the world, its fall speed kept
        -- through later corrections (runs 51f8e90f, 6e22bed0, cb8ae74f: landed 0.0 m over the request instead of 0.8 m,
        -- then ~20 m lower 2 s later, up to 164 m). It drops the 0.4 m and stands where the host's copy does.
        P.LIFT = 400
        P.WHY = {"not_ready", "roster", "unknown_entity", "unmapped", "terminal_local", "vehicle_local", "disabled_local",
            "native_off", "grant", "pose", "range", "stance", "realize_timeout", "position_timeout", "route_unknown", "context",
            "native_other", "unreadable_local", "busy", "far", "latched", "near_player", "unrealized"}
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
            -- Enemy gaps (place_sink; lab key place_lift, CFG r[19] bit 128): the owner's own GetPosition y (LIFT 800, no
            -- 0.4 m under the owner's surface: the 1-2.5 % of copies the engine left there fell a storey, runs 2d967b8a,
            -- d4460ea5, 25098847).
            P.LIFT = W.liftOn == true and 800 or 400
            P.seq, P.out, P.live, P.q, P.cur, P.pin, P.come, P.div, P.last, P.lastAck, P.routes =
                0, {}, {}, {}, nil, {}, {}, {}, nil, nil, nil
            P.patrol, P.lastS, P.sink = {rr = 0, s = {}, combat = {}, back = {}, skip = {}, scan = {}}, {}, {}
            -- M3: the host's owner decisions and both players' engagements (ms), the guest's owned guards and samples.
            P.own, P.engG, P.engH, P.owned, P.ownS = {}, {}, {}, {}, {}
            P.rz, P.rzN = {}, {landed = 0, timeout = 0}  -- place_reach: the force-realize holds, forgotten at a start
            P.n = {sent = 0, acked = 0, corrected = 0, refused = 0, applied = 0, failed = 0}
            W.pinned, W.rosterMatch = P.pin, false
        end
        P.reset()
        W.startHooks[#W.startHooks + 1] = P.reset
        if type(W.observers) == "table" then
            W.observers[#W.observers + 1] = function(kind, h, cls)
                if kind == "Damage" and P.patrol then P.patrol.combat[h] = W.now() end
                -- M3: this player's own hit on a guard is an engagement (the host's decides ownership; the guest's is sent).
                -- (ENGAGE is not sent: run 4ad309a3's Damage messages named the local player for guards nobody shot.)
                if kind == "Damage" and cls == "local" and W.ownerOn and P.engH and W.host then P.engH[h] = W.now() end
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
        -- place_reach (bit 2048, W.reachOn; design 7.17 / 7.18): a placement or drive step relayed or refused in the backoff
        -- force-realizes the guard (<= RZ_MAX, never staff) and retries at the backoff end (native b); released when he
        -- lands (placement or drive step), at RZ_MS or at stop.
        -- body_dead (bit 1024, W.deadOn): after a body placement the resting spot (world_snap.lua S.bd) is where this
        -- copy's body lies, moved or not, so it is never reported back (the M6 ping-pong).
        P.RZ_MS, P.RZ_MAX = 8000, 4
        local function unhold(h, why)
            local r, N = P.rz[h], P.rzN
            if not r then return end
            P.rz[h] = nil
            P.release({forced = true, en = r})
            N[why] = (N[why] or 0) + 1
            L("event=realize_hold;ent=%.0f;on=0;why=%s;ms=%d;src=%s;st=%d;rs=%d;rf=%d;rq=%d;landed=%d;timeout=%d", h, why,
                floor(W.now() - r.at), r.src, r.st, r.rs, r.rf, r.rq, N.landed, N.timeout)
        end
        P.unhold = unhold
        function P.doneHook(c, why)
            local S = c.body and W.deadOn and W.snap
            local b = S and S.bd and S.bd[c.h]
            if b then
                local x, y, z
                if c.en then x, y, z = pos(c.en.id) end
                if x then b.restX, b.restZ = x, z end
                b.pend = nil
            end
            if not why then return unhold(c.h, "landed") end
            if why == "unrealized" and c.kind == 3 and c.en then P.hold(c.h, c.en.id, c.native, c.back, c) end
        end
        function P.hold(h, id, why, b, c)
            if not W.reachOn or type(id) ~= "number" or (W.feature and W.feature.staffPlace) then return end
            local r, now = P.rz[h], W.now()
            if not r then
                local n = 0
                for _ in pairs(P.rz) do n = n + 1 end
                if n >= P.RZ_MAX then return end
                local ok = pcall(W.api.send, id, {id = "SetForceRealize"})
                r = {id = id, at = now, src = c and "place" or "drive", st = 0, rs = 0, rf = 0, rq = 0}
                P.rz[h] = r
                L("event=realize_hold;ent=%.0f;on=1;ok=%d;n=%d;why=%s;b=%d", h, ok and 1 or 0, n + 1, tostring(why), b or -1)
            end
            r.rf, r.retry, r.c = r.rf + 1, now + (tonumber(b) or 3000) + 50, c or r.c
            return r
        end
        -- S.drive's side (world_snap.lua): st / rs count a held guard's landed / resting steps.
        function P.hdr(h, id, r, dv)
            if not W.reachOn or type(r) ~= "table" then return end
            if r.refused == "relayed" or r.refused == "backoff" then
                local z = P.hold(h, id, r.refused, r.b)
                if z then dv.back = z.retry end
                return
            end
            local z = P.rz[h]
            if not z or r.refused then return end
            if r.h == 1 and (r.s or 0) >= 1 then z.st = z.st + 1; return unhold(h, "landed") end
            z.rs = z.rs + 1
        end
        -- S.produce's reach: d (mm, to the other player) <= 60 m, with place_reach <= 160 m if he is within 60 m of ours.
        function P.reach(d, x, z)
            if d <= 60000 then return true end
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            return W.reachOn == true and d <= 160000 and type(px) == "number" and type(pz) == "number" and
                flat(x, z, px * 1000, pz * 1000) <= 60000
        end
        function P.rzTick(now)
            for h, r in pairs(P.rz) do
                if now - r.at >= P.RZ_MS then unhold(h, "timeout")
                elseif r.c and now >= r.retry then
                    local c, s, busy = r.c, P.lastS[h], P.cur and P.cur.h == h or W.snapDriving and W.snapDriving(h, now)
                    for _, q in ipairs(P.q) do busy = busy or q.h == h end
                    if s and s.used and s.kind == 3 and s.at > c.at then c = s end
                    r.c = nil
                    if not busy then
                        c.landed, c.native, c.back, c.dy, c.pos = nil, nil, nil, nil, nil
                        r.rq = r.rq + 1
                        P.q[#P.q + 1] = c
                    end
                end
            end
        end
        -- Stop (OnTerminate, LEVEL 0): release a pending force-realize, then forget. W1 off
        -- (full=false) keeps everything: spawn sync does not depend on outcome sync.
        W.stopHooks[#W.stopHooks + 1] = function(full)
            if not full then return end
            for h in pairs(P.rz) do unhold(h, "stop") end
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
            -- Corrections are not kept: the guest answers only the ones it applies (an unknown seq counts as kind 3).
            P.seq, P.out[seq] = seq, kind ~= 3 and flags < 16 and {h = h, kind = kind} or nil
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
            -- Without follow, play shares only Mother Base staff: its guest refuses free roam corrections (lab: the lab key).
            if W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true and P.patrol and
                not (W.feature and W.feature.patrol == false) and
                (W.followOn == true or (W.feature and W.feature.staffPlace) or W.mode == 1) then
                P.scan(now)
            end
        end
        -- The patrol scan (docs/world-sync-design.md 7.7, 7.8): where the NORMAL guards of the active CPs stand (kind 3, y
        -- minus the 0.8 m body offset). Enemy follow (W.followOn): 55 m around the host or 60 m around the partner body
        -- (TppPlayer2 index 1), 4 reads and 16 scans per update, a guard read every 500 ms within 40 m of either player
        -- (1 s beyond, 2 s out of range), sent when it moved 1 m or every 2 s; a guard at least 15 m nearer the partner
        -- than the host is left to the guest's copy (two-way follow is later work); one whose correction the guest
        -- refused (native_off, far, latched) waits 10 s. Without follow (0.14.7): 55 m around the host (either player for
        -- Mother Base staff), 2 reads and 8 scans, sent when it moved 1.5 m or every 3 s. Never a combat flag on staff.
        local FOLLOW = {reads = 4, scans = 16, moved = 1000, every = 2000, host = 3025000000, partner = 3600000000, near = 1600000000}
        local OLD = {reads = 2, scans = 8, moved = 1500, every = 3000, host = 3025000000, partner = 3025000000}
        local clock = W.get(os, "clock")
        local function cpu() if type(clock) == "function" then local ok, v = pcall(clock); return ok and type(v) == "number" and v or 0 end return 0 end
        function P.scan(now)
            local list = W.entOrder
            local n = type(list) == "table" and #list or 0
            if n == 0 then return end
            local follow, staff = W.followOn == true, W.feature and W.feature.staffPlace
            local K = follow and FOLLOW or OLD
            local t0 = cpu()
            local hx, hz = (W.get(vars, "playerPosX") or 0) * 1000, (W.get(vars, "playerPosZ") or 0) * 1000
            local pat, scans, reads, sent = P.patrol, 0, 0, 0
            local qx, qz = nil, nil
            if follow or staff then qx, qz = P.partnerXZ(now) end
            while scans < K.scans and reads < K.reads and scans < n do
                pat.rr, scans = pat.rr % n + 1, scans + 1
                local h = list[pat.rr]
                local en = W.ent[h]
                local ps = pat.s[h]
                if en and not P.live[h] and E.active(en.cp, h) and not (pat.back[h] and now < pat.back[h]) and
                    not (pat.skip[h] and now < pat.skip[h]) and (not ps or now - ps.sampleAt >= (ps.every or 500)) then
                    reads = reads + 1
                    -- A guard whose Fulton message came in the last 30 s, or one rising more than 3 m above his last
                    -- standing sample faster than 2 m/s, is going up under a balloon (run 3e01f334: the debug Fulton's
                    -- message came 4.7 s into the lift, the rising NORMAL guard was sampled at 446 m and his place-first
                    -- sent that height): never sampled, his standing sample kept for place-first.
                    local fm = E.s and E.s.fmsg and E.s.fmsg[h]
                    local lifting = fm and now - fm < 30000
                    if P.state(en) == 0 and not lifting then
                        if ps and ps.down then ps = nil; pat.s[h] = nil end
                        local x, y, z = pos(en.id)
                        if (staff or follow) and x == 0 and z == 0 then x = nil end  -- no position: never shared
                        if x and ps and follow then
                            local dy = y - 800 - ps.y
                            if dy > 3000 and dy > 2 * (now - ps.sampleAt) then x = nil end
                        end
                        if x then
                            local dh = (x - hx) ^ 2 + (z - hz) ^ 2
                            local dq = qx and (x - qx) ^ 2 + (z - qz) ^ 2 or nil
                            if P.perceiveScan then P.perceiveScan(h, en, dh, dq, now) end  -- M7 step 1 (world_snap.lua)
                            local near = dh <= K.host or (dq ~= nil and dq <= K.partner)
                            -- Follow: at least 15 m nearer the partner than the host is the guest's copy's to move; M3
                            -- (W.ownerOn): the owner decision (P.ownerOf), the guest's ENEMY_PLACE then moves this copy.
                            local side
                            if near and follow and not staff and W.ownerOn then side = P.ownerOf(h, dh, dq, now) == 1
                            else side = near and follow and not staff and dq ~= nil and sqrt(dh) - sqrt(dq) >= 15000 end
                            if side then pat.scan.side = (pat.scan.side or 0) + 1 end
                            if near and not side then
                                local cp = W.cp and W.cp[en.cp]
                                local alert = not staff and ((cp and cp.localPhase == 3) or
                                    (pat.combat[h] and now - pat.combat[h] <= 5000)) or false
                                local flags = alert and 1 or 0
                                local every = follow and not (dh <= K.near or (dq ~= nil and dq <= K.near)) and 1000 or 500
                                if not ps then
                                    ps = {x = x, y = y - 800, z = z, yaw = 0, sent = now, sampleAt = now, alert = alert, every = every}
                                    pat.s[h] = ps
                                    if P.announce(3, h, x, y - 800 + P.LIFT, z, 0, flags, 0) then sent = sent + 1 end
                                else
                                    ps.sampleAt, ps.y, ps.every = now, y - 800, every
                                    local moved = flat(x, z, ps.x, ps.z)
                                    local combatNew = alert and not ps.alert
                                    if moved >= K.moved or now - ps.sent >= K.every or combatNew then
                                        local yaw = moved >= 300 and floor(math.atan2(x - ps.x, z - ps.z) * 1000 + 0.5) or ps.yaw
                                        if P.announce(3, h, x, y - 800 + P.LIFT, z, yaw, flags, 0) then
                                            ps.x, ps.z, ps.yaw, ps.sent, ps.alert = x, z, yaw, now, alert
                                            sent = sent + 1
                                        end
                                    end
                                end
                            elseif follow then
                                pat.skip[h] = now + (side and 1000 or 2000)  -- out of range or the guest's: read again later
                            end
                        end
                    elseif (staff or follow) and ps then
                        ps.down = true  -- kept for place-first: the standing ground y (a lifting guard too)
                    else
                        pat.s[h] = nil
                    end
                end
            end
            -- The scan cost and volume (follow): one line every 10 s.
            if follow then
                local sc = pat.scan
                local us = (cpu() - t0) * 1000000
                sc.at, sc.ups, sc.reads, sc.sent = sc.at or now, (sc.ups or 0) + 1, (sc.reads or 0) + reads, (sc.sent or 0) + sent
                sc.us, sc.max = (sc.us or 0) + us, max(sc.max or 0, us)
                if now - sc.at >= 10000 then
                    L("event=place_scan;ups=%d;reads=%d;sent=%d;side=%d;avg_us=%.0f;max_us=%.0f;partner=%d", sc.ups, sc.reads, sc.sent,
                        sc.side or 0, sc.us / sc.ups, sc.max, qx and 1 or 0)
                    pat.scan = {at = now}
                end
            end
        end
        -- The partner's position (mm) as this copy sees it, read at most every 500 ms; nil when unknown. The native PEER
        -- receipt (the remote pose; run f2d2db9b: GetPosition on the partner body, TppPlayer2 index 1, answered nothing,
        -- so the follow scan never saw the partner), else the partner body (an older loader).
        -- M3 ownership (W.ownerOn, docs/world-sync-design.md 7.9): the guest owns a guard at least 15 m nearer its player
        -- than the host's; he comes back to the host once he is less than 5 m nearer the guest's player, or within 20 m of
        -- the host's (a hysteresis band); a decision holds 10 s. Engagement is not used: run 4ad309a3's Damage messages
        -- named the local player as the attacker for guards neither player shot (81 ENGAGE from the guest, the host's own
        -- "hits" at 70 m: 102 owner flips within 10 s). OWNER 73 tells the guest each change (owner 0 host, 1 guest; why
        -- 1 near, 3 host_near, 0 none).
        local OWHY = {[0] = "none", "near", "engage", "host_near"}
        function P.ownerOf(h, dh, dq, now)
            local o = P.own[h]
            if not o then o = {o = 0, at = -1e9}; P.own[h] = o end
            local gap = dq ~= nil and sqrt(dh) - sqrt(dq) or nil
            local hostNear = dh <= 400000000
            local want, why = o.o, 0
            if hostNear then want, why = 0, 3
            elseif gap and gap >= 15000 then want, why = 1, 1
            elseif not gap or gap < 5000 then want, why = 0, 0 end
            if want ~= o.o and now - o.at >= 10000 and W.tx(OP.OWNER, h, want, why) then
                o.o, o.at = want, now
                L("event=owner;ent=%.0f;owner=%s;why=%s;dh_m=%.0f;dq_m=%s", h, want == 1 and "guest" or "host", OWHY[why],
                    sqrt(dh) / 1000, dq and fmt("%.0f", sqrt(dq) / 1000) or "na")
            end
            return o.o
        end
        -- ENGAGE stays on the wire and in the log (event=engage_rx); ownership no longer reads it (see P.ownerOf).
        H[OP.ENGAGE] = function(v, now)
            if W.host and W.ownerOn and P.engG then
                P.engG[v[1]] = now or W.now()
                L("event=engage_rx;ent=%.0f", v[1])
            end
        end
        -- The guest's samples of the guards it owns move this host's copies (world_place_guest.lua P.sample, c.host).
        H[OP.ENEMY_PLACE] = function(v, now)
            if not (W.host and W.ownerOn and W.w1 and P.sample) then return end
            P.sample({seq = v[1], h = v[2], kind = 3, x = v[4], y = v[5], z = v[6], yaw = v[7], flags = v[8], route = 0, go = v[10],
                at = now or W.now(), host = true})
        end
        function P.partnerXZ(now)
            local pat = P.patrol
            if now - (pat.pAt or -1e9) >= 500 then
                pat.pAt, pat.px, pat.pz = now, nil, nil
                local r = {W.receipt("return 'MGS_COOP_WORLD_PEER_V1'")}
                if r[1] == 1 and type(r[2]) == "number" and type(r[3]) == "number" then
                    pat.px, pat.pz, pat.src = r[2], r[3], "peer"
                else
                    local by = W.get(GameObject, "GetGameObjectIdByIndex")
                    local ok, id = false, nil
                    if type(by) == "function" then ok, id = pcall(by, "TppPlayer2", 1) end
                    if ok and type(id) == "number" and id ~= W.api.null then
                        local x, _, z = pos(id)
                        pat.px, pat.pz, pat.src = x, z, "body"
                    end
                end
                if (pat.px ~= nil) ~= pat.logged then
                    pat.logged = pat.px ~= nil
                    L("event=place_partner;known=%d;src=%s", pat.px and 1 or 0, pat.px and pat.src or "none")
                end
            end
            return pat.px, pat.pz
        end
        -- Place first (Mother Base staff, staffPlace; every W1 guard with enemy follow): right before the host's own outcome
        -- of a standing guard, where he is (kind 3, flag 4 first), so the guest places its copy there before applying it
        -- (world_place_guest.lua W.placeHold): x, z now, the ground y of his last standing sample (a lying body reads
        -- lower); the sampled x, z when the body rose (a Fulton lift). Never sampled standing (far from both players, or
        -- the guest's to move): nothing.
        function W.placeAnnounce(h)
            if not (W.host and P.patrol and ((W.feature and W.feature.staffPlace) or W.followOn == true)) then return end
            local en, ps = W.ent[h], P.patrol.s[h]
            if not (en and ps and ps.y) then return end
            local x, y, z = pos(en.id)
            if not x then return end
            if math.abs(y - 800 - ps.y) > 2000 then x, z = ps.x, ps.z end
            y = ps.y + P.LIFT
            local seq = P.announce(3, h, x, y, z, ps and ps.yaw or 0, 4, 0)
            L("event=place_first;step=tx;ent=%.0f;seq=%d;x=%d;y=%d;z=%d", h, seq or 0, x, y, z)
        end
        H[OP.ENEMY_DEBUG_ACK] = function(v)
            if not W.host then return end
            local o, ok = P.out[v[1]], v[3] == 0
            P.out[v[1]] = nil
            local kind = o and o.kind or 3
            if not ok then P.n.refused = P.n.refused + 1
            elseif kind == 3 then P.n.corrected = P.n.corrected + 1
            else P.n.acked = P.n.acked + 1 end
            local why = WHY[v[4]] or "none"
            local text = fmt("guest_spawn;seq=%d;ent=%.0f;kind=%s;result=%s;why=%s;err_m=%.2f;frames=%d", v[1], v[2],
                KIND[kind] or "unknown", ok and "applied" or "refused", why, v[5] / 100, v[6])
            L("event=place_ack;%s", string.sub(text, 13))
            -- A correction the guest's copy cannot take (its native placement off, the target too far, that guard latched):
            -- that guard waits 10 s; not realized on that copy (unrealized: the native relay, enemy latch) 3 s.
            if kind == 3 and not ok and P.patrol and (why == "native_off" or why == "far" or why == "latched") then
                P.patrol.back[v[2]] = W.now() + 10000
            elseif kind == 3 and not ok and P.patrol and why == "unrealized" then P.patrol.back[v[2]] = W.now() + 3000 end
            if kind ~= 3 then P.lastAck = text; P.remote(ok, text) end
        end
        -- A re-pair in the same epoch re-sends the tracked guards' positions; any pairing re-reads every guard near either
        -- player (a rejoining guest's copies are placed within a few seconds).
        W.pairedHooks[#W.pairedHooks + 1] = function(epochChanged)
            if W.host and not epochChanged then for _, l in pairs(P.live) do l.force, l.next = true, 0 end end
            if W.host and P.patrol then P.patrol.s, P.patrol.back, P.patrol.skip = {}, {}, {} end
        end
        function P.tick(now)
            if next(P.rz) then P.rzTick(now) end  -- enemy gaps 2 (place_reach)
            if W.host then
                P.hostTick(now)
                -- M3: the host's own placements of guest-owned guards (the same FIFO and steps as the guest's).
                if W.ownerOn and (P.cur or #P.q > 0) and P.guestTick then P.guestTick(now)
                elseif W.followOn == true and P.sinkTick then P.sinkTick(now) end  -- place_sink: every tick, not only queued
            elseif (W.paired or P.cur) and P.guestTick then P.guestTick(now) end
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
