-- Host-owned companion (original; docs/vehicles-companions-design.md 6.1, M1). With the lab
-- key companion_host_owned (CFG r[16], W.companionOn) the host's buddy is the session's one
-- companion (decisions D5 a, D1 a, D8 b). Guest: loads the host's buddy from the follow seeds
-- and keeps its own buddy vars for its saves (TppSave.SaveVarsToSlot writes them); reconciles
-- to the host's COMPANION kind (late join, a host change); refuses its own buddy calls (Lua
-- entry points; the F9 summon in debug_buddy.lua) and masks CALL (call menu, whistle; not in
-- a CQC hold, where CALL interrogates), with a toast (NOTE 13). Host: sends COMPANION; a guest
-- playing Quiet (CHARACTER) locks buddy Quiet (SetDisableBuddyType, the sortie rewrite to NONE,
-- DeleteBuddy mid-mission); the Mother Base cell Quiet is hidden on both copies then.
do
local W = e.MgsCoopWorld
if type(W) == "table" and type(W.follow) == "table" and type(W.followSeeds) == "table" then
local C = {orig = {}, hookLines = 0, seq = 0}
W.companion = C
local OP, H, fmt = W.OP, W.handlers, string.format
OP.COMPANION, OP.CHARACTER = 48, 56
local NAMES = {NONE = "none", HORSE = "horse", DOG = "dog", QUIET = "quiet", WALKER_GEAR = "walker", BATTLE_GEAR = "battle_gear"}
local WIRE = {DOG = 1, HORSE = 2, WALKER_GEAR = 3, QUIET = 4}
local CHARS = {default = 0, snake = 1, dd_male = 2, dd_female = 3, avatar = 4, ocelot = 5, quiet = 6, story = 7}
local MASK = "MgsCoopCompanion"
local function BT(name) local v = W.get(BuddyType, name); return type(v) == "number" and v or nil end
local function typeName(t)
 if t == nil then return "unknown" end
 for n, s in pairs(NAMES) do if BT(n) == t then return s end end
 return tostring(t)
