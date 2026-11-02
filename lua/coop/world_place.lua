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
        local fmt, floor, sqrt, max = string.format, math.floor, math.sqrt, math.max
        P.KIND = {"warp", "route", "correct", "come", "ignore", "notice", "disable", "enable"}
        P.WHY = {"not_ready", "roster", "unknown_entity", "unmapped", "terminal_local", "vehicle_local", "disabled_local",
            "native_off", "grant", "pose", "range", "stance", "realize_timeout", "position_timeout", "route_unknown", "context",
            "native_other", "unreadable_local", "busy", "far", "latched", "near_player"}
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
            P.patrol, P.lastS = {rr = 0, s = {}, combat = {}, back = {}, skip = {}, scan = {}}, {}
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
            -- Corrections are not kept: the guest answers only the ones it applies (an unknown seq counts as kind 3).
            P.seq, P.out[seq] = seq, kind ~= 3 and {h = h, kind = kind} or nil
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
                            local near = dh <= K.host or (dq ~= nil and dq <= K.partner)
                            -- Follow: at least 15 m nearer the partner than the host is the guest's copy's to move.
                            local side = near and follow and not staff and dq ~= nil and sqrt(dh) - sqrt(dq) >= 15000
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
                                    if P.announce(3, h, x, y - 800, z, 0, flags, 0) then sent = sent + 1 end
                                else
                                    ps.sampleAt, ps.y, ps.every = now, y - 800, every
                                    local moved = flat(x, z, ps.x, ps.z)
                                    local combatNew = alert and not ps.alert
                                    if moved >= K.moved or now - ps.sent >= K.every or combatNew then
                                        local yaw = moved >= 300 and floor(math.atan2(x - ps.x, z - ps.z) * 1000 + 0.5) or ps.yaw
                                        if P.announce(3, h, x, y - 800, z, yaw, flags, 0) then
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
            y = ps.y
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
            -- that guard waits 10 s.
            if kind == 3 and not ok and P.patrol and (why == "native_off" or why == "far" or why == "latched") then
                P.patrol.back[v[2]] = W.now() + 10000
            end
            if kind ~= 3 then P.lastAck = text; P.remote(ok, text) end
        end
        -- A re-pair in the same epoch re-sends the tracked guards' positions; any pairing re-reads every guard near either
        -- player (a rejoining guest's copies are placed within a few seconds).
        W.pairedHooks[#W.pairedHooks + 1] = function(epochChanged)
            if W.host and not epochChanged then for _, l in pairs(P.live) do l.force, l.next = true, 0 end end
            if W.host and P.patrol then P.patrol.s, P.patrol.back, P.patrol.skip = {}, {}, {} end
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
