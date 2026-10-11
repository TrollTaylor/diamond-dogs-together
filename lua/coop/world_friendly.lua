-- Authenticated friendly hits originate only in the native local-shooter
-- damage callback. Copied remote bullets stay visual; the real owner applies
-- one bounded event to its own health behind the exact mission/cycle gate.
do
    local W, floor, fmt = e.MgsCoopWorld, math.floor, string.format
    local D = W.downed
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
    W.handlers[W.OP.FRIENDLY_HIT] = function(v, now)
        now = now or W.now()
        if not on() or type(v) ~= "table" or #v ~= 5 or not valid(v[1], 1, U32 - 1) or
            not valid(v[2], 0, U32 - 1) or not valid(v[3], 1, 65535) or
            not valid(v[4], 0, 1000000000) or (v[5] ~= 0 and v[5] ~= 2) or
            not newer(v[1], F.seen) then return end
        F.seen = v[1]
        if not current(v, now) or #F.pending >= 8 then return end
        F.pending[#F.pending + 1] = {v = v, at = now}
    end
    function D.hitTick(r, now)
        if not on() then return end
        -- Preserve FIFO order across temporary input-window/native refusals.
        while #F.pending > 0 do
            local p = F.pending[1]
            if not current(p.v, now) or now < p.at or now - p.at > 500 then table.remove(F.pending, 1)
            elseif receipt(1, p.v[1], p.v[2], p.v[4], p.v[5]) then
                W.log(fmt("event=downed;step=friendly_applied;seq=%.0f;damage_milli=%.0f;cause=%d", p.v[1], p.v[4], p.v[5]))
                table.remove(F.pending, 1)
            else break end
        end
        local seq, damage, cause = r.hitSequence, r.hitDamageMilli, r.hitCause
        if not valid(seq, 1, U32 - 1) or not valid(damage, 0, 1000000000) or (cause ~= 0 and cause ~= 2) then return end
        local target = D.records[3 - W.role]
        if not target or not target.ready or now < target.at or now - target.at > 500 then return end
        if target.down or W.tx(W.OP.FRIENDLY_HIT, seq, target.generation, target.sequence, damage, cause) then
            receipt(0, seq, 0, 0, 0)
        end
    end
    D.hitReset()
end