end
C.typeName = typeName
local function toWire(t) for n, k in pairs(WIRE) do if BT(n) == t then return k end end return 0 end
local function fromWire(k) for n, w in pairs(WIRE) do if w == k then return BT(n) end end return BT("NONE") end
local function L(text) W.log("event=companion;role=" .. (W.host and "host" or "guest") .. ";" .. text) end
local function note(a) if type(W.note) == "function" then pcall(W.note, 13, a, 0) end end
function C.on() return W.companionOn == true and (W.level or 0) >= 3 end
-- Both PCs must run the key (review 2026-10-29: CFG r[16] is local). Each copy shows its own in
-- its traffic (the guest's CHARACTER, the host's COMPANION): C.on() sends those signals, C.live()
-- (the peer's seen too) runs the module. The agreement lasts until the session ends.
function C.live() return C.on() and C.peer == true end
function C.applies() return C.live() and not W.host end
local function peerSeen() if not C.peer then C.peer = true; L("step=peer;on=1;") end end
local function TBC(name) return C.orig[name] or W.get(TppBuddy2BlockController, name) end
local function active()
 local f = W.get(TppBuddy2BlockController, "GetActiveBuddyType")
 if type(f) ~= "function" then return nil end
 local ok, t = pcall(f)
 return ok and type(t) == "number" and t or nil
end
local function blockActive()
 local f = W.get(TppBuddy2BlockController, "IsBlockActive")
 local ok, yes = pcall(f)
 return ok and yes == true
end
C.active = active

-- The buddy keys of the follow seeds (key[6] "buddy"), read and written on vars.
local function keys()
 if C.keyList then return C.keyList end
 local list = {}
 for _, key in ipairs(W.followSeeds.keys) do if key[6] == "buddy" then list[#list + 1] = key end end
 C.keyList = list
 return list
end
local function read(key)
 local v = W.get(vars, key[1])
 if not key[2] then return type(v) == "number" and v or nil end
 if v == nil then return nil end
 local t = {}
 for i = 0, key[2] - 1 do
  local x = W.get(v, i)
  if type(x) ~= "number" then return nil end
  t[i] = x
 end
 return t
end
local function write(key, value)
 if key[2] then
  local array = W.get(vars, key[1])
  if array == nil then return end
  for i = 0, key[2] - 1 do array[i] = value[i] end
 else vars[key[1]] = value end
end
-- Own-only (never seeded), optional: the dead-buddy and common buddy flags. E19 (run d937191f): guards killed the
-- guest copy's held dog (deadBuddyType 0 -> 4, buddyCommonFlag 32 -> 0); the guest's saves keep its own (D8 b).
local OWN = {{"deadBuddyType"}, {"buddyCommonFlag"}}
function C.snapshot()
 local s = {}
 for _, key in ipairs(keys()) do
  local v = read(key)
  if v == nil then return nil, key[1] end
  s[key[1]] = v
 end
 for _, key in ipairs(OWN) do s[key[1]] = read(key) end
 return s
end
function C.restore(s)
 for _, list in ipairs({keys(), OWN}) do
  for _, key in ipairs(list) do
   local v = s[key[1]]
   if v ~= nil then pcall(write, key, v) end
  end
 end
end
-- D8 (b), guest only: its own buddy vars, kept while the host's seeds are in vars. Every save
-- writes them (the SaveVarsToSlot wrapper).
function C.keep(why)
 if C.own then return true end
 local s, bad = C.snapshot()
 if not s then L("step=own_refused;key=" .. tostring(bad) .. ";"); return false end
 C.own, C.ownWhy = s, why
 L(fmt("step=own_kept;why=%s;own=%s;", why, typeName(s.buddyType)))
 return true
end
function C.release(why)
 if not C.own then return end
 C.restore(C.own)
 L(fmt("step=own_restored;why=%s;own=%s;", why, typeName(C.own.buddyType)))
 C.own, C.ownWhy = nil, nil
end
-- Guest: the seeds were applied for this mission; the tick confirms the load.
-- A new mission loads buddyTypeMissionStart (ResetVarsMissionStart before the block Load); a
-- checkpoint continue (kind 4) the live buddyType.
local function nextType(kind)
 if kind == 4 then return W.get(vars, "buddyType") end
 return W.get(vars, "buddyTypeMissionStart")
end
-- What the guest's copy shows of the host's companion (design section 4 rule 6, "unknown means
-- hidden"; review 2026-10-29): a kind with a hold (M2: the dog), else none until its milestone
-- (M5 Quiet, M9 walker, M10 horse); never a second Quiet beside a guest who plays her.
C.HOLD = {DOG = true}
local function why(t) return t == BT("QUIET") and C.selfQuiet and "self_quiet" or "no_hold" end
function C.shown(t)
 if t == nil or t == BT("NONE") then return t end
 for n in pairs(C.HOLD) do if BT(n) == t and not (n == "QUIET" and C.selfQuiet) then return t end end
 return BT("NONE")
end
local PARK = {"buddyType", "buddyTypeMissionStart"}
function C.seeded(mission, kind)
 local raw = nextType(kind)
 for _, k in ipairs(PARK) do
  local v = W.get(vars, k)
  if type(v) == "number" and C.shown(v) ~= v then
   vars[k] = BT("NONE")
   L(fmt("step=seed_filter;var=%s;kind=%s;reason=%s;", k, typeName(v), why(v)))
  end
 end
 local t = nextType(kind)
 if C.expect and C.expect.mission == mission and C.expect.type == t and C.expect.raw == raw then return end
 C.expect = {mission = mission, type = t, raw = raw}
 L(fmt("step=seed_apply;kind=%s;own=%s;mission=%d;", typeName(t), typeName(C.own and C.own.buddyType), mission))
end
local function lockLog(via)
 L(fmt("step=quiet_lock;via=%s;guest_char=quiet;", via))
 if C.notedLock ~= W.mission then C.notedLock = W.mission; note(3) end
end
-- Host: the Quiet the lock removes is parked (design 6.1.5.2; review 2026-10-29, run de43726a):
-- the live var reads NONE and every save writes QUIET for it, so the host's save keeps her. The
-- slot restore of TppMain.OnAllocate (VarRestoreOnMissionStart / OnContinueFromCheckPoint) runs
-- before the block Load, so StageBlockCurrentPosition(true) (after it, before SetSortieBuddy)
-- parks her again on a locked load. A live non-Quiet choice (sortie prep, a call) drops the park.
local function parkVar(k, via)
 local Q = BT("QUIET")
 if not Q or W.get(vars, k) ~= Q then return false end
 vars[k] = BT("NONE")
 C.parked = C.parked or {}
 C.parked[k] = true
 lockLog(via)
 return true
end
local function park(via) local a, b = parkVar(PARK[1], via), parkVar(PARK[2], via); return a or b end
function C.parkCheck()
 if not C.parked then return end
 local Q, N, a = BT("QUIET"), BT("NONE"), active()
 for k in pairs(C.parked) do
  local v = W.get(vars, k)
  -- A loaded non-Quiet companion is the host's choice too (a native path that writes no var).
  if k == "buddyType" and type(a) == "number" and a ~= N and a ~= Q then v = a end
  if type(v) == "number" and v ~= N and v ~= Q then C.parked[k] = nil; L(fmt("step=park_dropped;var=%s;live=%s;", k, typeName(v))) end
 end
 if next(C.parked) == nil then C.parked = nil end
end
function C.unpark(why)
 if not C.parked then return end
 for k in pairs(C.parked) do if W.get(vars, k) == BT("NONE") then vars[k] = BT("QUIET") end end
 C.parked = nil
 L("step=quiet_back;why=" .. why .. ";")
end
-- TppMission.Load (world_follow.lua, before the seeds): coop is a co-op travel (host: on;
-- guest: following the host).
function C.load(nextMission, coop)
 if W.host then
  local lock = coop and C.on() and W.paired and C.guestQuiet == true
  -- Latched for this load's StageBlockCurrentPosition(true) (cleared at the next mission start).
  C.stageLock = lock or nil
  if lock then park("sortie_rewrite") else C.unpark("host_unlocked") end
  local t = W.follow.outgoing
  if coop and C.on() then
   L(fmt("step=seed;kind=%s;live=%s;mission=%d;", typeName(nextType(t and t[2])), typeName(W.get(vars, "buddyType")), nextMission or 0))
  end
 elseif C.own and not coop then C.release("solo") end
end

-- The guest's character (coop.ini, story resolved on this copy's own save) -> code, Quiet.
function C.character()
 local ok, name = pcall(W.receipt, "return 'MGS_COOP_CHARACTER_V1'")
 if not ok or type(name) ~= "string" or CHARS[name] == nil then return 0, false end
 if name == "story" then
  local K = W.get(W.get(Tpp, "MgsCoop"), "Character")
  if type(W.get(K, "Gather")) == "function" and type(W.get(K, "Story")) == "function" then
   local good, x = pcall(K.Gather, "story", "dd_male")
   if good and type(x) == "table" then
    local fine, r = pcall(K.Story, x.quiet)
    if fine then return 7, r == "quiet" end
   end
  end
  return 7, false
 end
 return CHARS[name], name == "quiet"
end
local function sendCharacter(now, force)
 if W.host or not C.on() or not W.paired then return end
 if not force and now < (C.charAt or 0) then return end
 local code, quiet = C.character()
 C.selfQuiet = quiet
 C.seq = C.seq % 65535 + 1
 if W.tx(OP.CHARACTER, C.seq, code, quiet and 1 or 0) then
  C.charAt = now + 15000
  if C.charSent ~= code * 2 + (quiet and 1 or 0) then
   C.charSent = code * 2 + (quiet and 1 or 0)
   L(fmt("step=character_tx;code=%d;quiet=%d;", code, quiet and 1 or 0))
  end
 end
end
H[OP.CHARACTER] = function(v)
 if not W.host or #v ~= 3 then return end
 peerSeen()
 local quiet = v[3] == 1
 if C.guestQuiet ~= quiet or C.guestChar ~= v[2] then
  C.guestQuiet, C.guestChar = quiet, v[2]
  L(fmt("step=character;guest_code=%d;guest_char=%s;", v[2], quiet and "quiet" or "other"))
 end
end
H[OP.COMPANION] = function(v, now)
 if W.host or #v ~= 12 then return end
 peerSeen()
 C.host = {kind = v[3], present = v[5] == 1, x = v[6] / 1000, y = v[7] / 1000, z = v[8] / 1000, yaw = v[9] / 1000, at = now}
 if C.hostKind ~= v[3] then
  C.hostKind = v[3]
  L(fmt("step=host_kind;kind=%s;", typeName(fromWire(v[3]))))
 end
end
-- The host's companion: its kind (COMPANION), else the seeded vars before the filter. What the
-- guest's copy may load of it: C.allowed().
function C.hostType()
 if C.hostKind then return fromWire(C.hostKind) end
 if C.expect then return C.expect.raw end
end
function C.allowed() return C.shown(C.hostType()) end

-- Host: the Quiet lock while the guest plays Quiet; COMPANION on change and every 5 s.
local function hostTick(now)
 local Q, S = BT("QUIET"), TppBuddyService
 -- The lock lasts the session (sessionTick: a mission change unpairs for a few seconds, run dd4c61ff).
 if C.guestQuiet == true and Q then
  if C.quietDisabled ~= W.mission then
   local is, set = W.get(S, "IsDisableBuddyType"), W.get(S, "SetDisableBuddyType")
   local okIs, already = false, false
   if type(is) == "function" then okIs, already = pcall(is, Q) end
   local mine = not (okIs and already == true)
   if mine and type(set) == "function" then pcall(set, Q) end
   C.quietDisabled, C.quietMine = W.mission, mine or C.quietMine
   lockLog("disable_type")
  end
  if W.paired and active() == Q and now >= (C.deleteAt or 0) and W.inGame() then
   C.deleteAt = now + 5000
   local del = TBC("DeleteBuddy")
   if type(del) == "function" then
    C.token = true
    local ok = pcall(del)
    C.token = nil
    -- Her out is the host's choice whatever DeleteBuddy wrote: parked.
    vars.buddyType = BT("NONE")
    C.parked = C.parked or {}
    C.parked.buddyType = true
    if ok then lockLog("delete_buddy") end
   end
  end
  -- The sortie copy too (run b993af09 read 3 at Mother Base after the heli delete; a restart's
  -- ResetVarsMissionStart would copy it back into buddyType); at most every 5 s.
  if W.paired and W.get(vars, "buddyTypeMissionStart") == Q and now >= (C.startAt or 0) then
   C.startAt = now + 5000
   parkVar("buddyTypeMissionStart", "start_rewrite")
  end
 elseif C.quietDisabled then
  if C.quietMine then pcall(W.get(S, "UnsetDisableBuddyType"), Q) end
  C.quietDisabled, C.quietMine = nil, nil
  L("step=quiet_unlock;")
 end
 C.parkCheck()
 local k = toWire(active())
 if now < (C.txAt or 0) and k == C.txKind then return end
 C.seq = C.seq % 65535 + 1
 -- M2: the dog's last streamed pose (world_buddy.lua COMPANION_POSE), so a reconcile calls it there.
 local b, p = W.get(W.get(W.buddy, "s"), "sent"), {0, 0, 0, 0, 0}
 if type(b) == "table" and b.k == k and b.present == 1 then
  local function mm(n) return math.floor(n * 1000 + 0.5) end
  p = {1, mm(b.x), mm(b.y), mm(b.z), mm(b.yaw)}
 end
 if W.tx(OP.COMPANION, C.seq, W.tick32(now), k, 0, p[1], p[2], p[3], p[4], p[5], 0, 0, 0) then
  C.txAt, C.txKind = now + 5000, k
 end
end

-- Guest: the CALL mask (call menu, horse whistle), released while in a CQC hold (interrogate).
local function cqcHold()
 local s, f = W.get(PlayerStatus, "CQC_CONTINUOUS"), W.get(PlayerInfo, "OrCheckStatus")
 if type(s) ~= "number" or type(f) ~= "function" then return false end
 local ok, yes = pcall(f, {s})
 return ok and yes == true
end
function C.mask(want)
 if want == (C.masked == true) then return end
 local call = W.get(PlayerPad, "CALL")
 local f = W.get(Player, want and "SetPadMask" or "ResetPadMask")
 if type(f) ~= "function" or type(call) ~= "number" then return end
 local ok = pcall(f, want and {settingName = MASK, except = false, buttons = call, sticks = 0, triggers = 0} or {settingName = MASK})
 if not ok then return end
 C.masked = want
 if want and C.maskNoted ~= W.mission then
  C.maskNoted = W.mission
  L("step=guard;path=call_menu;via=pad_mask;")
  note(1)
 end
end
local function vector(x, y, z)
 local ok, v = pcall(Vector3, x, y, z)
 return ok and v or nil
end
-- d m in front of this copy's player (forward sin/cos of playerRotY) and its yaw in radians.
function C.front(d)
 local px, py, pz, r = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ"), W.get(vars, "playerRotY")
 if type(px) ~= "number" or type(py) ~= "number" or type(pz) ~= "number" then return nil end
 local yaw = (((type(r) == "number" and r or 0) + 180) % 360 - 180) * math.pi / 180
 return {px + d * math.sin(yaw), py, pz + d * math.cos(yaw), yaw}
end
local function reconcile(now)
 local h = C.host
 if not h or now - h.at > 15000 or not W.inGame() or W.get(DemoDaemon, "IsDemoPlaying") and DemoDaemon.IsDemoPlaying() then
  C.mismatchAt = nil; return
 end
 if C.expect and C.expect.mission == W.mission and not C.expect.done then return end
 -- The host's kind as this copy shows it (C.shown: none for a kind without a hold here).
 local raw, have, N = fromWire(h.kind), active(), BT("NONE")
 local want = C.shown(raw)
 if want ~= raw and C.skipLogged ~= W.mission * 8 + h.kind then
  C.skipLogged = W.mission * 8 + h.kind
  L(fmt("step=reconcile_skip;reason=%s;kind=%s;", why(raw), typeName(raw)))
 end
 if want ~= nil and want == have then C.mismatchAt, C.fixWant, C.fixN, C.fixQuit = nil, nil, nil, nil end
 if want == nil or have == nil or want == have then C.mismatchAt = nil; return end
 C.mismatchAt = C.mismatchAt or now
 -- 8 s (15 s to send one away): a host warp, death or cluster change drops its kind for a few
 -- seconds (run 9e8f22a7: none for 3.5 s after a warp across Mother Base).
 if now - C.mismatchAt < (want == N and 15000 or 8000) or now < (C.fixAt or 0) then return end
 -- Called at the host's pose (COMPANION carries the dog's, M2); without one it waits, never
 -- where this player stands (M1 called it 4 m in front of the guest).
 local x, y, z, yaw = h.x, h.y, h.z, h.yaw
 if want ~= N and (not h.present or (x == 0 and y == 0 and z == 0)) then
  if C.waitLogged ~= W.mission then C.waitLogged = W.mission; L(fmt("step=reconcile_wait;kind=%s;reason=no_pose;", typeName(want))) end
  return
 end
 -- Six tries, 5 s apart and doubling (a block that cannot load the kind: E19's dead dog, run
 -- dc761c98), then one summary line; only with the guest's own snapshot (a CallBuddy writes
 -- vars.buddyType, which its saves must not keep; review 2026-10-29).
 if C.fixWant ~= want then C.fixWant, C.fixN, C.fixQuit = want, 0, nil end
 if C.fixN >= 6 then
  if not C.fixQuit then C.fixQuit = true; L(fmt("step=reconcile_gave_up;kind=%s;have=%s;tries=%d;", typeName(want), typeName(have), C.fixN)) end
  return
 end
 C.fixN = C.fixN + 1
 C.fixAt = now + math.min(60000, 5000 * 2 ^ (C.fixN - 1))
 if not C.keep("reconcile") then return end
 local via, ok = "call", false
 C.token = true
 if want == BT("NONE") then
  via = "delete"; ok = pcall(TBC("DeleteBuddy"))
 elseif blockActive() then
  local v = vector(x, y, z)
  local good, r = pcall(TBC("CallBuddy"), want, v, yaw)
  ok = good and r ~= false
 else
  via = "load"
  local v, n = vector(x, y, z), W.get(BuddyInitStatus, "NORMAL") or 0
  ok = pcall(TBC("ReserveCallBuddy"), want, n, v, yaw) and pcall(TBC("Load"))
 end
 C.token = nil
 L(fmt("step=reconcile;via=%s;kind=%s;have=%s;ok=%d;", via, typeName(want), typeName(have), ok and 1 or 0))
