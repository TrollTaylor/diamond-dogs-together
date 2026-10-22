-- Original owner-thread equipment actions; loaded after debug_menu.lua.
-- Retail s10260.GetRocketLuncher/s10050 give guns with Player.ChangeEquip.
-- Equip loading is an allocation flow, without a verified per-ID readiness
-- query. Only current resident guns/items are accepted during gameplay.
do
    local D = e.MgsCoopDebug
    local A = D.api
    local get, call, number, integer, pose = A.get, A.call, A.number, A.integer, A.pose
    local E = {}
    local fmt = string.format
    D.equipment = E
    local SLOT = {hip = {"PRIMARY_HIP", "PRIMARY_1"}, back = {"PRIMARY_BACK", "PRIMARY_2"}, secondary = {"SECONDARY", "SECONDARY"}}
    local function active()
        local slot = get(vars, "currentInventorySlot")
        return integer(slot, 0, 15) and fmt("%.0f", slot) or "unknown"
    end
    -- A default-off native query replaces this literal with a separately
    -- checked read-only fragment. It never selects or changes equipment.
    local update = D.update
    D.update = function(...)
        local result = update(...)
        local mission = get(vars, "missionCode")
        if (mission == 30010 or mission == 30020 or mission == 30050)
            and D.equipmentObservationMission ~= mission then
            D.equipmentObservationFrames = (D.equipmentObservationFrames or 0) + 1
            if D.equipmentObservationFrames % 30 ~= 1 then return result end
            D.equipmentObservationAttempts = (D.equipmentObservationAttempts or 0) + 1
            local value = A.receipt("return 'MGS_COOP_EQUIPMENT_OBSERVATION_QUERY_V1'")
            local accepted = false
            if type(value) == "string" and value:sub(1, 34) == "MGS_COOP_EQUIPMENT_OBSERVATION_V1;" then
                accepted = A.receipt("return 'MGS_COOP_EQUIPMENT_OBSERVATION_RECEIPT_V1' -- " .. value) == true
            end
            -- nil means disabled; false means enabled but the player is still
            -- loading. Retry a bounded number of times on natural updates.
            if value == nil or accepted or D.equipmentObservationAttempts >= 120 then
                D.equipmentObservationMission = mission
            end
        end
        return result
    end
    -- Read-only: a slot list's local ids (vars.<list>[0..7]) and each entry's stock through the retail getter.
    local function census(list, key)
        local ids, counts = {}, {}
        local native = get(PlayerSlotType, key)
        local readable = integer(native, 0, 15) and type(get(Player, "GetAmmoStockBySlot")) == "function"
        for i = 0, 7 do
            local v = get(get(vars, list), i)
            ids[i + 1] = integer(v, 0, 65535) and fmt("%.0f", v) or "na"
            local ok, count = false, nil
            if readable then ok, count = pcall(call, Player, "GetAmmoStockBySlot", native, i) end
            counts[i + 1] = ok and integer(count, 0, 65535) and fmt("%.0f", count) or "na"
        end
        return "ids=" .. table.concat(ids, ",") .. ";counts=" .. table.concat(counts, ",")
    end
    function E.run(a, id)
        local cmd = a[1]
        if cmd == "inventory" and #a == 1 then
            local ids = {}
            for i, name in ipairs({"hip", "back", "secondary"}) do
                local slot = get(get(TppDefine, "WEAPONSLOT"), SLOT[name][1])
                if not integer(slot, 0, 15) then return false, "weapon_slot_unavailable" end
                ids[i] = get(get(vars, "weapons"), slot)
                if not integer(ids[i], 0, 65535) then return false, "inventory_unreadable" end
            end
            return true, fmt("inventory;hip=%.0f;back=%.0f;secondary=%.0f;active=%s", ids[1], ids[2], ids[3], active())
        elseif cmd == "weapon" and #a == 3 then
            local keys = SLOT[a[2]]
            if not keys then return false, "weapon_args" end
            local slot = get(get(TppDefine, "WEAPONSLOT"), keys[1])
            local nativeSlot = get(PlayerSlotType, keys[2])
            if not integer(slot, 0, 15) or not integer(nativeSlot, 0, 15) then return false, "weapon_slot_unavailable" end
            local current = get(get(vars, "weapons"), slot)
            local equip = a[3] == "current" and current or a[3]:match("^EQP_WP_[%w_]+$") and get(TppEquip, a[3])
            if not integer(equip, 1, 65535) then return false, "weapon_unavailable" end
            if current ~= equip then return false, "weapon_not_resident" end
            for _, k in ipairs({"ChangeEquip", "GetAmmoStockBySlot"}) do
                if type(get(Player, k)) ~= "function" then return false, "missing_" .. k end
            end
            local bullet, ammo, stock, subbullet, ammoSub, stockSub = call(TppEquip, "GetAmmoInfo", equip)
            local values = {bullet, ammo, stock, subbullet, ammoSub, stockSub}
            for i = 1, 6 do
                if not integer(values[i], 0, 65535) then return false, "weapon_metadata" end
            end
            local details = fmt(";equip=%.0f;native_slot=%.0f;ammo=%.0f;stock=%.0f", equip, nativeSlot, ammo, stock)
            -- Completion is native inventory feedback, without writes to vars.
            if call(Player, "ChangeEquip", {equipId = equip, stock = stock, stockSub = stockSub, ammo = ammo, ammoSub = ammoSub,
                suppressorLife = 0, isSuppressorOn = false, isLightOn = false, toActive = true, dropPrevEquip = false}) == false then
                return false, "equip_refused"
            end
            local q = {kind = "equip", id = id, mission = D.mission, frames = 0}
            q.poll = function()
                local count, countSub = call(Player, "GetAmmoStockBySlot", nativeSlot, 0)
                if get(get(vars, "weapons"), slot) == equip and get(get(vars, "ammoInWeapons"), slot) == ammo
                    and get(get(vars, "ammoSubInWeapons"), slot) == ammoSub and count == stock and countSub == stockSub then
                    D.pending = nil
                    A.note(id, true, "equip_inventory_readback" .. details .. ";active=" .. active() .. ";render_unverified")
                elseif q.frames >= 300 then D.pending = nil; A.note(id, false, "equip_readback_timeout" .. details) end
            end
            D.pending = q
            return true, "submitted_equip" .. details
        elseif cmd == "drop_item" and #a == 3 then
            local count = number(a[3], 1, 10, true)
            local equip = a[2]:match("^EQP_[%w_]+$") and get(TppEquip, a[2])
            local keys = SLOT[a[2]:match("^current_(%a+)$")]
            if keys then
                local slot = get(get(TppDefine, "WEAPONSLOT"), keys[1])
                if integer(slot, 0, 15) then equip = get(get(vars, "weapons"), slot) end
            end
            if not count or not integer(equip, 1, 65535) then return false, "pickup_args" end
            local resident = false
            for _, name in ipairs({"weapons", "supportWeapons", "items"}) do
                for i = 0, 7 do if get(get(vars, name), i) == equip then resident = true end end
            end
            if not resident then return false, "pickup_not_equipped" end
            if Vector3 == nil then return false, "missing_Vector3" end
            if type(get(Quat, "RotationY")) ~= "function" or type(get(TppPickable, "DropItem")) ~= "function" then
                return false, "missing_pickup_api"
            end
            local p = pose()
            local ok, item = pcall(function()
                return {equipId = equip, number = count, position = Vector3(p[1], p[2] + 1, p[3]),
                    rotation = call(Quat, "RotationY", 0), linearVelocity = Vector3(0, 2, 0), angularVelocity = Vector3(0, 2, 0)}
            end)
            if not ok then return false, "pickup_constructor_error;vector_type=" .. type(Vector3) end
            if call(TppPickable, "DropItem", item) == false then return false, "pickup_refused" end
            return true, "submitted_pickup;number=" .. a[3] .. ";spawn_count_unverified"
        elseif cmd == "support" and #a == 1 then
            return true, "support;" .. census("supportWeapons", "SUPPORT") .. ";active=" .. active()
        elseif cmd == "items" and #a == 1 then
            -- F2 census, read-only: the local item ids, stocks and vars.currentItemIndex.
            local cur = get(vars, "currentItemIndex")
            return true, "items;" .. census("items", "ITEM") .. ";current=" .. (integer(cur, 0, 7) and fmt("%.0f", cur) or "na")
        elseif cmd == "give_item" and #a == 2 then
            -- F2, lab only (the native dispatcher refuses it outside mode=lab): one stock box
            -- EQP_IT_CBox into a FREE sub of the LOCAL player's item list in this running copy
            -- (Player.ChangeEquip toActive=false; a full list is refused, never replaced). A
            -- checkpoint save writes it only into the lab run's own copied save.
            if a[2] ~= "cbox" then return false, "give_item_args" end
            local equip = get(TppEquip, "EQP_IT_CBox")
            if not integer(equip, 1, 65535) then return false, "item_unavailable" end
            local free
            for i = 7, 1, -1 do
                local v = get(get(vars, "items"), i)
                if v == equip then return true, fmt("give_item;equip=%.0f;sub=%d;already=1", equip, i) end
                if v == 0 then free = i end
            end
            if not free then return false, "items_full" end
            if type(get(Player, "ChangeEquip")) ~= "function" then return false, "missing_ChangeEquip" end
            if call(Player, "ChangeEquip", {equipId = equip, stock = 1, stockSub = 0, ammo = 0, ammoSub = 0, suppressorLife = 0,
                isSuppressorOn = false, isLightOn = false, toActive = false, dropPrevEquip = false}) == false then
                return false, "give_item_refused"
            end
            local q = {kind = "equip", id = id, mission = D.mission, frames = 0}
            q.poll = function()
                for i = 1, 7 do
                    if get(get(vars, "items"), i) == equip then
                        D.pending = nil
                        return A.note(id, true, fmt("give_item_readback;equip=%.0f;sub=%d", equip, i))
                    end
                end
                if q.frames >= 300 then D.pending = nil; A.note(id, false, "give_item_readback_timeout") end
            end
            D.pending = q
            return true, fmt("submitted_give_item;equip=%.0f;free=%d", equip, free)
        elseif cmd == "slot" and #a == 3 then
            -- The retail slot selection (Player.SetCurrentSlot, the local player's
            -- equip controller vt+0x10) for the fire census: support/item/hand too.
            local key = ({hip = "PRIMARY_1", back = "PRIMARY_2", secondary = "SECONDARY", support = "SUPPORT", item = "ITEM",
                hand = "HAND"})[a[2]]
            local sub = number(a[3], 0, 7, true)
            local native = key and get(PlayerSlotType, key)
            if not key or not sub then return false, "slot_args" end
            if not integer(native, 0, 15) then return false, "slot_type_unavailable" end
            if type(get(Player, "SetCurrentSlot")) ~= "function" then return false, "missing_SetCurrentSlot" end
            if call(Player, "SetCurrentSlot", {slotType = native, subIndex = sub}) == false then return false, "slot_refused" end
            return true, fmt("submitted_slot;slot=%s;native=%.0f;sub=%.0f;active=%s", a[2], native, sub, active())
        end
        return false, "command_args"
    end
end
