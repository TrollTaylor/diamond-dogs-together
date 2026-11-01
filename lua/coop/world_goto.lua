-- MGS co-op world sync, the lab goto (original; docs/world-sync-protocol.md
-- "Lab goto"): both lab copies go from Mother Base (30050) to free roam
-- (30010/30020) without menus, through the game's own calls in two hops.
-- Step 1 (30050): f30050_sequence.ReserveMissionClear(), the heli boarding
-- (SetNextMBDemo clears a queued MB demo, then ReserveMissionClear
-- RIDE_ON_HELICOPTER -> MTBS_HELI). Step 2 (ACC 40050): the sortie's own
-- SelectNextMissionHeliStartRoute and OnEndMissionPreparation(CURRENT); it does
-- not wait for pairing: the follow sortie hold decides (step 2 stays pending
-- while held, 150 s cap). Each
-- step is reported with GOTO_ACK (2 waiting, 1 reserved, 0 refused). Lab only:
-- native arms it only from world.trigger in lab mode, Lua refuses it unless
-- the CFG mode is 1.
do
    local W = e.MgsCoopWorld
    if type(W) == "table" then
        local G, OP, H = {}, W.OP, W.handlers
        W.go = G
        local fmt = string.format
        -- Landing zones as "lz|rt" (route hash = Fox.StrCode32 of the whole string).
        local LZ = {[30010] = {"lz_drp_field_N0000|rt_drp_field_N_0000", "lz_drp_enemyBase_S0000|rt_drp_enemyBase_S_0000"},
            [30020] = {"lz_drp_savannahEast_N0000|rt_drp_savannahEast_N_0000", "lz_drp_pfCampNorth_S0000|rt_drp_pfCampNorth_S_0000"},
            -- fb1031 arrival: the Command platform 1 approach route (f30050 DROP_ROUTE_USE_SORTIE_DEMO), both lz values.
            [30050] = {"ly003_cl00_30050_heli0000|cl00pl0_mb_fndt_plnt_heli_30050|rt_apr", "ly003_cl00_30050_heli0000|cl00pl0_mb_fndt_plnt_heli_30050|rt_apr"}}
        -- CFG is metadata only. Legacy harnesses have no session field;
        -- known sessions refuse delayed work after a reconnect/link loss.
        local function session()
            if type(W.cfg) ~= "function" then return nil end
            local c = W.cfg(vars.missionCode, 0)
            if type(c) ~= "table" or c[8] == nil then return nil end
            return c[4] == 1 and type(c[8]) == "number" and c[8] > 0 and c[8] or 0
        end
        H[OP.GOTO] = function(v, now)
            if W.mode ~= 1 then
                W.tx(OP.GOTO_ACK, v[1], 0)
                W.log(fmt("event=goto;step=%d;result=refused;reason=mode", v[1]))
                return
            end
            W.gt = {step = v[1], target = v[2], lz = v[3], since = now, session = session()}
            W.log(fmt("event=goto;step=%d;target=%d;lz=%d;result=armed", v[1], v[2], v[3]))
        end
        local function ack(g, result, why)
            W.tx(OP.GOTO_ACK, g.step, result)
            W.log(fmt("event=goto;step=%d;result=%s", g.step, why))
        end
        function G.tick(now)
            local g = W.gt
            if not g then return end
            local age = now - g.since
            local live = session()
            -- Step 2 held by the follow sortie hold (at most 120 s) gets 150 s.
            local why = age < 0 and "clock_reset" or age >= (g.step == 1 and 30000 or g.held and 150000 or 60000) and "timeout"
                or g.session ~= nil and (g.session == 0 or live ~= g.session) and "session"
            if why then
                -- ACK has no request ID. A late refusal ACK could cancel a
                -- newer native request; native owns its own timeout receipt.
                if W.gt == g then W.gt = nil end
                W.log(fmt("event=goto;step=%d;result=cancelled;reason=%s", g.step, why))
                return
            end
            if g.check then
                -- The follow wrapper holds the sortie until the partner can come or it gives up.
                if W.follow and W.follow.hostDeploy then
                    if not g.held then g.held = true; ack(g, 2, "waiting;reason=held") end
                    return
                end
                W.gt = nil
                local ok = W.get(svars, "mis_isDefiniteMissionClear") == true
                return ack(g, ok and 1 or 0, ok and "reserved" or "refused;reason=readback")
            end
            if g.step == 1 then
                if W.inGame() then
                    local f = W.get(f30050_sequence, "ReserveMissionClear")
                    if vars.missionCode ~= 30050 then
                        -- Free roam: the helicopter ride to this location's ACC.
                        TppMission.ReserveMissionClearOnRideOnHelicopter()
                        f = nil
                    elseif type(f) == "function" then
                        f()
                    else
                        TppDemo.SetNextMBDemo()
                        TppMission.ReserveMissionClear{missionClearType = TppDefine.MISSION_CLEAR_TYPE.RIDE_ON_HELICOPTER,
                            nextMissionId = TppDefine.SYS_MISSION_ID.MTBS_HELI}
                    end
                    g.check = true
                    W.log("event=goto;step=1;call=" .. (vars.missionCode ~= 30050 and "ride_heli" or type(f) == "function" and "f30050_sequence" or "fallback"))
                elseif not g.waiting then
                    g.waiting = true
                    ack(g, 2, "waiting")
                end
                return
            end
            local seq = W.get(TppSequence, "GetCurrentSequenceName")
            if type(seq) == "function" and seq() == "Seq_Game_MainGame" and W.inGame() and now - W.t0 >= 3000 then
                if TppMission.IsHelicopterSpace(vars.missionCode) ~= true or W.get(gvars, "mis_nextMissionCodeForEmergency") ~= 0 then
                    W.gt = nil
                    return ack(g, 0, "refused;reason=not_heli_space")
                end
                local route = W.api.str32(LZ[g.target][g.lz + 1])
                TppMission.SelectNextMissionHeliStartRoute(g.target, route, false)
                W.log(fmt("event=goto;step=2;next=%.0f;route=%.0f;want=%.0f", W.get(gvars, "mis_nextMissionCodeForMissionClear") or -1,
                    W.get(gvars, "heli_missionStartRoute") or -1, route))
                -- arrival-prevent repro (lz 1 into Mother Base): the 0.14.5 host's stored helicopter start position over
                -- the Command platform 1 pad (the iDroid MbDvcActHeliLandStartPos call), which its Reserve writes as start.
                local store = W.get(TppMission, "SetHelicopterMissionStartPosition")
                if g.target == 30050 and g.lz == 1 and type(store) == "function" then
                    store(1, 9.116, 8.75, -42.43)
                    W.log("event=goto;step=2;heli_start_pos=9116,8750,-42430")
                end
                TppMission.OnEndMissionPreparation(TppClock.DEPLOY_TIME.CURRENT, nil)
                g.check = true
            elseif not g.waiting then
                g.waiting = true
                ack(g, 2, "waiting")
            end
        end
    end
