-- MGS co-op character readback (original). Appended to the fingerprinted
-- TppMain before its return, inside the session_lifecycle wrapper. Starts the
-- log-only Tpp.MgsCoop.Character readback after the original
-- OnMissionGameStart returns (the player object exists from then on), polls it
-- after each OnUpdate and stops it at OnTerminate entry, so nothing reads the
-- player during loading or teardown. Original arguments, results, errors and
-- yields pass through unchanged.
do
    local start, update, terminate = e.OnMissionGameStart, e.OnUpdate, e.OnTerminate
    local function call(name)
        local coop = Tpp and type(Tpp.MgsCoop) == "table" and Tpp.MgsCoop
        local character = coop and coop.Character
        if type(character) == "table" and type(character[name]) == "function" then pcall(character[name]) end
    end
    local function started(...) call("Started"); return ... end
    local function updated(...) call("Poll"); return ... end
    if type(start) == "function" then
        e.OnMissionGameStart = function(...) return started(start(...)) end
    end
    if type(update) == "function" then
        e.OnUpdate = function(...) return updated(update(...)) end
    end
    if type(terminate) == "function" then
        e.OnTerminate = function(...) call("Stopped"); return terminate(...) end
    end
end
