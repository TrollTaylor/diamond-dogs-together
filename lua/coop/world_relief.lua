-- Sortie (relief) vehicle research levers (docs/vehicles-companions-design.md section 3.5,
-- experiments E2, E17, E18; milestone M3). Original. Nothing here runs on its own: every
-- lever is a debug command (lua/coop/debug_vehicle.lua routes relief_*), and every one but the
-- read-only census refuses outside lab mode (CFG mode 1). The game hooks below are installed
-- only by the first lab lever, so play mode never wraps anything.
-- Static facts (capstone on mgsvtpp.exe 1.0.15.4, docs/vehicles-companions-design.md section 8 E2):
--   vars.reliefVehicleCarryType (u32; low byte = relief type 1..10, 0 none) and
--   vars.reliefVehicleCarryAttitude[0..3] (x, y, z, rotY) are what the vehicle manager's
--   SetRelief (vtable +0x78, 0x140A28980) writes and what Vehicle.LoadCarry (0x140A27FB0) reads at
--   the next mission start to request the relief vehicle (manager +0x58, 0x140A29320); LoadCarry
--   clears them in the ACC (40010/40020/40050). Relief type t loads pack t of
--   veh_rl_{west_lv, east_lv, west_trc, east_trc, west_wav_machinegun, west_wav_cannon, east_wav,
--   east_wav_rocket, west_tnk, east_tnk}; its paint is vars.userPresetVehiclePaintType of that
--   type, read again at every request (0x140A27270 -> 0x140A28AF0).
-- Levers (debug commands, results in script-observer.log as world_lua=event=relief; ...):
--   relief_census [tag]        read only: carry vars, index 0 (25600), MB vehicle resources and
--                              GMP, paint presets, the current sortie loadout slot bytes.
--   relief_arm <type> <x> <y> <z> <yaw> [paint]
--                              lab: at the next ACC sortie (SelectNextMissionHeliStartRoute) write
--                              the carry vars, as the sortie prep's SetRelief does (E2); paint (0..15,
--                              or fova0..fova2: Vehicle.paintType.FOVA_n) sets that type's preset too.
--   relief_seize stock|skip    lab: TppMission.SeizeReliefVehicleOn* run (logged) or are skipped (E18).
--   relief_remove instant|fadeout|fulton [quiet]
--                              lab: the stock Seize instance command on index 0 with only that
--                              method option, never DirectAccount / CheckFultonType (E17); quiet
--                              skips this copy's TppTerminal.OnFulton for 25600 (no Mother Base credit).
--   relief_load <type> <x> <y> <z> <rotY>
--                              lab, free roam, index 0 absent: write the carry vars (rotY in radians, as
--                              the census cp= shows) and call Vehicle.LoadCarry now, mid mission: what a
--                              guest continue or late join would need (M3 item 6).
--   relief_status, relief_lab ...  M3 (lua/coop/world_relief_sync.lua W.reliefSync.lever).
--   relief_face                lab: turn in place to face index 0 (within 20 m; captures). Hidden run
--                              fafd3fdd: a warp beside it at the vehicle's height fell through the
--                              30010 enemyBase_S slope, so nothing moves the player any more.
-- The installed hooks also log Vehicle.SaveCarry and TppTerminal.OnFultonVehicle /
-- AddTempResource calls (the Mother Base credit path of a vehicle Fulton).
do
local W = e.MgsCoopWorld
if type(W) == "table" then
local R = {seize = "stock"}
W.relief = R
local fmt, floor, concat = string.format, math.floor, table.concat
local RID = 25600
R.RID = RID
local FREE = {[30010] = true, [30020] = true, [30050] = true}
local HELI = {[40010] = true, [40020] = true, [40050] = true}
R.RES = {{"4wdE", "4wdEast"}, {"4wdW", "4wdWest"}, {"trcE", "TruckEast"}, {"trcW", "TruckWest"},
    {"apcE", "ArmoredVehicleEast"}, {"apcW", "ArmoredVehicleWest"}, {"wavW", "WheeledArmoredVehicleWest"},
    {"tnkE", "TankEast"}, {"tnkW", "TankWest"}}
