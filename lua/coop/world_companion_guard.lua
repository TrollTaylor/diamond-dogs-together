-- D7 (docs/vehicles-companions-design.md 6.12, 8.9; user decision 2026-10-29; lab key companion_host_owned): the
-- guest's copy of the host's D-Dog cannot be hurt on the guest's PC and follows the host dog's state; if it dies,
-- vanishes or is removed anyway it comes back from the host's live state (the restore fallback). Original code.
-- Native half: native/dog_guard.cpp, receipt MGS_COOP_DOG_GUARD_V1 (mission, epoch, role, arm 0/1/2 = read or disarm /
-- arm / hold, the host's byte, lab op, percent). The host's dog-state byte (bit 7 valid, bits 5-6 state 0 normal / 1 asleep / 2 down, bits 0-4
-- life in 31ths) rides COMPANION_POSE and COMPANION `state` (both sent 0 before: no wire change).
do
local W = e.MgsCoopWorld
local B, C = type(W) == "table" and W.buddy, type(W) == "table" and W.companion
if type(B) == "table" and type(C) == "table" and type(B.lib) == "table" and type(C.lib) == "table" then
local G = {s = {}, RESTORES = 3}
W.dogGuard = G
local fmt, floor = string.format, math.floor
local lib = C.lib
local DOG_ID = 9728
local FIELD = {[30010] = true, [30020] = true, [30050] = true}
local function L(text) W.log("event=dog_guard;role=" .. (W.host and "host" or "guest") .. ";" .. text) end
local function R(text) W.log("event=dog_restore;role=guest;" .. text) end
function G.decode(b)
 if type(b) ~= "number" or b < 128 or b > 255 then return false, 0, 0 end
 return true, floor(b / 32) % 4, b % 32
end
-- The native receipt; nil when this copy cannot answer (no mission, an older loader). mode: 0 the host's read or the
-- guest's disarm, 1 arm with the host's byte, 2 hold (drop only; review 2026-10-29).
function G.call(mode, byte, op, pct)
 if not FIELD[W.mission] then return nil end
 local src = fmt("return 'MGS_COOP_DOG_GUARD_V1' -- %d %d %d %d %d %d %d", W.mission, math.max(1, W.epoch or 1), W.host and 0 or 1,
  mode or 0, byte or 0, op or 0, pct or 0)
 local ok, r = pcall(W.receipt, src)
 if ok and type(r) == "table" then return r end
end
-- Host: its own dog's byte, read with every COMPANION_POSE read (B.hostCompanion, 100 ms); 0 without a dog.
function G.hostByte(now, k)
 local s = G.s
 if k ~= 1 then s.byte = 0; return 0 end
 local r = G.call(0, 0, s.labOp, s.labPct)
 s.labOp, s.labPct = nil, nil
 local b = type(r) == "table" and r.ready == true and type(r.byte) == "number" and r.byte >= 0 and r.byte <= 255 and r.byte or 0
 local _, st, life = G.decode(b)
 if b ~= s.byte and (s.byte == nil or floor(b / 32) ~= floor(s.byte / 32) or math.abs(life - s.byte % 32) >= 4 or now >= (s.logAt or 0)) then
  s.logAt = now + 10000
  L(fmt("step=state;byte=%d;state=%d;life31=%d;life=%s;max=%s;stun=%s;smax=%s", b, st, life, tostring(r and r.life), tostring(r and r.max),
   tostring(r and r.stun), tostring(r and r.smax)))
 end
 s.byte = b
 return b
end
-- Guest: the host's latest byte (COMPANION_POSE within 3 s, else COMPANION within 15 s), whether its dog lives (not
-- down), whether it is present (readable on the host) and whether the host's state is known at all. Without a fresh
-- source (a link loss: the pose is dropped while unpaired and the unpair forgets C.host) it is unknown, which is
-- neither alive nor down (review 2026-10-29); a fresh source naming another kind is a host with no dog out (down).
-- The lab lever link_sim makes it unknown while the link is up (the link-loss paths in a hidden run).
function G.hostState(now)
 local p, h = B.s and B.s.cpose, C.host
 local k, byte, have
 if G.s.linkSim then return 0, true, false, false end
 if p and now - p.at <= 3000 and (p.kind == 1 or not (h and h.kind == 1 and now - h.at <= 15000)) then k, byte, have = p.kind, p.st, p.present == true
 elseif h and now - h.at <= 15000 then k, byte, have = h.kind, h.state, h.present == true
 else return 0, true, false, false end
 if k ~= 1 then return 0, false, false, true end
 byte = have and byte or 0
 local valid, st = G.decode(byte)
 return byte, not (valid and st == 2), have, true
