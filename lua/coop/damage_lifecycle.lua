
-- Restore the original weapon data before the original mission termination.
do
    local terminate = e.OnTerminate
    if type(terminate) == "function" then
        e.OnTerminate = function(...)
            local state = Tpp and Tpp.MgsCoop
            if type(state) == "table" then
                -- Clear activation triggers even if a restoration call fails.
                state.hostSessionCreated = false
                state.clientPrepared = false
                state.clientJoinRequested = false
            end
            if type(MgsCoopDamage) == "table" and type(MgsCoopDamage.SetActive) == "function" then
                local ok, restored = pcall(MgsCoopDamage.SetActive, false)
                if type(state) == "table" then
                    state.damageRulesActive = not (ok and restored == true)
                    if not ok then state.sessionCleanupError = tostring(restored) end
                end
            end
            if type(state) == "table" and state.onlineInitializationOwned then
                local ok, failure = pcall(TppGameStatus.Reset, "Mission", "S_IS_ONLINE")
                if ok then state.onlineInitializationOwned = false
                else state.sessionCleanupError = tostring(failure) end
            end
            return terminate(...)
        end
    end
end