end
-- Debug menu Travel and Bring partner here (play and lab; docs/debug-menu.md "Travel",
-- docs/world-sync-protocol.md section 22). The lab goto's and the test boot's stock calls
-- without their lab gates: a free roam leaves through its helicopter clear (Mother Base:
-- f30050's own), the ACC sorties on foot (route 0), the target's ReservePlayerLoadingPosition
-- gets TppPlayer.SetInitialPosition and loads skip the tips key. The host leads (world follow
-- carries a paired guest); a guest's Go to asks the host (TRAVEL 1). Bring partner here (TRAVEL
-- 3): the partner warps beside the caller, or travels to the caller's mission and lands there.
do
    local W = e.MgsCoopWorld
    if type(W) == "table" then
        local T, OP, H = {seq = 0}, W.OP, W.handlers
        W.travel = T
        OP.TRAVEL = OP.TRAVEL or 59
        local fmt, floor, sqrt, sin, cos, rad = string.format, math.floor, math.sqrt, math.sin, math.cos, math.rad
        -- Seed-proven outdoor spots (afgh-ledge, africa-jeep, mb-ladder): x y z yaw (degrees).
        local SPOT = {[30010] = {828.18, 338.20, 1630.51, 95}, [30020] = {-656.01, 1.20, 877.73, 62}, [30050] = {22.61, 1.80, -15.55, 0}}
        local ACC = {[40010] = true, [40020] = true, [40050] = true}
        -- Answer reasons 1..8 (0 accepted; 7 native's Travel-off answer).
        local WHY = {"travelling", "gameplay", "on_foot", "downed", "demo", "mission", "off", "other_area"}
        local function log(s) W.log("event=travel;" .. s) end
        local function note(b, m) if type(W.note) == "function" then pcall(W.note, 14, m or 0, b) end end
        local function mission() return W.get(vars, "missionCode") or 0 end
        local function yes(t, k, ...)
            local f = W.get(t, k)
            if type(f) ~= "function" then return false end
            local ok, v = pcall(f, ...)
            return ok and v == true
        end
        local function pose()
            local p = {W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ"), W.get(vars, "playerRotY")}
            for i = 1, 4 do if type(p[i]) ~= "number" or p[i] ~= p[i] then return nil end end
            return p
        end
        local function onFoot()
            return yes(PlayerInfo, "OrCheckStatus", {W.get(PlayerStatus, "STAND"), W.get(PlayerStatus, "SQUAT"), W.get(PlayerStatus, "CRAWL")})
        end
        local function linked()
            local p = type(W.peer) == "function" and W.peer()
            return W.active == true and (W.level or 0) >= 3 and type(p) == "table" and p.linked == true and (p.level or 0) >= 3
        end
        -- The world follow guard lets this copy's own call through (F.token, as F.run), restored after.
        local function bypass(f, ...)
            local F = W.follow
            local token = F and F.token
            if F then F.token = true end
            local ok, err = pcall(f, ...)
            if F then F.token = token end
            return ok, err
        end
        -- The ACC's main game (no sortie prep or other menu sequence); true when unreadable.
        local function mainGame()
            local f = W.get(TppSequence, "GetCurrentSequenceName")
            if type(f) ~= "function" then return true end
            local ok, s = pcall(f)
            return not ok or s == "Seq_Game_MainGame"
        end
        -- Why this copy cannot travel or be brought now (nil: it can).
        function T.blocked()
            if T.job then return 1 end
            local m = mission()
            if not SPOT[m] and not ACC[m] then return 6 end
            if not yes(TppMission, "CheckMissionState") or (ACC[m] and not mainGame()) or yes(TppUiCommand, "IsMbDvcTerminalOpened") then
                return 2
            end
            if yes(DemoDaemon, "IsDemoPlaying") then return 5 end
            if SPOT[m] then
                local D = W.downed
                if D and type(D.hud) == "function" then
                    local ok, h = pcall(D.hud, W.now())
                    if ok and type(h) == "table" and h.down then return 4 end
                end
                if not onFoot() then return 3 end
            end
        end
        -- The leader lands on the spot; a follower `ahead` m in front of it, facing back (0.5 m up).
        local function landing(p, lead, ahead)
            if lead then return {p[1], p[2] + 0.5, p[3], p[4]} end
            local y = rad(p[4])
            return {p[1] + ahead * sin(y), p[2] + 0.5, p[3] + ahead * cos(y), (p[4] + 180) % 360}
        end
        local function tx(kind, m, p, reason)
            T.seq = T.seq % 65535 + 1
            p = p or {0, 0, 0, 0}
            local function mm(v) return floor(v * 1000 + 0.5) end
            return W.tx(OP.TRAVEL, T.seq, kind, m, mm(p[1]), mm(p[2]), mm(p[3]), floor((p[4] % 360) * 1000 + 0.5), reason or 0) == true
        end
        function T.start(target, spot, lead, by)
            T.job = {target = target, spot = spot, lead = lead, by = by, at = W.now(), from = mission()}
            log(fmt("step=start;target=%d;from=%d;lead=%d;by=%s;x=%.2f;y=%.2f;z=%.2f;yaw=%.1f", target, mission(), lead and 1 or 0, by,
                spot[1], spot[2], spot[3], spot[4]))
            if target ~= mission() then note(1, target) end
        end
        function T.stop(why)
            local j = T.job
            T.job = nil
            log(fmt("step=stopped;reason=%s;target=%d;mission=%d", why, j and j.target or 0, mission()))
            note(6, j and j.target or 0)
        end
        -- Menu: travel <30010|30020|30050> and bring_partner (debug_player.lua hands them here).
        function T.command(a)
            if a[1] == "travel" and #a == 2 then
                local target = tonumber(a[2])
                if not SPOT[target] then return false, "travel_target" end
                local b = T.blocked()
                if b then return false, "travel_refused;reason=" .. WHY[b] end
                if not W.host and linked() then
                    T.asked = target
                    if not tx(1, target, nil, 0) then return false, "travel_refused;reason=link" end
                    log(fmt("step=ask;target=%d", target))
                    return true, fmt("travel_asked;target=%d", target)
                end
                local live = linked()
                T.start(target, landing(SPOT[target], true), true, "self")
                if live then tx(2, target, SPOT[target], 0) end
                return true, fmt("travel_started;target=%d;linked=%d", target, live and 1 or 0)
            elseif a[1] == "bring_partner" and #a == 1 then
                if not linked() then return false, "partner_not_linked" end
                local b = T.blocked()
                if b then return false, "bring_refused;reason=" .. WHY[b] end
                -- fb1031-on: never run across missions; the partner elsewhere is told to use Go to first.
                local q = W.peer()
                local pm = type(q) == "table" and q.mission
                if type(pm) == "number" and pm ~= 0 and pm ~= mission() then
                    note(7, mission())
                    log(fmt("step=bring_other_area;mission=%d;partner=%d", mission(), pm))
                    return false, "bring_refused;reason=other_area"
                end
                local p = pose()
                if not p then return false, "position_unreadable" end
                if not tx(3, mission(), p, 0) then return false, "bring_refused;reason=link" end
                T.bring = W.now()
                note(4, mission())
                log(fmt("step=bring;mission=%d;x=%.2f;y=%.2f;z=%.2f", mission(), p[1], p[2], p[3]))
                return true, fmt("bring_sent;mission=%d", mission())
            end
            return false, "command_args"
        end
        -- The host leads a travel (its own press, or a guest's ask); the guest hears where it lands.
        local function lead(target, by)
            if not SPOT[target] then return 6 end
            local b = T.blocked()
            if b then return b end
            T.start(target, landing(SPOT[target], true), true, by)
            tx(2, target, SPOT[target], 0)
        end
        -- The stock camera behind the player's yaw (degrees, wrapped to -180..180).
        local function behind(yaw)
            if type(W.get(Player, "RequestToSetCameraRotation")) ~= "function" then return end
            pcall(Player.RequestToSetCameraRotation, {rotX = 0, rotY = (yaw + 180) % 360 - 180, interpTime = 0.2})
        end
        -- Brought by the partner: a warp beside it in this mission. Another mission refuses (8): Go to first.
        local function brought(m, p)
            local b = T.blocked()
            if b then return b end
            if m ~= mission() then return 8 end
            -- Crossing Brings (both pressed, or this copy's own still unanswered for 30 s): the host's wins.
            if T.bring and W.now() - T.bring < 30000 then
                if W.host then return 1 end
                log("step=bring_yields")
            end
            T.bring = nil
            local spot = landing(p, false, 3)
            if not pcall(TppPlayer.Warp, {pos = {spot[1], spot[2], spot[3]}, rotY = spot[4]}) then return 2 end
            behind(spot[4])
            log(fmt("step=brought;mission=%d;x=%.2f;y=%.2f;z=%.2f", m, spot[1], spot[2], spot[3]))
            note(3, m)
        end
        H[OP.TRAVEL] = function(v)
            local kind, m = v[2], v[3]
            local p = {(v[4] or 0) / 1000, (v[5] or 0) / 1000, (v[6] or 0) / 1000, (v[7] or 0) / 1000}
            log(fmt("step=rx;kind=%d;mission=%d;reason=%d;here=%d", kind or 0, m or 0, v[8] or 0, mission()))
            if kind == 1 then
                if W.host then tx(4, m, nil, lead(m, "partner") or 0) end
            elseif kind == 2 then
                if W.host then return end
                -- The host's travel replaces this copy's own; a paired guest follows it (world follow).
                if T.job then log("step=replaced;target=" .. T.job.target); T.job = nil end
                local b = T.blocked()
                if b then return tx(4, m, nil, b) end
                T.start(m, landing(p, false, 4), not W.paired, T.asked == m and "self" or "partner")
                T.asked = nil
            elseif kind == 3 then
                tx(4, m, nil, brought(m, p) or 0)
            elseif kind == 4 then
                T.bring = nil
                if v[8] ~= 0 then
                    log(fmt("step=refused_by_partner;mission=%d;reason=%s", m, WHY[v[8]] or tostring(v[8])))
                    note(v[8] == 8 and 7 or 5, m)
                end
            end
        end
        -- On foot in the target: within 3 m of the spot (else a warp, at most 3), then done.
        local function arrive(j, now)
            if not yes(TppMission, "CheckMissionState") or yes(DemoDaemon, "IsDemoPlaying") or not onFoot() then return end
            j.landed = j.landed or now
            if now - j.landed < 1500 then return end
            local p, want = pose(), j.spot
            if not p then return end
            local dx, dz = p[1] - want[1], p[3] - want[3]
            local err = sqrt(dx * dx + dz * dz)
            if err > 3 and (j.warps or 0) < 3 then
                j.warps, j.landed = (j.warps or 0) + 1, now
                pcall(TppPlayer.Warp, {pos = {want[1], want[2], want[3]}, rotY = want[4]})
                return log(fmt("step=warp;err_m=%.1f;try=%d", err, j.warps))
            end
            T.job = nil
            behind(p[4])
            log(fmt("step=arrived;mission=%d;err_m=%.2f;x=%.2f;y=%.2f;z=%.2f;ms=%.0f;lead=%d;by=%s", mission(), err, p[1], p[2], p[3],
                now - j.at, j.lead and 1 or 0, j.by))
            if j.by == "partner" then note(2, mission()) end
        end
        function T.tick(now)
            local j = T.job
            if not j then return end
            if now < j.at or now - j.at > 240000 then return T.stop("timeout") end
            local m = mission()
            if m ~= j.m then j.m, j.settle = m, nil end
            if m == j.target then return arrive(j, now) end
            -- A paired guest is carried by the host's world follow.
            if not j.lead or (not W.host and W.paired) or not yes(TppMission, "CheckMissionState") then return end
            if SPOT[m] then
                if j.hop1 then
                    if now - j.hop1 > 30000 then T.stop("hop1_stuck") end
                    return
                end
                j.hop1 = now
                local ok, err
                if m ~= 30050 then
                    ok, err = bypass(TppMission.ReserveMissionClearOnRideOnHelicopter)
                elseif type(W.get(f30050_sequence, "ReserveMissionClear")) == "function" then
                    ok, err = bypass(f30050_sequence.ReserveMissionClear)
                else
                    pcall(TppDemo.SetNextMBDemo)
                    ok, err = bypass(TppMission.ReserveMissionClear, {missionClearType = TppDefine.MISSION_CLEAR_TYPE.RIDE_ON_HELICOPTER,
                        nextMissionId = TppDefine.SYS_MISSION_ID.MTBS_HELI})
                end
                log(fmt("step=hop1;from=%d;ok=%d", m, ok and 1 or 0) .. (ok and "" or ";err=" .. tostring(err):sub(1, 60)))
                if not ok then T.stop("hop1_error") end
            elseif ACC[m] and not j.hop2 then
                if not mainGame() then j.settle = nil; return end
                j.settle = j.settle or now
                if now - j.settle < 2000 then return end
                if W.get(gvars, "mis_nextMissionCodeForEmergency") ~= 0 then return T.stop("emergency") end
                j.hop2 = now
                local ok, err = bypass(function()
                    TppMission.SelectNextMissionHeliStartRoute(j.target, 0, false)
                    TppMission.OnEndMissionPreparation(TppClock.DEPLOY_TIME.CURRENT, j.target == 30050 and 0 or nil)
                end)
                log(fmt("step=hop2;from=%d;target=%d;ok=%d", m, j.target, ok and 1 or 0) .. (ok and "" or ";err=" .. tostring(err):sub(1, 60)))
                if not ok then T.stop("hop2_error") end
            end
        end
        local tick = W.tick
        W.tick = function(...)
            if tick then tick(...) end
            if T.job then
                local ok, err = pcall(T.tick, W.now())
                if not ok then T.job = nil; log("step=error;msg=" .. tostring(err):sub(1, 80)) end
            end
        end
        -- Travel loads skip the loading-tips key (the game's own Load option).
        local load = W.get(TppMission, "Load")
        if type(load) == "function" then
            TppMission.Load = function(nextMission, current, options)
                if not T.job then return load(nextMission, current, options) end
                local o = {}
                if type(options) == "table" then for k, v in pairs(options) do o[k] = v end end
                o.waitOnLoadingTipsEnd = false
                log("step=load;next=" .. tostring(nextMission))
                return load(nextMission, current, o)
            end
        end
        -- The route-0 sortie lands on foot: no LZ route left over from the ACC.
        local deploy = W.get(TppMission, "OnEndMissionPreparation")
        if type(deploy) == "function" then
            TppMission.OnEndMissionPreparation = function(...)
                local j = T.job
                if j and W.get(gvars, "mis_nextMissionCodeForMissionClear") == j.target and W.get(gvars, "heli_missionStartRoute") == 0 and
                    type(mvars) == "table" then mvars.heli_missionStartRoute = nil end
                return deploy(...)
            end
        end
        -- Mother Base: no arrival demo (f30050 MissionPrepare reads the queued one).
        local allocate = W.get(e, "OnAllocate")
        if type(allocate) == "function" then
            e.OnAllocate = function(...)
                local j = T.job
                if j and j.target == 30050 and mission() == 30050 then
                    pcall(TppDemo.SetNextMBDemo)
                    log("step=mb_demo")
                end
                return allocate(...)
            end
        end
        -- The target's start: this copy's landing after the mission's own decision.
        local reserve = W.get(e, "ReservePlayerLoadingPosition")
        if type(reserve) == "function" then
            e.ReservePlayerLoadingPosition = function(kind, ...)
                local r = {n = 0}
                local function keep(...) r.n = select("#", ...); for i = 1, r.n do r[i] = select(i, ...) end end
                keep(reserve(kind, ...))
                local j = T.job
                if j and kind == W.get(W.get(TppDefine, "MISSION_LOAD_TYPE"), "MISSION_FINALIZE") and mission() == j.target then
                    local s = j.spot
                    local ok = pcall(TppPlayer.SetInitialPosition, {s[1], s[2], s[3]}, s[4])
                    log(fmt("step=initial;mission=%d;ok=%d;x=%.2f;y=%.2f;z=%.2f;yaw=%.1f", j.target, ok and 1 or 0, s[1], s[2], s[3], s[4]))
                end
                return unpack(r, 1, r.n)
            end
        end
    end
end