end
function G.arm(now, byte, mode)
 local s = G.s
 if s.labOff then return G.disarm("lab_off") end
 if s.armedAt and now < s.armedAt + 500 and byte == s.armByte and mode == s.armMode and not s.labOp then return end
 local op, pct = mode == 1 and s.labOp or nil, mode == 1 and s.labPct or nil
 local r = G.call(mode, byte, op, pct)
 if mode == 1 then s.labOp, s.labPct = nil, nil end
 if not r then if not s.noReply then s.noReply = true; L("step=no_receipt;") end return end
 s.armedAt, s.armByte, s.armMode, s.reply = now, byte, mode, r
 local armed = r.ready == true and r.armed == 1
 -- 'hold': the native keeps the lease drop-only (no follow) through a link loss, its own or the Lua's unpair.
 local held = armed and r.reason == "hold" or nil
 if armed ~= s.armed or byte ~= s.loggedByte or held ~= s.held then
  s.loggedByte = byte
  L(fmt("step=%s;reason=%s;byte=%d;life=%s;max=%s;stun=%s;state=%s;drops=%s;applied=%s;kills=%s",
   held and "held" or armed and "armed" or "refused", tostring(r.reason), byte, tostring(r.life), tostring(r.max), tostring(r.stun),
   tostring(r.state), tostring(r.drops), tostring(r.applied), tostring(r.kills)))
 end
 s.armed, s.held = armed or nil, held
 if type(r.drops) == "number" and r.drops > (s.drops or 0) and now >= (s.dropLogAt or 0) then
  s.dropLogAt = now + 5000
  L(fmt("step=drops;drops=%d;hits=%s;attacker=%s;attack=%s;life=%s;state=%s", r.drops, tostring(r.hits), tostring(r.attacker),
   tostring(r.attack), tostring(r.life), tostring(r.state)))
 end
 if type(r.drops) == "number" then s.drops = r.drops end
end
function G.disarm(why)
 local s = G.s
 if not s.armed and not s.armedAt then return end
 G.call(0, 0, 0, 0)
 s.armed, s.armedAt, s.armByte, s.armMode, s.held = nil, nil, nil, nil, nil
 L("step=disarmed;why=" .. why .. ";")
end
-- Restore: the reconcile (world_companion.lua) loads the host's kind at the host's pose; primed to try now.
local function prime() C.mismatchAt, C.fixAt, C.fixWant, C.fixN, C.fixQuit = W.now() - 60000, nil, nil, nil, nil end
function G.hurt(msg, id)
 if msg ~= "Dead" or id ~= DOG_ID or W.host or not C.applies() then return end
 local s, now = G.s, W.now()
 local byte, alive, _, known = G.hostState(now)
 s.dead = {at = now, mirrored = known and not alive, unknown = not known or nil}
 R(fmt("step=dead;host=%s;byte=%d;armed=%d;drops=%d;n=%d", not known and "unknown" or alive and "alive" or "down", byte,
  s.armed and 1 or 0, s.drops or 0, (s.restores or 0) + 1))
