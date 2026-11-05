-- Authenticated friendly hits originate only in the native local-shooter
-- damage callback. Copied remote bullets stay visual; the real owner applies
-- one bounded event to its own health behind the exact mission/cycle gate.
do
    local W, floor, fmt = e.MgsCoopWorld, math.floor, string.format
    local D = W.downed
    -- Enemy sync M7 (soldier_brain, docs/world-sync-design.md 7.13): SOLDIER_HIT 76, a guard this copy owns hit the
    -- replica; the same queue and apply as FRIENDLY_HIT, never dropped by the friendly fire switch, and the receiver's
    -- copy of that guard engages it (world_snap.lua W.soldierEngaged).
    W.OP.SOLDIER_HIT = W.OP.SOLDIER_HIT or 76
    local U32, F = 4294967296, {}
    local function valid(n, lo, hi)
        return type(n) == "number" and n == floor(n) and n >= lo and n <= hi
    end
    local function newer(n, old)
        local delta = (n - old) % U32
        return old == 0 or delta > 0 and delta <= 2147483647
    end
    local function on() return D.hitEnabled() and W.inGame() end
    local function receipt(command, sequence, generation, damage, cause)
        local ok, r = pcall(W.receipt, fmt("return 'MGS_COOP_FRIENDLY_HIT_V1' -- %d %d %d %.0f %.0f %.0f %d",
            W.mission, W.epoch, command, sequence, generation, damage, cause))
        return ok and type(r) == "table" and r.ready == true and r.applied == true
    end
    function D.hitReset()
        F.sent, F.order, F.pending, F.seen = {}, {}, {}, 0
    end
    function D.hitPublished(v, now)
        F.sent[v[1]] = {generation = v[2], at = now}
        F.order[#F.order + 1] = v[1]
        if #F.order > 32 then F.sent[table.remove(F.order, 1)] = nil end
    end
    local function current(v, now)
        local r, sent = D.records[W.role], F.sent[v[3]]
        return r and r.ready and not r.down and r.generation == v[2] and sent and
            sent.generation == v[2] and now >= sent.at and now - sent.at <= 1500
    end
    -- Friendly fire off (debug menu, the host's switch for both copies: debug_arms.lua sets W.arms.ff): the
    -- partner's bullets, explosives and melee (all through this queue) are dropped at both ends.
    local function ffOff() local a = W.arms; return type(a) == "table" and a.ff == false end
    local function dropped(side, seq)
        local now = W.now()
        if F.dropLog and now >= F.dropLog and now - F.dropLog < 500 then return end
        F.dropLog = now
        W.log(fmt("event=friendly_fire;step=dropped;side=%s;seq=%.0f", side, seq))
    end
    local function admit(v, now, n)
        return on() and type(v) == "table" and #v == n and valid(v[1], 1, U32 - 1) and
            valid(v[2], 0, U32 - 1) and valid(v[3], 1, 65535) and
            valid(v[4], 0, 1000000000) and (v[5] == 0 or v[5] == 2) and newer(v[1], F.seen)
    end
    W.handlers[W.OP.FRIENDLY_HIT] = function(v, now)
        now = now or W.now()
        if not admit(v, now, 5) then return end
        F.seen = v[1]
        if ffOff() then dropped("target", v[1]); return end
        if not current(v, now) or #F.pending >= 8 then return end
        F.pending[#F.pending + 1] = {v = v, at = now}
    end
    W.handlers[W.OP.SOLDIER_HIT] = function(v, now)
        now = now or W.now()
        if not admit(v, now, 6) or not valid(v[6], 1, U32 - 1) then return end
        F.seen = v[1]
        W.log(fmt("event=soldier_hit;step=rx;seq=%.0f;attacker=%.0f;damage_milli=%.0f;cause=%d", v[1], v[6], v[4], v[5]))
        if W.soldierEngaged then pcall(W.soldierEngaged, v[6], now) end
        if not current(v, now) or #F.pending >= 8 then return end
        F.pending[#F.pending + 1] = {v = v, at = now, soldier = v[6]}
    end
    function D.hitTick(r, now)
        if not on() then return end
        if ffOff() then F.pending = {} end
        -- Preserve FIFO order across temporary input-window/native refusals.
        while #F.pending > 0 do
            local p = F.pending[1]
            if not current(p.v, now) or now < p.at or now - p.at > 500 then table.remove(F.pending, 1)
            elseif receipt(1, p.v[1], p.v[2], p.v[4], p.v[5]) then
                if p.soldier then
                    W.log(fmt("event=soldier_hit;step=applied;seq=%.0f;attacker=%.0f;damage_milli=%.0f;cause=%d", p.v[1], p.soldier,
                        p.v[4], p.v[5]))
                else
                    W.log(fmt("event=downed;step=friendly_applied;seq=%.0f;damage_milli=%.0f;cause=%d", p.v[1], p.v[4], p.v[5]))
                end
                table.remove(F.pending, 1)
            else break end
        end
        local seq, damage, cause = r.hitSequence, r.hitDamageMilli, r.hitCause
        if not valid(seq, 1, U32 - 1) or not valid(damage, 0, 1000000000) or (cause ~= 0 and cause ~= 2) then return end
        -- M7: a guard this copy owns hit the replica (hitAttacker its GameObjectId): SOLDIER_HIT with its entity hash.
        local attacker = r.hitAttacker
        if valid(attacker, 0, 65534) then
            local hash = W.byId and W.byId[attacker]
            local target = D.records[3 - W.role]
            if not hash then
                if receipt(0, seq, 0, 0, 0) then W.log(fmt("event=soldier_hit;step=dropped;seq=%.0f;reason=unmapped", seq)) end
                return
            end
            if not target or not target.ready or now < target.at or now - target.at > 500 then return end
            if target.down or W.tx(W.OP.SOLDIER_HIT, seq, target.generation, target.sequence, damage, cause, hash) then
                if receipt(0, seq, 0, 0, 0) then
                    W.log(fmt("event=soldier_hit;step=tx;seq=%.0f;attacker=%.0f;damage_milli=%.0f;cause=%d;down=%d", seq, hash, damage,
                        cause, target.down and 1 or 0))
                end
            end
            return
        end
        if ffOff() then
            if receipt(0, seq, 0, 0, 0) then dropped("shooter", seq) end
            return
        end
        local target = D.records[3 - W.role]
        if not target or not target.ready or now < target.at or now - target.at > 500 then return end
        if target.down or W.tx(W.OP.FRIENDLY_HIT, seq, target.generation, target.sequence, damage, cause) then
            receipt(0, seq, 0, 0, 0)
        end
    end
    D.hitReset()
end