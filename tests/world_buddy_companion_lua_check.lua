-- M2 D-Dog synced and following the host (docs/vehicles-companions-design.md 6.2, 6.12): lua/coop/world_buddy.lua
-- with the host-owned companion on (W.companion.on()), on recording game APIs. No game process.
-- Usage: lua51 world_buddy_companion_lua_check.lua <world_buddy.lua>
-- Written before the M2 code: every check after the first failed (no COMPANION_POSE stream, release to the guest AI).
local file = assert(arg[1], "world_buddy.lua required")
local limit = assert(io.open(file, "rb")); local bytes = limit:read("*a"); limit:close()
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ": " .. why, 2) end end
check(#bytes:gsub("\r\n", "\n") <= 32000, "the chunk fits the 32000-byte world chunk limit")
local POSE, ACK, STATE = 57, 49, 39
local function vec(x, y, z) return {GetX = function() return x end, GetY = function() return y end, GetZ = function() return z end} end
local function F(host, o)
  o = o or {}
  local c = {time = 1000, active = true, demo = false, block = true, buddy = 2, id = 9728, pos = vec(1, 2, 3), calls = {}, tx = {}, logs = {}}
  local env = setmetatable({}, {__index = _G})
  env.e = {}
  env.vars = {missionCode = 30010, playerRotY = 45, deadBuddyType = 0, buddyCommonFlag = 0}
  env.BuddyType = {NONE = 0, HORSE = 1, DOG = 2, QUIET = 3, WALKER_GEAR = 4}
  env.Vector3 = vec
  env.DemoDaemon = {IsDemoPlaying = function() return c.demo end}
  env.GameObject = {NULL_ID = 65535, GetGameObjectIdByIndex = function(t, i) return c.id end}
  env.TppBuddy2BlockController = {GetActiveBuddyType = function() return c.buddy end, IsBlockActive = function() return c.block end,
    CallBuddy = function(k, p, y) c.calls[#c.calls + 1] = {name = "CallBuddy", kind = k, pos = p, yaw = y}; return true end}
  local W = {active = true, paired = true, eff = 3, level = 3, host = host, mission = 30010, epoch = 42, mode = o.play and 2 or 1,
    MISSIONS = {[30010] = {}, [30020] = {}, [30050] = {}}, OP = {}, api = {}, bindHooks = {}, startHooks = {}, stopHooks = {},
    pairedHooks = {}, unpairedHooks = {}, handlers = {}, companionOn = o.off ~= true, mirrorCount = 0}
  W.companion = {on = function() return W.companionOn == true end, tail = function() return "" end}
  env.e.MgsCoopWorld = W
  W.ent, W.entOrder = {}, {}
  for i, s in ipairs(o.soldiers or {}) do
    local h = 1000 + i
    W.ent[h] = {name = "sol" .. i, id = 2000 + i}; W.entOrder[i] = h
  end
  W.get = function(t, k) return t and t[k] end
  W.now = function() return c.time end
  W.tick32 = function(n) return n % 4294967296 end
  W.inGame = function() return c.active end
  W.log = function(s) c.logs[#c.logs + 1] = s end
  W.tx = function(op, ...) c.tx[#c.tx + 1] = {op, ...}; return c.txResult ~= false end
  W.api = {null = 65535, send = function(id, cmd)
    c.calls[#c.calls + 1] = {name = cmd.id, id = id, cmd = cmd}
    if cmd.id == "GetPosition" then
      local p = o.soldiers and o.soldiers[id - 2000]
      if p then return vec(p[1], p[2], p[3]) end
      error("GetPosition only for the fixture's soldiers")
    end
    if cmd.id == "SetWarpPosition" then check(cmd.pos ~= nil, "retail dog warp uses pos"); return end
    if cmd.id == "LuaAiStayAndSnarl" then
      check(cmd.position ~= nil or not W.companionOn, "the guest copy's dog is never cleared to its own AI (no position-less LuaAiStayAndSnarl)")
      check(not cmd.position or id == 9728 and type(cmd.useMarker) == "boolean" and (cmd.dash == 0 or cmd.dash == 1) and (cmd.warp == 0 or cmd.warp == 1) and cmd.ignoreMarker == true,
        "held command uses the exact Dog0 typed fields")
      return c.aiResult
    end
    if cmd.id == "SetDisableDamage" then return c.protectResult end
    error("unqualified game command " .. tostring(cmd.id))
  end}
  W.receipt = function(source)
    c.calls[#c.calls + 1] = {name = "NativePose", source = source}
    if c.pos == nil then return {ready = false, reason = "dog_unrealized"} end
    local x, y, z = c.pos:GetX(), c.pos:GetY(), c.pos:GetZ()
    local _, _, m, e = source:find("%-%- (%d+) (%d+)")
    return {ready = true, mission = W.mission, epoch = tonumber(e), id = 9728,
      pos = {math.floor(x * 1000 + 0.5), math.floor(y * 1000 + 0.5), math.floor(z * 1000 + 0.5)}}
  end
  setfenv(assert(loadfile(file)), env)()
  for _, f in ipairs(W.bindHooks) do f(W.api, function(t, n, k) return t and t[k] end, function(t, n, k) return t and t[k] end) end
  c.W, c.B, c.env = W, W.buddy, env
  function c:tick(at) if at then self.time = at end; self.B.tick(self.time) end
  -- COMPANION_POSE as Lua receives it: tick, kind, slot, present, x, y, z mm, yaw mrad, state, latency ms.
  function c:pose(tick, x, y, z, k, present, latency)
    self.W.handlers[POSE]({tick, k or 1, 0, present == nil and 1 or present, x or 4000, y or 2000, z or 3000, 100, 0, latency or 0}, self.time)
  end
  function c:count(name) local n = 0; for _, v in ipairs(self.calls) do if v.name == name then n = n + 1 end end; return n end
  function c:last(name) for i = #self.calls, 1, -1 do if self.calls[i].name == name then return self.calls[i] end end end
  function c:sent(op) local r = {}; for _, t in ipairs(self.tx) do if t[1] == op then r[#r + 1] = t end end; return r end
  function c:logged(pattern) for _, l in ipairs(self.logs) do if l:find(pattern) then return l end end end
  function c:unpair() self.W.paired = false; for _, f in ipairs(self.W.unpairedHooks) do f(1) end end
  function c:stop() for _, f in ipairs(self.W.stopHooks) do f(true) end; self.W.active = false end
  return c
end

-- Host: the dog pose rides COMPANION_POSE (a datagram) at 10 Hz while the dog moves and 2 Hz idle; no BUDDY_STATE.
local h = F(true)
h:tick()
local p = h:sent(POSE)
check(#p == 1 and #h:sent(STATE) == 0, "the host sends COMPANION_POSE, not BUDDY_STATE, with the companion on")
check(p[1][2] == 1000 and p[1][3] == 1 and p[1][4] == 0 and p[1][5] == 1, "sender tick, kind dog, slot 0, present")
check(p[1][6] == 1000 and p[1][7] == 2000 and p[1][8] == 3000 and p[1][9] == 785 and p[1][10] == 0, "the dog's xyz mm, the host's yaw mrad")
h:tick(1050); check(#h:sent(POSE) == 1, "no read before 100 ms")
h:tick(1100); check(#h:sent(POSE) == 1, "an idle dog is not re-sent at 10 Hz")
h:tick(1500); check(#h:sent(POSE) == 2, "an idle dog is re-sent at 2 Hz")
h.pos = vec(1.3, 2, 3); h:tick(1600); check(#h:sent(POSE) == 3, "a moving dog (30 cm in 100 ms) is sent at once")
h.pos = vec(1.6, 2, 3); h:tick(1700); check(#h:sent(POSE) == 4, "and at every 100 ms read while it moves")
h.pos = nil; h:tick(1800); p = h:sent(POSE)
check(#p == 5 and p[5][5] == 0 and p[5][6] == 0, "present=0 only when the dog itself is unreadable")
h.pos = vec(1.6, 2, 3); h.buddy = 1; h:tick(1900); p = h:sent(POSE); check(p[#p][3] == 2 and p[#p][5] == 0, "another kind is announced without a pose")
h = F(true); h.W.handlers[ACK]({4, 1, 0, 250}); check(h.B.s.remoteError == 250 and h.B.s.remoteResult == 0, "the host reads COMPANION_ACK")
h.W.handlers[ACK]({4, 1, 9, 250}); check(h.B.s.remoteResult == 0, "an out-of-range result is ignored")
h = F(true, {off = true}); h:tick(); check(#h:sent(STATE) == 1 and #h:sent(POSE) == 0, "without the key the host keeps BUDDY_STATE")

-- Guest: holds its copy of the host's dog at the host dog's pose every 100 ms; COMPANION_ACK at 1 Hz.
local g = F(false)
g:pose(900); g:tick()
check(g:count("LuaAiStayAndSnarl") == 1, "the guest holds the dog at the host dog's pose")
local hold = g:last("LuaAiStayAndSnarl").cmd
check(hold.position:GetX() == 4 and hold.position:GetY() == 2 and hold.position:GetZ() == 3, "the target is the host dog's pose")
local a = g:sent(ACK)
check(#a == 1 and a[1][3] == 1 and a[1][4] == 0 and a[1][5] == 300, "COMPANION_ACK: kind dog, result holding, error 300 cm")
check(g:logged("event=buddy;role=guest;result=target_submitted;error_cm=300"), "the proof's target_submitted line")
check(g.B.holdMode == "warp" and hold.useMarker == false and hold.warp == 1,
  "warp mode (default, run ffb2d7ab): the stop-AI form placed by the command's own warp")
g:tick(1050); check(g:count("LuaAiStayAndSnarl") == 1, "no hold before 100 ms")
g.pos = vec(4, 2, 3)
g:tick(1100); check(g:count("LuaAiStayAndSnarl") == 1, "a dog already on an unchanged target is not re-sent at once")
g:tick(1200); check(#g:sent(ACK) == 1, "the ack stays at 1 Hz while the result is unchanged")
g:tick(2000); check(g:count("LuaAiStayAndSnarl") == 2, "the hold is renewed every second")
g:pose(1950, 4100, 2000, 3000); g:tick(2100); check(g:count("LuaAiStayAndSnarl") == 3, "warp mode: a target moved by 10 cm is placed at once")
g.pos = vec(4.1, 2, 3); g:tick(2200); check(g:count("LuaAiStayAndSnarl") == 3, "and not again while the dog is on it")
g.pos = vec(3.5, 2, 3); g:tick(2300); check(g:count("LuaAiStayAndSnarl") == 4, "a dog pushed 60 cm off its target is placed again")
g:pose(2450, 30000, 2000, 3000); g:tick(2500); check(g:count("SetWarpPosition") == 1, "beyond 15 m the dog is warped")
g:pose(2400, 9000, 2000, 3000); check(g.B.s.cpose.x == 30, "an older sender tick is dropped")
-- Marker mode (the dog moves itself, modes 3/4): re-sent on a 0.75 m target change or every second; a dog that does
-- not close a 6 m gap within 2 s is warped (stuck), counted in the track line.
g = F(false); check(g.B.setHold("marker"), "hold mode marker"); g:pose(900); g:tick()
hold = g:last("LuaAiStayAndSnarl").cmd; check(hold.useMarker == true and hold.warp == 0, "marker: useMarker true (modes 3/4)")
g:tick(1100); check(g:count("LuaAiStayAndSnarl") == 1, "marker: an unchanged target is not re-sent at once (the dog's path is not restarted)")
g:tick(2000); check(g:count("LuaAiStayAndSnarl") == 2, "marker: renewed every second")
g:pose(1950, 5000, 2000, 3000); g:tick(2100); check(g:count("LuaAiStayAndSnarl") == 3, "marker: a target moved by 1 m is sent at once")
g = F(false); g.B.setHold("marker"); g:pose(900, 9000, 2000, 3000); g:tick()
for t = 1100, 2900, 100 do g.time = t; g:pose(t, 9000, 2000, 3000); g:tick() end
check(g:count("SetWarpPosition") == 0, "a 8 m gap is not warped before 2 s")
g.time = 3100; g:pose(3100, 9000, 2000, 3000); g:tick(); check(g:count("SetWarpPosition") == 1 and g.B.s.warps == 1, "after 2 s the stuck dog is warped")
-- Hold modes (lab lever companion_lab hold): stop = the retail s10040 stop-AI form (look, useMarker=false: modes 1/2),
-- warp = stop-AI placed every apply by the command's own warp (warp=1, facing the look point).
g = F(false); check(g.B.setHold("stop") and g:logged("event=companion_hold;role=guest;mode=stop"), "hold mode stop")
g:pose(900); g:tick(); hold = g:last("LuaAiStayAndSnarl").cmd; check(hold.useMarker == false and hold.warp == 0, "stop: useMarker false")
g = F(false); check(g.B.setHold("warp"), "hold mode warp"); g:pose(900); g:tick(); hold = g:last("LuaAiStayAndSnarl").cmd
check(hold.useMarker == false and hold.warp == 1, "warp: the stop-AI form with warp=1")
g:tick(1100); check(g:count("LuaAiStayAndSnarl") == 2 and g:count("SetWarpPosition") == 0, "warp mode places every 100 ms, no stuck warp")
check(not g.B.setHold("bogus") and g.B.holdMode == "warp", "an unknown mode is refused")
-- A running host dog: the target leads the pose by its age (at most 300 ms), the dog dashes.
g = F(false)
g:pose(1000, 4000, 2000, 3000); g.time = 1100; g:pose(1100, 5000, 2000, 3000, 1, 1, 50); g:tick(1150)
hold = g:last("LuaAiStayAndSnarl").cmd
check(math.abs(hold.position:GetX() - 6) < 0.001 and hold.warp == 1 and hold.dash == 0, "a moving dog's target leads by its age; warp mode places it there")
-- Never released to the guest's AI.
g = F(false); g:pose(900); g:tick()
g:tick(3000)
local stale = g:last("LuaAiStayAndSnarl").cmd
check(stale.position:GetX() == 4, "a stale stream holds the dog at the last host pose")
check(g:logged("result=hold_last"), "the stale hold is logged")
check(g:count("CallBuddy") == 0, "a stale stream never calls the dog back to the guest's AI")
g:pose(3000, 4000, 2000, 3000, 1, 0); g:tick(3100)
check(g:last("LuaAiStayAndSnarl").cmd.position:GetX() == 4 and g:count("CallBuddy") == 0, "present=0 (the host dog unreadable) holds the last pose")
g:pose(3100, 0, 0, 0, 2, 0); g:tick(3200)
check(g:count("CallBuddy") == 0 and g:last("LuaAiStayAndSnarl").cmd.position:GetX() == 4,
  "the host's kind changing holds the dog (world_companion.lua reconciles), never a release")
g:unpair(); check(g:count("CallBuddy") == 0, "unpairing never releases the dog to the guest's AI")
local n = g:count("LuaAiStayAndSnarl"); g:tick(3400); check(g:count("LuaAiStayAndSnarl") == n, "unpaired: no new targets (the native hold stays)")
g = F(false); g:pose(900); g:tick(); g:stop(); check(g:count("CallBuddy") == 0, "a mission stop never releases it either")
g = F(false); g:tick(); local here = g:last("LuaAiStayAndSnarl")
check(here and here.cmd.position:GetX() == 1 and g:logged("result=hold_here"), "no host pose yet: held where it stands, not left to its AI")
g:tick(1100); g.pos = vec(2, 2, 3); g:tick(1200)
check(g:last("LuaAiStayAndSnarl").cmd.position:GetX() == 1, "the stand-in hold point does not drift with the dog")
g = F(false); g.buddy = 0; g:pose(900); g:tick()
check(g:count("CallBuddy") == 0 and g:count("LuaAiStayAndSnarl") == 0, "no dog loaded: the guest never calls one itself (the reconcile does)")
g = F(false); g.buddy = 1; g:pose(900); g:tick(); check(g:count("LuaAiStayAndSnarl") == 0, "another buddy kind is not commandeered")
-- The own-dog read survives a hidden partner: dog_unreadable only when the dog itself cannot be read.
g = F(false); g.pos = nil; g:pose(900); g:tick(); check(g:logged("result=dog_unreadable"), "an unreadable dog is reported")
-- The ride yield (horse and walker levers) never clears the guest copy's hold.
g = F(false); g:pose(900); g:tick(); check(g.B.yieldForRide() == false, "the guest's ride yield is refused while it holds the host's dog")
-- Tracking for the proof: 2 s windows with the host dog's speed band and the error.
g = F(false)
for i = 0, 25 do g.time = 1000 + i * 100; g:pose(1000 + i * 100, 4000 + i * 600, 2000, 3000); g.pos = vec(4 + i * 0.6 - 1, 2, 3); g:tick() end
local track = g:logged("event=companion_track;band=run;")
check(track and track:find("err_max_cm=1[0-9][0-9];") and track:find("within=1"), "a 6 m/s window is band run with the error and within=1")
-- The leg marker names the next windows and restarts the window.
g = F(false); g.B.mark("dash"); check(g:logged("event=companion_mark;role=guest;leg=dash"), "the marker is logged")
for i = 0, 25 do g.time = 1000 + i * 100; g:pose(1000 + i * 100, 4000 + i * 900, 2000, 3000); g.pos = vec(4 + i * 0.9 - 2, 2, 3); g:tick() end
check(g:logged("event=companion_track;band=run;.*within=1;leg=dash"), "the windows after the marker carry its label")
-- E3 lab lever companion_lab near_guard: the held target becomes the nearest soldier (+1.5 m), the host's pose ignored.
g = F(false, {soldiers = {{20, 2, 3}, {100, 2, 3}}}); g:pose(900)
for i = 0, 12 do g:tick(1000 + i * 100) end
check(g.B.nearGuard(true) == true and g:logged("event=companion_near_guard;role=guest;on=1"), "near_guard on")
g:tick(2400); g:tick(2500)
hold = g:last("LuaAiStayAndSnarl").cmd
check(math.abs(hold.position:GetX() - 21.5) < 0.001 and hold.position:GetZ() == 3, "the dog is held 1.5 m from the nearest soldier")
check(g:logged("result=near_guard"), "the near-guard hold is logged")
g.B.nearGuard(false); g:pose(2500); g:tick(2600)
check(g:last("LuaAiStayAndSnarl").cmd.position:GetX() == 4, "near_guard off: back to the host dog's pose")
g = F(false, {play = true, soldiers = {{20, 2, 3}}}); check(g.B.nearGuard(true) == false, "near_guard is lab only")
-- E3 / E19 watch (lab): the nearest soldier to the held dog, the guest's buddy vars, the dog's own damage.
g = F(false, {soldiers = {{1.5, 2, 3}, {100, 2, 3}}}); g:pose(900)
for i = 0, 40 do g:tick(1000 + i * 100) end
check(g:logged("event=mirror_near;ent=1001;d_cm=50;"), "the nearest soldier within 10 m of the held dog is logged")
check(g.B.s.watch and g.B.s.watch.s3 >= 2, "seconds with a soldier within 3 m are counted")
check(g:logged("event=mirror_vars;dead=0;flag=0;active=2"), "the guest's buddy vars are logged")
g.env.vars.deadBuddyType = 2; g:tick(5200); check(g:logged("event=mirror_vars;dead=2;"), "a change of deadBuddyType is logged")
g.W.companionHurt("Damage", 9728, "other"); check(g:logged("event=companion_hurt;role=guest;msg=Damage;att=other;n=1"), "the mirror's own damage is logged")
g = F(false, {play = true, soldiers = {{1.5, 2, 3}}}); g:pose(900); for i = 0, 20 do g:tick(1000 + i * 100) end
check(not g:logged("event=mirror_near"), "the soldier scan is lab-only")
-- Protection candidate (E19, lab lever): SetDisableDamage on the held dog, once per dog.
g = F(false); g:pose(900); g:tick(); g.protectResult = true
check(g.B.protect(true) == true and g:count("SetDisableDamage") == 1, "the lab lever sends SetDisableDamage to the held dog")
g:tick(1200); check(g:count("SetDisableDamage") == 1, "once per dog")
-- Without the key the legacy path is unchanged: a stale guest sample still releases.
g = F(false, {off = true}); g.W.handlers[STATE]({1, 1, 1, 4000, 2000, 3000, 100, 123, 0}, g.time); g:tick(); g:tick(2001)
check(g:count("CallBuddy") == 1, "without the key the legacy release is unchanged")
print("world_buddy_companion_lua_check: " .. checks .. " checks passed")
