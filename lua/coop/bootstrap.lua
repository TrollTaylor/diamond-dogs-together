-- Project-authored fragment inserted before the fingerprinted Tpp.lua return.
-- The host module's local `e` is established by that exact source revision.
do
    local coop = {
        bootstrapVersion = 1,
        maxPlayers = 4,
        gameplayEnabled = false,
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
    e.MgsCoop = coop
    -- Natural Lua compilation provides an execution receipt through the existing
    -- load-buffer hook without native calls into the VM or retaining its state.
    if type(loadstring) == "function" then
        loadstring("return 'MGS_COOP_BOOTSTRAP_EXECUTED_V1'")
    end
end
