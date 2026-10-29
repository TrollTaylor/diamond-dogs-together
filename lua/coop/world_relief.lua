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
--   relief_arm <type> <x> <y> <z> <yaw>
--                              lab: at the next ACC sortie (SelectNextMissionHeliStartRoute) write
--                              the carry vars, as the sortie prep's SetRelief does (E2).
--   relief_seize stock|skip    lab: TppMission.SeizeReliefVehicleOn* run (logged) or are skipped (E18).
--   relief_remove instant|fadeout|fulton
--                              lab: the stock Seize instance command on index 0 with only that
--                              method option, never DirectAccount / CheckFultonType (E17).
--   relief_goto [dx]           lab: warp 8 m south of index 0, facing it (captures).
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
    L(fmt("step=census_paint;tag=%s;p=%s", tag, list(W.get(vars, "userPresetVehiclePaintType"), 9)))
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
    for _, name in ipairs({"OnFultonVehicle", "AddTempResource"}) do
        if wrap(TppTerminal, name, function(o) return function(...)
            L(fmt("step=credit;fn=%s;args=%s", name, args(...)))
            return o(...)
        end end) then R.creditHooks = R.creditHooks + 1 end
    end
    L(fmt("step=installed;hook=%s;save=%d;seize=%d;credit=%d", R.hook, R.saveHook and 1 or 0, R.seizeHooks, R.creditHooks))
    return R.hook
end
function R.remove(how)
    if not R.alive() then return false, "relief_absent" end
    if W.get(vars, "playerVehicleGameObjectId") == RID then return false, "relief_ridden" end
    R.install()
    local x, y, z = R.pose()
    send(RID, {id = "Seize", options = {REMOVE[how]}})
    local alive = R.alive() and 1 or 0
    L(fmt("step=remove;how=%s;alive_after=%d;x=%s;z=%s;%s", how, alive, num(x, "%.1f"), num(z, "%.1f"), R.resources()))
    return true, fmt("relief_remove;how=%s;alive=%d", how, alive)
end
function R.warpTo(dx)
    local x, y, z = R.pose()
    if not x then return false, "relief_absent" end
    local warp = W.get(TppPlayer, "Warp")
    if type(warp) ~= "function" then return false, "relief_warp_binding" end
    local ok = pcall(warp, {pos = {x + dx, y + 0.5, z - 8}, rotY = 0})
    L(fmt("step=goto;ok=%d;x=%.2f;y=%.2f;z=%.2f;dx=%.1f", ok and 1 or 0, x, y, z, dx))
    return ok, ok and fmt("relief_goto;x=%.1f;z=%.1f;dx=%.1f", x, z, dx) or "relief_warp_error"
end
local function real(s, lo, hi)
    local v = type(s) == "string" and s:match("^%-?%d+%.?%d*$") and tonumber(s)
    return v and v >= lo and v <= hi and v or nil
end
function R.run(a)
    local cmd = a[1]
    if cmd == "relief_census" and (#a == 1 or #a == 2 and a[2]:match("^[%w_]+$") and #a[2] <= 16) then return R.census(a[2]) end
    if cmd ~= "relief_census" and not lab() then return false, "relief_lab_only" end
    if cmd == "relief_arm" and #a == 6 then
        local t, x, y, z, yaw = real(a[2], 0, 10), real(a[3], -10000, 10000), real(a[4], -1000, 3000), real(a[5], -10000, 10000), real(a[6], -360, 360)
        if not (t and t == floor(t) and x and y and z and yaw) then return false, "relief_args" end
        R.armed = {type = t, x = x, y = y, z = z, yaw = yaw}
        local hook = R.install()
        L(fmt("step=armed;type=%d;x=%.2f;y=%.2f;z=%.2f;yaw=%.2f;hook=%s", t, x, y, z, yaw, hook))
        return true, fmt("relief_arm;type=%d;hook=%s", t, hook)
    elseif cmd == "relief_seize" and #a == 2 and (a[2] == "stock" or a[2] == "skip") then
        R.seize = a[2]
        local hook = R.install()
        L(fmt("step=seize_mode;mode=%s;hooks=%d", a[2], R.seizeHooks or 0))
        return true, fmt("relief_seize=%s;hooks=%d;hook=%s", a[2], R.seizeHooks or 0, hook)
    elseif cmd == "relief_remove" and #a == 2 and REMOVE[a[2]] then
        return R.remove(a[2])
    elseif cmd == "relief_goto" and #a <= 2 then
        local dx = a[2] == nil and 0 or real(a[2], -20, 20)
        if not dx then return false, "relief_args" end
        return R.warpTo(dx)
    end
    return false, "relief_args"
end
end
end
