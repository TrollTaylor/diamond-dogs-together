-- Offline W4 checkpoint alignment. Host reads a bounded initial sweep; the
-- guest stays inactive until a complete same-epoch snapshot is validated.
-- Unreadable soldiers are explicit unknowns, discovered when their area loads.
-- Before any free-roam area trap the host infers its area (A.infer).
do
    local W = e.MgsCoopWorld
    local A, OP, H = {serial = 0}, W.OP, W.handlers
    W.align = A
    local floor, fmt, max = math.floor, string.format, math.max
    local function log(text) W.log("event=align;" .. text) end
    local function on()
        return A.enabled and W.active and W.level >= 3 and W.feature and W.feature.enemies and W.w1ok and W.rosterOk and
            #W.entOrder > 0 and #W.cpOrder > 0
    end
    local function valid(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
    local function digest(d, r)
        for i = 2, 7 do d = (d * 31 + r[i]) % 4294967296 end
        return d
    end
    local function read(e)
        if e.id == W.api.null then return nil end
        local act = W.activeCp or W.inferredCp
        if not act or act[e.cp] ~= true then
            local ok, recovered = pcall(W.api.recovered, e.name)
            return ok and recovered == true and 6 or nil
        end
        local ok, recovered = pcall(W.api.recovered, e.name)
        if ok and recovered == true then return 6 end
        if W.api.action and W.api.fultonRecovered ~= nil then
            local ok, action = pcall(W.api.action, e.id)
            if ok and action == W.api.fultonRecovered then return 6 end
        end
        local ok, raw = pcall(W.api.life, e.id)
        if not ok or type(raw) ~= "number" then return nil end
        if raw == 0 then return 0 end
        return W.lifeWire[raw]
    end
    function A.install(cfg)
        A.enabled = cfg and cfg[1] >= 3 or false
    end
    -- No trap_FreePlay_areaNN crossed yet: the snapshot reads the area holding the CP
    -- of the nearest soldier with a readable position. Bookkeeping only (no original
    -- call, no SetOutOfArea, W.activeCp untouched); a real trap replaces it.
    if W.area then
        local install = W.area.install
        W.area.install = function(subScripts, name)
            local t = W.get(subScripts, "enemy")
            A.areaEnemy = type(t) == "table" and t or rawget(_G, name)
            return install(subScripts, name)
        end
    end
    function A.infer()
        local enemy = A.areaEnemy
        local list = type(enemy) == "table" and W.get(enemy, "cpList")
        if W.areaState.localArea ~= nil or W.activeCp ~= nil or type(list) ~= "table" or not W.area then return end
        local x, z = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
        if type(x) ~= "number" or type(z) ~= "number" then return end
        local areaOf, best, bd = {}, nil, nil
        for k, g in pairs(list) do for name in pairs(W.area.set(g)) do if areaOf[name] == nil then areaOf[name] = k end end end
        for _, h in ipairs(W.entOrder) do
            local e = W.ent[h]
            local k = areaOf[W.cp[e.cp].name]
            if k ~= nil and e.id ~= W.api.null then
                local ok, p = pcall(W.api.send, e.id, {id = "GetPosition"})
                local good, px, pz = false, nil, nil
                if ok and (type(p) == "userdata" or type(p) == "table") then good, px, pz = pcall(function() return p:GetX(), p:GetZ() end) end
                if good and type(px) == "number" and type(pz) == "number" and px == px and pz == pz then
                    local d = (px - x) ^ 2 + (pz - z) ^ 2
                    if not bd or d < bd then best, bd = k, d end
                end
            end
        end
        if best == nil then log("step=area;inferred=none"); return end
        local act = {[W.h("quest_cp")] = true}
        for name in pairs(W.area.set(list[best])) do act[W.h(name)] = true end
        W.inferredCp, W.inferredArea = act, best
        W.log(fmt("event=area;inferred=%s;nearest_m=%.0f", tostring(best), math.sqrt(bd)))
    end
    function A.reset()
        A.tx, A.rx, A.ack, A.unknown, A.discovery, A.pairEpoch = nil, nil, nil, {}, 0, nil
        W.inferredCp, W.inferredArea = nil, nil
        A.dead = false
        A.baseW1 = W.w1 and W.api.isStart and W.api.isStart() == true
        if A.enabled and W.active and W.level >= 3 and W.feature and W.feature.enemies then
            W.w1 = false; log("step=wait;reason=unpaired")
        end
    end
    W.startHooks[#W.startHooks + 1] = A.reset
    W.stopHooks[#W.stopHooks + 1] = function() A.tx, A.rx, A.ack = nil, nil, nil end
    W.unpairedHooks[#W.unpairedHooks + 1] = function()
        A.tx, A.rx, A.ack, A.pairEpoch = nil, nil, nil, nil
        if on() then W.w1 = false end
    end
    function A.start()
        if not on() or not W.paired then return end
        if W.eff < 3 then W.w1 = A.baseW1 and W.eff >= 2 or false; return end
        if A.pairEpoch == W.epoch or A.dead then return end
        A.pairEpoch, W.w1 = W.epoch, false
        if W.host then
            A.infer()
            A.serial = A.serial % 4294967295 + 1
            A.tx = {id = A.serial, mission = W.mission, epoch = W.epoch, at = W.now(), records = {},
                scan = 1, cp = 1, sent = 1, digest = 0, soldiers = #W.entOrder, cps = #W.cpOrder}
            A.unknown = {}
            log(fmt("step=scan;id=%.0f;epoch=%d;soldiers=%d;cps=%d", A.serial, W.epoch, #W.entOrder, #W.cpOrder))
        else log("step=wait;reason=host_snapshot") end
    end
    W.pairedHooks[#W.pairedHooks + 1] = A.start
    H[OP.ALIGN_BEGIN] = function(v, now)
        if W.host or not on() or not W.paired or W.eff < 3 or v[2] ~= W.mission or v[3] ~= W.roster or
            v[4] ~= #W.entOrder or v[5] ~= #W.cpOrder or v[6] ~= W.epoch or not valid(v[1], 1, 4294967295) then return end
        if A.rx and A.rx.id == v[1] then return end
        W.w1 = false
        A.rx = {id = v[1], epoch = v[6], at = now or W.now(), soldiers = v[4], cps = v[5], records = {},
            seen = {}, ns = 0, nc = 0, digest = 0, invalid = false}
        log(fmt("step=begin;id=%.0f;epoch=%d", v[1], v[6]))
    end
    H[OP.ALIGN_RECORD] = function(v)
        local r = A.rx
        if W.host or not on() or not r or r.id ~= v[1] or r.epoch ~= W.epoch then return end
        local order = v[2] == 1 and W.entOrder or v[2] == 2 and W.cpOrder
        if not order or not valid(v[3], 1, #order) or order[v[3]] ~= v[4] or not valid(v[5], 0, 1) or
            not valid(v[6], 0, v[2] == 1 and 7 or 3) or not valid(v[7], 1, 65535) then r.invalid = true; return end
        local key = v[2] .. ":" .. v[3]
        if r.seen[key] then
            for i = 2, 7 do if r.seen[key][i] ~= v[i] then r.invalid = true end end
            return
        end
        r.seen[key], r.records[#r.records + 1] = v, v
        if v[2] == 1 then r.ns = r.ns + 1 else r.nc = r.nc + 1 end
        r.digest = digest(r.digest, v)
    end
    H[OP.ALIGN_END] = function(v)
        local r = A.rx
        if W.host or not on() or A.dead or not r or r.id ~= v[1] or r.epoch ~= W.epoch or v[6] ~= W.epoch then return end
        local readable = 0
        for _, a in ipairs(r.records) do if a[2] == 1 and a[5] == 1 then readable = readable + 1 end end
        local complete = not r.invalid and r.ns == r.soldiers and r.nc == r.cps and v[2] == r.ns and v[3] == r.nc and v[4] == r.digest
        if not complete or readable == 0 then
            log(fmt("step=refused;reason=%s;id=%.0f;soldiers=%d/%d;cps=%d/%d;readable=%d;invalid=%d;digest_match=%d",
                complete and "no_readable_state" or "incomplete_snapshot", r.id, r.ns, r.soldiers, r.nc, r.cps, readable,
                r.invalid and 1 or 0, v[4] == r.digest and 1 or 0))
            A.ack = {r.id, W.mission, W.epoch, 0}
            A.rx = nil
            return
        end
        -- The complete snapshot authorizes W1; all actual game commands use
        -- the existing forward-only apply/readback/ledger and phase floors.
        W.w1 = true
        local unknown, diverged = 0, 0
        for _, a in ipairs(r.records) do
            if a[2] == 1 then
                if a[5] == 1 then
                    local ent, st = W.ent[a[4]], a[6]
                    local cur = read(ent)
                    if cur and W.enemy.TERMINAL[cur] and W.enemy.RANK[cur] > W.enemy.RANK[st] then
                        diverged = diverged + 1
                        log(fmt("step=divergence;ent=%.0f;local=%d;host=%d;action=no_revive", a[4], cur, st))
                    end
                    W.enemy.apply(a[4], st, a[7], "resync")
                else unknown = unknown + 1 end
            elseif a[5] == 1 and W.phase then
                H[OP.PHASE]({a[4], a[6], a[7], 0, 0, 1})
            end
        end
        W.enemy.s.seen = v[5]
        A.ack = {r.id, W.mission, W.epoch, 1}
        A.rx = nil
        log(fmt("step=accepted;id=%.0f;soldiers=%d;cps=%d;unknown=%d;divergence=%d;w1=1", r.id, r.ns, r.nc, unknown, diverged))
    end
    H[OP.ALIGN_ACK] = function(v)
        local t = A.tx
        if not W.host or not on() or not t or v[1] ~= t.id or v[2] ~= W.mission or v[3] ~= W.epoch then return end
        if v[4] == 1 and t.ended then
            W.w1, A.tx = true, nil
            log(fmt("step=ready;id=%.0f;w1=1", t.id))
        elseif v[4] == 0 then
            A.pairEpoch, A.tx = nil, nil
            log("step=retry;reason=guest_refused")
        end
    end
    function A.capture(t)
        for _ = 1, 8 do
            local h = W.entOrder[t.scan]
            if h then
                local ent, st = W.ent[h], read(W.ent[h])
                if st ~= nil then
                    if ent.ver == 0 or ent.committed ~= st then W.enemy.commit(h, st, 0, 0, 8, 1) end
                end
                -- Re-read after ACK too: combat may change while capturing.
                A.unknown[h] = true
                local r = {t.id, 1, t.scan, h, st ~= nil and 1 or 0, st or 0, max(ent.ver, 1)}
                t.records[#t.records + 1], t.digest = r, digest(t.digest, r)
                t.scan = t.scan + 1
            else
                local cp = W.cpOrder[t.cp]
                if not cp then t.captured, t.seq = true, W.enemy.s.seq; return end
                local ok, raw = pcall(W.api.getPhase, W.cp[cp].name)
                local phase = ok and W.phaseWire[raw] or nil
                local readable = valid(phase, 0, 3)
                if readable then W.phase.onLocal(cp, raw) end
                local r = {t.id, 2, t.cp, cp, readable and 1 or 0, readable and phase or 0, max(W.cp[cp].ver, 1)}
                t.records[#t.records + 1], t.digest = r, digest(t.digest, r)
                t.cp = t.cp + 1
            end
        end
    end
    function A.tick(now)
        if W.recovery then W.recovery.tick(now) end
        if not on() or not W.paired or W.eff < 3 then return end
        if W.host and not A.tx and not W.w1 and A.pairEpoch == nil then A.start() end
        if A.rx and now - A.rx.at > 30000 then
            A.ack, A.rx = {A.rx.id, W.mission, W.epoch, 0}, nil
            log("step=timeout;reason=snapshot;w1=0")
        end
        if A.ack and W.tx(OP.ALIGN_ACK, unpack(A.ack)) then A.ack = nil end
        local t = A.tx
        if t then
            if not t.begun then
                t.begun = W.tx(OP.ALIGN_BEGIN, t.id, t.mission, W.roster, t.soldiers, t.cps)
                if not t.begun then return end
            end
            if not t.captured then A.capture(t); return end
            for _ = 1, 8 do
                local r = t.records[t.sent]
                if not r then break end
                if not W.tx(OP.ALIGN_RECORD, unpack(r)) then return end
                t.sent = t.sent + 1
            end
            if t.sent > #t.records and not t.ended then
                t.ended = W.tx(OP.ALIGN_END, t.id, t.soldiers, t.cps, t.digest, t.seq)
                if t.ended then t.at = now end
            end
            if t.ended and now - t.at > 30000 then
                A.tx, A.pairEpoch = nil, nil
                log("step=timeout;reason=ack;w1=0")
            end
            return
        end
        if W.host and W.w1 then
            -- Unknown initial state is queried only when its area activates.
            for _ = 1, math.min(4, #W.entOrder) do
                A.discovery = A.discovery % #W.entOrder + 1
                local h = W.entOrder[A.discovery]
                if A.unknown[h] then
                    local ent, st = W.ent[h], read(W.ent[h])
                    if st ~= nil then
                        if ent.ver == 0 or ent.committed ~= st then W.enemy.commit(h, st, 0, 0, 8, 1) end
                        A.unknown[h] = nil
                        log(fmt("step=discovered;ent=%.0f;state=%d", h, st))
                    end
                end
            end
        end
    end
    if W.follow then
        W.follow.installHooks = W.follow.installHooks or {}
        W.follow.installHooks[#W.follow.installHooks + 1] = A.install
    end
end
