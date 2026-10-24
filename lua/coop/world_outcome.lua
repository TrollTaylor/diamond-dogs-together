-- MGS co-op world sync, enemy outcome requests (original;
-- docs/world-sync-protocol.md "Enemy outcomes"): attribution, the guest's
-- requests, the host's request validation and the outcome wire handlers
-- (RECEIVED, REJECT, ENEMY_OUTCOME, REQ_OUTCOME). A request whose answer
-- cannot come any more (unpaired, timeout, REJECT 5/6/9) is held and sent
-- again at the next RESYNC_END while it is still forward of what is known.
do
    local W = e.MgsCoopWorld
    if type(W) == "table" and type(W.enemy) == "table" then
        local E, OP, H = W.enemy, W.OP, W.handlers
        local fmt, floor, max = string.format, math.floor, math.max
        local RANK, TERMINAL = E.RANK, E.TERMINAL
        local function L(...) W.log(fmt(...)) end
        -- IsRecovered only knows the mission's recovered-state list; any fultoned
        -- soldier reads ACTION_STATUS.FULTON_RECOVERD (live 2026-10-05).
        W.bindHooks[#W.bindHooks + 1] = function(a, fn)
            a.action = fn(TppEnemy, "TppEnemy", "GetActionStatus", true)
            local act = W.get(TppEnemy, "ACTION_STATUS")
            a.fultonRecovered = W.get(act, "FULTON_RECOVERD")
            a.holdupStand = W.get(act, "HOLD_UP_STAND")
            a.holdupCrawl = W.get(act, "HOLD_UP_CROWL")
            a.setDisable = fn(TppEnemy, "TppEnemy", "SetDisable", true)
        end

        -- Attribution of an attacker id. local: the local player, its horse or
        -- walker gear; buddy: D-Dog/Quiet.
        function W.attClass(att)
            local a = W.api
            if type(att) ~= "number" or att == a.null or not a.isLocal then return "none" end
            if a.isLocal(att) == true or (a.isHorse and a.isHorse(att) == true) or (a.isWalker and a.isWalker(att) == true) then
                return "local"
            end
            if a.typeIndex and (a.dog or a.quiet) then
                local t = a.typeIndex(att)
                if t ~= nil and (t == a.dog or t == a.quiet) then return "buddy" end
            end
            return "other"
        end
        function E.keep(q)
            local held = E.s.held
            if #held < 256 then held[#held + 1] = q end
        end
        function E.request(h, st, cause, attack, now)
            local s, e = E.s, W.ent[h]
            local q = {h = h, st = st, cause = cause, attack = attack, sent = now}
            if not W.paired then return E.keep(q) end
            local rid = W.nextRid()
            local x, y, z = E.pos(e.id)
            if W.tx(OP.REQ_OUTCOME, rid, h, 1, st, e.kver, cause, attack, 0, x, y, z) then
                s.req[rid], s.out[h] = q, st
                W.stats.reqs = W.stats.reqs + 1
                L("event=req;rid=%d;ent=%.0f;state=%d;seen=%d", rid, h, st, e.kver)
            else E.keep(q) end
        end
        -- A request whose answer will not come (timeout, REJECT 5/6/9): held,
        -- and a RESYNC 1 (at most once per 10 s) brings the RESYNC_END.
        function E.lost(rid, now)
            local s = E.s
            local q = s.req[rid]
            if not q then return end
            s.req[rid], s.out[q.h] = nil, nil
            E.keep(q)
            if now - (s.timeoutAt or -1e9) >= 10000 then
                s.timeoutAt = now
                W.tx(OP.RESYNC, W.nextRid(), 1)
            end
        end
        function E.forward(h, st)
            local out = E.s.out[h]
            return RANK[st] > max(RANK[W.ent[h].known], out and RANK[out] or 0)
        end
        function E.dropRequests(keep)
            local s, n = E.s, 0
            for _, q in pairs(s.req) do
                n = n + 1
                if keep then E.keep(q) end
            end
            s.req, s.out = {}, {}
            if n > 0 then W.log((keep and "event=req_hold;n=" or "event=req_drop;n=") .. n) end
        end
        -- Host: guest requests, validated after the host's own pending reads (C32).
        function E.hostReqs()
            local s = E.s
            for i = 1, #s.hostReq do
                local v = s.hostReq[i]
                local rid, h, st = v[1], v[2], v[4]
                local e = W.ent[h]
                local reason
                if not W.w1 then reason = 11
                elseif not e then reason = 1
                elseif not W.rosterOk then reason = 7
                elseif TERMINAL[e.committed] then reason = 2
                elseif RANK[st] <= RANK[e.committed] then reason = 3 end
                if reason then
                    W.tx(OP.REJECT, rid, OP.REQ_OUTCOME, reason, h, e and e.committed or 0, e and e.ver or 0)
                    W.log(fmt("event=req_rx;rid=%d;ent=%.0f;state=%d;result=reject;reason=%d", rid, h, st, reason))
                else
                    local cur = E.localState(e)
                    if cur ~= nil and RANK[cur] > RANK[st] then st = cur end
                    E.commit(h, st, 1, rid, 6, 0)
                    E.apply(h, st, e.ver, "commit")
                end
            end
            s.hostReq = {}
        end
        -- Wire handlers.
        H[OP.RECEIVED] = function(v)
            local q = v[2] == OP.REQ_OUTCOME and E.s.req[v[1]]
            if q then q.received = true end
        end
        H[OP.REJECT] = function(v, now)
            local s, r = E.s, v[3]
            local q = v[2] == OP.REQ_OUTCOME and s.req[v[1]]
            if q and (r == 5 or r == 6 or r == 9) then E.lost(v[1], now)
            elseif q then s.req[v[1]], s.out[q.h] = nil, nil end
            W.log(fmt("event=req_done;rid=%d;result=reject;reason=%d;op=%d;state=%d;ver=%d", v[1], v[3], v[2], v[5], v[6]))
            if v[2] == OP.REQ_OUTCOME and (v[3] == 2 or v[3] == 3) then E.apply(v[4], v[5], v[6], "reject") end
        end
        H[OP.OUTCOME] = function(v)
            local s = E.s
            local q = v[7] ~= 0 and s.req[v[7]]
            if q then
                s.req[v[7]], s.out[q.h] = nil, nil
                W.log(fmt("event=req_done;rid=%d;ms=%d;result=outcome", v[7], floor(W.now() - q.sent)))
            end
            if v[8] % 2 == 0 then s.seen = (s.seen + 1) % 65536 end
            E.apply(v[1], v[3], v[4], v[8] % 2 == 1 and "resync" or "outcome")
        end
        H[OP.REQ_OUTCOME] = function(v)
            local q = E.s.hostReq
            q[#q + 1] = v
        end
    end
end
