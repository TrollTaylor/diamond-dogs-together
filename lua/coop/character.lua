-- MGS co-op character selection (original). Spliced into the fingerprinted
-- TppMain.OnAllocate after the mission's MissionPrepare; the anchor proves `n`
-- is OnAllocate's mission-script table here. Uses the vanilla temporary player
-- type: MissionStartPlayerTypeSetting saves the own type in gvars and applies
-- it, and the next mission without an override restores it. A checkpoint
-- continue (title Continue, retry) runs TppVarInit.InitializeForContinue
-- instead, which reloads vars and applies a temporary type only when
-- sequence.ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING is set; this chunk sets
-- that flag for one allocation and restores it at the end of OnAllocate.
-- Receipts: MGS_COOP_CHARACTER_V1 (coop.ini character, fallback, readback),
-- _BYPASS_ON/_OFF_V1 (native Ocelot/Quiet reset bypass), _REPORT/_READBACK_V1
-- (log only), _PLAYERS_V1 (native guard before Player.GetPlayerType). Every
-- step is protected; failures leave the vanilla type.
do
    local coop = Tpp and type(Tpp.MgsCoop) == "table" and Tpp.MgsCoop
    local scripts = n
    if coop then
        local C = {}
        coop.Character = C
        local NAMES = {snake = "SNAKE", dd_male = "DD_MALE", dd_female = "DD_FEMALE",
            avatar = "AVATAR", ocelot = "OCELOT", quiet = "QUIET"}
        -- Scripts that force the type (docs/characters.md): hospital missions,
        -- MissionPrepare temporary Snake, the DD-to-Snake reload of 10151 and
        -- Subsistence parts registered later by TppVarInit.
        C.STORY_FORCED = {[10010] = "hospital", [10280] = "hospital", [10030] = "temp_snake",
            [10240] = "temp_snake", [10151] = "snake_for_dd", [11043] = "subsistence", [11044] = "subsistence"}
        -- Story Quiet is present (boss fight, Extreme, A Quiet Exit).
        C.QUIET_CONFLICT = {[10050] = true, [11050] = true, [10260] = true}
        C.FALLBACKS = {snake = true, dd_male = true, dd_female = true, avatar = true}
        -- Infinite Heaven's default DD faces; a matching sortie face wins.
        C.DEFAULT_FACE = {dd_male = 0, dd_female = 350}
        C.ALWAYS = "ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING"

        local function read(get)
            local ok, value = pcall(get)
            if ok then return value end
        end
        local function receipt(source)
            local chunk = loadstring(source)
            if type(chunk) ~= "function" then return nil end
            return chunk()
        end
        function C.Log(kind, payload)
            local text = string.gsub(tostring(payload), "[^%w_=:,.;%-]", "_")
            loadstring("return 'MGS_COOP_CHARACTER_" .. kind .. "_V1' -- " .. string.sub(text, 1, 192))
        end

        -- Pure decision. x = {character, fallback, mission, titleMode, storySnake,
        -- existingTemp, subsistence, buddyQuiet}. result: skip | keep | apply.
        function C.Decide(x)
            local name = x.character
            if name == nil or name == "" or name == "default" then return {result = "skip", reason = "default"} end
            if name == "MGS_COOP_CHARACTER_V1" then return {result = "skip", reason = "native_unavailable"} end
            local d = {requested = tostring(name)}
            if not NAMES[name] then d.result, d.reason = "skip", "unknown_character"; return d end
            if x.titleMode then d.result, d.reason = "skip", "title_mode"; return d end
            local class = math.floor((tonumber(x.mission) or 0) / 10000)
            if class ~= 1 and class ~= 3 then d.result, d.reason = "skip", "mission_class"; return d end
            -- storySnake: MissionStartPlayerTypeSetting forces SNAKE while the story
            -- sequence is CLEARD_ESCAPE_THE_HOSPITAL; a temporary type would win.
            local forced = C.STORY_FORCED[x.mission]
            if forced or x.storySnake or x.subsistence or x.existingTemp then
                d.result = "keep"
                d.reason = "story_" .. (forced or (x.storySnake and "sequence_snake") or
                    (x.subsistence and "subsistence") or "temp_type")
                return d
            end
            d.result, d.reason, d.applied = "apply", "override", name
            d.fallback = C.FALLBACKS[x.fallback] and x.fallback or "dd_male"
            if name == "quiet" and (C.QUIET_CONFLICT[x.mission] or x.buddyQuiet) then
                d.applied = d.fallback
                d.reason = C.QUIET_CONFLICT[x.mission] and "quiet_conflict" or "buddy_quiet"
            end
            d.bypass = d.applied == "ocelot" or d.applied == "quiet"
            return d
        end

        -- Pure temporary type for one character, or nil to keep the own type
        -- (x.realType, what MissionStartPlayerTypeSetting restores). The second
        -- result is a DD face for after the type is applied.
        function C.Plan(name, x, E)
            local key, s = NAMES[name], x.sortie or {}
            local T = E.PlayerType[key]
            if name == "ocelot" or name == "quiet" then
                return {playerType = T, partsType = E.PlayerPartsType[key], camoType = E.PlayerCamoType[key],
                    handEquip = E.handNormal, faceEquipId = 0}
            end
            if name == "snake" or name == "avatar" then
                if x.realType == T then return nil end
                return {playerType = T, partsType = s.snakeParts or E.PlayerPartsType.NORMAL_SCARF,
                    camoType = s.snakeCamo or E.PlayerCamoType.OLIVEDRAB, faceEquipId = s.snakeFaceEquip or 0}
            end
            if x.realType == E.PlayerType.DD_MALE or x.realType == E.PlayerType.DD_FEMALE then return nil end
            local face = C.DEFAULT_FACE[name]
            if s.type == T and s.face ~= nil then face = s.face end
            return {playerType = T, partsType = s.ddParts or E.PlayerPartsType.NORMAL,
                camoType = s.ddCamo or E.PlayerCamoType.OLIVEDRAB, faceEquipId = s.ddFaceEquip or 0}, face
        end

        function C.Gather(character, fallback)
            local x = {character = character, fallback = fallback}
            x.mission = read(function() return vars.missionCode end)
            x.titleMode = read(function() return gvars.ini_isTitleMode end) == true
            x.storySnake = read(function()
                local hospital = TppDefine.STORY_SEQUENCE.CLEARD_ESCAPE_THE_HOSPITAL
                return hospital ~= nil and TppStory.GetCurrentStorySequence() == hospital
            end) == true
            x.existingTemp = read(function() return mvars.ply_isExistTempPlayerType end) == true
            x.subsistence = read(function() return TppMission.IsSubsistenceMission() end) == true
            x.buddyQuiet = read(function()
                return type(BuddyType.QUIET) == "number" and vars.buddyType == BuddyType.QUIET end) == true
            x.usingTemp = read(function() return gvars.ply_isUsingTempPlayerType end) == true
            x.realType = read(function()
                if x.usingTemp then return gvars.ply_lastPlayerTypeUsingTemp end
                return vars.playerType
            end)
            -- Log only: OnAllocate takes the InitializeForContinue path when false.
            local start = read(function() return TppMission.IsMissionStart() end)
            x.start = (start == true and "new") or (start == false and "continue") or "unknown"
            x.sortie = read(function()
                return {type = vars.sortiePrepPlayerType, face = vars.sortiePrepPlayerFaceId,
                    snakeParts = vars.sortiePrepPlayerSnakePartsType, snakeCamo = vars.sortiePrepPlayerSnakeCamoType,
                    snakeFaceEquip = vars.sortiePrepPlayerSnakeFaceEquipId, ddParts = vars.sortiePrepPlayerDDPartsType,
                    ddCamo = vars.sortiePrepPlayerDDCamoType, ddFaceEquip = vars.sortiePrepPlayerDDFaceEquipId}
            end)
            return x
        end

        -- The continue flag is set only where the mission does not set it itself
        -- and is put back (normally by C.Post, else by the next allocation).
        function C.SetAlways(sequence)
            if type(sequence) ~= "table" or sequence[C.ALWAYS] == true then return false end
            coop.characterAlways = {sequence = sequence, previous = sequence[C.ALWAYS]}
            sequence[C.ALWAYS] = true
            return true
        end
        function C.RestoreAlways()
            local mark = coop.characterAlways
            coop.characterAlways = nil
            if type(mark) == "table" and type(mark.sequence) == "table" then mark.sequence[C.ALWAYS] = mark.previous end
        end

        -- Runs from the hooked ForceChangePlayerFromOcelot at the end of the
        -- same OnAllocate. Restores the continue flag; if no type setting ran
        -- (unexpected path), applies the registered type the vanilla way now;
        -- sets the DD face; reports vars; returns true to skip the Ocelot/Quiet
        -- revert for this allocation only.
        function C.Post(post)
            C.RestoreAlways()
            local late = "0"
            if post.type ~= nil and mvars.ply_isExistTempPlayerType == true and mvars.ply_tempPlayerType == post.type and
               vars.playerType ~= post.type then
                late = "1;before=" .. tostring(vars.playerType)
                TppPlayer.SaveCurrentPlayerType()
                TppPlayer.ApplyTemporaryPlayerType()
            end
            if post.face ~= nil and mvars.ply_isExistTempPlayerType == true and
               gvars.ply_isUsingTempPlayerType == true and vars.playerType == post.faceType then
                vars.playerFaceId = post.face
                C.Log("REPORT", "face;mission=" .. tostring(post.mission) .. ";face=" .. tostring(post.face))
            end
            C.Log("REPORT", "allocated;mission=" .. tostring(post.mission) .. ";vars=" .. tostring(vars.playerType) ..
                ";parts=" .. tostring(vars.playerPartsType) .. ";camo=" .. tostring(vars.playerCamoType) ..
                ";temp=" .. (gvars.ply_isUsingTempPlayerType == true and "1" or "0") .. ";start=" ..
                tostring(post.start) .. ";late=" .. late)
            if post.skip then C.Log("REPORT", "ocelot_reset_skipped;mission=" .. tostring(post.mission)) end
            return post.skip == true
        end
        function C.InstallPostHook()
            local current = TppPlayer and TppPlayer.ForceChangePlayerFromOcelot
            if type(current) ~= "function" or current == coop.characterPostHook then return end
            local original = current
            local function consume()
                local post = coop.characterPost
                if type(post) ~= "table" or not post.armed or vars.missionCode ~= post.mission then return false end
                post.armed = false
                return coop.Character.Post(post)
            end
            local function hook(...)
                local ok, skip = pcall(consume)
                if ok and skip then return end
                return original(...)
            end
            coop.characterPostHook = hook
            TppPlayer.ForceChangePlayerFromOcelot = hook
        end

        function C.Apply()
            C.RestoreAlways()
            local post = {armed = false}
            coop.characterPost = post
            -- Readback (character_readback.lua): lab only, for a story/free-roam
            -- decision (apply/keep), between OnMissionGameStart and OnTerminate.
            local poll = {frames = 0, reports = 0, enabled = false, started = false}
            coop.characterPoll = poll
            local character, fallback, readback = receipt("return 'MGS_COOP_CHARACTER_V1'")
            local x = C.Gather(character, fallback)
            local d = C.Decide(x)
            coop.characterStatus = d
            if d.reason == "default" or d.reason == "native_unavailable" then return d end
            poll.enabled = readback == true and (d.result == "apply" or d.result == "keep")
            if d.bypass and receipt("return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'") ~= true then
                d.applied, d.reason, d.bypass = d.fallback, "bypass_unavailable", false
            end
            if not d.bypass then receipt("return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'") end
            if d.result == "apply" then
                local E = {PlayerType = PlayerType, PlayerPartsType = PlayerPartsType, PlayerCamoType = PlayerCamoType,
                    handNormal = TppEquip.EQP_HAND_NORMAL}
                local temp, face = C.Plan(d.applied, x, E)
                local sequence = type(scripts) == "table" and scripts.sequence
                if temp then
                    TppPlayer.RegisterTemporaryPlayerType(temp)
                    d.type, d.expect = temp.playerType, temp.playerType
                    post.type, post.face, post.faceType = temp.playerType, face, face ~= nil and temp.playerType or nil
                    d.always = C.SetAlways(sequence)
                else
                    d.reason, d.expect = d.reason .. "_sortie_kept", x.realType
                    -- A checkpoint saved under an earlier override: a continue
                    -- then runs the vanilla restore as well.
                    if x.usingTemp then d.always = C.SetAlways(sequence) end
                end
                post.skip, post.mission, post.start = d.bypass == true, x.mission, x.start
                post.armed = temp ~= nil or d.always == true
                if post.armed then C.InstallPostHook() end
            end
            C.Log("REPORT", d.result .. ";mission=" .. tostring(x.mission) .. ";requested=" .. tostring(d.requested) ..
                ";applied=" .. tostring(d.applied) .. ";type=" .. tostring(d.type) .. ";reason=" .. d.reason ..
                ";bypass=" .. (d.bypass and "1" or "0") .. ";real=" .. tostring(x.realType) .. ";start=" .. x.start ..
                ";always=" .. (d.always and "1" or "0"))
            return d
        end

        local ok, failure = pcall(C.Apply)
        if not ok then
            coop.characterError = tostring(failure)
            pcall(C.RestoreAlways)
            pcall(C.Log, "REPORT", "error;reason=lua_error")
        end
    end
end
