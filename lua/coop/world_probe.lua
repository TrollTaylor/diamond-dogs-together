-- MGS co-op world sync, lab probes (original; docs/world-sync-protocol.md
-- "Probes"). Lab only (CFG mode 1; native never schedules one in play). A
-- probe runs once as a coroutine stepped from the tick on wall-clock delays
-- and logs event=probe;id=<name>;step=...; its failure ends only the probe.
do
    local W = e.MgsCoopWorld
    if type(W) == "table" then
        local R, B, OP, H = {}, {}, W.OP, W.handlers
        W.probe = R
        R.handlers = B
        local fmt, floor = string.format, math.floor
        local NAMES = {"p1", "p3", "p3b", "p4", "p12", "p17", "race_kill", "remote_area_kill", "kill_near", "sleep_near",
            "fulton_near", "alert_near", "weather_rain", "clock_jump", "clock_fast"}
        R.NAMES = NAMES
        local function log(name, step, rest)
            W.log(fmt("event=probe;id=%s;step=%s", name, tostring(step)) .. (rest and (";" .. rest) or ""))
        end
        local function done(pr, result, rest) log(pr.name, "done", "result=" .. result .. (rest and (";" .. rest) or "")) end
        local function refuse(pr, reason) done(pr, "refused", "reason=" .. reason) end
        -- Yields until `ms` from now (0: the next frame).
        local function wait(pr, ms)
            pr.wake = W.now() + ms
            coroutine.yield()
        end
        local function life(e) return W.api.life(e.id) end
        local function send(e, cmd) return W.api.send(e.id, cmd) end
        function R.schedule(id, at)
            local name = NAMES[id]
            if not name then return end
            if W.mode ~= 1 then log(name, "done", "result=refused;reason=mode"); return end
            if W.pr then log(name, "done", "result=refused;reason=busy"); return end
            W.pr = {id = id, name = name, at = at, frames = 0}
        end
        -- The nearest readable NORMAL soldier in an active CP (positions of active CPs only).
        function R.nearest(pred)
            local px, py, pz = vars.playerPosX, vars.playerPosY, vars.playerPosZ
            local best, bestD
            for _, h in ipairs(W.entOrder) do
                local e = W.ent[h]
                if W.enemy.active(e.cp) and (not pred or pred(e)) and W.enemy.localState(e) == 0 then
                    local p = send(e, {id = "GetPosition"})
                    if p then
                        local dx, dy, dz = p:GetX() - px, p:GetY() - py, p:GetZ() - pz
                        local d = dx * dx + dy * dy + dz * dz
                        if not bestD or d < bestD then best, bestD = h, d end
                    end
                end
            end
            return best
        end
        function R.areaMap()
            local map, st = {}, W.areaState
            if not st.enemy then return nil end
            for k, g in pairs(st.enemy.cpList) do for name in pairs(W.area.set(g)) do map[name] = k end end
            return map
        end
        local function needSoldier(pr)
            if not W.w1ok then refuse(pr, "level"); return nil end
            local h = R.nearest()
            if not h then refuse(pr, "mission") end
            return h
        end
        -- The first soldier (entOrder) of a CP in an area that is neither local nor the partner's.
        local function elsewhere(map, ok)
            local st = W.areaState
            for _, h in ipairs(W.entOrder) do
                local e = W.ent[h]
                local a = map[W.cp[e.cp].name]
                if a and a ~= st.localArea and a ~= st.partner and (not ok or ok(e)) then return h, a end
            end
        end
        -- Host, paired at eff >= 2, in a mission with areas: the area map, else refused.
        local function hostAreas(pr)
            local map = R.areaMap()
            if not W.host then return refuse(pr, "role") end
            if not map or not W.w1 then return refuse(pr, "mission") end
            if not W.paired or W.eff < 2 then return refuse(pr, "paired") end
            return map
        end
        local function reads(pr, e, t0, list, step, extra)
            for _, ms in ipairs(list) do
                wait(pr, t0 + ms - W.now())
                local s = W.get(TppEnemy, "GetStatus")
                log(pr.name, step, fmt("ms=%d;life=%s", floor(W.now() - t0), tostring(life(e))) .. (extra or "") ..
                    (step == "read" and (";status=" .. tostring(s and s(e.id))) or ""))
            end
        end

        B.p1 = function(pr)
            if #W.cpOrder == 0 or not W.w1ok then return refuse(pr, "mission") end
            local P = W.phaseConst
            log("p1", "consts", fmt("sneak=%s;caution=%s;evasion=%s;alert=%s;enemy=0,1,2,3", tostring(P[0]), tostring(P[1]),
                tostring(P[2]), tostring(P[3])))
            local h = R.nearest(function(e) return W.cp[e.cp].localPhase == 0 end)
            if not h then return refuse(pr, "mission") end
            local ch = W.ent[h].cp
            local c = W.cp[ch]
            pr.seen = {}
            W.api.send(c.id, {id = "SetPhase", phase = P[1]})
            wait(pr, 0)
            local f1 = W.phaseWire[W.api.getPhase(c.name)]
            wait(pr, 2000)
            local t2 = W.phaseWire[W.api.getPhase(c.name)]
            log("p1", "result", fmt("cp=%.0f;before=0;set=1;f1=%s;t2s=%s;msg=%d", ch, tostring(f1), tostring(t2), pr.seen[ch] and 1 or 0))
            done(pr, "ok")
        end
        B.p3 = function(pr)
            local L = W.lifeConst
            log(pr.name, "consts", fmt("dead=%s;dying=%s;sleep=%s;faint=%s;ls_normal=0;ls_dead=1;ls_dying=2;ls_sleep=3;ls_faint=4",
                tostring(L[5]), tostring(L[4]), tostring(L[2]), tostring(L[3])))
            local h = needSoldier(pr)
            if not h then return end
            local e, t0 = W.ent[h], W.now()
            pr.watch = h
            log(pr.name, "set", fmt("ent=%.0f;state=%s", h, tostring(L[3])))
            send(e, {id = "ChangeLifeState", state = L[3]})
            reads(pr, e, t0, {0, 1000, 5000}, "read")
            if pr.name == "p3b" then
                wait(pr, t0 + 6000 - W.now())
                send(e, {id = "RecoveryLife"})
                reads(pr, e, t0, {7000, 10000}, "wake")
            end
            done(pr, "ok", fmt("ent=%.0f", h))
        end
        B.p3b = B.p3
        B.p4 = function(pr)
            local st, map = W.areaState, R.areaMap()
            if not map or not st.localArea or not W.w1ok then return refuse(pr, "mission") end
            local h, area = elsewhere(map)
            if not h then return refuse(pr, "mission") end
            local e, t0 = W.ent[h], W.now()
            log("p4", "start", fmt("ent=%.0f;area=%s;local=%s;life=%s", h, tostring(area), tostring(st.localArea), tostring(life(e))))
            send(e, {id = "ChangeLifeState", state = W.lifeConst[5]})
            reads(pr, e, t0, {0, 1000}, "read", ";phase=out")
            W.area.simulate(area)
            reads(pr, e, t0, {2000, 5000}, "read", ";phase=active")
            W.area.simulate(st.localArea)
            done(pr, "ok")
        end
        B.p12 = function(pr)
            local isHost = W.get(TppNetworkUtil, "IsHost")
            local okH, host = pcall(isHost)
            local r = {pcall(W.get(TppMission, "GetSyncMissionStatus"))}
            log("p12", "result", fmt("is_host=%s;sync=%s,%s,%s,%s", tostring(okH and host), tostring(r[2]), tostring(r[3]),
                tostring(r[4]), tostring(r[5])))
            done(pr, "ok")
        end
        B.p17 = function(pr)
            local last, n = W.now(), 0
            pr.frames = 0
            while n < 60 do
                wait(pr, 0)
                local now = W.now()
                if now - last >= 1000 then
                    n = n + 1
                    log("p17", "sec", fmt("n=%d;frames=%d;dt_ms=%d", n, pr.frames, floor(now - last)))
                    pr.frames, last = 0, now
                end
            end
            done(pr, "ok")
        end
        B.race_kill = function(pr)
            local map = hostAreas(pr)
            if not map then return end
            local h = R.nearest(function(e) return map[W.cp[e.cp].name] == W.areaState.localArea end)
            if not h then return refuse(pr, "mission") end
            W.tx(OP.PROBE_SYNC, 7, 1, h, 3000)
            log("race_kill", "start", fmt("ent=%.0f;delay_ms=3000", h))
            wait(pr, 6000)
            done(pr, "ok", fmt("ent=%.0f", h))
        end
        B.remote_area_kill = function(pr)
            local map = hostAreas(pr)
            if not map then return end
            local h, area = elsewhere(map, function(e) return e.committed == 0 and W.enemy.localState(e) == 0 end)
            if not h then return refuse(pr, "mission") end
            log("remote_area_kill", "start", fmt("area=%d;ent=%.0f", area, h))
            W.enemy.commit(h, 5, 2, 0, 7, 0)
            pcall(send, W.ent[h], {id = "ChangeLifeState", state = W.lifeConst[5]})
            wait(pr, 5000)
            W.tx(OP.PROBE_SYNC, 8, 2, h, area)
            log("remote_area_kill", 2, fmt("ent=%.0f;area=%d", h, area))
            wait(pr, 5000)
            W.tx(OP.PROBE_SYNC, 8, 3, h, 0)
            log("remote_area_kill", 3, fmt("ent=%.0f", h))
            done(pr, "ok")
        end
        local function near(pr, cmd, wire)
            if not W.paired or W.eff < 2 then return refuse(pr, "level") end
            local h = needSoldier(pr)
            if not h then return end
            W.attribOverride[h] = W.now()
            send(W.ent[h], wire and {id = cmd, state = W.lifeConst[wire]} or {id = cmd})
            done(pr, "ok", fmt("ent=%.0f", h))
        end
        B.kill_near = function(pr) near(pr, "ChangeLifeState", 5) end
        B.sleep_near = function(pr) near(pr, "ChangeLifeState", 2) end
        B.fulton_near = function(pr) near(pr, "RequestForceFulton") end
        B.alert_near = function(pr)
            if W.host then return refuse(pr, "role") end
            if not W.paired or W.eff < 2 or not W.w1 then return refuse(pr, "level") end
            local h = R.nearest()
            if not h then return refuse(pr, "mission") end
            local ch = W.ent[h].cp
            local c, t0 = W.cp[ch], W.now()
            log("alert_near", "start", fmt("cp=%.0f", ch))
            while W.now() - t0 < 60000 do
                if c.localPhase < 3 then W.api.send(c.id, {id = "SetPhase", phase = W.phaseConst[3]}) end
                wait(pr, 5000)
            end
            done(pr, "ok", fmt("cp=%.0f", ch))
        end
        local function w2(pr)
            if not W.host then refuse(pr, "role"); return false end
            if not (W.env and W.env.clockOn() and W.paired) then refuse(pr, "level"); return false end
            return true
        end
        B.weather_rain = function(pr)
            if not w2(pr) then return end
            if not W.env.weatherOn() then return refuse(pr, "mission") end
            TppWeather.ForceRequestWeather(TppDefine.WEATHER.RAINY, 5)
            W.env.s.probeForced = true
            done(pr, "ok", "type=" .. tostring(TppDefine.WEATHER.RAINY))
        end
        B.clock_jump = function(pr)
            if not w2(pr) then return end
            TppClock.AddTime(3600)
            done(pr, "ok", "add_s=3600")
        end
        B.clock_fast = function(pr)
            if not w2(pr) then return end
            local t0 = W.now()
            while W.now() - t0 < 10000 do
                vars.clock = (floor(vars.clock) + 300) % 86400
                wait(pr, 250)
            end
            done(pr, "ok")
        end

        H[OP.PROBE] = function(v, now) R.schedule(v[1], math.max(now + 1000, W.t0 + 10000)) end
        H[OP.PROBE_FIRE] = function(v)
            local name, h = NAMES[v[1]], v[3]
            if W.mode ~= 1 or not name then return end
            log(name, name == "race_kill" and "fire" or v[2], fmt("ent=%.0f;arg=%d", h, v[4]))
            local e = W.ent[h]
            if name == "race_kill" and e and W.w1ok then
                if not W.host then W.attribOverride[h] = W.now() end
                send(e, {id = "ChangeLifeState", state = W.lifeConst[5]})
            elseif name == "remote_area_kill" and not W.host then
                W.area.simulate(v[2] == 2 and v[4] or W.areaState.localArea)
            end
        end
        W.observers[#W.observers + 1] = function(kind, h, cls)
            local pr = W.pr
            if not pr then return end
            if kind == "ChangePhase" and pr.seen then pr.seen[h] = true end
            if pr.watch == h and (kind == "Dead" or kind == "Neutralize" or kind == "Unconscious") then
                log(pr.name, "msg", fmt("m=%s;att=%s", kind, cls == "none" and "null" or cls))
            end
        end

        function R.tick(now)
            local pr = W.pr
            if pr then
                pr.frames = pr.frames + 1
                if not pr.co and now >= pr.at then pr.co, pr.wake = coroutine.create(B[pr.name]), now end
                if pr.co and now >= pr.wake then
                    local ok, err = coroutine.resume(pr.co, pr)
                    if not ok then
                        W.pr = nil
                        done(pr, "fail", "msg=" .. string.sub(tostring(err), 1, 80))
                    elseif coroutine.status(pr.co) == "dead" then
                        W.pr = nil
                    end
                end
            end
        end
    end
end
