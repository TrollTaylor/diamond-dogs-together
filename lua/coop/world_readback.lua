-- MGS co-op world sync, enemy round 7 track A (original; docs/world-sync-design.md 7.20, protocol section 37). Spliced
-- after world_mirror.lua; only with follow (W.followOn). The y read-backs of the guards' copies and the far put-back:
-- * event=readback 0.5 s and 2 s (at=500 / 2000) after every drive_vert snap (DRIVE_V1 reply s=3; src=vert), every landed
--   drive step that ends a place_reach hold (src=drive), every landed kind-3 placement of a held copy or of one beyond
--   60 m of this player (src=place) and every put-back (src=put): dy_m his GetPosition y minus where he was put (the
--   target), held=1 within 1 m, far_m his distance from this player when he was put there. Round 6 logged h=1 only.
-- * drive_sink (moved here from world_snap.lua): a driven copy more than 1.5 m under its target (the reply's v) for 2 s
--   in all; the time under is counted apart from snaps (round 6: a 1 s drive_vert snap reset the 2 s window, so a copy
--   that popped up and dropped again never counted); level for 2 s starts the count over.
-- * place_reach (W.reachOn): a copy beyond 60 m that a read-back finds more than 1.5 m under its target (and under the
--   owner's newest sample of him, when one is at most 3 s old) is put back once (his placement queued again, or that
--   newest sample), his force-realize ends when the put-back is done (or at once when nothing can put him back), and he
--   is not held again for 10 s (event=readback_fix; put=1, or put=0; why=once / owner / busy / none; event=
--   readback_nohold). Run 35d4074f: the host's copy of 1179373153, about 150 m off, landed, was force-realized and
--   dropped 3.7-5.4 m [I: through a floor that PC has not loaded].
do
    local W = e.MgsCoopWorld
    local P = type(W) == "table" and W.place
    local S = type(W) == "table" and W.snap
    if type(P) == "table" and type(S) == "table" and type(P.hdr) == "function" and type(P.sinkTick) == "function" and
        type(P.doneHook) == "function" and type(P.hold) == "function" then
        local R = {}
        W.readback = R
        local fmt, floor, min, abs = string.format, math.floor, math.min, math.abs
        local pos, flat = P.pos, P.flat
        local function L(...) W.log(fmt(...)) end
        function R.reset() R.rb, R.pb, R.stop, R.noted = {}, {}, {}, {} end
        R.reset()
        W.startHooks[#W.startHooks + 1] = R.reset
        -- This player's distance (mm) from x, z (mm); 0 without a position.
        local function far(x, z)
            local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
            if type(px) ~= "number" or type(pz) ~= "number" or type(x) ~= "number" or type(z) ~= "number" then return 0 end
            return flat(x, z, px * 1000, pz * 1000)
        end
        -- Two read-backs of h: ty where he was put (mm, GetPosition y), d his distance from this player (mm).
        function R.arm(h, src, ty, d, c)
            if W.followOn ~= true or type(ty) ~= "number" then return end
            R.rb[h] = {at = W.now(), ty = ty, src = src, d = d or 0, st = 0, c = c}
        end
        function R.sink(h, r, dv, now)
            local off = type(r.v) == "number" and r.v * 10 - (800 - P.LIFT) or nil
            if not off then return end
            local low = off < -1500
            if dv.lowT then dv.lowMs = (dv.lowMs or 0) + min(500, now - dv.lowT) end
            dv.lowT = low and now or nil
            if low then dv.upAt = nil
            else
                dv.upAt = dv.upAt or now
                if now - dv.upAt >= 2000 then dv.lowMs, dv.sunk = 0, nil end
            end
            if low and (dv.lowMs or 0) >= 2000 and not dv.sunk then
                dv.sunk, S.n.sink = true, S.n.sink + 1
                L("event=drive_sink;ent=%.0f;off_m=%.2f;e_cm=%d;low_ms=%d", h, off / 1000, r.e or -1, dv.lowMs)
            end
        end
        -- place_reach: a far copy that fell. Put back once; his force-realize ends; no new hold for 10 s.
        function R.fix(h, b, dy, y, now)
            local o = P.lastS[h]
            local ody = o and type(o.at) == "number" and now - o.at <= 3000 and type(o.y) == "number" and y - o.y or nil
            local why = b.src == "put" and "once" or ody and ody > -1500 and "owner" or nil
            local c = b.c or (o and o.kind == 3 and ody and o) or nil
            local busy = P.cur ~= nil and P.cur.h == h
            for _, q in ipairs(P.q) do busy = busy or q.h == h end
            if not why and (busy or not c) then why = busy and "busy" or "none" end
            if why ~= "owner" then R.stop[h] = now + 10000 end
            if why then
                if why ~= "owner" and P.rz[h] and P.unhold then P.unhold(h, "fell") end
                L("event=readback_fix;ent=%.0f;src=%s;far_m=%.0f;dy_m=%.2f;put=0;why=%s", h, b.src, b.d / 1000, dy / 1000, why)
                return
            end
            c.landed, c.native, c.back, c.dy, c.pos = nil, nil, nil, nil, nil
            R.pb[h] = c
            P.q[#P.q + 1] = c
            L("event=readback_fix;ent=%.0f;src=%s;far_m=%.0f;dy_m=%.2f;put=1", h, b.src, b.d / 1000, dy / 1000)
        end
        function R.tick(now)
            for h, t in pairs(R.stop) do if now >= t then R.stop[h], R.noted[h] = nil, nil end end
            for h, b in pairs(R.rb) do
                local due = b.st == 0 and 500 or 2000
                if now - b.at >= due then
                    local en = W.ent[h]
                    local x, y, z
                    if en and type(en.id) == "number" then x, y, z = pos(en.id) end
                    b.st = b.st + 1
                    if b.st >= 2 or not x then R.rb[h] = nil end
                    if x and type(y) == "number" then
                        local dy = y - b.ty
                        local held = abs(dy) <= 1000
                        L("event=readback;ent=%.0f;src=%s;at=%d;ms=%d;dy_m=%.2f;held=%d;far_m=%.0f", h, b.src, due, floor(now - b.at),
                            dy / 1000, held and 1 or 0, b.d / 1000)
                        if not held and dy < -1500 and b.d > 60000 and W.reachOn == true and not b.fixed then
                            b.fixed = true
                            R.fix(h, b, dy, y, now)
                        end
                    end
                end
            end
        end
        -- S.drive's receipt (world_snap.lua passes this copy's x, y, z read before the call).
        local hdr = P.hdr
        function P.hdr(h, id, r, dv, x, y, z)
            if type(r) == "table" and not r.refused and type(dv) == "table" then
                R.sink(h, r, dv, W.now())
                if type(r.v) == "number" and type(y) == "number" then
                    if r.s == 3 then R.arm(h, "vert", y - r.v * 10, far(x, z))
                    elseif P.rz[h] and r.h == 1 and (r.s or 0) >= 1 then R.arm(h, "drive", y - r.v * 10, far(x, z)) end
                end
            end
            return hdr(h, id, r, dv)
        end
        local done = P.doneHook
        function P.doneHook(c, why)
            local h = c.h
            local held, put = P.rz[h] ~= nil, R.pb[h] == c
            if put then R.pb[h] = nil end
            done(c, why)
            if put and why and P.rz[h] and P.unhold then P.unhold(h, "fell") end
            if not why and c.kind == 3 and not c.body and type(c.pos) == "table" and type(c.y) == "number" then
                local d = far(c.pos[1], c.pos[3])
                if put or held or d > 60000 then R.arm(h, put and "put" or "place", c.y, d, not put and c or nil) end
            end
        end
        local hold = P.hold
        function P.hold(h, id, why, b, c)
            local t = R.stop[h]
            if t and W.now() < t then
                if R.noted[h] ~= t then
                    R.noted[h] = t
                    L("event=readback_nohold;ent=%.0f;why=%s;left_ms=%d", h, tostring(why), floor(t - W.now()))
                end
                return nil
            end
            return hold(h, id, why, b, c)
        end
        local sinkTick = P.sinkTick
        function P.sinkTick(now)
            sinkTick(now)
            if next(R.rb) or next(R.stop) then R.tick(now) end
        end
    end
end
