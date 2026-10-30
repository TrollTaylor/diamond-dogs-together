-- D7 dog guard (docs/vehicles-companions-design.md 6.12, 8.9): lua/coop/world_companion_guard.lua on top of the M2 hold
-- (world_buddy.lua, world_companion_dog.lua) with a recording companion module and native receipts. No game process.
-- Usage: lua51 world_companion_guard_lua_check.lua <world_buddy.lua> <world_companion_dog.lua> <world_companion_guard.lua>
--   <world_sync.lua>
-- Written before world_companion_guard.lua: the first check after the chunk-size checks failed (no W.dogGuard).
-- Review 2026-10-29 (hold through a link loss, the unknown host state, the one-shot DOG-bit repair, play refusals):
-- written before the chunk changed; the red run is scratchpad decisions/dog/fix-red-lua.log.
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
  -- Heroism, demon points and the dog's bond (raw vars.buddyFriendlyPoint, 0..1000; GetFriendlyPoint is raw / 10).
  env.BuddyFriendlyType = {QUIET = 0, DOG = 1, HORSE = 2}
  env.vars.buddyFriendlyPoint = {[0] = 400, [1] = o.bond or 523, [2] = 300}
  c.mbm = {h = o.heroic or 1000, o = o.ogre or 50}
  env.TppBuddyService.GetFriendlyPoint = function(t) return math.floor(env.vars.buddyFriendlyPoint[t] * 100 / 1000) end
  if not o.noMbm then
    env.TppMotherBaseManagement = {GetHeroicPoint = function() return math.max(0, math.min(99999999, c.mbm.h)) end,
      AddHeroicPoint = function(t) c.calls[#c.calls + 1] = {name = "AddHeroicPoint", n = t.heroicPoint}; c.mbm.h = math.min(99999999, c.mbm.h + t.heroicPoint) end,
      GetOgrePoint = function() return c.mbm.o end,
      SubOgrePoint = function(t) c.calls[#c.calls + 1] = {name = "SubOgrePoint", n = t.ogrePoint}; c.mbm.o = c.mbm.o - t.ogrePoint end}
  end
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
      -- The native admission (dog_guard::Admit): arm 1 is held ('hold') while the link is down, arm 2 holds an armed
      -- lease and never starts one ('not_armed'); a stale snapshot leaves the record fields out (Fresh).
      local arm, reason = 0, "ok"
      if g[4] == 1 then arm = c.armedReply; if c.linkDown and arm == 1 then reason = "hold" end
      elseif g[4] == 2 then if c.holdReply ~= false then arm, reason = 1, "hold" else reason = "not_armed" end end
      local rec = not c.stale
      return {ready = reason ~= "not_armed", reason = reason, armed = arm, installed = 1, snap = 1, fresh = rec and 1 or 0,
        life = rec and c.life or nil, max = rec and c.max or nil, stun = rec and 10000 or nil, smax = rec and 10000 or nil,
        state = rec and 0 or nil, byte = host and c.hostByte or 0, drops = c.drops, hits = c.drops, applied = 0, kills = 0,
        attacker = 2051, attack = 7}
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
  function c:nlogged(p) local k = 0; for _, l in ipairs(self.logs) do if l:find(p) then k = k + 1 end end; return k end
  function c:lastGuard() return self.guard[#self.guard] end
  -- The stock dying penalty (BuddyServiceImpl slot 0x258, 0x140A4F440, run by the dog's life update just before its Dead
  -- message): nothing when the DOG bit of deadBuddyType is set; else 30 heroism and 20 raw bond, or 60, 100 raw and +150
  -- demon points when a player (id 0..511) made the kill; heroism never below 0 by it; then the DOG bit.
  function c:penalty(att)
    local v = self.env.vars
    if math.floor(v.deadBuddyType / 4) % 2 == 1 then return end
    local p = type(att) == "number" and att >= 0 and att < 512
    self.mbm.h = self.mbm.h - math.min(p and 60 or 30, math.max(0, self.mbm.h))
    v.buddyFriendlyPoint[1] = math.max(0, v.buddyFriendlyPoint[1] - (p and 100 or 20))
    self.mbm.o = self.mbm.o + (p and 150 or 0)
    v.deadBuddyType = v.deadBuddyType + 4
  end
  function c:die(att, cls) self:penalty(att); self.W.companionHurt("Dead", 9728, cls or "other", att) end
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
-- Review 2026-10-29: a link loss holds the lease drop-only (arm 2, no host byte) until the session or mission ends.
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.paired, g.W.eff = false, 0; for _, f in ipairs(g.W.unpairedHooks) do f(1) end
check(not g:logged("step=disarmed"), "an unpair does not disarm")
g:tick(1100); q = g:lastGuard()
check(q[4] == 2 and q[5] == 0 and g:logged("step=held;reason=hold;byte=0;"), "unpaired: the armed guest holds (arm 2) with no host byte")
n = #g.guard; g:tick(1300); check(#g.guard == n, "a hold is not re-sent every tick")
g:tick(1600); check(#g.guard == n + 1 and g:lastGuard()[4] == 2, "it is renewed every 500 ms")
g.W.paired, g.W.eff = true, 3; g:pose(1650, 0x80 + 31); g:tick(1700); q = g:lastGuard()
check(q[4] == 1 and q[5] == 0x80 + 31 and g:nlogged("step=armed;reason=ok;byte=159;") == 2, "re-paired: the full arm with the host's byte at once")
g = F(false); g.W.paired, g.W.eff = false, 0; g:tick(); g:tick(1600); check(#g.guard == 0, "unpaired before any arm: nothing is armed")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.paired = false; g.holdReply = false
g:tick(1100); check(g:lastGuard()[4] == 2 and g:logged("step=refused;reason=not_armed"), "a refused hold (the lease lapsed) is logged")
g:tick(1200); check(g:lastGuard()[4] == 0 and g:logged("step=disarmed;why=no_link"), "then disarmed once")
n = #g.guard; g:tick(1700); check(#g.guard == n, "and nothing more while unpaired")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.paired = false; g:tick(1100); g.C.applies = function() return false end
g:tick(1200); check(g:lastGuard()[4] == 0 and g:logged("step=disarmed;why=off"), "the session end disarms a held lease")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.linkDown = true; g:pose(1050, 0x80 + 16); g:tick(1100)
check(g:logged("step=held;reason=hold;byte=144;"), "the native's own hold of a full arm (its link down first) is logged")
g = F(false); g.stale = true; g:pose(900, 0x80 + 31); g:tick()
check(g:logged("step=armed;reason=ok;byte=159;life=nil;"), "a stale native record is logged as unknown, not as a dead dog")
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
check(g.env.vars.deadBuddyType == 0 and g:logged("step=vars;why=death;dead=4>0;ok=1"), "the dead DOG bit is cleared once (review 2026-10-29)")
check(g.env.vars.buddyCommonFlag == 0, "buddyCommonFlag is left to the game")
for t = 1200, 1900, 100 do g.env.vars.buddyCommonFlag = 0; g:tick(t) end
check(g:nlogged("step=vars") == 1 and g.env.vars.buddyCommonFlag == 0, "a stock writer of the flag is not fought (one vars line)")
g:tick(2000); check(g:count("DeleteBuddy") == 0, "not sent away before 1.5 s")
g:pose(2550, 0x80 + 31); g:tick(2600)
check(g:count("DeleteBuddy") == 1 and g.calls[#g.calls].token == true and g:logged("step=delete;ok=1"), "sent away with the module's token")
g:pose(2650, 0x80 + 31); g:tick(2700); check(g:count("DeleteBuddy") == 1, "once")
g.buddy = 0; g.env.vars.deadBuddyType = 4; g:pose(2750, 0x80 + 31); g:tick(2800)
check(g.C.mismatchAt and g.C.mismatchAt <= 2800 - 60000 and g.C.fixN == nil and g:logged("step=reload;n=1"), "the reconcile is primed")
check(g.env.vars.deadBuddyType == 0 and g:logged("step=vars;why=reload;dead=4>0;ok=1"), "a DOG bit set again is cleared once before the reload")
check(g:nlogged("step=vars") == 2, "two vars lines in this restore, no more")
g.buddy = 2; g:pose(2850, 0x80 + 31); g:tick(2900); check(g:logged("step=back;ms=1900"), "back when the dog reads again")
check(g:lastGuard()[4] == 1, "and armed again")
-- Resume (run 9fa48400): an arm 0.9 s after the send-away read the dead copy's record as fresh (life=0; state=2) while
-- the reload was under way; an arm during a restore says so (restoring=1), an ordinary one restoring=0.
check(g:logged("step=armed;reason=ok;byte=159;.*;restoring=1$"), "an arm while a restore is in flight is marked restoring=1")
check(g:logged("step=armed;reason=ok;byte=159;.*;restoring=0$"), "an ordinary arm is restoring=0")
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
-- Only the DOG bit: another buddy type's dead bit stays.
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.env.vars.deadBuddyType = 4 + 8; g.W.companionHurt("Dead", 9728, "other")
g:pose(1050, 0x80 + 31); g:tick(1100); check(g.env.vars.deadBuddyType == 8 and g:logged("step=vars;why=death;dead=12>8;"), "only the DOG bit")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.companionHurt("Dead", 9728, "other"); g:pose(1050, 0x80 + 31); g:tick(1100)
check(not g:logged("step=vars"), "no write when the DOG bit is clear")
-- Review 2026-10-29: a death while the host's state is unknown (no COMPANION_POSE for 3 s, no COMPANION for 15 s, as
-- through a link loss) is neither alive nor down: no send-away, no reload, no mirror until it is known again.
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g:tick(5000)
g.W.companionHurt("Dead", 9728, "other")
check(g:logged("step=dead;host=unknown;byte=0;"), "an unknown host state is logged as unknown")
for t = 5100, 9000, 100 do g:tick(t) end
check(g:count("DeleteBuddy") == 0 and not g:logged("step=host_down") and not g:logged("step=reload"), "nothing while unknown")
check(g.W.dogGuard.s.dead and not g.W.dogGuard.s.dead.mirrored, "not mirrored while unknown")
g:pose(9050, 0x80 + 31); g:tick(9100)
check(g:logged("step=host_known;host=alive;ms=4100;"), "known again: alive")
check(g:count("DeleteBuddy") == 1, "then the restore runs (sent away at once, 1.5 s having passed)")
g.buddy = 0; g:pose(9150, 0x80 + 31); g:tick(9200); check(g:logged("step=reload;n=1"), "and reloads")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g:tick(5000); g.W.companionHurt("Dead", 9728, "other")
g:pose(6050, 0x80 + 64); g:tick(6100)
check(g:logged("step=host_known;host=down;") and g.W.dogGuard.s.dead.mirrored, "known again: down, the mirrored wait")
g:tick(8000); check(g:count("DeleteBuddy") == 0, "no send-away")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.companionHurt("Dead", 9728, "other"); g:tick(4500)
check(not g:logged("step=host_down") and not g.W.dogGuard.s.dead.mirrored, "a stale tick does not mirror a pending restore")
g = F(false); g.C.host = {kind = 0, present = false, state = 0, at = 990}; g:tick(); g.W.companionHurt("Dead", 9728, "other")
check(g:logged("step=dead;host=down;"), "a host known to have no dog out is not unknown (no reload)")
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
check(r:find(";installed=1;fresh=1$"), "dog_state names whether the native slots are installed and the record fresh")
ok, r = h.C.lever({"companion_lab", "near_guard", "on"}); check(ok and r == "orig=companion_lab,near_guard,on", "other levers pass to the M2 levers")
h = F(true, {play = true}); ok, r = h.C.lever({"companion_lab", "dog_life", "30"}); check(not ok and r == "lab_only", "lab only")
ok, r = h.C.lever({"companion_lab", "dog_stun", "0"}); check(not ok and r == "lab_only", "dog_stun refused in play")
ok, r = h.C.lever({"companion_lab", "recall"}); check(not ok and r == "lab_only" and h:count("UnsetDeadBuddyType") == 0, "recall refused in play")
ok, r = h.C.lever({"companion_lab", "dog_state"}); check(ok, "dog_state also in play (read only)")
g = F(false, {play = true}); g:pose(900, 0x80 + 31); g:tick()
ok, r = g.C.lever({"companion_lab", "kill_mirror"}); check(not ok and r == "lab_only", "kill_mirror refused in play")
ok, r = g.C.lever({"companion_lab", "guard", "off"}); check(not ok and r == "lab_only", "guard off refused in play")
g:tick(1600); check(g:lastGuard()[4] == 1 and g:lastGuard()[6] == 0, "play: still armed, no lab op")
g = F(false); ok, r = g.C.lever({"companion_lab", "dog_life", "30"}); check(not ok and r == "host_lab_only", "dog_life is the host's")
g:pose(900, 0x80 + 31); g:tick(); ok, r = g.C.lever({"companion_lab", "kill_mirror"}); check(ok and r == "kill_mirror;queued=1", "guest kill_mirror")
g:tick(1100); q = g:lastGuard(); check(q[4] == 1 and q[6] == 1 and q[7] == 0, "the next arm carries the lab kill (op 1, 0%) at once")
ok, r = g.C.lever({"companion_lab", "dog_state"}); q = g:lastGuard()
check(ok and r:find("^dog_state;role=guest;ready=true") and q[4] == 1, "the guest's dog_state re-arms (a guest's arm 0 would disarm)")
ok, r = g.C.lever({"companion_lab", "guard", "off"}); g:tick(1700); check(ok and g:lastGuard()[4] == 0 and g:logged("why=lab_off"), "guard off disarms")
g.life = 4000; ok, r = g.C.lever({"companion_lab", "dog_state"})
check(ok and r:find(";armed=0;life=4000;", 1, true) and g:lastGuard()[4] == 0, "a disarmed guest's dog_state reads the live record (arm 0)")
ok, r = g.C.lever({"companion_lab", "guard", "on"}); g:tick(2300); check(ok and g:lastGuard()[4] == 1, "guard on arms again")
-- Lab lever link_sim on|off (guest; resume of the review fixes): the guard module sees the link as lost (the hold, an
-- unknown host state) while the world link stays up, so a hidden run can put the native hold under guard fire and walk
-- the unknown-state restore. Red run: scratchpad decisions/dog/fix-red-lua3.log.
g = F(false); g:pose(900, 0x80 + 31); g:tick()
ok, r = g.C.lever({"companion_lab", "link_sim", "on"}); check(ok and r == "link_sim;on=1", "guest link_sim on")
check(g:logged("event=dog_guard;role=guest;step=lab;link_sim=on;"), "link_sim is logged")
g:pose(1050, 0x80 + 31); g:tick(1100); q = g:lastGuard()
check(q[4] == 2 and q[5] == 0 and g:logged("step=held;reason=hold;byte=0;"), "link_sim: the armed guest holds (arm 2, no host byte) while paired")
n = #g.guard; g:pose(1550, 0x80 + 31); g:tick(1600); check(#g.guard == n + 1 and g:lastGuard()[4] == 2, "the hold is renewed")
g.W.companionHurt("Dead", 9728, "other")
check(g:logged("step=dead;host=unknown;byte=0;"), "a death under link_sim: the host's state is unknown although a pose is fresh")
for t = 1700, 4000, 100 do g:pose(t - 50, 0x80 + 31); g:tick(t) end
check(g:count("DeleteBuddy") == 0 and not g:logged("step=reload") and not g:logged("step=host_down"), "nothing while unknown")
ok, r = g.C.lever({"companion_lab", "link_sim", "off"}); check(ok and r == "link_sim;on=0", "link_sim off")
g:pose(4050, 0x80 + 31); g:tick(4100)
check(g:logged("step=host_known;host=alive;ms=") and g:count("DeleteBuddy") == 1, "off: known again (alive), the restore sends the copy away")
q = g:lastGuard(); check(q[4] == 1 and q[5] == 0x80 + 31, "and the full arm with the host's byte at once")
g = F(false); g.C.lever({"companion_lab", "link_sim", "on"}); g:pose(900, 0x80 + 31); g:tick(); g:tick(1600)
check(#g.guard == 0, "link_sim before any arm: nothing is armed (a hold never starts the guard)")
for _, f in ipairs(g.W.startHooks) do f() end; check(g.W.dogGuard.s.linkSim == nil, "a new mission clears link_sim")
ok, r = F(true).C.lever({"companion_lab", "link_sim", "on"}); check(not ok and r == "guest_lab_only", "link_sim is the guest's")
ok, r = F(false, {play = true}).C.lever({"companion_lab", "link_sim", "on"}); check(not ok and r == "lab_only", "link_sim refused in play")
ok, r = F(false).C.lever({"companion_lab", "link_sim", "maybe"}); check(not ok and r == "companion_args", "link_sim takes on or off")

-- Heroism at a copy's death (user decision 2026-10-29; design 8.14): when the guest's copy of the host's dog dies on the
-- guest's PC the stock game takes the guest's heroism and bond (and demon points for a player's kill) for a death that
-- is not real; under the key the guest gets exactly that back, once per death, measured against its last sample before
-- the death. The host and a guest without the key keep the stock penalty. Written before the chunk changed; red run in
-- the session scratchpad next2/dog/red-lua.log.
local HG = "event=dog_hero;role=guest;"
g = F(false); g:pose(900, 0x80 + 31); g:tick()
g:die(65535, "other")
check(g.mbm.h == 1000 and g.env.vars.buddyFriendlyPoint[1] == 523 and g.mbm.o == 50, "a guard's kill of the copy: heroism and bond given back")
check(g:logged(HG .. "step=given;by=other;att=65535;h=1000:970:1000;b=523:503:523;o=50:50:50;give=30,20,0;ms=0;n=1;$"),
  "logged with the sample before, the value after the penalty and the readback")
check(g:count("AddHeroicPoint") == 1 and g:count("SubOgrePoint") == 0, "through AddHeroicPoint; no demon points to take")
do
  local L = F(false, {heroic = 9999999, ogre = 9999999}); L:pose(900, 0x80 + 31); L:tick(); L:die(65535, "other")
  local line = L:logged("step=given;")
  check(line and #line <= 160 and line:find(";n=1;$") and not line:find("[^%w_=:,.;%-]"), "a given line with 7-digit heroism and demon points fits W.log's 160 bytes and its sanitizer")
end
g.W.companionHurt("Dead", 9728, "other", 65535)
check(g:nlogged("step=given") == 1 and g.mbm.h == 1000 and g:count("AddHeroicPoint") == 1, "a repeated Dead message of the same death gives nothing more")
for t = 1100, 3000, 100 do g:pose(t - 50, 0x80 + 31); g:tick(t) end
check(g.env.vars.deadBuddyType == 0, "(the restore cleared the DOG bit)")
g:die(65535, "other")
check(g:nlogged("step=given") == 2 and g:logged("step=given;.*;n=2;$") and g.mbm.h == 1000 and g.env.vars.buddyFriendlyPoint[1] == 523,
  "a later death of the restored copy: given back again (once per death)")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g:die(0, "local")
check(g:logged(HG .. "step=given;by=player;att=0;h=1000:940:1000;b=523:423:523;o=50:200:50;give=60,100,150;ms=0;"),
  "the guest's own kill: 60 heroism, 100 raw bond and the 150 demon points back")
check(g.mbm.h == 1000 and g.env.vars.buddyFriendlyPoint[1] == 523 and g.mbm.o == 50 and g:count("SubOgrePoint") == 1, "all three restored")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g:die(1, "mirror")
check(g:logged("step=given;by=player;att=1;") and g.mbm.h == 1000 and g.mbm.o == 50, "the host's body (player 1) counts as a player, as the stock rule does")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g:penalty(0); g.W.companionHurt("Dead", 9728, "other")
check(g:logged("step=given;by=unknown;att=nil;.*give=60,100,150;") and g.mbm.h == 1000, "no attacker from an older observer: what was lost, at most a player's amounts")
-- The stock gate: a DOG bit already set means the game took nothing; nothing is given.
g = F(false); g:pose(900, 0x80 + 31); g.env.vars.deadBuddyType = 4; g:tick(); g:die(65535, "other")
check(g:logged(HG .. "step=none;by=other;att=65535;h=1000:1000;b=523:523;o=50:50;gate=1;") and g:count("AddHeroicPoint") == 0, "the stock gate: nothing taken, nothing given")
-- Clamps: never more than the death took, never more than the stock amount.
g = F(false, {heroic = 20, bond = 15}); g:pose(900, 0x80 + 31); g:tick(); g:die(65535, "other")
check(g.mbm.h == 20 and g.env.vars.buddyFriendlyPoint[1] == 15 and g:logged("give=20,15,0;"), "heroism and bond near 0: what the death took")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.mbm.h = g.mbm.h - 10; g:die(65535, "other")
check(g.mbm.h == 990 and g:logged("h=1000:960:990;.*give=30,20,0;"), "another loss in the same 100 ms is not the death's: only its 30")
-- Order: a penalty that lands after the Dead message is given back at the next tick; none within 1 s is logged once.
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.companionHurt("Dead", 9728, "other", 65535)
check(not g:logged("event=dog_hero"), "nothing lost yet: wait")
g:penalty(65535); g:pose(1050, 0x80 + 31); g:tick(1100)
check(g:logged("step=given;by=other;att=65535;h=1000:970:1000;.*ms=100;n=1;$") and g.mbm.h == 1000, "a late penalty: given at the next tick")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g.W.companionHurt("Dead", 9728, "other", 65535)
for t = 1100, 2500, 100 do g:pose(t - 50, 0x80 + 31); g:tick(t) end
check(g:nlogged(HG .. "step=none;by=other;att=65535;h=1000:1000;b=523:523;o=50:50;gate=0;ms=1") == 1 and g:count("AddHeroicPoint") == 0,
  "nothing taken within 1 s: logged once, nothing given")
-- Every copy death under the key: the host's dog down too (mirrored) and an unknown host state.
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g:pose(1050, 0x80 + 64); g:tick(1100); g:die(65535, "other")
check(g:logged("step=dead;host=down") and g:logged("step=given;by=other;") and g.mbm.h == 1000, "the mirrored down: the host pays on its PC, the guest gets it back")
g = F(false); g:pose(900, 0x80 + 31); g:tick(); g:tick(5000); g:die(65535, "other")
check(g:logged("step=dead;host=unknown") and g:logged("step=given;") and g.mbm.h == 1000, "an unknown host state: given back")
-- Left alone: the host's own dog, a guest without the key or without the peer's, a missing API.
h = F(true); h:tick(); h:die(65535, "other")
check(h.mbm.h == 970 and h.env.vars.buddyFriendlyPoint[1] == 503 and not h:logged("event=dog_hero"), "the host's own dog's death keeps the stock penalty")
g = F(false, {off = true}); g:tick(); g:die(0, "local")
check(g.mbm.h == 940 and g.env.vars.buddyFriendlyPoint[1] == 423 and g.mbm.o == 200 and not g:logged("event=dog_hero"), "key off: the guest's own dog's death keeps it")
g = F(false, {peer = false}); g:tick(); g:die(65535, "other")
check(g.mbm.h == 970 and not g:logged("event=dog_hero"), "the peer's key not seen: the guest's own dog, stock")
g = F(false, {noMbm = true}); g:pose(900, 0x80 + 31); g:tick(); g:die(65535, "other")
check(g:logged(HG .. "step=no_sample;by=other;att=65535;n=1;") and g:logged("step=dead;host=alive"), "no heroism API: named once, the restore unaffected")
-- The readback lever (both roles, play too: read only).
g = F(false); g:pose(900, 0x80 + 31); g:tick()
ok, r = g.C.lever({"companion_lab", "hero"})
check(ok and r == "hero;role=guest;heroic=1000;ogre=50;bond=52;bond_raw=523;dead_bit=0;applies=1;deaths=0", "companion_lab hero reads the guest's values")
g:die(65535, "other"); ok, r = g.C.lever({"companion_lab", "hero"}); check(ok and r:find(";heroic=1000;.*;deaths=1$"), "and counts the deaths given back")
ok, r = F(true, {play = true}).C.lever({"companion_lab", "hero"}); check(ok and r:find("^hero;role=host;heroic=1000;.*;applies=0;"), "the host's, in play too")
ok, r = F(false, {off = true}).C.lever({"companion_lab", "hero"}); check(ok and r:find(";applies=0;"), "key off: read only")
-- world_sync.lua's observer hands the raw attacker on (the stock rule's player test needs the id, not the class).
local hs = assert(io.open(assert(arg[4]), "rb")); local syncText = hs:read("*a"); hs:close()
check(syncText:find("pcall(W.companionHurt, kind, a0, class2, att)", 1, true), "world_sync.lua passes the attacker id to W.companionHurt")
print("world_companion_guard_lua_check: " .. checks .. " checks PASS")
