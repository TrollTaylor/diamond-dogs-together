
-- Adapted from Dynamite standalone/DamageParameterTables.lua, commit
-- c90f252f8200765f66c005d6bf4357fda0c0d98e. GPLv3 + Commons Clause 1.0.
-- Changes: walk actual DamageParameter rows (the upstream loop walks the outer
-- wrapper), keep the original table, and pair activation with the native rule.
do
    local original = mgsDamage
    local coop = {}
    for key, value in pairs(original) do coop[key] = value end
    coop.DamageParameter = {}
    local changed = 0
    for key, value in pairs(original.DamageParameter) do
        if type(value) == "table" then
            local row = {}
            for field, item in pairs(value) do row[field] = item end
            if row[13] == 1 then
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
    local control = { changedRows = changed }
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
            loadstring("return 'MGS_COOP_DAMAGE_TABLE_ACTIVE_V1'")
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
