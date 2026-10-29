-- M1 id-aware attribution (docs/vehicles-companions-design.md 6.12, K23): W.attClass in
-- lua/coop/world_outcome.lua on stubbed bindings. No game process.
-- Usage: lua51 world_outcome_attclass_lua_check.lua <world_outcome.lua>
-- Before M1 any horse and any player walker were "local" and every D-Dog / Quiet "buddy",
-- so a mirror's hit was forwarded as the guest's own: the mirror checks here failed.
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(checks .. ': ' .. why, 2) end
end
-- Ids: the local player 0, the partner body 1, horse index 0/1 = 6656/6657, walker index 0/1 =
-- 22528/22529, the dog 9728, Quiet 9984, a soldier 1024. Type indices: dog 19, Quiet 18.
local HORSE0, HORSE1, WALKER0, WALKER1, DOG, QUIET, SOLDIER = 6656, 6657, 22528, 22529, 9728, 9984, 1024
local function Fixture(o)
    o = o or {}
    local env = setmetatable({}, {__index = _G})
    env.vars = {playerVehicleGameObjectId = o.riding or 65535}
    local W = {OP = {RECEIVED = 3, REJECT = 4, OUTCOME = 16, REQ_OUTCOME = 17}, handlers = {}, bindHooks = {}, startHooks = {}, stopHooks = {}, pairedHooks = {}, unpairedHooks = {},
        observers = {}, host = o.host == true, companionOn = o.companion == true, logs = {}}
    W.get = function(t, k) if t == nil then return nil end return t[k] end
    W.log = function(s) W.logs[#W.logs + 1] = s end
    W.enemy = {RANK = {}, TERMINAL = {}, s = {}}
    W.api = {null = 65535,
        isLocal = function(id) return id == 0 end,
        isHorse = function(id) return id == HORSE0 or id == HORSE1 end,
        isWalker = function(id) return id == WALKER0 or id == WALKER1 end,
        typeIndex = function(id) return (id == DOG and 19) or (id == QUIET and 18) or 1 end,
        dog = 19, quiet = 18,
        buddyId = function(name, index)
            if name == "TppHorse2" then return index == 0 and HORSE0 or HORSE1 end
            if name == "TppWalkerGear2" then return index == 0 and WALKER0 or WALKER1 end
        end}
    env.e = {MgsCoopWorld = W}
    local chunk = assert(loadfile(assert(arg[1])))
    setfenv(chunk, env)
    chunk()
    return W, env
end

-- Companion mode off (today's play): the copy owns its buddy; only the second capacity-two
-- mount (index 1, the partner's) is a mirror.
local W = Fixture({host = false})
check(W.attClass(0) == "local", "the local player is local")
check(W.attClass(SOLDIER) == "other" and W.attClass(65535) == "none" and W.attClass(nil) == "none", "others and no attacker")
check(W.attClass(HORSE0) == "buddy", "an unridden own horse is this copy's companion (buddy), not local")
check(W.attClass(WALKER0) == "buddy", "an unpiloted own walker is buddy")
check(W.attClass(DOG) == "buddy" and W.attClass(QUIET) == "buddy", "the own D-Dog and Quiet are buddy")
check(W.attClass(WALKER1) == "mirror", "the partner's walker (index 1) is a mirror")
check(W.attClass(HORSE1) == "mirror", "the partner's horse (index 1) is a mirror")
W = Fixture({host = false, riding = HORSE0})
check(W.attClass(HORSE0) == "local", "the horse the local player rides is local")
W = Fixture({host = false, riding = WALKER0})
check(W.attClass(WALKER0) == "local", "the walker this copy pilots is local")
check(W.attClass(WALKER1) == "mirror", "the other walker stays a mirror while piloting")
W = Fixture({host = false, riding = HORSE1})
check(W.attClass(HORSE1) == "local", "a partner-index horse the local player rides is local")

-- Companion mode on, guest copy: every companion instance is the host's mirror unless ridden here.
W = Fixture({host = false, companion = true})
check(W.attClass(HORSE0) == "mirror", "an unridden horse on the guest copy is a mirror")
check(W.attClass(WALKER0) == "mirror", "an unpiloted walker on the guest copy is a mirror")
check(W.attClass(DOG) == "mirror" and W.attClass(QUIET) == "mirror", "the guest's D-Dog and Quiet are mirrors")
W = Fixture({host = false, companion = true, riding = HORSE0})
check(W.attClass(HORSE0) == "local", "the horse the guest rides is local")
W = Fixture({host = false, companion = true, riding = HORSE1})
check(W.attClass(HORSE0) == "mirror", "a partner-ridden horse (not the local mount) is a mirror")

-- Companion mode on, host copy: the host owns the companion.
W = Fixture({host = true, companion = true})
check(W.attClass(DOG) == "buddy" and W.attClass(HORSE0) == "buddy" and W.attClass(WALKER0) == "buddy",
    "the host's own companion is buddy")
check(W.attClass(WALKER1) == "mirror", "the guest's walker on the host copy is a mirror")

-- The mirror hook: a mirror never counts as the guest's own outcome, and it is logged.
W = Fixture({host = false, companion = true})
check(type(W.mirrorEngaged) == "function", "W.mirrorEngaged exists")
W.mirrorEngaged("Dead", 7, DOG)
check(W.mirrorCount == 1 and W.logs[#W.logs]:find("event=mirror_engaged;kind=dog;msg=Dead;ent=7;", 1, true) == 1,
    "a mirror kill is logged as event=mirror_engaged with its kind")
W.mirrorEngaged("Damage", 8, WALKER0)
check(W.mirrorCount == 2 and W.logs[#W.logs]:find("kind=walker;msg=Damage;ent=8;", 1, true) ~= nil, "a mirror walker's damage")

print("world_outcome_attclass_lua_check: " .. checks .. " checks passed")
