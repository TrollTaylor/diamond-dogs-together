-- MGS co-op world sync, out-of-area union and alert (original;
-- docs/world-sync-protocol.md "Area union", "Alert"). Free roam sets every CP
-- outside the local player's area out of area; while paired, the wrapped
-- UpdateOutOfAreaSetting keeps the partner's area active too (the original
-- runs once with cpList[area] replaced by the union, so nothing toggles).
-- Alert syncs escalations only: the host broadcasts its upward transitions,
-- the guest requests the ones it raised itself (ownPhase), and the host's
-- floor follows the guest's own phases only (no mutual ALERT latch), and only
-- while paired and reported in the last 5 s (PHASE_VEC once a second or
-- REQ_PHASE). Unpairing clears the guest's phases and area. Phase wire
-- values: 0 SNEAK, 1 CAUTION, 2 EVASION, 3 ALERT (TppGameObject.PHASE_*).
do
    local W = e.MgsCoopWorld
    if type(W) == "table" then
        local A, P, OP, H = {}, {}, W.OP, W.handlers
        W.area, W.phase = A, P
        local fmt, floor, max, min = string.format, math.floor, math.max, math.min

        -- W1 bindings (enemy outcomes and alert): life/phase constants and attribution
        -- (W.attClass is in world_outcome.lua).
        W.bindHooks[#W.bindHooks + 1] = function(a, fn, num, missing)
            local before = #missing
            a.life = fn(TppEnemy, "TppEnemy", "GetLifeStatus")
            a.recovered = fn(TppEnemy, "TppEnemy", "IsRecovered")
            a.getPhase = fn(TppEnemy, "TppEnemy", "GetPhase")
            a.setOut = fn(TppEnemy, "TppEnemy", "SetOutOfArea")
            a.isLocal = fn(Tpp, "Tpp", "IsLocalPlayer")
            a.isHorse = fn(Tpp, "Tpp", "IsHorse", true)
            a.isWalker = fn(Tpp, "Tpp", "IsPlayerWalkerGear", true)
            a.typeIndex = fn(GameObject, "GameObject", "GetTypeIndex", true)
            a.dog = num(TppGameObject, "TppGameObject", "GAME_OBJECT_TYPE_BUDDYDOG2", true)
            a.quiet = num(TppGameObject, "TppGameObject", "GAME_OBJECT_TYPE_BUDDYQUIET2", true)
            local L, P = {}, {}
            for i, k in ipairs({"DEAD", "DYING", "SLEEP", "FAINT"}) do L[i] = num(TppGameObject, "TppGameObject", "NPC_LIFE_STATE_" .. k) end
            for i, k in ipairs({"SNEAK", "CAUTION", "EVASION", "ALERT"}) do P[i] = num(TppGameObject, "TppGameObject", "PHASE_" .. k) end
            W.w1ok = #missing == before
            W.lifeWire, W.lifeConst, W.phaseWire, W.phaseConst = {}, {}, {}, {}
            if not W.w1ok then return end
            local wires, bad = {5, 4, 2, 3}, false
            for i = 1, 4 do
                local c = L[i]
                if c ~= floor(c) or c < 1 or c > 4 or W.lifeWire[c] then bad = true end
                W.lifeWire[c], W.lifeConst[wires[i]] = wires[i], c
                if W.phaseWire[P[i]] then W.w1ok = false end
                W.phaseWire[P[i]], W.phaseConst[i - 1] = i - 1, P[i]
            end
            if bad then
                W.w1ok = false
                W.log(fmt("event=inactive;reason=bad_const;dead=%s;dying=%s;sleep=%s;faint=%s", tostring(L[1]), tostring(L[2]),
                    tostring(L[3]), tostring(L[4])))
            end
        end

        -- A CP group as a set of names, from either a keyed set or an array.
        function A.set(group, log)
            local s, shape = {}, "set"
            if type(group) ~= "table" then return s end
            if #group > 0 then
                shape = "array"
                for _, n in ipairs(group) do s[n] = true end
            else
                for k in pairs(group) do if type(k) == "string" then s[k] = true end end
            end
            if log and not W.areaState.shape then
                W.areaState.shape = shape
                W.log("event=area;shape=" .. shape)
            end
            return s
        end
        function A.install(subScripts, name)
            local enemy = W.get(subScripts, "enemy")
            for i = 1, 2 do
                local t = i == 1 and enemy or rawget(_G, name)
                if type(t) == "table" and not W.areaWrapped[t] and type(t.UpdateOutOfAreaSetting) == "function" then
                    local original = t.UpdateOutOfAreaSetting
                    W.areaWrapped[t] = true
                    t.UpdateOutOfAreaSetting = function(currentArea, ...)
                        local st = W.areaState
                        if type(st) == "table" then st.localArea, st.enemy, st.original = currentArea, t, original end
                        return A.call(t, original, currentArea, false, ...)
                    end
                end
            end
        end
        -- The union for `area`, or nil (then the original runs unchanged).
        function A.union(enemy, area)
            local partner = W.areaState.partner
            if not (W.isStarted and W.active and W.paired and W.eff >= 2 and W.w1 and partner and partner ~= area) then return nil end
            local list = enemy.cpList
            local u = A.set(list[area], true)
            for name in pairs(A.set(list[partner])) do u[name] = true end
            return u
        end
        function A.after(enemy, area, simulate, union, ...)
            pcall(A.update, enemy, area, simulate, union)
            return ...
        end
        function A.call(enemy, original, area, simulate, ...)
            local ok, union = pcall(A.union, enemy, area)
            if not ok or not union then return A.after(enemy, area, simulate, nil, original(area, ...)) end
            local list = enemy.cpList
            local saved = list[area]
            list[area] = union
            local r = {pcall(original, area, ...)}
            list[area] = saved
            if not r[1] then error(r[2], 0) end
            return A.after(enemy, area, simulate, union, unpack(r, 2, table.maxn(r)))
        end
        -- Active CPs = the area set (or union) plus quest_cp; ledger re-checked.
        function A.update(enemy, area, simulate, union)
            if not W.isStarted or not W.active then return end
            local act = {[W.h("quest_cp")] = true}
            for name in pairs(union or A.set(enemy.cpList[area], true)) do act[W.h(name)] = true end
            W.activeCp = act
            if not simulate then P.areaDirty = true end
            if W.enemy then W.enemy.areaChanged() end
        end
        function A.simulate(area)
            local st = W.areaState
            if st.enemy and st.original and area then A.call(st.enemy, st.original, area, true) end
        end
        function A.areaOf(name)
            local st = W.areaState
            if not st.enemy then return nil end
            for k, g in pairs(st.enemy.cpList) do if A.set(g)[name] then return k end end
            return nil
        end

        function P.reset()
            P.echo, P.reqs, P.lastReq, P.floorAt, P.floorSet, P.vecAt, P.rr, P.areaDirty = {}, {}, {}, {}, {}, {}, 0, false
            P.areaAt, P.recall, P.floorLog, P.vecRxAt, P.vecOff = nil, false, nil, nil, #W.cpOrder > 192
            if P.vecOff then W.log("event=phase_vec;disabled=1;cps=" .. #W.cpOrder) end
            W.activeCp = nil
            local st = W.areaState
            if st.enemy and st.localArea then A.update(st.enemy, st.localArea, true, nil) end
        end
        W.startHooks[#W.startHooks + 1] = P.reset
        W.pairedHooks[#W.pairedHooks + 1] = function() P.areaDirty = W.areaState.localArea ~= nil end
        -- Unpaired (rerun now) or stopped: the guest's phases and the partner's
        -- area stop counting; the original reruns for the local area alone.
        function P.forget(rerun)
            for _, c in pairs(W.cp or {}) do c.guestOwn = 0 end
            local st = W.areaState
            if not st.partner then return end
            st.partner = nil
            if rerun then pcall(A.simulate, st.localArea) else P.recall = true end
        end
        W.unpairedHooks[#W.unpairedHooks + 1] = function() P.forget(true) end
        W.stopHooks[#W.stopHooks + 1] = function() P.forget(false) end
        function P.txPhase(h, flags, rid)
            local c = W.cp[h]
            if c.ver == 0 then c.ver = 1 end
            return W.tx(OP.PHASE, h, c.localPhase, c.ver, 0, rid or 0, flags)
        end
        function P.setPhase(c, wire) W.api.send(c.id, {id = "SetPhase", phase = W.phaseConst[wire]}) end
        -- A local phase reading (ChangePhase message or GetPhase refresh).
        function P.onLocal(h, raw)
            local wire = W.phaseWire[raw]
            if wire == nil then return end
            local c = W.cp[h]
            local old = c.localPhase
            c.localPhase = wire
            if not W.w1 or wire == old then return end
            local now = W.now()
            if wire < old then
                if not W.host then c.own = min(c.own, wire) end
            elseif W.host then
                c.ver = c.ver % 65535 + 1
                local pe, rid = P.echo[h], 0
                if pe and wire >= pe.phase and now <= pe.untilT then rid, P.echo[h] = pe.rid, nil end
                local src = rid > 0 and "req" or (P.floorSet[h] and "floor" or "host")
                P.floorSet[h] = nil
                if W.paired and W.eff >= 2 then W.tx(OP.PHASE, h, wire, c.ver, 0, rid, 0) end
                W.log(fmt("event=phase;cp=%.0f;phase=%d;ver=%d;src=%s;rid=%d", h, wire, c.ver, src, rid))
            elseif wire > c.hostPhase then
                c.own = wire
                local lr = P.lastReq[h]
                if (not lr or now - lr.at > 30000 or wire > lr.phase) and W.paired and W.eff >= 2 then
                    local rid = W.nextRid()
                    local x, y, z = W.get(vars, "playerPosX") or 0, W.get(vars, "playerPosY") or 0, W.get(vars, "playerPosZ") or 0
                    local sh = W.enemy and W.enemy.s and W.enemy.s.lastByCp and W.enemy.s.lastByCp[h] or 0
                    if W.tx(OP.REQ_PHASE, rid, h, wire, 0, sh, floor(x * 1000 + 0.5), floor(y * 1000 + 0.5), floor(z * 1000 + 0.5)) then
                        P.lastReq[h] = {phase = wire, at = now}
                        W.log(fmt("event=req_phase;rid=%d;cp=%.0f;phase=%d", rid, h, wire))
                    end
                end
            end
        end
        function P.refresh(h)
            local c = W.cp[h]
            local raw = W.api.getPhase(c.name)
            if raw ~= nil then P.onLocal(h, raw) end
            return c.localPhase
        end
        -- One PHASE_VEC: 2 bits per CP, 16 CPs per u32 (arithmetic, never bit ops).
        function P.pack(field)
            local order, words = W.cpOrder, {}
            local n = #order
            for k = 1, 12 do
                local w, mul = 0, 1
                for i = 1, 16 do
                    local h = order[(k - 1) * 16 + i]
                    if h then w = w + W.cp[h][field] * mul end
                    mul = mul * 4
                end
                words[k] = w
            end
            return n, floor((n + 15) / 16), words
        end
        function P.txVector()
            if P.vecOff or #W.cpOrder == 0 or not W.w1 then return end
            local n, nw, w = P.pack(W.host and "localPhase" or "own")
            W.tx(OP.PVEC, n, 0, nw, w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12])
        end
        H[OP.PVEC] = function(v, now)
            if not W.w1 or v[1] ~= #W.cpOrder then return end
            if W.host then P.vecRxAt = now end
            for i, h in ipairs(W.cpOrder) do
                local k = floor((i - 1) / 16)
                local p = floor(v[4 + k] / 4 ^ ((i - 1) % 16)) % 4
                local c = W.cp[h]
                if W.host then
                    c.guestOwn = p
                elseif p ~= c.hostPhase or p > c.localPhase then
                    c.hostPhase = p
                    if p > c.localPhase and now - (P.vecAt[h] or -1e9) >= 5000 then
                        P.vecAt[h] = now
                        P.setPhase(c, p)
                    end
                end
            end
        end
        H[OP.PHASE] = function(v)
            local c = W.w1 and W.cp[v[1]]
            if not c then return end
            c.hostPhase = max(c.hostPhase, v[2])
            local applied = c.localPhase < v[2]
            if applied then P.setPhase(c, v[2]) end
            W.log(fmt("event=phase;cp=%.0f;phase=%d;ver=%d;src=rx;rid=%d;applied=%d", v[1], v[2], v[3], v[5], applied and 1 or 0))
        end
        H[OP.REQ_PHASE] = function(v)
            P.reqs[#P.reqs + 1] = v
        end
        H[OP.AREA] = function(v)
            if v[2] ~= W.mission then return end
            W.areaState.partner = v[1] > 0 and v[1] or nil
            P.recall = true
        end
        function P.tick(now)
            local st = W.areaState
            if P.recall and st.localArea and st.enemy and now - (P.areaAt or -1e9) >= 2000 then
                P.recall, P.areaAt = false, now
                A.call(st.enemy, st.original, st.localArea, true)
            end
            if P.areaDirty and W.paired and W.eff >= 2 and st.localArea and now - (P.areaTx or -1e9) >= 2000 then
                P.areaDirty, P.areaTx = false, now
                W.tx(OP.AREA, st.localArea, W.mission)
            end
            if W.host then
                for i = 1, #P.reqs do
                    local v = P.reqs[i]
                    local rid, h, req, sh = v[1], v[2], v[3], v[5]
                    local c = W.cp[h]
                    if not W.w1 or not c then
                        W.tx(OP.REJECT, rid, OP.REQ_PHASE, W.w1 and 1 or 11, h, 0, 0)
                    else
                        local cur = P.refresh(h)
                        if req > cur then
                            P.setPhase(c, req)
                            P.echo[h] = {phase = req, rid = rid, untilT = now + 3000, at = now}
                        else
                            c.ver = c.ver % 65535 + 1
                            W.tx(OP.PHASE, h, cur, c.ver, 0, rid, 0)
                            W.log(fmt("event=phase;cp=%.0f;phase=%d;ver=%d;src=req;rid=%d", h, cur, c.ver, rid))
                        end
                        if req == 3 and sh and sh > 0 and W.ent[sh] then
                            pcall(W.api.send, W.ent[sh].id, {id = "SetDiscoveryPlayer"})
                        end
                        c.guestOwn, c.guestAt = max(c.guestOwn, req), now
                    end
                end
                P.reqs = {}
                for h, pe in pairs(P.echo) do
                    local cur = now - pe.at >= 500 and P.refresh(h)
                    if cur and P.echo[h] then
                        P.echo[h] = nil
                        local c = W.cp[h]
                        c.ver = c.ver % 65535 + 1
                        W.tx(OP.PHASE, h, cur, c.ver, 0, pe.rid, 0)
                        W.log(fmt("event=phase;cp=%.0f;phase=%d;ver=%d;src=req;rid=%d;readback=1", h, cur, c.ver, pe.rid))
                    end
                end
            end
            if not W.w1 then return end
            local order = W.cpOrder
            for _ = 1, min(4, #order) do
                P.rr = P.rr % #order + 1
                P.refresh(order[P.rr])
            end
            if W.host and W.paired and W.eff >= 2 then
                local logIt, vecAt = now - (P.floorLog or -1e9) >= 10000, P.vecRxAt or -1e9
                for _, h in ipairs(order) do
                    local c = W.cp[h]
                    if c.guestOwn > 0 and now - max(vecAt, c.guestAt or -1e9) > 5000 then c.guestOwn = 0 end
                    if c.guestOwn > 0 then
                        if c.localPhase < c.guestOwn and now - (P.floorAt[h] or -1e9) >= 10000 then
                            P.floorAt[h], P.floorSet[h] = now, true
                            P.setPhase(c, c.guestOwn)
                        end
                        if logIt then
                            P.floorLog = now
                            W.log(fmt("event=floor;cp=%.0f;host=%d;guest=%d", h, c.localPhase, c.guestOwn))
                        end
                    end
                end
            end
        end
    end
end
