-- MGS co-op world sync, the lab goto (original; docs/world-sync-protocol.md
-- "Lab goto"): both lab copies go from Mother Base (30050) to free roam
-- (30010/30020) without menus, through the game's own calls in two hops.
-- Step 1 (30050): f30050_sequence.ReserveMissionClear(), the heli boarding
-- (SetNextMBDemo clears a queued MB demo, then ReserveMissionClear
-- RIDE_ON_HELICOPTER -> MTBS_HELI). Step 2 (ACC 40050): the sortie's own
-- SelectNextMissionHeliStartRoute and OnEndMissionPreparation(CURRENT). Each
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
        H[OP.GOTO] = function(v, now)
            if W.mode ~= 1 then
                W.tx(OP.GOTO_ACK, v[1], 0)
                W.log(fmt("event=goto;step=%d;result=refused;reason=mode", v[1]))
                return
            end
            W.gt = {step = v[1], target = v[2], lz = v[3], since = now}
            W.log(fmt("event=goto;step=%d;target=%d;lz=%d;result=armed", v[1], v[2], v[3]))
        end
        local function ack(g, result, why)
            W.tx(OP.GOTO_ACK, g.step, result)
            W.log(fmt("event=goto;step=%d;result=%s", g.step, why))
        end
        function G.tick(now)
            local g = W.gt
            if g.check then
                W.gt = nil
                local ok = W.get(svars, "mis_isDefiniteMissionClear") == true
                return ack(g, ok and 1 or 0, ok and "reserved" or "refused;reason=readback")
            end
            if g.step == 1 then
                if W.inGame() then
                    local f = W.get(f30050_sequence, "ReserveMissionClear")
                    if type(f) == "function" then
                        f()
                    else
                        TppDemo.SetNextMBDemo()
                        TppMission.ReserveMissionClear{missionClearType = TppDefine.MISSION_CLEAR_TYPE.RIDE_ON_HELICOPTER,
                            nextMissionId = TppDefine.SYS_MISSION_ID.MTBS_HELI}
                    end
                    g.check = true
                    W.log("event=goto;step=1;call=" .. (type(f) == "function" and "f30050_sequence" or "fallback"))
                elseif now - g.since > 30000 then
                    W.gt = nil
                    ack(g, 0, "refused;reason=not_in_game")
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