end
-- Guest: the disable byte for every buddy type that is neither the host's nor loaded here, so a
-- native swap request (the iDroid buddy change; the controller update reaches the CallBuddy
-- core 0xA44740 that tests the byte, static read) refuses. Only bits this module set are
-- cleared (at mission end, unpair); never the loaded or the host's type (K5).
local GUARD = {"HORSE", "DOG", "QUIET", "WALKER_GEAR"}
local function guardTypes(now)
 if now < (C.typeAt or 0) then return end
 C.typeAt = now + 1000
 local S = TppBuddyService
 local is, set, unset = W.get(S, "IsDisableBuddyType"), W.get(S, "SetDisableBuddyType"), W.get(S, "UnsetDisableBuddyType")
 local allowed, have = C.allowed(), active()
 if type(is) ~= "function" or type(set) ~= "function" or type(unset) ~= "function" or allowed == nil then return end
 C.mine = C.mine or {}
 local changed = false
 for _, n in ipairs(GUARD) do
  local t = BT(n)
  if t and C.mine[t] and (t == allowed or t == have) then
   pcall(unset, t); C.mine[t] = nil; changed = true
  elseif t and t ~= allowed and t ~= have and not C.mine[t] then
   local ok, already = pcall(is, t)
   if ok and already ~= true and pcall(set, t) then C.mine[t] = true; changed = true end
  end
 end
 if changed then L(fmt("step=guard;path=idroid_swap;via=disable_type;allowed=%s;", typeName(allowed))) end