end
-- A dead DOG bit in deadBuddyType stops every reload (runs dc761c98, 033193d3). Only that bit is cleared, once after
-- the death and once more before a primed reload if it is set again (review 2026-10-29: the whole-word rewrite every
-- 100 ms for 10 s fought the stock game's buddyCommonFlag writes at DeleteBuddy and at the retreat; that flag is the
-- game's). True when it wrote.
local function clearDead(why)
 local t, dead = W.api.buddyDog, W.get(vars, "deadBuddyType")
 if type(t) ~= "number" or type(dead) ~= "number" then return false end
 local bit = 2 ^ t
 if floor(dead / bit) % 2 == 0 then return false end
 local ok = pcall(function() vars.deadBuddyType = dead - bit end)
 R(fmt("step=vars;why=%s;dead=%d>%d;ok=%d", why, dead, dead - bit, ok and 1 or 0))
 return true
end
local function restore(now, have, alive, present, known)
 local s = G.s
 local d, DOG, NONE = s.dead, W.api.buddyDog, W.api.buddyNone
 -- The death's own write of the DOG bit: cleared the first time it is seen, until the send-away, a reload or 10 s.
 if d and not d.cleared and not d.deleted and not d.primed and now - d.at <= 10000 and clearDead("death") then d.cleared = true end
 if not d then
  -- Gone without a Dead message while the host's dog lives: the reconcile tries at once instead of after 8 s.
  if alive and present and have == NONE and (not C.expect or C.expect.done) then
   s.goneAt = s.goneAt or now
   if now - s.goneAt >= 2000 and not s.vanished and (s.restores or 0) < G.RESTORES then
    s.vanished, s.restores = now, (s.restores or 0) + 1; prime(); R("step=vanished;reload=1;")
   end
  else s.goneAt = nil; if have == DOG then s.vanished = nil end end
  return
 end
 if d.back then s.dead = nil; return end
 if d.unknown then
  -- Died while the host's state was unknown: nothing until it is known again (or 60 s), then alive or mirrored.
  if not known then
   if now - d.at > 60000 then R(fmt("step=gave_up;ms=%d;", now - d.at)); s.dead = nil end
   return
  end
  d.unknown, d.mirrored = nil, not alive
  R(fmt("step=host_known;host=%s;ms=%d;", alive and "alive" or "down", now - d.at))
 elseif not d.mirrored and known and not alive then d.mirrored = true; R("step=host_down;") end
 -- The host's dog is down too: the copy went with it (its byte); the host's re-call reloads it (the reconcile).
 if d.mirrored then
  if have == DOG and d.gone then d.back = now; R(fmt("step=recalled;ms=%d;", now - d.at)) end
  if have ~= DOG then d.gone = true end
  return
 end
 if now - d.at > 60000 then R(fmt("step=gave_up;ms=%d;", now - d.at)); s.dead = nil; return end
 if have == DOG then
  if d.primed then
   if B.dog() then d.back = now; R(fmt("step=back;ms=%d;", now - d.at)) end
  elseif not d.deleted and now - d.at >= 1500 then
   d.deleted = true
   C.token = true
   local ok = pcall(lib.TBC("DeleteBuddy"))
   C.token = nil
   R(fmt("step=delete;ok=%d;", ok and 1 or 0))
  end
 elseif not d.primed then
  if (s.restores or 0) >= G.RESTORES then R("step=limit;"); d.back = now; return end
  s.restores, d.primed = (s.restores or 0) + 1, true
  clearDead("reload")
  prime(); R(fmt("step=reload;n=%d;", s.restores))
 end
end
function G.tick(now)
 local s = G.s
 if now < (s.next or 0) then return end
 s.next = now + 100
 if W.host or not C.applies() then if s.armed or s.armedAt then G.disarm("off") end return end
 local _, have = pcall(lib.active)
 local byte, alive, present, known = G.hostState(now)
 if have == W.api.buddyDog and W.inGame() then
  -- Paired: the full arm with the host's byte. Unpaired or a lower level (a link loss) while armed: the hold, drop only,
  -- until the session ends (C.applies, above) or the mission does (native Disarm); never a new arm (review 2026-10-29).
  if W.paired and (W.eff or 0) >= 3 and not s.linkSim then G.arm(now, byte, 1)
  elseif s.armed then G.arm(now, 0, 2)
  else G.disarm("no_link") end
 else G.disarm("no_dog") end
 restore(now, have, alive, present, known)
end
local tick = C.tick
function C.tick(now)
 tick(now)
 if C.on() then
  local ok, err = pcall(G.tick, now)
  if not ok and now >= (G.s.errAt or 0) then G.s.errAt = now + 10000; L("step=error;msg=" .. string.sub(tostring(err), 1, 80)) end
 end
end
local hurt = W.companionHurt
function W.companionHurt(msg, id, cls)
 if hurt then hurt(msg, id, cls) end
 pcall(G.hurt, msg, id, cls)
end
W.startHooks[#W.startHooks + 1] = function() local l = G.s.labOff; G.s = {labOff = l} end
-- An unpair no longer disarms (review 2026-10-29): the next tick sends the hold at once.
W.unpairedHooks[#W.unpairedHooks + 1] = function() G.s.armedAt = nil end
-- Lab levers: companion_lab dog_life|dog_stun <0..100> (the host's own dog; 0 life = its death), recall (host: the
-- dead DOG bit cleared, the dog called 5 m in front), kill_mirror (guest: the forced death the restore answers),
-- guard on|off (guest), link_sim on|off (guest: this module sees the link as lost, the hold and an unknown host state,
-- while the world link stays up; cleared at mission start), dog_state (both: the receipt).
local lever = C.lever
function C.lever(a, id, A, mission)
 if a[1] ~= "companion_lab" or not ({dog_life = 1, dog_stun = 1, recall = 1, kill_mirror = 1, guard = 1, link_sim = 1, dog_state = 1})[a[2]] then
  return lever(a, id, A, mission)
 end
 local s, n = G.s, tonumber(a[3])
 if a[2] == "dog_state" and #a == 2 then
  -- An armed guest reads by re-arming (a guest's arm 0 is a disarm); a disarmed guest (guard off) and the host read
  -- with arm 0, which answers the live record (review 2026-10-29: the last arm's reply was stale).
  local r
  if not W.host and s.armed then r = G.call(s.armMode or 1, s.armByte or 0, 0, 0) else r = G.call(0, 0, 0, 0) end
  if not r then return false, "dog_state;receipt=none" end
  return true, fmt("dog_state;role=%s;ready=%s;reason=%s;armed=%s;life=%s;max=%s;stun=%s;state=%s;byte=%s;drops=%s;hits=%s;applied=%s;kills=%s;restores=%d;installed=%s;fresh=%s",
   W.host and "host" or "guest", tostring(r.ready), tostring(r.reason), tostring(s.armed and 1 or r.armed), tostring(r.life), tostring(r.max),
   tostring(r.stun), tostring(r.state), tostring(r.byte), tostring(r.drops), tostring(r.hits), tostring(r.applied), tostring(r.kills), s.restores or 0,
   tostring(r.installed), tostring(r.fresh))
 end
 if W.mode ~= 1 then return false, "lab_only" end
 if (a[2] == "dog_life" or a[2] == "dog_stun") and #a == 3 and n and n >= 0 and n <= 100 and n == floor(n) then
  if not W.host then return false, "host_lab_only" end
  s.labOp, s.labPct = a[2] == "dog_life" and 1 or 2, n
  return true, fmt("%s;pct=%d;queued=1", a[2], n)
 end
 if a[2] == "recall" and #a == 2 then
  if not W.host then return false, "host_lab_only" end
  local D = W.api.buddyDog
  local before = W.get(vars, "deadBuddyType")
  pcall(W.get(TppBuddyService, "UnsetDeadBuddyType"), D)
  local after = W.get(vars, "deadBuddyType")
  local ok, r = lever({"companion_lab", "call_here", "dog"}, id, A, mission)
  return ok, fmt("recall;dead=%s>%s;%s", tostring(before), tostring(after), tostring(r))
 end
 if a[2] == "kill_mirror" and #a == 2 then
  if W.host then return false, "guest_lab_only" end
  s.labOp, s.labPct, s.armedAt = 1, 0, nil
  return true, "kill_mirror;queued=1"
 end
 if a[2] == "guard" and #a == 3 and (a[3] == "on" or a[3] == "off") then
  if W.host then return false, "guest_lab_only" end
  s.labOff = a[3] == "off" or nil
  L("step=lab;guard=" .. a[3] .. ";")
  return true, "guard;on=" .. (a[3] == "on" and 1 or 0)
 end
 if a[2] == "link_sim" and #a == 3 and (a[3] == "on" or a[3] == "off") then
  if W.host then return false, "guest_lab_only" end
  s.linkSim, s.armedAt = a[3] == "on" or nil, nil
  L("step=lab;link_sim=" .. a[3] .. ";")
  return true, "link_sim;on=" .. (a[3] == "on" and 1 or 0)
 end
 return false, "companion_args"
end
end
end
