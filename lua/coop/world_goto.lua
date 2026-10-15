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
            [30020] = {"lz_drp_savannahEast_N0000|rt_drp_savannahEast_N_0000", "lz_drp_pfCampNorth_S0000|rt_drp_pfCampNorth_S_0000"}}
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
                TppMission.OnEndMissionPreparation(TppClock.DEPLOY_TIME.CURRENT, nil)
                g.check = true
            elseif not g.waiting then
                g.waiting = true
                ack(g, 2, "waiting")
            end
        end
    end
end
