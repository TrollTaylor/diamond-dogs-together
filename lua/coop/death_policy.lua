-- Scoped adaptation of Dynamite's disabled TppMission.OnPlayerDead handler.
-- Same pinned GPLv3 + Commons Clause source as respawn.lua.
do
    local original = e.OnPlayerDead
    if type(original) == "function" then
        e.OnPlayerDead = function(index, reason, ...)
            -- The offline puppet is not a second local player. Its fall/death
            -- must not invoke vanilla's shared game over; the real remote death
            -- still arrives through the paired world's REQ_GAME_OVER path.
            local W = e.MgsCoopWorld
            local coop = Tpp and Tpp.MgsCoop
            local mission = vars and vars.missionCode
            if type(W) == "table" and W.active == true and W.paired == true and
                type(W.eff) == "number" and W.eff >= 3 and W.mission == mission and
                (mission == 30010 or mission == 30020 or mission == 30050) and
                type(coop) == "table" and coop.playerPackageSelected == true and
                (index == 0 or index == 1) and PlayerInfo and type(PlayerInfo.GetLocalPlayerIndex) == "function" then
                local ok, localIndex = pcall(PlayerInfo.GetLocalPlayerIndex)
                if ok and (localIndex == 0 or localIndex == 1) and index ~= localIndex then
                    if type(W.log) == "function" then pcall(W.log, "event=recovery;partner_death=ignored;index=" .. index) end
                    return
                end
            end
            local controller = MgsCoopRespawn
            if type(controller) == "table" and type(controller.Handle) == "function" then
                local ok, handled = pcall(controller.Handle, index, reason, function() return original(index, reason) end)
                if ok and handled then return end
            end
            return original(index, reason, ...)
        end
    end
end