local REMOVE = {instant = "Instant", fadeout = "FadeOut", fulton = "Fulton"}
-- Relief type t -> its userPresetVehiclePaintType index (the loadout vehicle id - 0x0D, static 0x14095D230).
R.PAINT = {1, 0, 3, 2, 5, 7, 4, 6, 9, 8}
local SEIZE = {"SeizeReliefVehicleOnAbort", "SeizeReliefVehicleOnClear", "SeizeReliefVehicleOnForceGoToMb"}
local function L(text) W.log("event=relief;" .. text) end
local function n(v) return type(v) == "number" and v == v and v ~= math.huge and v ~= -math.huge end
local function num(v, f) return n(v) and fmt(f or "%.0f", v) or "nil" end
local function lab() return W.mode == 1 end
local function send(id, cmd)
    local f = W.get(GameObject, "SendCommand")
    if type(f) ~= "function" then return nil end
    local ok, a, b = pcall(f, id, cmd)
    if ok then return a, b end
    return nil
end
function R.alive() return send(RID, {id = "IsAlive"}) == true end
function R.pose()
    local p = send(RID, {id = "GetPosition"})
    if p == nil then return nil end
    local ok, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end)
    if ok and n(x) and n(y) and n(z) then return x, y, z end
    return nil
end
-- Values 0..hi of a vars array (nil ends it), joined.
local function list(t, hi, f)
    local o = {}
    for i = 0, hi do
        local v = W.get(t, i)
        if v == nil then break end
        o[#o + 1] = num(v, f)
    end
    return #o > 0 and concat(o, ",") or "none"
end
function R.carry()
    local t, a, p = W.get(vars, "reliefVehicleCarryType"), W.get(vars, "reliefVehicleCarryAttitude"), {}
    for i = 0, 3 do
        local v = W.get(a, i)
        p[i + 1] = n(v) and v or 0
    end
    return n(t) and t or -1, p
end
local function carryText(ct, p)
    return fmt("ct=%s;t=%s;cp=%.2f,%.2f,%.2f,%.3f", num(ct), ct >= 0 and tostring(ct % 256) or "nil", p[1], p[2], p[3], p[4])
end
function R.resources()
    local M, o = W.get(_G, "TppMotherBaseManagement"), {}
    local gmp, count = W.get(M, "GetGmp"), W.get(M, "GetResourceUsableCount")
    local okG, g = false, nil
    if type(gmp) == "function" then okG, g = pcall(gmp) end
    o[1] = "gmp=" .. num(okG and g or nil)
    for i, r in ipairs(R.RES) do
        local ok, v = false, nil
        if type(count) == "function" then ok, v = pcall(count, {resource = r[2]}) end
        o[i + 1] = r[1] .. "=" .. num(ok and v or nil)
    end
    return concat(o, ";")
end
-- The current sortie loadout slot (4 slots of 0x73 bytes, decoded at 0x140EEE060), as hex.
local function loadout(tag)
    local buf, slot = W.get(vars, "sortieLoadoutInfoU8buf"), W.get(vars, "currentSortieLoadoutSlot")
    local s = n(slot) and slot >= 0 and slot <= 3 and floor(slot) or 0
    local hex = {}
    for i = 0, 114 do
        local v = W.get(buf, s * 115 + i)
        hex[#hex + 1] = n(v) and fmt("%02x", v % 256) or "zz"
    end
    L(fmt("step=census_lo;tag=%s;slot=%s;ver=%s;nr=%s", tag, num(slot), num(W.get(vars, "sortieLoadoutInfoSaveFormVersion")),
        num(W.get(vars, "currentSortieLoadoutSlotNonRental"))))
    for part = 0, 2 do
        L(fmt("step=census_lo%d;tag=%s;b=%s", part, tag, concat(hex, "", part * 40 + 1, math.min(115, part * 40 + 40))))
    end
end
function R.census(tag)
    tag = tag or "now"
    local m, role = W.get(vars, "missionCode"), W.host and "host" or "guest"
    local ct, p = R.carry()
    L(fmt("step=census;tag=%s;m=%s;role=%s;route=%s;%s", tag, num(m), role, num(W.get(gvars, "heli_missionStartRoute")), carryText(ct, p)))
    local st = W.get(vars, "reliefVehicleCarryState")
    L(fmt("step=census_carry;tag=%s;life=%s;ammo=%s;st=%s", tag, list(W.get(vars, "reliefVehicleCarryLife"), 15),
        list(W.get(vars, "reliefVehicleCarryAmmo"), 7), n(st) and num(st) or list(st, 3)))
    local alive, x, y, z = R.alive(), R.pose()
    local vt, riders = send(RID, {id = "GetVehicleType"}), send(RID, {id = "GetRiderId"})
    local r0 = type(riders) == "table" and W.get(riders, 1) or nil
    local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
    local d = x and n(px) and n(pz) and math.sqrt((x - px) ^ 2 + (z - pz) ^ 2)
    L(fmt("step=census_v0;tag=%s;alive=%d;vt=%s;x=%s;y=%s;z=%s;rider0=%s;dist=%s", tag, alive and 1 or 0, num(vt), num(x, "%.2f"),
        num(y, "%.2f"), num(z, "%.2f"), num(r0), num(d, "%.1f")))
    L(fmt("step=census_res;tag=%s;%s", tag, R.resources()))
    local pt = W.get(Vehicle, "paintType")
    L(fmt("step=census_paint;tag=%s;p=%s;fova=%s,%s,%s;none=%s", tag, list(W.get(vars, "userPresetVehiclePaintType"), 9),
        num(W.get(pt, "FOVA_0")), num(W.get(pt, "FOVA_1")), num(W.get(pt, "FOVA_2")), num(W.get(pt, "NONE"))))
    pcall(loadout, tag)
    return true, fmt("relief;m=%s;t=%s;v0=%d;vt=%s;x=%s;z=%s", num(m), ct >= 0 and tostring(ct % 256) or "nil", alive and 1 or 0,
        num(vt), num(x, "%.1f"), num(z, "%.1f"))
end
-- Apply an armed choice: the carry vars the sortie prep's SetRelief writes. "sortie" is the ACC sortie
-- (TppMission.SelectNextMissionHeliStartRoute, before OnEndMissionPreparation: where the sortie prep calls
-- SetRelief; the ACC's own LoadCarry has already cleared them); "loadcarry" / "initnew" are a free-roam
-- mission start, before the original reads them (fallbacks; hidden run 698e67cf showed the Vehicle table
-- refuses writes and logged no InitializeForNewMission call).
function R.apply(where)
    local a = R.armed
    if not a then return end
    local m = W.get(vars, "missionCode")
    if not (where == "sortie" and HELI[m] or where ~= "sortie" and FREE[m]) then
        L(fmt("step=arm_wait;where=%s;m=%s", where, num(m)))
        return
    end
    R.armed = nil
    local before = R.carry()
    local ok = pcall(function()
        vars.reliefVehicleCarryType = a.type
        local t = vars.reliefVehicleCarryAttitude
        t[0], t[1], t[2], t[3] = a.x, a.y, a.z, a.yaw
        if a.paint and R.PAINT[a.type] then vars.userPresetVehiclePaintType[R.PAINT[a.type]] = a.paint end
    end)
    local after, p = R.carry()
    L(fmt("step=arm_applied;where=%s;m=%s;ok=%d;before=%s;%s", where, num(m), ok and 1 or 0, num(before), carryText(after, p)))
end
-- Replace t[name] by make(original); true only when the table now holds the wrapper.
local function wrap(t, name, make)
    local original = W.get(t, name)
    if type(original) ~= "function" then return false end
    local f = make(original)
    local ok = pcall(function() t[name] = f end)
    return ok and W.get(t, name) == f
end
local function args(...)
    local o = {}
    for i = 1, math.min(select("#", ...), 6) do
        local v = select(i, ...)
        o[i] = type(v) == "number" and num(v) or type(v) == "string" and #v <= 24 and v or type(v)
    end
    return concat(o, ",")
end
function R.install()
    if R.hook then return R.hook end
    R.hook = "none"
    local function after(label, ct0, ...)
        local ct1, p = R.carry()
        L(fmt("step=%s;m=%s;before=%s;%s", label, num(W.get(vars, "missionCode")), num(ct0), carryText(ct1, p)))
        return ...
    end
    local function carryCall(label, original, ...)
        if label ~= "savecarry" then pcall(R.apply, label) end
        return after(label, R.carry(), original(...))
    end
    local hooks = {}
    for _, h in ipairs({{Vehicle, "LoadCarry", "loadcarry"}, {TppVarInit, "InitializeForNewMission", "initnew"},
        {TppMission, "SelectNextMissionHeliStartRoute", "sortie"}}) do
        if wrap(h[1], h[2], function(o) return function(...) return carryCall(h[3], o, ...) end end) then hooks[#hooks + 1] = h[3] end
    end
    R.hook = #hooks > 0 and concat(hooks, "+") or "none"
    R.saveHook = wrap(Vehicle, "SaveCarry", function(o) return function(...) carryCall("savecarry", o, ...) end end)
    R.seizeHooks = 0
    for _, name in ipairs(SEIZE) do
        if wrap(TppMission, name, function(o) return function(...)
            local alive = R.alive() and 1 or 0
            if R.seize == "skip" then
                L(fmt("step=seize_skipped;fn=%s;alive=%d", name, alive))
                return
            end
            L(fmt("step=seize;fn=%s;alive=%d", name, alive))
            return o(...)
        end end) then R.seizeHooks = R.seizeHooks + 1 end
    end
    R.creditHooks = 0
    for _, name in ipairs({"OnFulton", "OnFultonVehicle", "AddTempResource"}) do
        if wrap(TppTerminal, name, function(o) return function(...)
            if name == "OnFulton" and R.quiet and (...) == RID then
                R.quiet = nil
                L(fmt("step=credit_skipped;fn=%s;args=%s", name, args(...)))
                return
            end
            L(fmt("step=credit;fn=%s;args=%s", name, args(...)))
            return o(...)
        end end) then R.creditHooks = R.creditHooks + 1 end
    end
    L(fmt("step=installed;hook=%s;save=%d;seize=%d;credit=%d", R.hook, R.saveHook and 1 or 0, R.seizeHooks, R.creditHooks))
    return R.hook
end
function R.remove(how, quiet)
    if not R.alive() then return false, "relief_absent" end
    if W.get(vars, "playerVehicleGameObjectId") == RID then return false, "relief_ridden" end
    R.install()
    R.quiet = quiet or nil
    local x, y, z = R.pose()
    send(RID, {id = "Seize", options = {REMOVE[how]}})
    local alive = R.alive() and 1 or 0
    L(fmt("step=remove;how=%s;quiet=%d;alive_after=%d;x=%s;z=%s;%s", how, quiet and 1 or 0, alive, num(x, "%.1f"), num(z, "%.1f"), R.resources()))
    return true, fmt("relief_remove;how=%s;quiet=%d;alive=%d", how, quiet and 1 or 0, alive)
end
-- Mid-mission request: the carry vars, then the stock Vehicle.LoadCarry (vehicle manager RequestRelief,
-- 0x140A29320) outside a mission start. Free roam only (the ACC's LoadCarry clears the carry), never over a
-- live index 0 or an armed sortie choice; no hook is installed.
function R.load(t, x, y, z, rotY)
    local m = W.get(vars, "missionCode")
    if not FREE[m] then return false, "relief_not_free_roam" end
    if R.alive() then return false, "relief_present" end
    if R.armed then return false, "relief_armed_pending" end
    local f = W.get(Vehicle, "LoadCarry")
    if type(f) ~= "function" then return false, "relief_loadcarry_binding" end
    local before = R.carry()
    local okW = pcall(function()
        vars.reliefVehicleCarryType = t
        local a = vars.reliefVehicleCarryAttitude
        a[0], a[1], a[2], a[3] = x, y, z, rotY
    end)
    local okC = okW and pcall(f) or false
    local after, p = R.carry()
    local alive = R.alive() and 1 or 0
    L(fmt("step=load;m=%s;write=%d;call=%d;before=%s;%s;alive_after=%d", num(m), okW and 1 or 0, okC and 1 or 0, num(before),
        carryText(after, p), alive))
    return okW and okC, fmt("relief_load;type=%d;call=%d;alive=%d", t, okC and 1 or 0, alive)
end
-- Turn in place (TppPlayer.Warp to the player's own position; rotY in degrees, forward = (sin, cos)).
function R.face()
    local x, y, z = R.pose()
    if not x then return false, "relief_absent" end
    local px, py, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
    if not (n(px) and n(py) and n(pz)) then return false, "relief_player_pose" end
    local dx, dz = x - px, z - pz
    local d = math.sqrt(dx * dx + dz * dz)
    if d > 20 then return false, fmt("relief_far;dist=%.1f", d) end
    local warp = W.get(TppPlayer, "Warp")
    if type(warp) ~= "function" then return false, "relief_warp_binding" end
    local yaw = math.deg(math.atan2(dx, dz))
    local ok = pcall(warp, {pos = {px, py, pz}, rotY = yaw})
    L(fmt("step=face;ok=%d;dist=%.1f;yaw=%.1f;x=%.2f;z=%.2f", ok and 1 or 0, d, yaw, x, z))
    return ok, ok and fmt("relief_face;dist=%.1f;yaw=%.1f", d, yaw) or "relief_warp_error"
end
local function real(s, lo, hi)
    local v = type(s) == "string" and s:match("^%-?%d+%.?%d*$") and tonumber(s)
    return v and v >= lo and v <= hi and v or nil
end
-- M6 E6 (docs/vehicles-companions-design.md section 8): vehicle_census [first 0..511] [n 1..64], read-only, lab or
-- play: each live TppVehicle2 id 25600+first.. (GetVehicleType answers) with its GetRiderId seat count and riders and
-- its pose, one line each (event=vehicle_census); the reply counts them.
function R.vehicles(a)
    local first, cnt = real(a[2] or "0", 0, 511), real(a[3] or "48", 1, 64)
    if #a > 3 or not (first and cnt and first == floor(first) and cnt == floor(cnt)) then return false, "vehicle_census_args" end
    local found, o = 0, {}
    for i = first, math.min(511, first + cnt - 1) do
        local id = RID + i
        local t = send(id, {id = "GetVehicleType"})
        if n(t) then
            local r, s = send(id, {id = "GetRiderId"}), {}
            if type(r) == "table" then for k = 1, 16 do if r[k] == nil then break end s[k] = num(r[k]) end end
            local p, x, y, z = send(id, {id = "GetPosition"})
            if p ~= nil then local ok; ok, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end) end
            W.log(fmt("event=vehicle_census; id=%d; type=%s; seats=%d; riders=%s; x=%s; y=%s; z=%s", id, num(t), #s,
                #s > 0 and concat(s, ",") or "none", num(x, "%.1f"), num(y, "%.1f"), num(z, "%.1f")))
            found = found + 1
            if #o < 8 then o[#o + 1] = fmt("%d:%s:%d", id, num(t), #s) end
        end
    end
    return true, fmt("vehicle_census;first=%d;n=%d;found=%d;%s", first, cnt, found, concat(o, ","))
end
function R.run(a)
    local cmd = a[1]
    if cmd == "vehicle_census" then return R.vehicles(a) end
    if cmd == "relief_census" and (#a == 1 or #a == 2 and a[2]:match("^[%w_]+$") and #a[2] <= 16) then return R.census(a[2]) end
    if cmd == "relief_status" or cmd == "relief_lab" then
        local S = W.reliefSync
        if type(S) ~= "table" or type(S.lever) ~= "function" then return false, "relief_sync_unavailable" end
        return S.lever(a)
    end
    if cmd ~= "relief_census" and not lab() then return false, "relief_lab_only" end
    if cmd == "relief_arm" and (#a == 6 or #a == 7) then
        local t, x, y, z, yaw = real(a[2], 0, 10), real(a[3], -10000, 10000), real(a[4], -1000, 3000), real(a[5], -10000, 10000), real(a[6], -360, 360)
        local paint = a[7] and (a[7]:match("^fova[012]$") and W.get(W.get(Vehicle, "paintType"), "FOVA_" .. a[7]:sub(5)) or real(a[7], 0, 15))
        if not (t and t == floor(t) and x and y and z and yaw) or a[7] and not (n(paint) and paint == floor(paint) and paint >= 0 and paint <= 255) then
            return false, "relief_args"
        end
        R.armed = {type = t, x = x, y = y, z = z, yaw = yaw, paint = paint}
        local hook = R.install()
        L(fmt("step=armed;type=%d;x=%.2f;y=%.2f;z=%.2f;yaw=%.2f;hook=%s;paint=%s", t, x, y, z, yaw, hook, num(paint)))
        return true, fmt("relief_arm;type=%d;hook=%s", t, hook)
    elseif cmd == "relief_seize" and #a == 2 and (a[2] == "stock" or a[2] == "skip") then
        R.seize = a[2]
        local hook = R.install()
        L(fmt("step=seize_mode;mode=%s;hooks=%d", a[2], R.seizeHooks or 0))
        return true, fmt("relief_seize=%s;hooks=%d;hook=%s", a[2], R.seizeHooks or 0, hook)
    elseif cmd == "relief_remove" and (#a == 2 or #a == 3 and a[3] == "quiet") and REMOVE[a[2]] then
        return R.remove(a[2], a[3] == "quiet")
    elseif cmd == "relief_load" and #a == 6 then
        local t, x, y, z, r = real(a[2], 1, 10), real(a[3], -10000, 10000), real(a[4], -1000, 3000), real(a[5], -10000, 10000), real(a[6], -7, 7)
        if not (t and t == floor(t) and x and y and z and r) then return false, "relief_args" end
        return R.load(t, x, y, z, r)
    elseif cmd == "relief_face" and #a == 1 then
        return R.face()
    end
    return false, "relief_args"
end
end
end
