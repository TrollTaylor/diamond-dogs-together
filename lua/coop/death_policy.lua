-- Scoped adaptation of Dynamite's disabled TppMission.OnPlayerDead handler.
-- Same pinned GPLv3 + Commons Clause source as respawn.lua.
do
    local original = e.OnPlayerDead
    if type(original) == "function" then
        e.OnPlayerDead = function(index, reason, ...)
            local controller = MgsCoopRespawn
            if type(controller) == "table" and type(controller.Handle) == "function" then
                local ok, handled = pcall(controller.Handle, index, reason, function() return original(index, reason) end)
                if ok and handled then return end
            end
            return original(index, reason, ...)
        end
    end
end
