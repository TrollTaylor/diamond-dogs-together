-- MGS co-op offline partner spike (original).
-- Native CFG authorizes offline-v1 + partner=1 only.
-- Typed optional bindings, owner-thread calls; errors stop this mission's spike.
-- Original callbacks preserve their arguments, returns, errors and yields.
do
    local start, update, terminate = e.OnMissionGameStart, e.OnUpdate, e.OnTerminate
    local active, failed, frame, allActive, partsCopied = false, false, 0, false, false
    local api, skipped = {}, {}
    local fireUntil, reloadUntil, actionTime, lastGun
    local actionCfgDone, actionCfgAttempts, actionPublished = false, 0, false
    local fmt, floor, gsub, sub = string.format, math.floor, string.gsub, string.sub
    -- Direct-motion catalogue; GANI[index] is the op's 14th reply value.
    local DMP = "/Assets/tpp/motion/SI_game/fani/bodies/snap/snapnon/"
    local GMP = "/Assets/tpp/motion/SI_game/fani/bodies/snap/"
    local GANI = {[0] = DMP .. "snapnon_s_idl_l.gani", DMP .. "snapnon_q_idl_l.gani", DMP .. "snapnon_c_idl_l.gani",
        DMP .. "snapnon_q_wk_lp_l.gani", DMP .. "snapnon_q_rn_lp_l.gani", DMP .. "snapnon_s_dh_lp.gani",
        DMP .. "snapnon_c_fst_lp_r.gani", DMP .. "snapnon_p_idl.gani", "stateSquatIdleLeft", "stateStandIdleLeft",
        DMP .. "snapnon_s_wk_lp_vr1_l.gani", DMP .. "snapnon_s_rn_lp_vr1_l.gani", DMP .. "snapnon_s_jg_lp_vr1.gani",
        DMP .. "snapnon_s_tn_l90.gani", DMP .. "snapnon_s_tn_r90.gani", DMP .. "snapnon_s_tn_l180.gani", DMP .. "snapnon_s_tn_r180.gani",
        DMP .. "snapnon_q_tn_l90.gani", DMP .. "snapnon_q_tn_r90.gani", DMP .. "snapnon_q_tn_l180.gani", DMP .. "snapnon_q_tn_r180.gani",
        -- Manual probes only: these Upper-layer state hashes exist in the
        -- resident MOG; DirectMotion acceptance is not yet demonstrated.
        "stateGunHold", "stateGunHoldStart", "stateGunHoldEnd", "stateGunFire", "stateGunReload",
        -- Manual finite probes only; exact receiver MTAR paths, layering unverified.
        GMP .. "snaphg00/snaphg00_s_fre.gani", GMP .. "snapar00/snapar00_aw1_s_fre.gani",
        GMP .. "snapar01/snapar01_aw0_s_fre.gani", GMP .. "snapsr02/snapsr02_s_fre2.gani", GMP .. "snapsr02/snapsr02_s_fre_rld.gani"}
    local function receipt(source)
        local chunk = loadstring(source)
        if type(chunk) ~= "function" then return nil end
        return chunk()
    end
    local function note(text)
        receipt("return 'MGS_COOP_PARTNER_LOG_V1' -- " .. sub(gsub(tostring(text), "[^%w_=:,.;%-]", "_"), 1, 160))
    end
    local function index(t, k) return t[k] end
    local function get(t, k)
        if t == nil then return nil end
        local ok, value = pcall(index, t, k)
        if ok then return value end
        return nil
    end
    local function finite(v) return type(v) == "number" and v == v and v > -1e15 and v < 1e15 end
    -- Values sent through %d (a 32-bit long in Lua 5.1 on Windows) stay within
    -- the native bounds: +-1e6 m positions, +-36000 degree angles.
    local function within(v, limit) return finite(v) and v > -limit and v < limit end
    local function integer(v, low, high) return type(v) == "number" and v == floor(v) and v >= low and v <= high end
    -- Resolves the bindings; returns the missing names and whether the
    -- required ones (vars, Player.IsInstanceValid) exist.
    local function resolve()
        local missing = {}
        local function fn(t, name, key)
            local value = get(t, key)
            if type(value) == "function" then return value end
            missing[#missing + 1] = name .. "." .. key
            return nil
        end
        local function num(t, name, key)
            local value = get(t, key)
            if type(value) == "number" then return value end
            missing[#missing + 1] = name .. "." .. key
            return nil
        end
        api = {
            isValid = fn(Player, "Player", "IsInstanceValid"),
            setAllActive = fn(Player, "Player", "SetAllInstanceActiveMode"),
            copyParts = fn(Player, "Player", "CopyPartsInfoFromLocalPlayer"),
            setParts = fn(Player, "Player", "SetPartsInfoAtInstanceIndex"),
            mirror = fn(Player, "Player", "SetOtherInstanceIsForMirrorMode"),
            dmPlay = fn(Player, "Player", "RequestToPlayDirectMotion"),
            dmStop = fn(Player, "Player", "RequestToStopDirectMotion"),
            localIndex = fn(PlayerInfo, "PlayerInfo", "GetLocalPlayerIndex"),
            orCheck = fn(PlayerInfo, "PlayerInfo", "OrCheckStatus"),
            sendCommand = fn(GameObject, "GameObject", "SendCommand"),
            CRAWL = num(PlayerStatus, "PlayerStatus", "CRAWL"),
            SQUAT = num(PlayerStatus, "PlayerStatus", "SQUAT"),
            DASH = num(PlayerStatus, "PlayerStatus", "DASH"),
            RUN = num(PlayerStatus, "PlayerStatus", "RUN"),
            WALK = num(PlayerStatus, "PlayerStatus", "WALK"),
            GUN_FIRE = num(PlayerStatus, "PlayerStatus", "GUN_FIRE"),
            GUN_FIRE_SUPPRESSOR = num(PlayerStatus, "PlayerStatus", "GUN_FIRE_SUPPRESSOR"),
            band = fn(bit, "bit", "band"),
            HOLD = num(PlayerPad, "PlayerPad", "HOLD"),
            RELOAD = num(PlayerPad, "PlayerPad", "RELOAD"),
            time = fn(Time, "Time", "GetRawElapsedTimeSinceStartUp"),
        }
        local required = vars ~= nil and api.isValid ~= nil
        if vars == nil then missing[#missing + 1] = "vars" end
        return table.concat(missing, ","), required
    end
    local function has(status)
        if status == nil or api.orCheck == nil then return false end
        return api.orCheck({status}) == true
    end
    -- Calls an optional binding; a missing one is noted once and skipped.
    local function call(f, name, ...)
        if f == nil then
            if not skipped[name] then skipped[name] = true; note("event=op_skipped;missing=" .. name) end
            return false
        end
        f(...)
        return true
    end
    local function camera()
        local rotation = vars.playerCameraRotation
        if rotation == nil then return 0 end
        local ok, value = pcall(index, rotation, 1)
        if ok and within(value, 36000) then return value end
        return 0
    end
    local function supported()
        local coop = type(Tpp) == "table" and Tpp.MgsCoop
        return type(coop) == "table" and type(coop.IsPartnerBodyMission) == "function"
            and coop.IsPartnerBodyMission(vars.missionCode) == true
    end
    local function clearActions()
        fireUntil, reloadUntil, actionTime, lastGun = nil, nil, nil, nil
        if actionPublished then
            receipt("return 'MGS_COOP_ACTION_EVENTS_CLEAR_V1'")
            actionPublished = false
        end
        local A = get(e, "MgsCoopPartnerActions")
        if type(A) == "table" and type(A.reset) == "function" then A.reset() end
    end
    local function configureActions()
        if actionCfgDone or actionCfgAttempts >= 120 then return end
        local A = get(e, "MgsCoopPartnerActions")
        if type(A) ~= "table" then actionCfgDone = true; return end
        actionCfgAttempts = actionCfgAttempts + 1
        local accepted = receipt("return 'MGS_COOP_ACTION_EVENTS_CFG_V1'")
        A.enabled = accepted == true
        actionCfgDone = accepted == true or accepted == nil
    end
    local function gunActions()
        -- currentInventorySlot is a retail vars value, not PlayerSlotType.
        -- Restrict it to authored gun slots; items/binoculars are not guns.
        local slot, gun, slots = get(vars, "currentInventorySlot"), 0, get(TppDefine, "WEAPONSLOT")
        for _, name in ipairs({"PRIMARY_HIP", "PRIMARY_BACK", "SECONDARY"}) do
            local known = get(slots, name)
            if integer(known, 0, 15) and slot == known then
                local id = get(get(vars, "weapons"), slot)
                if integer(id, 1, 65535) then gun = id end
            end
        end
        if gun == 0 then clearActions(); return 0, 0, false end
        if gun ~= lastGun then clearActions(); lastGun = gun end
        -- Avoid translating input scanned during non-gameplay/player modes.
        for _, name in ipairs({"ON_HELICOPTER", "ON_VEHICLE", "ON_HORSE", "BINOCLE", "DEAD", "DEAD_FRESH", "CUTIN"}) do
            if has(get(PlayerStatus, name)) then clearActions(); return 0, gun, false end
        end
        local flags = 0
        local now
        if api.time then
            local ok, v = pcall(api.time)
            if ok and finite(v) and v >= 0 and v < 1e9 then now = v end
        end
        if not now or (actionTime and now < actionTime) then fireUntil, reloadUntil = nil, nil end
        actionTime = now
        local fire = has(api.GUN_FIRE) or has(api.GUN_FIRE_SUPPRESSOR)
        -- Public scannedButtonsDirect is used by the exact retail TppPlayer.
        -- HOLD/RELOAD mean requested input, not executed aim/reload state.
        -- SUBJECT is deliberately not used: binocular/first-person is broader.
        local buttons = get(PlayerVars, "scannedButtonsDirect")
        local function pressed(mask)
            if api.band == nil or not integer(mask, 1, 4294967295) or not integer(buttons, 0, 4294967295) then return false end
            local ok, value = pcall(api.band, buttons, mask)
            return ok and value == mask
        end
        if pressed(api.HOLD) then flags = flags + 1 end
        local reload = pressed(api.RELOAD)
        -- Retail raw time is seconds. Bounded pulses bridge the 30 Hz sender.
        if fire and now then fireUntil = now + 0.1 end
        if reload and now then reloadUntil = now + 1 / 3 end
        if fire or (now and fireUntil and now < fireUntil) then flags = flags + 2 end
        if reload or (now and reloadUntil and now < reloadUntil) then flags = flags + 8 end
        return flags, gun, true
    end
    local function tick()
        if not active or failed or not supported() or not api.isValid() then clearActions(); return end
        local x, y, z, ry = vars.playerPosX, vars.playerPosY, vars.playerPosZ, vars.playerRotY
        if not (within(x, 1e6) and within(y, 1e6) and within(z, 1e6) and within(ry, 36000)) then clearActions(); return end
        frame = frame + 1
        if not actionCfgDone and frame % 30 == 0 then configureActions() end
        local stance = (has(api.CRAWL) and 2) or (has(api.SQUAT) and 1) or 0
        local move = (has(api.DASH) and 3) or (has(api.RUN) and 2) or (has(api.WALK) and 1) or 0
        local cam = camera()
        local actions, gun, gunEligible = gunActions()
        local A = get(e, "MgsCoopPartnerActions")
        -- A.enabled stays false until a separate owner-validated native opt-in.
        -- Automatic upper playback capabilities remain independently false.
        if type(A) == "table" and A.enabled == true and type(A.sample) == "function"
            and type(A.receipt) == "function" then
            if A.sample(gunEligible == true, gun, stance) ~= nil then
                local source = A.receipt()
                if type(source) == "string" then actionPublished = receipt(source) == true end
            elseif actionPublished then
                receipt("return 'MGS_COOP_ACTION_EVENTS_CLEAR_V1'")
                actionPublished = false
            end
        end
        -- The reply carries 5 look values after the 8 pose values when op has
        -- SET_PARTS (16): type, partsType, camoType, handType, faceId.
        local op, R, px, py, pz, pry, _, _, lt, lp, lc, lh, lf, dg = receipt(fmt("return 'MGS_COOP_PARTNER_V1' -- %d %d %d %d %d %d %d %d %d %d %d",
            frame % 1000000, floor(x * 1000 + 0.5), floor(y * 1000 + 0.5), floor(z * 1000 + 0.5),
            floor(ry * 100 + 0.5), floor(cam * 100 + 0.5), stance, move, (allActive and 1 or 0) + (partsCopied and 2 or 0), actions, gun))
        if type(op) ~= "number" or op <= 0 then return end
        if R ~= 1 or (api.localIndex ~= nil and R == api.localIndex()) then
            failed = true; note("event=refused;reason=bad_index;R=" .. tostring(R)); return
        end
        if op % 16 >= 8 and allActive and call(api.setAllActive, "Player.SetAllInstanceActiveMode", false) then
            allActive = false; note("event=allactive_cleared")
        end
        -- SET is idempotent: native sends it until Lua latches it, and again
        -- (rate-limited) when P+0x204 bit 0x2000 was lost.
        if op % 4 >= 2 and call(api.setAllActive, "Player.SetAllInstanceActiveMode", true) then
            if not allActive then note("event=allactive_set") end
            allActive = true
        end
        if op % 8 >= 4 and not partsCopied and
            call(api.copyParts, "Player.CopyPartsInfoFromLocalPlayer", {instanceIndex = R}) then
            partsCopied = true; note("event=parts_copied;R=1")
        end
        -- SET_PARTS: the partner's own look (or a fixed test look) chosen and
        -- validated natively. SetPartsInfoAtInstanceIndex (0x9DF530) defaults a
        -- missing key (instanceIndex 0 = the LOCAL player, handType 1) and has
        -- no bounds check, so all 6 keys are always passed, R is 1 (checked
        -- above) and every value is a bounded integer, else nothing is called.
        if op % 32 >= 16 then
            if integer(lt, 0, 6) and lt ~= 4 and integer(lp, 0, 27) and integer(lc, 0, 116) and integer(lh, 0, 7) and
                integer(lf, 0, 65535) then
                if call(api.setParts, "Player.SetPartsInfoAtInstanceIndex", {instanceIndex = R, type = lt, partsType = lp,
                    camoType = lc, handType = lh, faceId = lf}) then
                    partsCopied = true
                    note(fmt("event=parts_set;R=%d;type=%d;parts=%d;camo=%d;hand=%d;face=%d", R, lt, lp, lc, lh, lf))
                end
            else
                note("event=parts_set_refused;reason=bad_look")
            end
        end
        if op % 64 >= 32 and call(api.mirror, "Player.SetOtherInstanceIsForMirrorMode", true) then note("event=mirror_on") end
        -- MIRROR_OFF (extension): native ends the F3c diagnostic after at most 10 s.
        if op % 128 >= 64 and call(api.mirror, "Player.SetOtherInstanceIsForMirrorMode", false) then note("event=mirror_off") end
        -- DM_PLAY (128) / DM_STOP (256): the gani is the 14th reply value (0..30; partner::max_gani);
        -- after SET_PARTS, before WARP. Needs the native comparator patch.
        if op % 256 >= 128 then
            if integer(dg, 0, 30) and GANI[dg] ~= nil then
                -- Resident turn clips 13..20 are finite transitions, not loops.
                if call(api.dmPlay, "Player.RequestToPlayDirectMotion", {{"coopLoco", R}, {GANI[dg], dg <= 12 or dg == 21, "", "", "", true}}) then
                    note("event=dm_play;gani=" .. dg)
                end
            else
                note("event=dm_refused;reason=bad_gani")
            end
        end
        if op % 512 >= 256 and call(api.dmStop, "Player.RequestToStopDirectMotion") then note("event=dm_stop") end
        if op % 2 == 1 and finite(px) and finite(py) and finite(pz) and finite(pry) then
            call(api.sendCommand, "GameObject.SendCommand", {type = "TppPlayer2", index = R},
                {id = "Warp", pos = {px / 1000, py / 1000, pz / 1000}, rotY = pry / 1000})
        end
    end
    local function begin()
        if not supported() or receipt("return 'MGS_COOP_PARTNER_CFG_V1'") ~= true then return end
        local missing, required = resolve()
        if not required then note("event=inactive;reason=missing_required;names=" .. missing); return end
        active = true
        configureActions()
        note("event=started")
        if missing ~= "" then note("event=missing;names=" .. missing) end
    end
    local function started(...)
        frame, failed, allActive, partsCopied, active, skipped = 0, false, false, false, false, {}
        clearActions()
        actionCfgDone, actionCfgAttempts = false, 0
        local A = get(e, "MgsCoopPartnerActions")
        if type(A) == "table" then A.enabled = false end
        local ok, failure = pcall(begin)
        if not ok then active = false; pcall(note, "event=error;msg=" .. tostring(failure)) end
        return ...
    end
    local function updated(...)
        if active and not failed then
            local ok, failure = pcall(tick)
            if not ok then failed = true; pcall(note, "event=error;msg=" .. tostring(failure)) end
        end
        return ...
    end
    -- SetAllInstanceActiveMode (0x9DCD20) clears P+0x204 bit 0x2000 without a
    -- null check on P = [root+8]; IsInstanceValid (0x9D6360) tests the same
    -- pointer and its valid bit 0. pcall cannot catch a native fault, so the
    -- clear at mission exit runs only while the instance is still valid.
    local function stopping()
        if not active then return end
        active = false
        clearActions()
        if allActive and api.setAllActive ~= nil then
            local ok, valid = pcall(api.isValid)
            if ok and valid == true then
                pcall(api.setAllActive, false)
            else
                pcall(note, "event=allactive_kept;reason=invalid_instance")
            end
            allActive = false
        end
        receipt("return 'MGS_COOP_PARTNER_STOP_V1'")
    end
    if type(start) == "function" then e.OnMissionGameStart = function(...) return started(start(...)) end end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    if type(terminate) == "function" then e.OnTerminate = function(...) pcall(stopping); return terminate(...) end end
end
