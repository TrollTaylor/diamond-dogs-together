-- Original read-only receipt, executed only by a separately gated owner-thread
-- observer. It calls no game API and never changes vars or equipment.
-- Retail executable SHA256 085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45:
-- customizedWeapon is uint16[3] at vars+0xB06 (registration RVA 0x99C2E4);
-- chimeraParts is uint8[36] at vars+0xB30 (registration RVA 0x99C412).
-- Local initialization records the base ID and 12-byte recipe before assigning
-- the per-instance slot aliases 871/873/875. These arrays describe local gear;
-- their presence does not establish remote resources or a receiver ID mapping.
local missing, faults, invalid = 0, 0, 0
local function read(object, key)
    if object == nil then missing = missing + 1; return nil end
    local ok, value = pcall(function() return object[key] end)
    if not ok then faults = faults + 1; return nil end
    if value == nil then missing = missing + 1 end
    return value
end
local function number(value, maximum)
    if type(value) == "number" and value == value and value >= 0
        and value <= maximum and value % 1 == 0 then
        return string.format("%.0f", value)
    end
    if value ~= nil then invalid = invalid + 1 end
    return "?"
end
local localVars = vars
local definitions = read(TppDefine, "WEAPONSLOT")
local weapons = read(localVars, "weapons")
local customized = read(localVars, "customizedWeapon")
local chimera = read(localVars, "chimeraParts")
local slots, bases, parts = {}, {}, {}
for i, name in ipairs({"PRIMARY_HIP", "PRIMARY_BACK", "SECONDARY"}) do
    local slot = read(definitions, name)
    if type(slot) == "number" and slot == slot and slot >= 0 and slot <= 15 and slot % 1 == 0 then
        slots[i] = number(read(weapons, slot), 65535)
    else
        if slot ~= nil then invalid = invalid + 1 end
        slots[i] = "?"
    end
    bases[i] = number(read(customized, i - 1), 65535)
end
for i = 0, 35 do parts[i + 1] = number(read(chimera, i), 255) end
local mission = number(read(localVars, "missionCode"), 4294967295)
local active = number(read(localVars, "currentInventorySlot"), 15)
return "MGS_COOP_EQUIPMENT_OBSERVATION_V1;mission=" .. mission .. ";active=" .. active
    .. ";slots=" .. table.concat(slots, ",") .. ";base=" .. table.concat(bases, ",")
    .. ";parts=" .. table.concat(parts, ",")
    .. ";complete=" .. ((missing == 0 and faults == 0 and invalid == 0) and "1" or "0")
    .. ";missing=" .. missing .. ";faults=" .. faults .. ";invalid=" .. invalid
    .. ";read_only=1;remote_readiness_unverified=1;receiver_mapping_unverified=1"
