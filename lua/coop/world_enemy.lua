-- MGS co-op world sync, enemy outcomes (original; docs/world-sync-protocol.md
-- "Enemy outcomes"): host commits, the apply rule, the sweep and the pending
-- ledger (requests and the wire handlers are in world_outcome.lua). Wire states
-- NORMAL 0 HOLDUP 1 SLEEP 2 FAINT 3 DYING 4 DEAD 5 FULTONED 6 VANISHED 7, ranks
-- 0 1 2 2 3 4 5 5; terminal local states are never commanded. A fulton counts
-- once IsRecovered is true; RequestForceFulton is never resent after the
-- entity's Fulton message.
do
    local W = e.MgsCoopWorld
    if type(W) == "table" then
        local E, OP = {}, W.OP
        W.enemy = E
        local fmt, floor = string.format, math.floor
        local function L(...) W.log(fmt(...)) end
        local RANK = {[0] = 0, 1, 2, 2, 3, 4, 5, 5}
        local TERMINAL = {[5] = true, [6] = true, [7] = true}
        local ACTOR = {[0] = "host", "guest", "probe"}
        local CAUSE = {Dead = 1, Neutralize = 2, Holdup = 2, Unconscious = 3, Dying = 4, FultonInfo = 5, FultonFailed = 5, Conscious = 9}
        E.RANK, E.TERMINAL = RANK, TERMINAL

        function E.reset()
            E.s = {pending = {}, queue = {}, qHead = 1, rb = {}, ledger = {}, lkeys = {}, lidx = 1, req = {}, out = {}, held = {},
                attrib = {}, last = {}, lastByCp = {}, holdup = {}, fpend = {}, fmsg = {}, sweep = 0, seq = 0, sum = 0, count = 0, seen = 0, hostReq = {}}
        end
        E.reset()
        W.startHooks[#W.startHooks + 1] = E.reset
        -- The local wire state, or nil when it cannot be read (UNKNOWN).
        function E.localState(e)
            local a = W.api
            if a.recovered(e.name) == true then return 6 end
            local act = a.action and a.action(e.id)
            if act ~= nil and a.fultonRecovered ~= nil and act == a.fultonRecovered then return 6 end
            local v = a.life(e.id)
            if type(v) ~= "number" then return nil end
            if v == 0 then
                if (act ~= nil and ((a.holdupStand ~= nil and act == a.holdupStand) or (a.holdupCrawl ~= nil and act == a.holdupCrawl))) or
                    (E.s and E.s.holdup and E.s.holdup[e.id] == true) then
                    return 1
                end
                return 0
            end
            return W.lifeWire[v]
        end
        -- A guard placed by spawn sync (W.pinned) is kept in area on this PC.
        function E.active(cp, h)
            local act, pin = W.activeCp, W.pinned
            return act == nil or act[cp] == true or (h ~= nil and pin ~= nil and pin[h] == true)
        end
        function E.pos(id)
            local p = W.api.send(id, {id = "GetPosition"})
            if type(p) ~= "userdata" and type(p) ~= "table" then return 0, 0, 0 end
            return floor(p:GetX() * 1000 + 0.5), floor(p:GetY() * 1000 + 0.5), floor(p:GetZ() * 1000 + 0.5)
        end
        function E.attributed(h, now)
            local a, o = E.s.attrib[h], W.attribOverride[h]
            return (a ~= nil and now - a <= 10000) or (o ~= nil and now - o <= 10000)
        end

        -- Observer hook: records attribution, fulton progress and pending reads.
        function E.observe(kind, h, cls, attackId)
            if not W.w1 then return end
            local s, now = E.s, W.now()
            local mine = cls == "local" or cls == "buddy"
            local e = W.ent[h]
            if kind == "Holdup" and e then
                s.holdup[e.id] = true
                s.attrib[h] = now
            elseif kind == "Conscious" and e then
                s.holdup[e.id] = nil
            end
            if kind == "Fulton" then s.fpend[h], s.fmsg[h] = now, now; return end
            if kind == "FultonInfo" or kind == "FultonFailed" then s.fpend[h] = nil end
            if mine and kind ~= "FultonFailed" then
                if kind ~= "FultonInfo" then s.last[h] = now end
                if kind ~= "Damage" then s.attrib[h] = now end
                if e and e.cp then s.lastByCp[e.cp] = h end
            elseif cls == "none" and kind ~= "FultonInfo" and kind ~= "FultonFailed" and s.last[h] and now - s.last[h] <= 10000 then
                s.attrib[h] = now
                if e and e.cp then s.lastByCp[e.cp] = h end
            end
            local c, q = CAUSE[kind], s.pending[h]
            -- The Neutralize that follows FultonInfo must not cut a fulton read short.
            if c and not (q and q.cause == 5 and c ~= 5 and now - q.at <= 20000) then
                local attack = kind == "Dead" and type(attackId) == "number" and attackId >= 0 and attackId < 4294967296 and attackId or 0
                s.pending[h] = {cause = c, at = now, attack = floor(attack)}
            end
        end

        function E.commit(h, st, actor, rid, cause, flags)
            local s, e = E.s, W.ent[h]
            if e.ver == 0 then s.count = s.count + 1 end
            if e.ver < 65535 then e.ver, s.sum = e.ver + 1, (s.sum + 1) % 4294967296 end
            e.committed, e.known, e.kver = st, st, e.ver
            s.seq = (s.seq + 1) % 65536
            -- Guest/probe commits wait for a readback before a backward sweep.
            e.reached = actor == 0
            W.stats.commits = W.stats.commits + 1
            L("event=commit;ent=%.0f;state=%d;ver=%d;actor=%s;rid=%d;cause=%d", h, st, e.ver, ACTOR[actor], rid, cause)
            if W.paired and W.eff >= 2 then
                local x, y, z = E.pos(e.id)
                W.tx(OP.OUTCOME, h, 1, st, e.ver, cause, actor, rid, flags, x, y, z)
            end
        end
        -- Host, own world: forward commits; backward only after a readback once
        -- reached the committed state (no false wake-ups from failed applies).
        function E.check(h, cause, sweep)
            local e = W.ent[h]
            local st = E.localState(e)
            if st == nil then return false end
            local c = e.committed
            if RANK[st] >= RANK[c] then e.reached = true end
            if TERMINAL[c] then return true end
            if RANK[st] > RANK[c] then E.commit(h, st, 0, 0, cause, 0)
            elseif e.reached and RANK[st] < RANK[c] then E.commit(h, st, 0, 0, sweep and 9 or cause, 2) end
            return true
        end

        -- Apply rule (both PCs). `known` is the state of the highest ver seen.
        function E.apply(h, st, ver, src)
            local e = W.ent[h]
            if e and (ver > e.kver or src == "resync" or src == "reject") then
                if not W.host then
                    local s = E.s
                    if e.kver == 0 and ver > 0 then s.count = s.count + 1 end
                    s.sum = (s.sum + ver - e.kver) % 4294967296
                end
                e.known, e.kver = st, ver
            end
            local q = E.s.queue
            q[#q + 1] = {h, st, ver, src}
        end
        function E.hold(h, st, ver, src, reason)
            local s = E.s
            local l = s.ledger[h]
            if not l then
                W.stats.ledger = W.stats.ledger + 1
                s.ledger[h] = {st = st, ver = ver, src = src, since = W.now(), reason = reason}
            else
                if ver >= l.ver then l.st, l.ver, l.src = st, ver, src end
                if l.reason == reason then return "held" end
                l.reason = reason
            end
            L("event=ledger;ent=%.0f;op=hold;reason=%s;waited_ms=%d", h, reason, floor(W.now() - s.ledger[h].since))
            return "held"
        end
        function E.done(h, st, ver, src, result, readback)
            local s, e = E.s, W.ent[h]
            W.stats.applies = W.stats.applies + 1
            if result == "noop" then W.stats.noops = W.stats.noops + 1 end
            L("event=apply;ent=%.0f;state=%d;ver=%d;src=%s;result=%s;readback=%s", h, st, ver, src, result, tostring(readback))
            if readback and e and RANK[readback] >= RANK[e.committed] and W.host then e.reached = true end
            local l = s.ledger[h]
            if l and result ~= "fail" then
                s.ledger[h] = nil
                L("event=ledger;ent=%.0f;op=applied;reason=%s;waited_ms=%d", h, l.reason, floor(W.now() - l.since))
            end
        end
        function E.applyOne(h, st, ver, src, now)
            local s, e = E.s, W.ent[h]
            if not e then return E.hold(h, st, ver, src, "unknown") end
            if not W.w1 then return end
            if s.rb[h] then return "defer" end
            if not E.active(e.cp, h) then return E.hold(h, st, ver, src, "out_of_area") end
            local cur = E.localState(e)
            if cur == nil then return E.hold(h, st, ver, src, "unreadable") end
            local result = "noop"
            if TERMINAL[cur] then result = "terminal_local"
            elseif st == 0 then
                s.holdup[e.id] = nil
                if cur == 2 or cur == 3 then
                    W.api.send(e.id, {id = "RecoveryLife"})
                    s.rb[h] = {st = 0, ver = ver, src = src, at = now}
                    return "sent"
                else
                    result = "skip_backward"
                end
            elseif RANK[cur] < RANK[st] and st == 1 then
                s.holdup[e.id] = true
                pcall(W.api.send, e.id, {id = "SetSneakRoute", route = "", point = 0})
                pcall(W.api.send, e.id, {id = "SetCautionRoute", route = "", point = 0})
                result, cur = "ok", 1
            elseif RANK[cur] < RANK[st] and st == 7 then
                if W.api.setDisable then pcall(W.api.setDisable, e.id)
                else pcall(W.api.send, e.id, {id = "SetEnabled", enabled = false}) end
                result, cur = "ok", 7
            elseif RANK[cur] < RANK[st] and st == 6 then
                if s.fpend[h] then return E.hold(h, st, ver, src, "fulton_pending") end
                W.api.send(e.id, {id = "RequestForceFulton"})
                s.rb[h] = {st = 6, ver = ver, src = src, at = now, next = now + 250}
                return "sent"
            elseif RANK[cur] < RANK[st] and W.lifeConst[st] then
                s.holdup[e.id] = nil
                W.api.send(e.id, {id = "ChangeLifeState", state = W.lifeConst[st]})
                s.rb[h] = {st = st, ver = ver, src = src, at = now}
                return "sent"
            end
            E.done(h, st, ver, src, result, cur)
        end
        -- Life states: read from +200 ms, one retry at +1 s, failed at +2 s.
        -- FULTONED: IsRecovered or FULTON_RECOVERD (E.localState) until +20 s; one retry at
        -- +5 s only without a Fulton message.
        function E.readbacks(now)
            local s, a = E.s, W.api
            for h, r in pairs(s.rb) do
                local e, age, cur, final = W.ent[h], now - r.at, nil, false
                if r.st == 6 then
                    if now >= r.next then
                        r.next = now + 250
                        cur = E.localState(e) == 6 and 6 or nil
                        final = age >= 20000
                        if not cur and age >= 5000 and not r.retried and not (s.fmsg[h] and s.fmsg[h] >= r.at) then
                            r.retried = true
                            a.send(e.id, {id = "RequestForceFulton"})
                        end
                    end
                elseif r.st == 0 and age >= 200 then
                    cur = E.localState(e)
                    final = age >= 2000
                    if age >= 1000 and not r.retried and cur ~= 0 then
                        r.retried = true
                        a.send(e.id, {id = "RecoveryLife"})
                    end
                elseif r.st ~= 0 and age >= 200 then
                    cur = E.localState(e)
                    final = age >= 2000
                    if age >= 1000 and not r.retried and not (cur and RANK[cur] >= RANK[r.st]) then
                        r.retried = true
                        a.send(e.id, {id = "ChangeLifeState", state = W.lifeConst[r.st]})
                    end
                end
                local ok = cur ~= nil and (r.st == 0 and cur == 0 or (r.st ~= 0 and RANK[cur] >= RANK[r.st]))
                if ok or final then
                    s.rb[h] = nil
                    E.done(h, r.st, r.ver, r.src, ok and "ok" or "fail", cur or E.localState(e))
                    if not ok then E.hold(h, r.st, r.ver, r.src, "apply_failed") end
                end
            end
        end
        -- Re-checks held entries (all of them after an area change).
        function E.recheck(now, all)
            local s = E.s
            if all or s.lidx > #s.lkeys then
                s.lkeys, s.lidx = {}, 1
                for h in pairs(s.ledger) do s.lkeys[#s.lkeys + 1] = h end
            end
            local n = all and #s.lkeys or 10
            while n > 0 and s.lidx <= #s.lkeys do
                local h = s.lkeys[s.lidx]
                s.lidx, n = s.lidx + 1, n - 1
                local l = s.ledger[h]
                if l and (all or l.reason ~= "apply_failed") then E.applyOne(h, l.st, l.ver, l.src, now) end
            end
        end
        function E.areaChanged()
            if W.w1 then E.recheck(W.now(), true) end
        end
        function E.dropLedger(why)
            for h in pairs(E.s.ledger) do L("event=ledger;ent=%.0f;op=dropped;reason=%s;waited_ms=0", h, why) end
            E.s.ledger, E.s.lkeys, E.s.lidx = {}, {}, 1
        end
        W.stopHooks[#W.stopHooks + 1] = function()
            E.dropLedger("stop")
            E.dropRequests()
            E.s.held = {}
        end
        W.pairedHooks[#W.pairedHooks + 1] = function(epochChanged)
            local s = E.s
            if epochChanged then E.dropLedger("epoch") end
            -- Another host epoch in this mission is another host world: W1 off.
            if s.pairEpoch and s.pairEpoch ~= W.epoch then W.w1off("epoch") end
            s.pairEpoch = s.pairEpoch or W.epoch
            -- Guest: the host's truth (outcomes, phases, clock + weather) after every pairing.
            if not W.host then W.tx(OP.RESYNC, W.nextRid(), 7) end
        end
        W.unpairedHooks[#W.unpairedHooks + 1] = function(reason)
            E.dropRequests(true)
            if reason == 2 then E.dropLedger("remote_mission") end
        end

        function E.tick(now)
            local s = E.s
            if W.w1 and (W.get(svars, "mis_isDefiniteGameOver") == true or W.get(mvars, "mis_isReserveGameOver") == true) then
                W.w1off("game_over")
            end
            if W.w1 then
                E.readbacks(now)
                for h, p in pairs(s.pending) do
                    local e = W.ent[h]
                    -- A fulton (FultonInfo, cause 5) reads FULTONED only once IsRecovered
                    -- turns true, seconds after the message: keep reading up to 20 s.
                    local fw = p.cause == 5 and now - p.at <= 20000
                    if W.host then
                        if (E.check(h, p.cause, false) and not (fw and e.committed ~= 6)) or now - p.at > (fw and 20000 or 1000) then
                            s.pending[h] = nil
                        end
                    else
                        local st = E.localState(e)
                        if st ~= nil and not (fw and st ~= 6) then
                            s.pending[h] = nil
                            if E.forward(h, st) then
                                if E.attributed(h, p.at) then E.request(h, st, p.cause, p.attack, now)
                                else
                                    W.stats.div = W.stats.div + 1
                                    L("event=divergence;ent=%.0f;state=%d;why=not_attributed", h, st)
                                end
                            end
                        elseif not fw and now - p.at > 1000 then s.pending[h] = nil end
                    end
                end
                if W.host then
                    local list, n, reads, scans = W.entOrder, #W.entOrder, 0, 0
                    while reads < 4 and scans < 64 and scans < n do
                        s.sweep, scans = s.sweep % n + 1, scans + 1
                        local h = list[s.sweep]
                        local e = W.ent[h]
                        if e.reached and e.committed ~= 0 and not TERMINAL[e.committed] and E.active(e.cp, h) then
                            reads = reads + 1
                            E.check(h, 9, true)
                        end
                    end
                else
                    for rid, q in pairs(s.req) do
                        if not q.received and now - q.sent > 3000 then
                            L("event=req_timeout;rid=%d;ent=%.0f", rid, q.h)
                            E.lost(rid, now)
                        end
                    end
                end
            end
            if W.host then E.hostReqs() end
            if W.w1 then
                -- Deferred entries (readback in flight) retry once per frame.
                local q, n, later = s.queue, 0, {}
                while n < 8 and s.qHead <= #q do
                    local a = q[s.qHead]
                    s.qHead, n = s.qHead + 1, n + 1
                    if E.applyOne(a[1], a[2], a[3], a[4], now) == "defer" then later[#later + 1] = a end
                end
                if s.qHead > #q then s.queue, s.qHead = later, 1
                else for _, a in ipairs(later) do q[#q + 1] = a end end
                E.recheck(now, false)
            end
        end

    end
end
