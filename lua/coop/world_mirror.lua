-- MGS co-op world sync, enemy sync M7 FM1, the far mirror (original; docs/world-sync-design.md 7.15, protocol section 33).
-- Lab key soldier_act (CFG r[19] bit 512, W.actOn), only with follow; 30010 / 30020 / 30050. Spliced after world_snap.lua.
-- While the guest's player is beyond R of a host-owned guard, the guest's copy of him is a mirror puppet (SetPuppet and
-- restrict notice: his own brain is off) that replays the host copy's own request and order (SOLDIER_ACT 0x58, native
-- soldier_act.hpp) with his own animations: walk, run, turn, aim, fire (his bullets and noise are dropped: the host copy's
-- shots are the real ones). R per phase (M.R) is 1.5 x the farthest notice / engagement distance measured at the defaults
-- (floors 80 m calm / caution, 120 m ALERT); a guard is taken beyond R + 15 m and released within R, so nobody is made
-- unseen who could be seen without it. Released at once (event=mirror;on=0;why=...): near (within R), shot (the guest
-- fired: every puppet, for 15 s), state (no longer NORMAL), stale (no fresh SOLDIER_ACT for 1.5 s), owner (M3 gave him to
-- the guest), phase (a guest-local phase rise in his CP), paused (research command mirror off), key_off, unpaired,
-- gone (his object is gone). Every release sends ACT_V1 r (and native lets the fire drop lapse 1.5 s after the last m).
-- The host registers its own NORMAL guards within 300 m of the guest and beyond R - 10 m of it (ACT_V1 t), then sends
-- (ACT_V1 s). The guest queries a candidate (ACT_V1 q: is his stream fresh? nothing armed) and sends m only for a
-- puppet (the mailbox, the hook's apply and the fire drop). 12 guards are scanned per update (one GetPosition each),
-- round robin; every puppet is checked every update (near, phase), and one m per update returns the guest's shots.
-- Round 6 (design 7.19, protocol section 36): nothing runs until the partner's ACT_CAPS (0x5C) says its far mirror is on
-- (a 0.15.0 partner never sends one; why=peer); riders are skipped (why=vehicle); apart only for a still owner copy;
-- soldier_act (1 / calm; the user's decision D-FM1 (a): native's p reply pl=1) mirrors only in calm / caution
-- (why=phase_limit, before the R check); soldier_act=all (research, pl=0) keeps every phase.
do
    local W = e.MgsCoopWorld
    local P = type(W) == "table" and W.place
    local S = type(W) == "table" and W.snap
    if type(P) == "table" and type(S) == "table" and type(P.tick) == "function" then
        local M = {}
        W.mirror = M
        local fmt = string.format
        local pos, flat = P.pos, P.flat
        local function L(...) W.log(fmt(...)) end
        -- R per phase, metres: calm / caution the 80 m floor (no first-notice distance measured yet); evasion / ALERT
        -- 1.5 x the farthest ALERT hit (122.4 m on the host in 3bc7280b gave 184 m). No realize cap: 12 copies stayed
        -- realized out to 212 m (host) and 400 m (guest).
        -- FM1 retry, run 94a7bf2a: the guest's own copies hit it from up to 173.9 m in ALERT (guard 16; guard 13 from
        -- 150.6 m) -> 1.5 x 173.9 = 261 m.
        -- Round 13 C: R(caution) 90 m: a guard first went for the host at 54.3 / 58.7 m in caution (run 4735fee5; 1.5 x 58.7 =
        -- 88 m). Calm stays 80 m until a calm first notice is measured.
        M.R = {[0] = 80, [1] = 90, [2] = 261, [3] = 261}
        -- RANGE 300 m: the ALERT watch point (beyond R(ALERT) + 15 m of the fight) is ~255 m from it (FM1 retry).
        M.ENTER, M.HOST_MARGIN, M.RANGE, M.SHOT_MS, M.STALE_MS, M.SCAN = 15, 10, 300, 15000, 1500, 12
        M.MISSIONS = {[30010] = true, [30020] = true, [30050] = true}
        -- Apart (run 94a7bf2a: guard 19's host copy climbed a tower, his puppet stood at its foot 7 m off; nothing warps a
        -- copy under 15 m): > 5 m (3D) from the owner copy (the m reply's ox/oy/oz) for 2 s releases him for 30 s. Not in
        -- his first 4 s as a puppet: the engine's catch-up runs a copy that starts apart toward the owner's position first.
        M.APART_MM, M.APART_MS, M.HOLD_MS, M.APART_GRACE = 5000, 2000, 30000, 4000
        -- Round 6: apart only while the owner copy is still (moved under STILL_MM for STILL_MS; its order part refreshes every
        -- 1 s): a walker lagging a moving owner copy is the catch-up's (native re-applies his request every 1 s), not apart's.
        M.STILL_MM, M.STILL_MS = 500, 2000
        M.P_WAIT = 10000  -- ACT_V1 p refused "hook" (the defaults: no hook installed): offered again after 10 s
        -- Round 6: ACT_CAPS (92 = 0x5C): caps bit 1 the far mirror on this PC, bit 2 its calm phase limit. Round 12 (design
        -- 7.33): FM1 revision 7 (act_plan with it by default); a partner's older revision mirrors nothing (why=peer).
        M.CAPS, M.REV = 92, 7
        W.OP.ACT_CAPS = M.CAPS
        local function act(op, slot, id)
            local r = W.receipt(fmt("return 'MGS_COOP_WORLD_ACT_V1' -- %s %d %d", op, slot, id))
            if type(r) ~= "table" then return nil end
            M.last = r
            return r
        end
        -- Every way a puppet is dropped tells native (ACT_V1 r: mailbox, apply and fire-drop bit), also a mission start;
        -- native lets the fire drop lapse by itself 1.5 s after the last m as well (soldier_authority::ExpireMirrored).
        function M.reset()
            if M.pp then for _, p in pairs(M.pp) do if p.slot and p.id then act("r", p.slot, p.id) end end end
            M.pp, M.i, M.shots, M.shotAt, M.statAt, M.pAt, M.why, M.hold = {}, 0, nil, nil, nil, nil, {}, {}
            M.n = {on = 0, off = 0, refused = 0, tx = 0}
            M.last = {}
        end
        M.reset()
        function M.drop(h, p, why)
            if p and p.slot and p.id then act("r", p.slot, p.id) end
            M.pp[h], M.n.off = nil, M.n.off + 1
            L("event=mirror;ent=%.0f;on=0;why=%s;d_m=-1.0;R=0;ok=1", h, why)
        end
        function M.rOf(en)
            local c = W.cp and en.cp and W.cp[en.cp]
            return M.R[c and c.localPhase or 0] or M.R[0], c
        end
        function M.set(h, en, on, why, d, R)
            local p = M.pp[h]
            if (p ~= nil) == on then return end
            local ok1 = pcall(W.api.send, en.id, {id = "SetPuppet", enabled = on})
            local ok2 = pcall(W.api.send, en.id, {id = "SetRestrictNotice", enabled = on})
            if on then
                M.pp[h], M.n.on = {at = W.now(), fresh = W.now(), slot = S.slotOf(h), id = en.id}, M.n.on + 1
            else
                M.pp[h], M.n.off = nil, M.n.off + 1
                local slot = S.slotOf(h) or p.slot
                if slot then act("r", slot, en.id) end
            end
            L("event=mirror;ent=%.0f;on=%d;why=%s;d_m=%.1f;R=%d;ok=%d", h, on and 1 or 0, why, d and d / 1000 or -1, R or 0,
                ok1 and ok2 and 1 or 0)
        end
        function M.releaseAll(why)
            for h in pairs(M.pp) do
                local en = W.ent[h]
                if en and type(en.id) == "number" then M.set(h, en, false, why) else M.drop(h, M.pp[h], why) end
            end
        end
        -- world_snap.lua's drive mode: a mirror puppet the stream drives (within 60 m) takes DRIVE_V1 mode 2.
        W.mirrorMode = function(h) return M.pp and M.pp[h] and 2 or nil end
        W.startHooks[#W.startHooks + 1] = M.reset
        W.unpairedHooks[#W.unpairedHooks + 1] = function() M.releaseAll("unpaired"); M.peer, M.capsTx, M.capsTry = nil, nil, nil end
        -- Round 6 version gate: the partner's ACT_CAPS (kept across missions, forgotten when unpaired); ours goes out once
        -- per pairing and mission start (a reliable world-queue op: a mission change may purge it before it is read).
        W.handlers[M.CAPS] = function(v)
            M.peer = {caps = type(v[1]) == "number" and v[1] or 0, rev = type(v[2]) == "number" and v[2] or 0}
            L("event=mirror_peer;caps=%d;rev=%d", M.peer.caps, M.peer.rev)
        end
        W.pairedHooks[#W.pairedHooks + 1] = function() M.capsTx, M.capsTry = nil, nil end
        W.startHooks[#W.startHooks + 1] = function() M.capsTx, M.capsTry = nil, nil end
        function M.peerOn() return M.peer ~= nil and M.peer.caps % 2 == 1 and M.peer.rev >= M.REV end
        -- Round 6 (2): a rider (run adecad01: two LRRP riders of another CP puppeted at 297 m, apart 283-285 m off) is
        -- skipped: his vehicle carries him (the stock rider array), not his legs. [U] whether a gun emplacement answers too.
        function M.rides(en)
            local ok, v = pcall(W.api.send, en.id, {id = "GetVehicleGameObjectId"})
            return ok and type(v) == "number" and v ~= W.api.null
        end
        -- The guest's own shot (native counts the local player's bullets) releases every puppet for SHOT_MS.
        function M.shotCheck(r, now)
            if type(r) ~= "table" or type(r.sh) ~= "number" then return end
            if M.shots and r.sh > M.shots then
                M.shotAt = now
                if next(M.pp) then M.releaseAll("shot") end
            end
            M.shots = r.sh
        end
        function M.apart(h, p, en, r, now)
            if type(r) ~= "table" or type(r.ox) ~= "number" or type(r.oy) ~= "number" or type(r.oz) ~= "number" or not M.pp[h] then
                return
            end
            local o = p.o
            if not o or math.sqrt((r.ox - o[1]) ^ 2 + (r.oy - o[2]) ^ 2 + (r.oz - o[3]) ^ 2) > M.STILL_MM then
                p.o, p.oAt = {r.ox, r.oy, r.oz}, now
            end
            local x, y, z = pos(en.id)
            if not x then return end
            local a = math.sqrt((x - r.ox) ^ 2 + (y - r.oy) ^ 2 + (z - r.oz) ^ 2)
            if a <= M.APART_MM or now - p.at < M.APART_GRACE or now - p.oAt < M.STILL_MS then p.apartAt = nil; return end
            p.apartAt = p.apartAt or now
            if now - p.apartAt < M.APART_MS then return end
            M.hold[h] = now + M.HOLD_MS
            L("event=mirror_apart;ent=%.0f;apart_m=%.1f", h, a / 1000)
            M.set(h, en, false, "apart")
        end
        -- Round 13 C (the user's decision D-FM3 (b)): a puppet may be warped onto his owner copy's spot only while both spots
        -- are outside this player's view: the M-series camera cone (world_place_guest.lua P.seen, 100 degrees) with no
        -- distance cut (binoculars and scopes look along the camera's yaw). After each m: ACT_V1 w while both are out (native
        -- skips the glide for 1 s, so the unpack's 15 m warp may move him); event=mirror_view when the verdict changes.
        M.CONE = 50
        function M.cone(px, pz, yaw, x, z)
            local dx, dz = x - px, z - pz
            if dx * dx + dz * dz < 1000000 then return true end
            local a = (math.deg(math.atan2(dx, dz)) - yaw + 180) % 360 - 180
            return math.abs(a) <= M.CONE
        end
        function M.unseen(x, z, ox, oz)
            if type(P.view) ~= "function" then return false end
            local px, pz, yaw = P.view()
            if not px then return false end
            return not M.cone(px, pz, yaw, x, z) and not M.cone(px, pz, yaw, ox, oz), yaw
        end
        function M.view(h, p, en, r)
            if not p or M.pp[h] ~= p or type(r) ~= "table" or type(r.ox) ~= "number" or type(r.oz) ~= "number" then return end
            local x, _, z = pos(en.id)
            if not x then return end
            local off, yaw = M.unseen(x, z, r.ox, r.oz)
            if yaw == nil then return end
            if off then act("w", p.slot or S.slotOf(h) or 0, en.id) end
            if off ~= p.off then
                p.off = off
                L("event=mirror_view;ent=%.0f;view=%s;apart_m=%.1f;yaw=%.0f", h, off and "off" or "on", flat(x, z, r.ox, r.oz) / 1000, yaw)
            end
        end
        function M.guest(now)
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            local list = W.entOrder
            if type(px) ~= "number" or type(pz) ~= "number" or type(list) ~= "table" or #list == 0 then return end
            px, pz = px * 1000, pz * 1000
            -- Round 13 C: this player's camera every 2 s while a puppet lives (the jump row: could a player see it).
            if next(M.pp) and now - (M.camAt or -1e9) >= 2000 and type(P.view) == "function" then
                M.camAt = now
                local _, _, yaw = P.view()
                L("event=mirror_cam;x=%.1f;z=%.1f;yaw=%.0f", px / 1000, pz / 1000, yaw or 0)
            end
            local quiet = not (M.shotAt and now - M.shotAt < M.SHOT_MS)
            -- Round 12 C (D-FM1 (a)): the calm limit ends on any CP at EVASION / ALERT on this PC, not only the guard's own
            -- (an LRRP crew whose CP stayed calm fought in CP 557's ALERT and was taken at 97-117 m, runs 516e3ef7 / 4735fee5).
            local lim = false
            if M.lim then
                for _, ch in ipairs(W.cpOrder or {}) do
                    local cc = W.cp and W.cp[ch]
                    if cc and (cc.localPhase or 0) >= 2 then lim = true; break end
                end
            end
            -- Every puppet is checked every update (the round robin below visits a guard only every ~0.8 s with ~280
            -- roster entries): the guest within R, a guest-local phase rise in his CP, and the guest's own shot release at
            -- once. One ACT_V1 m per update goes to the puppet asked longest ago (his lease and the shot count).
            local due, dueAt
            for h, p in pairs(M.pp) do
                local en = W.ent[h]
                if not en or type(en.id) ~= "number" then M.drop(h, p, "gone")
                else
                    local x, _, z = pos(en.id)
                    local d = x and flat(x, z, px, pz)
                    local R, c = M.rOf(en)
                    if lim or M.lim and (c and c.localPhase or 0) >= 2 then M.set(h, en, false, "phase_limit", d, R)
                    elseif not d or d < R * 1000 then M.set(h, en, false, "near", d, R)
                    elseif c and c.ownAt and c.ownAt >= p.at then M.set(h, en, false, "phase", d, R)
                    elseif not dueAt or (p.mAt or 0) < dueAt then due, dueAt = h, p.mAt or 0 end
                end
            end
            local dp = due and M.pp[due]
            if dp then
                dp.mAt = now
                local r = act("m", dp.slot or S.slotOf(due) or 0, W.ent[due].id)
                M.shotCheck(r, now)
                if M.pp[due] and type(r) == "table" then
                    if r.refused then M.set(due, W.ent[due], false, r.refused)
                    elseif r.f == 1 then dp.fresh = now end
                    M.apart(due, dp, W.ent[due], r, now)
                    M.view(due, dp, W.ent[due], r)
                end
            end
            for _ = 1, math.min(M.SCAN, #list) do
                M.i = M.i % #list + 1
                local h = list[M.i]
                local en, p = W.ent[h], M.pp[h]
                local slot = S.slotOf(h)
                if en and type(en.id) == "number" and slot then
                    local x, _, z = pos(en.id)
                    local d = x and flat(x, z, px, pz)
                    local R, c = M.rOf(en)
                    local rise = c and c.ownAt
                    local why
                    if S.mine(h) then why = "owner"
                    elseif P.state(en) ~= 0 then why = "state"
                    elseif not d or d < R * 1000 then why = "near"
                    elseif d > M.RANGE * 1000 then why = "range"
                    elseif lim or M.lim and (c and c.localPhase or 0) >= 2 then why = "phase_limit"
                    elseif M.rides(en) then why = "vehicle"
                    elseif not quiet then why = "shot"
                    elseif p and rise and rise >= p.at then why = "phase" end
                    if p then
                        if not why then
                            local r = act("m", slot, en.id)
                            M.shotCheck(r, now)
                            if type(r) == "table" and r.refused then why = r.refused
                            elseif type(r) == "table" and r.f == 1 then p.fresh = now
                            elseif now - p.fresh >= M.STALE_MS then why = "stale" end
                            if not why then M.apart(h, p, en, r, now); M.view(h, p, en, r) end
                        end
                        if why and M.pp[h] then M.set(h, en, false, why, d, R) end
                    elseif not why and d > (R + M.ENTER) * 1000 and not (rise and now - rise < M.SHOT_MS) and
                        not (M.hold[h] and now < M.hold[h]) then
                        local r = act("q", slot, en.id)
                        M.shotCheck(r, now)
                        -- Round 13 C (D-FM3 (b); 1fb060aa guard 18): a take that would open with the warp waits (native
                        -- near_wait); both his spot and his owner copy's (the q reply's ox / oz) off view: w, then q again.
                        if type(r) == "table" and r.f ~= 1 and not r.refused and type(r.ox) == "number" and type(r.oz) == "number" and
                            M.unseen(x, z, r.ox, r.oz) then
                            act("w", slot, en.id)
                            r = act("q", slot, en.id)
                            M.shotCheck(r, now)
                        end
                        if type(r) == "table" and r.f == 1 and not (M.shotAt and now - M.shotAt < M.SHOT_MS) then
                            M.set(h, en, true, "far", d, R)
                            local r2 = act("m", slot, en.id)  -- arm the mailbox, the hook's apply and the fire drop now
                            M.view(h, M.pp[h], en, r2)        -- round 13 C: the take may warp him while both spots are unseen
                        elseif type(r) == "table" and r.refused then
                            M.n.refused = M.n.refused + 1
                            M.why[r.refused] = (M.why[r.refused] or 0) + 1
                        end
                    end
                end
            end
        end
        function M.host(now)
            local gx, gz = P.partnerXZ(now)
            local list = W.entOrder
            if type(gx) ~= "number" or type(gz) ~= "number" or type(list) ~= "table" or #list == 0 then return end
            for _ = 1, math.min(M.SCAN, #list) do
                M.i = M.i % #list + 1
                local h = list[M.i]
                local en = W.ent[h]
                local slot = S.slotOf(h)
                if en and type(en.id) == "number" and slot and S.mine(h) and P.state(en) == 0 then
                    local x, _, z = pos(en.id)
                    local d = x and flat(x, z, gx, gz)
                    local R = M.rOf(en)
                    if d and d <= M.RANGE * 1000 and d >= (R - M.HOST_MARGIN) * 1000 and not M.rides(en) then
                        act("t", slot, en.id)
                        M.n.tx = M.n.tx + 1
                    end
                end
            end
            act("s", 0, 0)
        end
        function M.stats(now)
            if M.statAt and now - M.statAt < 10000 then return end
            M.statAt = now
            local k, n = M.last or {}, 0
            for _ in pairs(M.pp) do n = n + 1 end
            local why = {}
            for w, c in pairs(M.why) do why[#why + 1] = w .. ":" .. c end
            L("event=mirror_stats;role=%s;puppets=%d;on=%d;off=%d;refused=%d;why=%s;reg=%d;tx=%s;rx=%s;ap=%s;od=%s;fd=%s;fn=%s;sh=%s;peer=%s;lim=%d",
                W.host and "host" or "guest", n, M.n.on, M.n.off, M.n.refused, table.concat(why, ","), M.n.tx, tostring(k.tx),
                tostring(k.rx), tostring(k.ap), tostring(k.od), tostring(k.fd), tostring(k.fn), tostring(k.sh),
                M.peer and tostring(M.peer.caps) or "none", M.lim and 1 or 0)
        end
        local tick = P.tick
        P.tick = function(now)
            tick(now)
            -- The local player for the native census distances (act_census / soldier_act; refused without the hook).
            if W.followOn == true and W.paired and (not M.pAt or now >= M.pAt) then
                M.pAt = now + 250
                local x, y, z = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
                if type(x) == "number" and type(y) == "number" and type(z) == "number" then
                    local r = W.receipt(fmt("return 'MGS_COOP_WORLD_ACT_V1' -- p %d %d %d", math.floor(x * 1000 + 0.5),
                        math.floor(y * 1000 + 0.5), math.floor(z * 1000 + 0.5)))
                    if type(r) == "table" and r.refused == "hook" then M.pAt = now + M.P_WAIT end
                    -- Round 6 (4): soldier_act=calm (pl=1) mirrors only in calm / caution.
                    if type(r) == "table" and r.p == 1 then M.lim = r.pl == 1 end
                end
            end
            -- Round 6 (5): our ACT_CAPS, once per pairing and mission start, only with soldier_act (the defaults send none).
            if W.actOn and W.followOn == true and W.paired and W.eff >= 2 and not M.capsTx and (not M.capsTry or now - M.capsTry >= 1000) then
                M.capsTry = now
                local caps = 1 + (M.lim and 2 or 0)
                if W.tx(M.CAPS, caps, M.REV) then M.capsTx = now; L("event=mirror_caps;caps=%d;rev=%d", caps, M.REV) end
            end
            local peer = M.peerOn()
            local on = W.actOn and W.followOn == true and W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true and
                M.MISSIONS[W.mission] and peer
            if not on then
                if next(M.pp) then M.releaseAll(W.actOn and not peer and "peer" or "key_off") end
                return
            end
            if W.host then M.host(now) else M.guest(now) end
            M.stats(now)
        end
    end
end
