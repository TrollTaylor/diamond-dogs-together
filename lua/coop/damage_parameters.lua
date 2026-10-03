
-- Adapted from Dynamite standalone/DamageParameterTables.lua, commit
-- c90f252f8200765f66c005d6bf4357fda0c0d98e. GPLv3 + Commons Clause 1.0.
-- Changes: keep the original table and pair activation with the native rule.
-- The upstream field-13 loop walks the outer wrapper, so the tested Dynamite
-- build never changed a row: the 146 vanilla rows whose field 13
-- (ignore-defender) is 1 kept it. Clearing it in the nested DamageParameter
-- rows is therefore an explicit experiment, off by default for Dynamite
-- parity. Set this to true only for a recorded experiment build.
do
    local CLEAR_IGNORE_DEFENDER_FLAG = false
    local original = mgsDamage
    local coop = {}
    for key, value in pairs(original) do coop[key] = value end
    coop.DamageParameter = {}
    local changed = 0
    for key, value in pairs(original.DamageParameter) do
        if type(value) == "table" then
            local row = {}
            for field, item in pairs(value) do row[field] = item end
            if CLEAR_IGNORE_DEFENDER_FLAG and row[13] == 1 then
                row[13] = 0
                changed = changed + 1
            end
            coop.DamageParameter[key] = row
        else
            coop.DamageParameter[key] = value
        end
    end
    local active = false
    local function native(enabled)
        local request
        if enabled then
            request = loadstring("return 'MGS_COOP_DAMAGE_ON_V1'")
        else
            request = loadstring("return 'MGS_COOP_DAMAGE_OFF_V1'")
        end
        return request and request() == true
    end
    local control = { changedRows = changed, clearsIgnoreDefender = CLEAR_IGNORE_DEFENDER_FLAG }
    -- The activation receipt names the loaded table, so live logs can tell a
    -- parity build from a recorded field-13 experiment.
    local mode = CLEAR_IGNORE_DEFENDER_FLAG and "clear_field13" or "parity"
    local activeReceipt = "return 'MGS_COOP_DAMAGE_TABLE_ACTIVE_V2' -- mode=" .. mode .. ";changed_rows=" .. changed
    function control.SetActive(enabled)
        enabled = enabled == true
        -- A previous native restoration may have failed even when the Lua
        -- table was restored. Recheck that side before reporting success.
        if active == enabled then return native(enabled) end
        if enabled then
            if not native(true) then return false end
            local ok, failure = pcall(TppDamage.ReloadDamageParameter, coop)
            if not ok then
                native(false)
                error(failure)
            end
            active = true
            loadstring(activeReceipt)
            return true
        end
        local restored = native(false)
        TppDamage.ReloadDamageParameter(original)
        active = false
        loadstring("return 'MGS_COOP_DAMAGE_TABLE_RESTORED_V1'")
        return restored
    end
    _G.MgsCoopDamage = control
    TppDamage.ReloadDamageParameter(original)
    -- Some scenes load their damage table after the early mission insertion.
    if vars and vars.missionCode == 30050 and Tpp and type(Tpp.MgsCoop) == "table"
        and (Tpp.MgsCoop.hostSessionCreated == true or Tpp.MgsCoop.clientPrepared == true) then
        Tpp.MgsCoop.damageRulesActive = control.SetActive(true)
    end
end
