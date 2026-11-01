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
            -- Mother Base staff (staffPlace): the host's own act on a standing staff member says where first.
            if actor == 0 and e.committed == 0 and st ~= 0 and W.staffAnnounce and W.paired and W.eff >= 2 then
                pcall(W.staffAnnounce, h)
            end
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
            -- Mother Base staff (staffPlace): this copy is placed where the host's is first (world_place_guest.lua).
            if st ~= 0 and src == "outcome" and W.staffHold and W.staffHold(h, now or W.now()) then return "defer" end
            local cur = E.localState(e)
            if cur == nil then return E.hold(h, st, ver, src, "unreadable") end
            -- Mother Base (spareStaff): a partner never kills or removes a staff member in this copy's save (stock
            -- f30050 reserves TARGET_DEAD on any staff Dead or Dying): dying and dead land as a knockout; a Fulton
            -- disables the staff member for this mission load (TppEnemy.SetDisable: gone, no Fulton message).
            if W.feature and W.feature.spareStaff and (st == 4 or st == 5 or st == 6) and not TERMINAL[cur] then
                local as = st == 6 and 7 or 3
                L("event=spare;ent=%.0f;state=%d;as=%d", h, st, as)
                st = as
            end
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
                -- r1101 fulton (W.fulton below): the lift in the Fultoner's look, visual only on this copy.
                if W.fultonOn and W.fulton then pcall(W.fulton.before, 1, e.id, h) end
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
                -- Mother Base (allCp): no area change re-checks the ledger, so every 10 s all of it
                -- (an apply that failed while the staff member's platform was not loaded here).
                local all = W.feature and W.feature.allCp and now >= (s.allAt or 0)
                if all then s.allAt = now + 10000 end
                E.recheck(now, all)
            end
            local F = W.fulton
            if F and F.restoreAt and now >= F.restoreAt then F.restore() end
        end

        -- r1101 fulton (lab key fulton_sync, CFG r[18] W.fultonOn; docs/world-sync-protocol.md section 25): FULTON 0x46
        -- {seq, kind 1 soldier / 2 vehicle, id, entity hash, wormhole}. The Fultoner's copy announces its own Fulton at
        -- FultonInfo (wormhole: its EQP_IT_Fulton_WormHole level >= 1, the stock test; a vehicle: its type and xyz mm
        -- there, 255 and 0, 0, 0 unknown). The other copy's lift (W1's RequestForceFulton above; a vehicle's
        -- Seize {"Fulton"} in world_vehicle_rules.lua) takes that look only with fulton_look (r[18] bit 4: the level is
        -- global, so this copy's own Fultons in that window use it too): this copy's level for HOLD_MS, restored after
        -- and never saved. A soldier's lift is visual only when the partner announced it: this copy's
        -- TppTerminal.OnFulton is skipped once for it (the Fultoner keeps the recruit); with no FULTON from the partner
        -- by then (an older partner, or its key off) the stock credit runs (D1). Vehicles: world_vehicle_rules.lua.
        local FU = {}
        W.fulton = FU
        OP.FULTON = OP.FULTON or 70
        local NOTE_MS, HOLD_MS, SKIP_MS = 60000, 5000, 30000
        function FU.reset() FU.seq, FU.note, FU.lift, FU.skip, FU.real, FU.want, FU.restoreAt = 0, {}, {}, {}, nil, nil, nil end
        FU.reset()
        function FU.on() return W.fultonOn == true and W.active == true end
        local function equip() return W.get(TppEquip, "EQP_IT_Fulton_WormHole") end
        local function level()
            local f, q = W.get(Player, "GetItemLevel"), equip()
            if type(f) ~= "function" or type(q) ~= "number" then return nil end
            local ok, v = pcall(f, q)
            if ok and type(v) == "number" then return v end
        end
        local function setLevel(n)
            local f, q = W.get(Player, "SetItemLevel"), equip()
            if type(f) ~= "function" or type(q) ~= "number" then return false end
            return (pcall(f, q, n))
        end
        -- This copy's own look: the real level while a partner's look is shown.
        function FU.own()
            if FU.real ~= nil then return FU.real end
            return level()
        end
        function FU.restore()
            FU.restoreAt = nil
            local r = FU.real
            if r == nil then return end
            FU.real, FU.want = nil, nil
            L("event=fulton;step=restore;level=%d;ok=%d", r, setLevel(r) and 1 or 0)
        end
        -- The two wraps (once per binding: a reloaded TppTerminal / TppSave is wrapped again).
        function FU.install()
            local T, S = W.get(_G, "TppTerminal") or TppTerminal, W.get(_G, "TppSave") or TppSave
            local o = W.get(T, "OnFulton")
            local t1 = type(o) == "function" and o == FU.wrap
            if type(o) == "function" and not t1 then
                FU.wrap = function(...)
                    local id = ...
                    local k = type(id) == "number" and FU.skip[id]
                    if k then
                        FU.skip[id] = nil
                        local t = W.now()
                        local n = FU.note[k.h]
                        if t - k.at <= SKIP_MS and n and t - n.at <= NOTE_MS then
                            L("event=fulton;step=credit_skipped;id=%d;why=partner_lift", id)
                            return
                        end
                        if t - k.at <= SKIP_MS then L("event=fulton;step=credit_kept;id=%d;why=no_partner_note", id) end
                    end
                    return o(...)
                end
                t1 = pcall(function() T.OnFulton = FU.wrap end) and W.get(T, "OnFulton") == FU.wrap
            end
            -- Log only: the stock staff add, counted (the roster check: the Fultoner's copy only).
            local add = W.get(T, "AddTempStaffFulton")
            if type(add) == "function" and add ~= FU.addWrap then
                FU.addWrap = function(...)
                    FU.adds = (FU.adds or 0) + 1
                    if FU.adds <= 20 then L("event=fulton;step=staff_add;n=%d", FU.adds) end
                    return add(...)
                end
                pcall(function() T.AddTempStaffFulton = FU.addWrap end)
            end
            local save = W.get(S, "SaveVarsToSlot")
            local t2 = type(save) == "function" and save == FU.saveWrap
            if type(save) == "function" and not t2 then
                FU.saveWrap = function(...)
                    local real, want = FU.real, FU.want
                    if real ~= nil then setLevel(real) end
                    local r = {pcall(save, ...)}
                    if real ~= nil and FU.real == real and want ~= nil then setLevel(want) end
                    if not r[1] then error(r[2], 0) end
                    return unpack(r, 2)
                end
                t2 = pcall(function() S.SaveVarsToSlot = FU.saveWrap end) and W.get(S, "SaveVarsToSlot") == FU.saveWrap
            end
            if not FU.logged then FU.logged = true; L("event=fulton;step=install;terminal=%d;save=%d", t1 and 1 or 0, t2 and 1 or 0) end
        end
        W.startHooks[#W.startHooks + 1] = function()
            FU.restore()
            FU.reset()
            if W.fultonOn then pcall(FU.install) end
        end
        W.stopHooks[#W.stopHooks + 1] = function() FU.restore() end
        -- The Fultoner (W.observe: this copy's FultonInfo, att local).
        function FU.observe(h, id)
            if not FU.on() or not W.paired or type(id) ~= "number" then return end
            local t, V = W.now(), W.vehicleGone
            local l = FU.lift[id]
            if l and t - l <= SKIP_MS then return end
            local kind = h and 1 or (type(V) == "table" and type(V.typed) == "function" and V.typed(id) and 2)
            if not kind then return end
            local lv = FU.own()
            local worm = type(lv) == "number" and lv >= 1 and 1 or 0
            local vt, x, y, z = 255, 0, 0, 0
            if kind == 2 then
                local okp, px, py, pz = pcall(V.pose, id)
                local okt, pt = pcall(V.vtype, id)
                if okp and type(px) == "number" and type(py) == "number" and type(pz) == "number" then x, y, z = floor(px), floor(py), floor(pz) end
                if okt and type(pt) == "number" and pt >= 0 and pt <= 255 then vt = pt end
            end
            FU.seq = FU.seq % 65535 + 1
            local ok = W.tx(OP.FULTON, FU.seq, kind, id, h or 0, worm, vt, x, y, z)
            L("event=fulton;step=tx;kind=%d;id=%d;ent=%.0f;worm=%d;sent=%d", kind, id, h or 0, worm, ok and 1 or 0)
        end
        W.handlers[OP.FULTON] = function(v)
            if not FU.on() or type(v) ~= "table" then return end
            local seq, kind, id, h, worm = v[1], v[2], v[3], v[4], v[5] == 1 and 1 or 0
            if (kind ~= 1 and kind ~= 2) or type(id) ~= "number" or type(seq) ~= "number" then return end
            FU.note[kind == 1 and h or id] = {worm = worm, at = W.now()}
            L("event=fulton;step=rx;seq=%d;kind=%d;id=%d;ent=%.0f;worm=%d", seq, kind, id, h or 0, worm)
            -- A vehicle: this copy's lift at once (world_vehicle_rules.lua G.early, key fulton_early), at the Fultoner's
            -- type and pose (an older frame has none: no early lift there), not after VEHICLE_GONE's readback.
            local V = W.vehicleGone
            if kind == 2 and type(V) == "table" and type(V.early) == "function" then pcall(V.early, id, v[6], v[7], v[8], v[9]) end
        end
        -- The receiver, just before its lift (kind 1: soldier id + entity hash; kind 2: vehicle id).
        function FU.before(kind, id, h)
            if not FU.on() or type(id) ~= "number" then return end
            local t = W.now()
            local n = FU.note[kind == 1 and h or id]
            if n and t - n.at > NOTE_MS then n = nil end
            FU.lift[id] = t
            if kind == 1 then
                -- Decided at OnFulton (about 3.7 s later): skipped only if the partner's FULTON has arrived by then.
                FU.skip[id] = {at = t, h = h}
                pcall(FU.install)
            end
            local own, set = FU.own(), 0
            local mine = type(own) == "number" and own >= 1 and 1 or 0
            if n and W.fultonLookOn == true and type(own) == "number" and mine ~= n.worm then
                local want = n.worm == 1 and 1 or 0
                if FU.real == nil then FU.real = own end
                if setLevel(want) then set, FU.want = 1, want end
            end
            if FU.real ~= nil then FU.restoreAt = t + HOLD_MS end
            L("event=fulton;step=lift;kind=%d;id=%d;ent=%.0f;worm=%s;own=%d;set=%d;credit=%s", kind, id, h or 0,
                n and tostring(n.worm) or "own", mine, set, kind == 1 and "0" or "vr")
        end

    end
end
