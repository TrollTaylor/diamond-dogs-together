-- MGS co-op character readback (original). Appended to the fingerprinted
-- TppMain before its return, inside the session_lifecycle wrapper. Calls the
-- log-only Tpp.MgsCoop.Character.Poll (enabled by the native query) at mission
-- start and from OnUpdate; original arguments, results, errors and yields pass
-- through unchanged.
do
    local started, update = e.OnMissionGameStart, e.OnUpdate
    local function poll(tag)
        local coop = Tpp and type(Tpp.MgsCoop) == "table" and Tpp.MgsCoop
        local character = coop and coop.Character
        if type(character) == "table" and type(character.Poll) == "function" then pcall(character.Poll, tag) end
    end
    if type(started) == "function" then
        e.OnMissionGameStart = function(...) poll("start"); return started(...) end
    end
    if type(update) == "function" then
        e.OnUpdate = function(...) poll(nil); return update(...) end
    end
end
