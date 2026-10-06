-- Adapted from Dynamite TppMain marker-loading lifecycle, revision
-- c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
-- No marker replication or replay: loading requests are intentionally dropped.
do
    local allocate, canStart, terminate = e.OnAllocate, e.OnMissionCanStart, e.OnTerminate
    local function receipt(source)
        local call = loadstring(source)
        return call and call() == true
    end
    local function stop()
        -- Native cleanup is independent of Lua state availability.
        receipt("return 'MGS_COOP_MARKER_LOADING_END_V1'")
        if Tpp and type(Tpp.MgsCoop) == "table" then Tpp.MgsCoop.markerLoadingGuardActive = false end
    end
    local function begin()
        stop()
        local state = Tpp and Tpp.MgsCoop
        if type(state) == "table" and type(state.IsPartnerBodyMission) == "function" and state.IsPartnerBodyMission(vars.missionCode) then
            state.markerLoadingGuardActive = receipt("return 'MGS_COOP_MARKER_LOADING_BEGIN_V1'")
        end
    end
    if type(allocate) == "function" then
        e.OnAllocate = function(...) begin(); return allocate(...) end
    end
    local function started(...) stop(); return ... end
    if type(canStart) == "function" then
        e.OnMissionCanStart = function(...) return started(canStart(...)) end
    end
    if type(terminate) == "function" then
        e.OnTerminate = function(...) stop(); return terminate(...) end
    end
end
