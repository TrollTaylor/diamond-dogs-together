-- Project-authored fragment inserted before the fingerprinted Tpp.lua return.
-- The host module's local `e` is established by that exact source revision.
do
    local coop = {
        bootstrapVersion = 1,
        maxPlayers = 4,
        gameplayEnabled = false,
        -- Only normal free-roam packs own a two-slot controller. The package
        -- selection latch also excludes labelled demos sharing a mission code.
        partnerMissions = {
            [30010] = {original = "/Assets/tpp/pack/mission2/free/f30010/f30010.fpk",
                replacement = "/Assets/tpp/pack/mission2/free/f30010/mgs_coop_f30010.fpk",
                controller = "/Assets/tpp/pack/player/game_object/mgs_coop_two_player_scarf_controller.fpk"},
            [30020] = {original = "/Assets/tpp/pack/mission2/free/f30020/f30020.fpk",
                replacement = "/Assets/tpp/pack/mission2/free/f30020/mgs_coop_f30020.fpk",
                controller = "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk"},
            [30050] = {original = "/Assets/tpp/pack/mission2/free/f30050/f30050.fpk",
                replacement = "/Assets/tpp/pack/mission2/free/f30050/mgs_coop_f30050.fpk",
                controller = "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk"},
        },
        playerPackages = {
            "/Assets/tpp/pack/player/game_object/mgs_coop_four_players_game_obj.fpk",
            "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk",
        },
    }
    -- A fresh list keeps callers from modifying the bootstrap's package order.
    -- Mounting these definitions and mission selection are separate engine work.
    function coop.GetPlayerPackages()
        return { coop.playerPackages[1], coop.playerPackages[2] }
    end
    function coop.IsPartnerBodyMission(missionCode)
        return coop.partnerMissions[missionCode] ~= nil and coop.playerPackageSelected == true
            and coop.playerPackageMission == missionCode and coop.archiveDirectReadSucceeded == true
    end
    e.MgsCoop = coop
    -- Natural Lua compilation provides an execution receipt through the existing
    -- load-buffer hook without native calls into the VM or retaining its state.
    if type(loadstring) == "function" then
        loadstring("return 'MGS_COOP_BOOTSTRAP_EXECUTED_V1'")
    end
end