end
function C.unguard()
 local unset = W.get(TppBuddyService, "UnsetDisableBuddyType")
 if C.mine and type(unset) == "function" then for t in pairs(C.mine) do pcall(unset, t) end end
 C.mine = nil
end
local function guestTick(now)
 local E = C.expect
 if E and E.mission == W.mission and not E.done and W.inGame() then
  local have = active()
  if have ~= nil and have == E.type then
   E.done = true
   L(fmt("step=loaded;kind=%s;source=seed;mission=%d;", typeName(have), W.mission))
  elseif E.deadline and now > E.deadline then
   E.done = true
   L(fmt("step=load_mismatch;want=%s;have=%s;", typeName(E.type), typeName(have)))
  end
 end
 sendCharacter(now)
 if not C.live() then if C.masked then C.mask(false) end if C.mine then C.unguard() end return end
 if now >= (C.maskAt or 0) then
  C.maskAt = now + 250
  -- Only while the host has a companion out: the call menu also holds knock and sonar (run dd4c61ff).
  local out = C.hostType()
  C.mask(W.paired and W.inGame() and out ~= nil and out ~= BT("NONE") and not cqcHold())
 end
 if W.paired then guardTypes(now); reconcile(now) end
end
-- The session ends 10 s after a link-session or level-0 unpair, else after 120 s alone in game (a
-- mission change re-pairs within seconds), while no host transition is pending. The host forgets
-- the guest's character (the lock lifts); the guest sends the host's companion away (never a
-- release to its own AI) and gets its own buddy vars back (D8 b). Review 2026-10-29.
function C.endSession(why)
 L(fmt("step=session_end;why=%s;", why))
 C.peer, C.host, C.hostKind, C.guestQuiet, C.guestChar, C.aloneAt, C.unpairedAt, C.unpairWhy = nil, nil, nil, nil, nil, nil, nil, nil
 if W.host then return end
 local B, have = W.buddy, active()
 if type(B) == "table" and type(B.clearOwnership) == "function" and type(B.s) == "table" then pcall(B.clearOwnership, B.s) end
 if C.own and have and have ~= BT("NONE") then
  C.token = true
  local ok = pcall(TBC("DeleteBuddy"))
  C.token = nil
  L(fmt("step=session_end;via=delete;kind=%s;ok=%d;", typeName(have), ok and 1 or 0))
 end
 C.release("session_end")
 C.mask(false); C.unguard(); C.expect = nil
