
-- Original development insertion immediately before TppMain's Mission.Start.
-- Client online initialization follows pinned Dynamite; joining is requested later.
-- Clear the previous co-op result scope before every new Mission.Start,
-- including an ordinary mission. New co-op preparation arms it again below.
do
    local boundary = loadstring("return 'MGS_COOP_RESULTS_NEW_MISSION_V1'")
    if boundary then boundary() end
end
if vars.missionCode == 30050 and type(Tpp.MgsCoop) == "table"
    and Tpp.MgsCoop.playerPackageSelected and Tpp.MgsCoop.archiveDirectReadSucceeded then
    Tpp.MgsCoop.equipmentPreparationAttempted = false
    Tpp.MgsCoop.equipmentPreparationReturned = false
    Tpp.MgsCoop.equipmentPreparationError = nil
    local trial = loadstring("return 'MGS_COOP_HOST_CREATE_V1'")
    local result = trial and trial()
    if result ~= true and result ~= "client" then
        Tpp.MgsCoop.markerLoadingGuardActive = false
        local stopMarkers = loadstring("return 'MGS_COOP_MARKER_LOADING_END_V1'")
        if stopMarkers then stopMarkers() end
    end
    if result == true then
        Tpp.MgsCoop.hostSessionCreated = true
    elseif result == "client" then
        Tpp.MgsCoop.clientPrepared = true
    end
    if result == true or result == "client" then
        Tpp.MgsCoop.onlineInitializationOwned = not TppGameStatus.IsSet("Mission", "S_IS_ONLINE")
        TppGameStatus.Set("Mission", "S_IS_ONLINE")
    end
    if (result == true or result == "client") and type(MgsCoopDamage) == "table"
        and type(MgsCoopDamage.SetActive) == "function" then
        Tpp.MgsCoop.damageRulesActive = MgsCoopDamage.SetActive(true)
    end
end
