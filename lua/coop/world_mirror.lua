-- MGS co-op world sync, enemy sync M7 FM1, the far mirror (original; docs/world-sync-design.md 7.15, protocol section 32).
-- Lab key soldier_act (CFG r[19] bit 512, W.actOn), only with follow; 30010 / 30020 / 30050. Spliced after world_snap.lua.
-- While the guest's player is beyond R of a host-owned guard, the guest's copy of him is a mirror puppet (SetPuppet and
-- restrict notice: his own brain is off) that replays the host copy's own request and order (SOLDIER_ACT 0x58, native
-- soldier_act.hpp) with his own animations: walk, run, turn, aim, fire (his bullets and noise are dropped: the host copy's
-- shots are the real ones). R per phase (M.R) is 1.5 x the farthest notice / engagement distance measured at the defaults
-- (floors 80 m calm / caution, 120 m ALERT); a guard is taken beyond R + 15 m and released within R, so nobody is made
-- unseen who could be seen without it. Released at once (event=mirror;on=0;why=...): near (within R), shot (the guest
-- fired: every puppet, for 15 s), state (no longer NORMAL), stale (no fresh SOLDIER_ACT for 1.5 s), owner (M3 gave him to
-- the guest), phase (a guest-local phase rise in his CP), paused (research command mirror off), key_off, unpaired.
-- The host registers its own NORMAL guards within 250 m of the guest and beyond R - 10 m of it (ACT_V1 t), then sends
-- (ACT_V1 s). 12 guards are scanned per update (one GetPosition each), round robin.
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
        M.R = {[0] = 80, [1] = 80, [2] = 120, [3] = 120}
        M.ENTER, M.HOST_MARGIN, M.RANGE, M.SHOT_MS, M.STALE_MS, M.SCAN = 15, 10, 250, 15000, 1500, 12
        M.MISSIONS = {[30010] = true, [30020] = true, [30050] = true}
        function M.reset()
            M.pp, M.i, M.shots, M.shotAt, M.statAt, M.pAt, M.why = {}, 0, nil, nil, nil, nil, {}
            M.n = {on = 0, off = 0, refused = 0, tx = 0}
            M.last = {}
        end
        M.reset()
        local function act(op, slot, id)
            local r = W.receipt(fmt("return 'MGS_COOP_WORLD_ACT_V1' -- %s %d %d", op, slot, id))
            if type(r) ~= "table" then return nil end
            M.last = r
            return r
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
                M.pp[h], M.n.on = {at = W.now(), fresh = W.now()}, M.n.on + 1
            else
                M.pp[h], M.n.off = nil, M.n.off + 1
                local slot = S.slotOf(h)
                if slot then act("r", slot, en.id) end
            end
            L("event=mirror;ent=%.0f;on=%d;why=%s;d_m=%.1f;R=%d;ok=%d", h, on and 1 or 0, why, d and d / 1000 or -1, R or 0,
                ok1 and ok2 and 1 or 0)
        end
        function M.releaseAll(why)
            for h in pairs(M.pp) do
                local en = W.ent[h]
                if en and type(en.id) == "number" then M.set(h, en, false, why) else M.pp[h] = nil end
            end
        end
        -- world_snap.lua's drive mode: a mirror puppet the stream drives (within 60 m) takes DRIVE_V1 mode 2.
        W.mirrorMode = function(h) return M.pp and M.pp[h] and 2 or nil end
        W.startHooks[#W.startHooks + 1] = M.reset
        W.unpairedHooks[#W.unpairedHooks + 1] = function() M.releaseAll("unpaired") end
        -- The guest's own shot (native counts the local player's bullets) releases every puppet for SHOT_MS.
        function M.shotCheck(r, now)
            if type(r) ~= "table" or type(r.sh) ~= "number" then return end
            if M.shots and r.sh > M.shots then
                M.shotAt = now
                if next(M.pp) then M.releaseAll("shot") end
            end
            M.shots = r.sh
        end
        function M.guest(now)
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            local list = W.entOrder
            if type(px) ~= "number" or type(pz) ~= "number" or type(list) ~= "table" or #list == 0 then return end
            px, pz = px * 1000, pz * 1000
            local quiet = not (M.shotAt and now - M.shotAt < M.SHOT_MS)
            -- Every puppet is checked every update for the guest's approach (the round robin below visits a guard only
            -- every ~0.8 s with ~280 roster entries; a puppet within R must go back to his own brain within 500 ms).
            for h in pairs(M.pp) do
                local en = W.ent[h]
                local x, _, z = nil, nil, nil
                if en and type(en.id) == "number" then x, _, z = pos(en.id) end
                local d = x and flat(x, z, px, pz)
                local R = en and M.rOf(en) or M.R[0]
                if en and type(en.id) == "number" and (not d or d < R * 1000) then M.set(h, en, false, "near", d, R)
                elseif not en or type(en.id) ~= "number" then M.pp[h] = nil end
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
                    elseif not quiet then why = "shot"
                    elseif p and rise and rise >= p.at then why = "phase" end
                    if p then
                        if not why then
                            local r = act("m", slot, en.id)
                            M.shotCheck(r, now)
                            if type(r) == "table" and r.refused then why = r.refused
                            elseif type(r) == "table" and r.f == 1 then p.fresh = now
                            elseif now - p.fresh >= M.STALE_MS then why = "stale" end
                        end
                        if why and M.pp[h] then M.set(h, en, false, why, d, R) end
                    elseif not why and d > (R + M.ENTER) * 1000 and not (rise and now - rise < M.SHOT_MS) then
                        local r = act("m", slot, en.id)
                        M.shotCheck(r, now)
                        if type(r) == "table" and r.f == 1 and not (M.shotAt and now - M.shotAt < M.SHOT_MS) then
                            M.set(h, en, true, "far", d, R)
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
                    if d and d <= M.RANGE * 1000 and d >= (R - M.HOST_MARGIN) * 1000 then
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
            L("event=mirror_stats;role=%s;puppets=%d;on=%d;off=%d;refused=%d;why=%s;reg=%d;tx=%s;rx=%s;ap=%s;or=%s;fd=%s;fn=%s;sh=%s",
                W.host and "host" or "guest", n, M.n.on, M.n.off, M.n.refused, table.concat(why, ","), M.n.tx, tostring(k.tx),
                tostring(k.rx), tostring(k.ap), tostring(k["or"]), tostring(k.fd), tostring(k.fn), tostring(k.sh))
        end
        local tick = P.tick
        P.tick = function(now)
            tick(now)
            -- The local player for the native census distances (act_census / soldier_act; refused without the hook).
            if W.followOn == true and W.paired and (not M.pAt or now - M.pAt >= 250) then
                M.pAt = now
                local x, y, z = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
                if type(x) == "number" and type(y) == "number" and type(z) == "number" then
                    W.receipt(fmt("return 'MGS_COOP_WORLD_ACT_V1' -- p %d %d %d", math.floor(x * 1000 + 0.5),
                        math.floor(y * 1000 + 0.5), math.floor(z * 1000 + 0.5)))
                end
            end
            local on = W.actOn and W.followOn == true and W.w1 and W.paired and W.eff >= 2 and W.rosterMatch == true and
                M.MISSIONS[W.mission]
            if not on then
                if next(M.pp) then M.releaseAll("key_off") end
                return
            end
            if W.host then M.host(now) else M.guest(now) end
            M.stats(now)
        end
    end
end