end
local function sessionTick(now)
 if W.paired then C.aloneAt, C.unpairedAt, C.unpairWhy = nil, nil, nil; return end
 if not C.peer and not C.guestQuiet and not C.own then return end
 C.aloneAt = C.aloneAt or now
 local short = C.unpairedAt and (C.unpairWhy == 1 or C.unpairWhy == 5)
 if now - (short and C.unpairedAt or C.aloneAt) < (short and 10000 or 120000) then return end
 local F = W.follow
 if F.current or F.queue[1] then return end
 C.endSession(short and "unpaired" or "alone")
end
function C.tick(now)
 if not C.on() then if C.masked then C.mask(false) end if C.mine then C.unguard() end return end
 if now < (C.nextTick or 0) then return end
 C.nextTick = now + 100
 pcall(sessionTick, now)
 local ok, err = pcall(W.host and hostTick or guestTick, now)
 if not ok and now >= (C.errAt or 0) then C.errAt = now + 10000; L("step=error;msg=" .. string.sub(tostring(err), 1, 80)) end
end
W.startHooks[#W.startHooks + 1] = function()
 C.nextTick, C.txAt, C.txKind, C.deleteAt, C.fixAt, C.mismatchAt, C.host, C.hostKind = nil, nil, nil, nil, nil, nil, nil, nil
 C.fixWant, C.fixN, C.fixQuit = nil, nil, nil
 C.masked, C.cellLogged, C.hookLines, C.startAt, C.stageLock = nil, nil, 0, nil, nil
 if C.expect and C.expect.mission == W.mission then C.expect.deadline = W.now() + 30000 else C.expect = nil end
 if C.on() and not W.host then C.charAt = nil end
end
W.pairedHooks[#W.pairedHooks + 1] = function() C.charAt, C.txAt, C.pairedAt = nil, nil, W.now() end
-- The host's deploy waits for the guest's CHARACTER (world_follow.lua F.hold; at most 10 s after
-- pairing), so its sortie seeds know the Quiet lock (review 2026-10-29).
W.follow.waitPeer = function(now) return W.host and C.on() and W.paired and not C.peer and now - (C.pairedAt or now) < 10000 end
W.unpairedHooks[#W.unpairedHooks + 1] = function(reason)
 C.host, C.hostKind, C.unpairedAt, C.unpairWhy = nil, nil, W.now(), reason
 pcall(C.mask, false)
 pcall(C.unguard)
end
W.stopHooks[#W.stopHooks + 1] = function() pcall(C.mask, false); pcall(C.unguard); C.typeAt = nil end

-- E4 census and guest refusal of the Lua buddy entry points (the co-op module's own calls
-- pass: C.token, and W.api.buddyCall is the original, bound below).
local function census(fn, t)
 if C.hookLines >= 40 then return end
 C.hookLines = C.hookLines + 1
 L(fmt("step=hook;fn=%s;type=%s;", fn, typeName(t)))
end
function C.refuse(path, t)
 L(fmt("step=refused;path=%s;type=%s;host=%s;", path, typeName(t), typeName(C.hostType())))
 note(2)
 return false
end
local function guard(fn, t)
 if W.host or not C.live() then return false end
 local allowed = C.allowed()
 if allowed == nil then return false end
 if fn == "DeleteBuddy" then return allowed ~= BT("NONE") end
 return t ~= allowed
end
function C.install()
 if C.installed then return end
 C.installed = true
 for _, fn in ipairs({"CallBuddy", "CallBuddyWithHalo", "ReserveCallBuddy", "DeleteBuddy"}) do
  local f = W.get(TppBuddy2BlockController, fn)
  if type(f) == "function" then
   C.orig[fn] = f
   TppBuddy2BlockController[fn] = function(...)
    if C.token then return f(...) end
    local t = (...)
    pcall(census, fn, t)
    local ok, refuse = pcall(guard, fn, t)
    if ok and refuse then return C.refuse("lua_" .. fn, t) end
    return f(...)
   end
  end
 end
 local summon = W.get(TppBuddyService, "SummonBuddy")
 if type(summon) == "function" then
  TppBuddyService.SummonBuddy = function(...)
   pcall(census, "SummonBuddy", (...))
   return summon(...)
  end
 end
 -- Every save: the guest writes its own buddy vars (D8 b), the host QUIET for each parked var
 -- that reads NONE; vars are as they were afterwards. The first three lines, then one per save
 -- burst (5 s), so a checkpoint's write shows.
 local save = W.get(TppSave, "SaveVarsToSlot")
 if type(save) == "function" then
  local function logged()
   C.saves = (C.saves or 0) + 1
   local now = W.now()
   if C.saves <= 3 or now - (C.saveLogAt or -1e9) >= 5000 then C.saveLogAt = now; return true end
  end
  TppSave.SaveVarsToSlot = function(...)
   local slot = tostring((...))
   if W.host then
    C.parkCheck()
    if not C.parked then return save(...) end
    local N, put = BT("NONE"), {}
    for k in pairs(C.parked) do if W.get(vars, k) == N then put[#put + 1] = k; vars[k] = BT("QUIET") end end
    local r = {pcall(save, ...)}
    for _, k in ipairs(put) do vars[k] = N end
    if logged() then L(fmt("step=save_quiet;slot=%s;vars=%d;n=%d;", slot, #put, C.saves)) end
    if not r[1] then error(r[2], 0) end
    return unpack(r, 2)
   end
   local own = C.own
   if not own then return save(...) end
   local live = C.snapshot()
   if not live then return save(...) end
   C.restore(own)
   local r = {pcall(save, ...)}
   C.restore(live)
   if logged() then
    L(fmt("step=save_own;slot=%s;own=%s;live=%s;n=%d;own_dead=%s;live_dead=%s;own_flag=%s;live_flag=%s;", slot,
     typeName(own.buddyType), typeName(live.buddyType), C.saves, tostring(own.deadBuddyType), tostring(live.deadBuddyType),
     tostring(own.buddyCommonFlag), tostring(live.buddyCommonFlag)))
   end
   if not r[1] then error(r[2], 0) end
   return unpack(r, 2)
  end
 end
 -- Host: after OnAllocate's slot restore, before SetSortieBuddy and the block Load (TppMain), a
 -- locked load parks Quiet again (C.load latched the lock).
 local stage = W.get(e, "StageBlockCurrentPosition")
 if type(stage) == "function" then
  e.StageBlockCurrentPosition = function(first, ...)
   if first == true and W.host and C.stageLock then pcall(park, "stage_rewrite") end
   return stage(first, ...)
  end
 end
 -- E20: no Mother Base cell Quiet (quest mtbs_wait_quiet) while the guest plays Quiet.
 local arrival = W.get(TppStory, "CanArrivalQuietInMB")
 if type(arrival) == "function" then
  TppStory.CanArrivalQuietInMB = function(n, ...)
   if n ~= true and W.get(vars, "missionCode") == 30050 and
    ((W.host and C.on() and C.guestQuiet == true) or (not W.host and C.live() and C.selfQuiet)) then
    if not C.cellLogged then
     -- The stock answer (true: she would be in her cell now), read once for the log.
     local ok, stock = pcall(arrival, n, ...)
     C.cellLogged = true
     L("step=mbquiet_hidden;stock=" .. (ok and tostring(stock) or "error") .. ";")
    end
    return false
   end
   return arrival(n, ...)
  end
 end
end
W.follow.installHooks[#W.follow.installHooks + 1] = function(cfg)
 if type(cfg) == "table" and cfg[16] == 1 then
  local ok, err = pcall(C.install)
  if not ok then W.log("event=companion;step=error;at=install;msg=" .. string.sub(tostring(err), 1, 80)) end
  if not W.host and C.selfQuiet == nil then local _, _, q = pcall(C.character); C.selfQuiet = q == true end
 end
end
W.bindHooks[#W.bindHooks + 1] = function(a)
 if C.orig.CallBuddy then a.buddyCall = C.orig.CallBuddy end
end

-- For the lab levers (world_companion_dog.lua).
C.lib = {BT = BT, TBC = TBC, active = active, blockActive = blockActive, vector = vector}
-- buddy_status tail (world_buddy.lua): vars.buddyType and this copy's own kept type (- none;
-- the host: its parked Quiet).
function C.tail()
 local vt, own = W.get(vars, "buddyType"), C.own and C.own.buddyType or C.parked and BT("QUIET")
 return fmt(" vars_type=%s own=%s", type(vt) == "number" and tostring(vt) or "?", type(own) == "number" and tostring(own) or "-")
end
-- Read-only report (debug companion_status) and the lab seed lever (debug seed_buddy <kind>).
function C.status()
 return fmt("companion;on=%d;role=%s;active=%s;vars_type=%s;start=%s;own=%s;allowed=%s;guest_quiet=%s;self_quiet=%s;masked=%d;lock=%s;saves=%d",
  C.on() and 1 or 0, W.host and "host" or "guest", typeName(active()), tostring(W.get(vars, "buddyType")), tostring(W.get(vars, "buddyTypeMissionStart")),
  C.own and typeName(C.own.buddyType) or C.parked and "quiet_parked" or "-", typeName(C.allowed()), tostring(C.guestQuiet), tostring(C.selfQuiet),
  C.masked and 1 or 0, tostring(C.quietDisabled ~= nil), C.saves or 0)
end
end
end
