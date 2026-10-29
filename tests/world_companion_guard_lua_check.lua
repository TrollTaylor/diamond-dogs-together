-- D7 dog guard (docs/vehicles-companions-design.md 6.12, 8.9): lua/coop/world_companion_guard.lua on top of the M2 hold
-- (world_buddy.lua, world_companion_dog.lua) with a recording companion module and native receipts. No game process.
-- Usage: lua51 world_companion_guard_lua_check.lua <world_buddy.lua> <world_companion_dog.lua> <world_companion_guard.lua>
-- Written before world_companion_guard.lua: the first check after the chunk-size checks failed (no W.dogGuard).
local buddyFile, dogFile, guardFile = assert(arg[1]), assert(arg[2]), assert(arg[3])
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ": " .. why, 2) end end
local function size(f) local h = assert(io.open(f, "rb")); local b = h:read("*a"); h:close(); return #b:gsub("\r\n", "\n") end
check(size(guardFile) <= 32000 and size(dogFile) <= 32000, "the chunks fit the 32000-byte world chunk limit")
local POSE = 57
local function vec(x, y, z) return {GetX = function() return x end, GetY = function() return y end, GetZ = function() return z end} end
local function F(host, o)
  o = o or {}
  local c = {time = 1000, active = true, block = true, buddy = 2, id = 9728, pos = vec(1, 2, 3), calls = {}, tx = {}, logs = {}, guard = {},
    life = 10000, max = 10000, drops = 0, hostByte = 0x80 + 31, armedReply = 1}
  local env = setmetatable({}, {__index = _G})
  env.e = {}
  env.vars = {missionCode = 30010, playerRotY = 45, deadBuddyType = 0, buddyCommonFlag = 32}
  env.BuddyType = {NONE = 0, HORSE = 1, DOG = 2, QUIET = 3, WALKER_GEAR = 4}
  env.Vector3 = vec
  env.DemoDaemon = {IsDemoPlaying = function() return false end}
  env.GameObject = {NULL_ID = 65535, GetGameObjectIdByIndex = function() return c.id end}
  env.TppBuddyService = {UnsetDeadBuddyType = function(t) c.calls[#c.calls + 1] = {name = "UnsetDeadBuddyType", t = t}; env.vars.deadBuddyType = 0 end}
  env.TppBuddy2BlockController = {GetActiveBuddyType = function() return c.buddy end, IsBlockActive = function() return c.block end,
    CallBuddy = function() return true end,
    DeleteBuddy = function() c.calls[#c.calls + 1] = {name = "DeleteBuddy", token = c.W.companion.token}; return true end}
  local W = {active = true, paired = true, eff = 3, level = 3, host = host, mission = 30010, epoch = 42, mode = o.play and 2 or 1,
    MISSIONS = {[30010] = {}, [30020] = {}, [30050] = {}}, OP = {}, api = {}, bindHooks = {}, startHooks = {}, stopHooks = {},
    pairedHooks = {}, unpairedHooks = {}, handlers = {}, companionOn = o.off ~= true, mirrorCount = 0}
  local C = {orig = {}}
  C.on = function() return W.companionOn == true end
  C.live = function() return C.on() and o.peer ~= false end
  C.applies = function() return C.live() and not W.host end
  C.tail = function() return "" end
  C.tick = function() c.ctick = (c.ctick or 0) + 1 end
  C.lever = function(a) c.calls[#c.calls + 1] = {name = "lever", a = a}; return true, "orig=" .. table.concat(a, ",") end
  C.lib = {BT = function(n) return env.BuddyType[n] end, TBC = function(n) return env.TppBuddy2BlockController[n] end,
    active = function() return c.buddy end, blockActive = function() return c.block end, vector = vec}
  W.companion = C
  env.e.MgsCoopWorld = W
  W.ent, W.entOrder = {}, {}
  W.get = function(t, k) return t and t[k] end
  W.now = function() return c.time end
  W.tick32 = function(n) return n % 4294967296 end
  W.inGame = function() return c.active end
  W.log = function(s) c.logs[#c.logs + 1] = s end
  W.tx = function(op, ...) c.tx[#c.tx + 1] = {op, ...}; return true end
  W.api = {null = 65535, send = function(id, cmd) c.calls[#c.calls + 1] = {name = cmd.id, id = id, cmd = cmd} end}
  W.receipt = function(source)
    local g = {source:match("^return 'MGS_COOP_DOG_GUARD_V1' %-%- (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+)$")}
    if #g == 7 then
      for i = 1, 7 do g[i] = tonumber(g[i]) end
      c.guard[#c.guard + 1] = g
      if c.noGuard then return "MGS_COOP_DOG_GUARD_V1" end
      local arm = g[4] == 1 and c.armedReply or 0
      return {ready = true, reason = "ok", armed = arm, installed = 1, snap = 1, life = c.life, max = c.max, stun = 10000, smax = 10000,
        state = 0, byte = host and c.hostByte or 0, drops = c.drops, hits = c.drops, applied = 0, kills = 0, attacker = 2051, attack = 7}
    end
    c.calls[#c.calls + 1] = {name = "NativePose", source = source}
    if c.pos == nil then return {ready = false, reason = "dog_unrealized"} end
    local _, _, m, e = source:find("%-%- (%d+) (%d+)")
    return {ready = true, mission = W.mission, epoch = tonumber(e), id = 9728,
      pos = {math.floor(c.pos:GetX() * 1000 + 0.5), math.floor(c.pos:GetY() * 1000 + 0.5), math.floor(c.pos:GetZ() * 1000 + 0.5)}}
  end
  setfenv(assert(loadfile(buddyFile)), env)()
  setfenv(assert(loadfile(dogFile)), env)()
  -- The M2 levers (world_companion_dog.lua) recorded: the guard chunk wraps C.lever.
  C.lever = function(a) c.calls[#c.calls + 1] = {name = "lever", a = a}; return true, "orig=" .. table.concat(a, ",") end
  if not o.noGuardChunk then setfenv(assert(loadfile(guardFile)), env)() end
  for _, f in ipairs(W.bindHooks) do f(W.api, function(t, n, k) return t and t[k] end, function(t, n, k) return t and t[k] end) end
  c.W, c.B, c.C, c.env = W, W.buddy, C, env
  function c:tick(at) if at then self.time = at end; self.B.tick(self.time); self.W.companion.tick(self.time) end
  function c:pose(tick, st, present, k)
    self.W.handlers[POSE]({tick, k or 1, 0, present == nil and 1 or present, 4000, 2000, 3000, 100, st or 0, 0}, self.time)
  end
  function c:count(name) local n = 0; for _, v in ipairs(self.calls) do if v.name == name then n = n + 1 end end; return n end
  function c:sent(op) local r = {}; for _, t in ipairs(self.tx) do if t[1] == op then r[#r + 1] = t end end; return r end
  function c:logged(p) for _, l in ipairs(self.logs) do if l:find(p) then return l end end end
  function c:lastGuard() return self.guard[#self.guard] end
  return c
end

check(F(true).W.dogGuard ~= nil, "the guard chunk publishes W.dogGuard")

-- Host: the native byte of its own dog rides COMPANION_POSE `state`; a change is sent at once.
local h = F(true)
h:tick()
local p = h:sent(POSE)
check(#p == 1 and p[1][10] == 0x80 + 31, "COMPANION_POSE state carries the host dog's byte (valid, normal, 31/31)")
local q = h:lastGuard()
check(q and q[1] == 30010 and q[2] == 42 and q[3] == 0 and q[4] == 0 and q[6] == 0, "the host reads its own record: role 0, never armed")
h:tick(1100); check(#h:sent(POSE) == 1, "an idle dog with an unchanged byte is not re-sent at 10 Hz")
h.hostByte = 0x80 + 64; h:tick(1200); p = h:sent(POSE)
check(#p == 2 and p[2][10] == 0x80 + 64, "the host's dog down: sent at once")
check(h:logged("event=dog_guard;role=host;step=state;byte=192;state=2;life31=0"), "the host logs its dog's state change")
h.buddy = 1; h:tick(1300); p = h:sent(POSE); check(p[#p][10] == 0, "no dog: byte 0")
h = F(true, {noGuardChunk = true}); h:tick(); check(h:sent(POSE)[1][10] == 0, "without the guard chunk the state stays 0 (as M2)")
h = F(true); h.noGuard = true; h:tick(); check(h:sent(POSE)[1][10] == 0, "an older loader (no receipt): byte 0")
check(#F(true).guard == 0, "nothing is read before a tick")

-- Guest: armed while its copy of the host's dog is loaded and paired, with the host's latest byte; re-armed every
-- 500 ms or at once on a new byte; disarmed (arm 0) once when the dog is gone, unpaired or the module is off.
local g = F(false)
g:pose(900, 0x80 + 31); g:tick()
q = g:lastGuard()
check(q and q[3] == 1 and q[4] == 1 and q[5] == 0x80 + 31, "the guest arms with the host's byte")
check(g:logged("event=dog_guard;role=guest;step=armed;reason=ok;byte=159"), "armed is logged")
local n = #g.guard
g:tick(1100); check(#g.guard == n, "not re-armed within 500 ms")
g:pose(1150, 0x80 + 16); g:tick(1200); check(#g.guard == n + 1 and g:lastGuard()[5] == 0x80 + 16, "a new byte is armed at once")
g:tick(1800); check(#g.guard == n + 2 and g:lastGuard()[4] == 1, "the lease is renewed every 500 ms")
g.drops = 5; g:tick(2400); check(g:logged("event=dog_guard;role=guest;step=drops;drops=5;hits=5;attacker=2051;attack=7"), "dropped hits are logged")
g.buddy = 0; g:tick(2500); q = g:lastGuard()
check(q[4] == 0 and g:logged("step=disarmed;why=no_dog"), "no dog: disarmed")
n = #g.guard; g:tick(2600); check(#g.guard == n, "disarmed once")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.paired = false; for _, f in ipairs(g.W.unpairedHooks) do f(1) end
check(g:lastGuard()[4] == 0 and g:logged("why=unpaired"), "an unpair disarms")
g = F(false, {peer = false}); g:pose(900, 0x80 + 31); g:tick(); g:tick(1600); check(#g.guard == 0, "the peer's key not seen: never armed")
g = F(false, {off = true}); g:tick(); check(#g.guard == 0, "key off: never armed")
g = F(false); g:tick(); check(g:lastGuard()[5] == 0, "no host stream yet: armed with byte 0 (guard only, no follow)")
g = F(false); g.C.host = {kind = 1, present = true, state = 0x80 + 9, at = 990}; g:tick(); check(g:lastGuard()[5] == 0x80 + 9, "COMPANION's state when no pose stream")
g = F(false); g.armedReply = 0; g:pose(900, 0x80 + 31); g:tick(); check(g:logged("step=refused;reason=ok"), "a refused arm is logged")
g = F(false); g.noGuard = true; g:pose(900, 0x80 + 31); g:tick(); check(g:logged("step=no_receipt;"), "an older loader is named once")
h = F(true); h:tick(); h:tick(1600); for _, r in ipairs(h.guard) do check(r[4] == 0, "the host never arms") end

-- Restore: the copy dies while the host's dog lives: the dead-buddy vars come back at once, the copy is sent away
-- after 1.5 s and the reconcile is primed to load the host's dog now; back once it reads again.
g = F(false)
g:pose(900, 0x80 + 31); g:tick()
g.env.vars.deadBuddyType, g.env.vars.buddyCommonFlag = 4, 0
g.W.companionHurt("Dead", 9728, "other")
check(g:logged("event=dog_restore;role=guest;step=dead;host=alive;byte=159;armed=1"), "the death is logged with the host's state")
g:pose(1050, 0x80 + 31); g:tick(1100)
check(g.env.vars.deadBuddyType == 0 and g.env.vars.buddyCommonFlag == 32 and g:logged("step=vars;dead=4>0;flag=0>32"), "the vars come back")
g:tick(2000); check(g:count("DeleteBuddy") == 0, "not sent away before 1.5 s")
g:pose(2550, 0x80 + 31); g:tick(2600)
check(g:count("DeleteBuddy") == 1 and g.calls[#g.calls].token == true and g:logged("step=delete;ok=1"), "sent away with the module's token")
g:pose(2650, 0x80 + 31); g:tick(2700); check(g:count("DeleteBuddy") == 1, "once")
g.buddy = 0; g:pose(2750, 0x80 + 31); g:tick(2800)
check(g.C.mismatchAt and g.C.mismatchAt <= 2800 - 60000 and g.C.fixN == nil and g:logged("step=reload;n=1"), "the reconcile is primed")
g.buddy = 2; g:pose(2850, 0x80 + 31); g:tick(2900); check(g:logged("step=back;ms=1900"), "back when the dog reads again")
check(g:lastGuard()[4] == 1, "and armed again")
-- Mirrored: the host's dog is down too: no send-away, no reload; the host's re-call brings it back (reconcile).
g = F(false)
g:pose(900, 0x80 + 31); g:tick()
g:pose(1050, 0x80 + 64); g:tick(1100)
check(g:lastGuard()[5] == 0x80 + 64, "the down byte reaches the native follow (the copy's own death)")
g.W.companionHurt("Dead", 9728, "other")
check(g:logged("step=dead;host=down;byte=192"), "a death with the host's down is mirrored")
local before = g.C.mismatchAt
g:pose(2550, 0x80 + 64); g:tick(2600); g.buddy = 0; g:tick(2700); g:tick(5000)
check(g:count("DeleteBuddy") == 0 and g.C.mismatchAt == before, "no send-away and no primed reload while the host's is down")
g.buddy = 2; g:pose(9000, 0x80 + 31); g:tick(9100); check(g:logged("step=recalled;ms="), "the host's re-call is logged when the copy is back")
-- Gone without a Dead message while the host's dog lives (present): primed once after 2 s.
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.buddy = 0
for t = 1000, 3000, 100 do g:pose(t - 50, 0x80 + 31); g:tick(t) end
check(not g:logged("step=vanished"), "not before 2 s")
g:pose(3050, 0x80 + 31); g:tick(3100)
check(g:logged("step=vanished;reload=1;") and g.C.mismatchAt <= 3100 - 60000, "a vanished copy primes the reconcile after 2 s")
local m = g.C.mismatchAt; g:pose(3150, 0x80 + 31); g:tick(3200); check(g.C.mismatchAt == m, "once")
g = F(false); g:pose(900, 0, 0); g:tick(); g.buddy = 0
for t = 1000, 4000, 100 do g:pose(t - 50, 0, 0); g:tick(t) end
check(not g:logged("step=vanished"), "a host dog that is not present is no vanish")
-- Bounded: three restores per mission.
g = F(false); g:pose(900, 0x80 + 31); g:tick()
for k = 1, 4 do
  g.buddy = 2; g:pose(g.time + 50, 0x80 + 31); g:tick(g.time + 100)
  g.W.companionHurt("Dead", 9728, "other")
  g.buddy = 0; g:pose(g.time + 50, 0x80 + 31); g:tick(g.time + 100)
  g.buddy = 2; g:pose(g.time + 50, 0x80 + 31); g:tick(g.time + 100)
end
check(g:logged("step=reload;n=3") and not g:logged("step=reload;n=4") and g:logged("step=limit;"), "at most three restores per mission")
for _, f in ipairs(g.W.startHooks) do f() end
check(g.W.dogGuard.s.restores == nil, "a new mission resets the count")
g = F(true); g.W.companionHurt("Dead", 9728, "other"); check(not g:logged("event=dog_restore"), "the host never restores")
g = F(false); g.W.companionHurt("Dead", 2001, "other"); check(not g:logged("event=dog_restore"), "another id is no dog death")

-- Lab levers.
h = F(true)
local ok, r = h.C.lever({"companion_lab", "dog_life", "30"})
check(ok and r == "dog_life;pct=30;queued=1", "host dog_life queued")
h:tick(); q = h:lastGuard(); check(q[6] == 1 and q[7] == 30 and q[4] == 0, "the host's next read carries lab op 1, 30%")
h:tick(1100); check(h:lastGuard()[6] == 0, "once")
ok, r = h.C.lever({"companion_lab", "dog_stun", "0"}); h:tick(1200); q = h:lastGuard(); check(ok and q[6] == 2 and q[7] == 0, "dog_stun is op 2")
check(not h.C.lever({"companion_lab", "dog_life", "101"}) and not h.C.lever({"companion_lab", "dog_life", "x"}), "bad percent refused")
ok, r = h.C.lever({"companion_lab", "recall"})
check(ok and h:count("UnsetDeadBuddyType") == 1 and h.calls[#h.calls].name == "lever" and h.calls[#h.calls].a[2] == "call_here" and
  h.calls[#h.calls].a[3] == "dog" and r:find("^recall;dead="), "recall: the dead bit cleared, then the stock call_here dog lever")
ok, r = h.C.lever({"companion_lab", "kill_mirror"}); check(not ok and r == "guest_lab_only", "kill_mirror is the guest's")
ok, r = h.C.lever({"companion_lab", "dog_state"}); check(ok and r:find("^dog_state;role=host;ready=true;reason=ok;armed=0;life=10000;max=10000"), "dog_state reads the host's record")
ok, r = h.C.lever({"companion_lab", "near_guard", "on"}); check(ok and r == "orig=companion_lab,near_guard,on", "other levers pass to the M2 levers")
h = F(true, {play = true}); ok, r = h.C.lever({"companion_lab", "dog_life", "30"}); check(not ok and r == "lab_only", "lab only")
ok, r = h.C.lever({"companion_lab", "dog_state"}); check(ok, "dog_state also in play (read only)")
g = F(false); ok, r = g.C.lever({"companion_lab", "dog_life", "30"}); check(not ok and r == "host_lab_only", "dog_life is the host's")
g:pose(900, 0x80 + 31); g:tick(); ok, r = g.C.lever({"companion_lab", "kill_mirror"}); check(ok and r == "kill_mirror;queued=1", "guest kill_mirror")
g:tick(1100); q = g:lastGuard(); check(q[4] == 1 and q[6] == 1 and q[7] == 0, "the next arm carries the lab kill (op 1, 0%) at once")
ok, r = g.C.lever({"companion_lab", "dog_state"}); q = g:lastGuard()
check(ok and r:find("^dog_state;role=guest;ready=true") and q[4] == 1, "the guest's dog_state re-arms (a guest's arm 0 would disarm)")
ok, r = g.C.lever({"companion_lab", "guard", "off"}); g:tick(1700); check(ok and g:lastGuard()[4] == 0 and g:logged("why=lab_off"), "guard off disarms")
ok, r = g.C.lever({"companion_lab", "guard", "on"}); g:tick(2300); check(ok and g:lastGuard()[4] == 1, "guard on arms again")
print("world_companion_guard_lua_check: " .. checks .. " checks PASS")
