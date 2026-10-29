-- Original host-led D-Dog mirror; ride yield clears its typed held target.
-- M2 (docs/vehicles-companions-design.md 6.2, 6.12; lab key companion_host_owned, W.companion.on()): the host
-- streams its dog in COMPANION_POSE (57, a datagram: 10 Hz while the dog moves, 2 Hz idle) instead of BUDDY_STATE;
-- the guest holds its copy of the host's dog there (LuaAiStayAndSnarl every 100 ms, SetWarpPosition beyond 15 m)
-- and answers COMPANION_ACK (49, 1 Hz). That dog is never released to the guest's AI: without a fresh pose it is
-- held at the last one (or where it stands); a host without a dog is world_companion.lua's reconcile (DeleteBuddy).
do
local W = e.MgsCoopWorld
if type(W) == "table" then
local B, floor, sqrt, min, fmt = {}, math.floor, math.sqrt, math.min, string.format
W.buddy = B
W.OP.BUDDY_STATE, W.OP.BUDDY_ACK = 39, 40
local STATE, ACK = 39, 40
local CPOSE, CACK = 57, 49
W.OP.COMPANION_POSE, W.OP.COMPANION_ACK = CPOSE, CACK
local function companion()
 local C = W.companion
 return type(C) == "table" and type(C.on) == "function" and C.on() == true
end
B.companion = companion
local function finite(n, bound)
return type(n) == "number" and n == n and n >= -bound and n <= bound
end
local function integer(n, lo, hi) return finite(n, hi) and n >= lo and n == floor(n) end
local function vector(p)
if type(p) ~= "table" and type(p) ~= "userdata" then return nil,nil,nil,"type" end
local ok, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end)
if not ok then return nil,nil,nil,"methods" end
if finite(x, 1000000) and finite(y, 1000000) and finite(z, 1000000) then return x, y, z end
return nil,nil,nil,"finite"
end
local function yaw()
local n = W.get(vars, "playerRotY")
if not finite(n, 1000000000) then return 0 end
return ((n + 180) % 360 - 180) * math.pi / 180
end
local function mm(n) return floor(n * 1000 + 0.5) end
local function kind(t)
local a = W.api
if t == a.buddyDog then return 1 end
if t == a.buddyHorse then return 2 end
if t == a.buddyWalker then return 3 end
if t == a.buddyQuiet then return 4 end
return 0
end
W.bindHooks[#W.bindHooks + 1] = function(a, fn, num)
a.buddyType = fn(TppBuddy2BlockController, "TppBuddy2BlockController", "GetActiveBuddyType", true)
a.buddyBlock = fn(TppBuddy2BlockController, "TppBuddy2BlockController", "IsBlockActive", true)
a.buddyCall = fn(TppBuddy2BlockController, "TppBuddy2BlockController", "CallBuddy", true)
a.buddyId = fn(GameObject, "GameObject", "GetGameObjectIdByIndex", true)
local constructor = Vector3
a.buddyVector = constructor ~= nil and function(x,y,z) return constructor(x,y,z) end or nil
local vectorReason
if a.buddyVector then
 local ok, p = pcall(a.buddyVector, 0, 0, 0)
 if not ok then a.buddyVector, vectorReason = nil, "vector_not_callable"
 elseif not vector(p) then a.buddyVector, vectorReason = nil, "vector_unreadable" end
else vectorReason = "missing_Vector3" end
a.buddyDemo = fn(DemoDaemon, "DemoDaemon", "IsDemoPlaying", true)
a.buddyDog = num(BuddyType, "BuddyType", "DOG", true)
a.buddyNone = num(BuddyType, "BuddyType", "NONE", true)
a.buddyHorse = num(BuddyType, "BuddyType", "HORSE", true)
a.buddyWalker = num(BuddyType, "BuddyType", "WALKER_GEAR", true)
a.buddyQuiet = num(BuddyType, "BuddyType", "QUIET", true)
B.bound, B.bindReason = true, nil
for _, name in ipairs({"buddyType", "buddyBlock", "buddyCall", "send", "buddyId", "buddyVector", "buddyDemo", "buddyDog", "buddyNone"}) do
 if a[name] == nil then
  B.bound, B.bindReason = false, name == "buddyVector" and vectorReason or "missing_" .. name
  break
 end
end
end
function B.guard(requireActive)
if not B.bound then return false, B.bindReason or "missing_binding" end
if requireActive and not W.active then return false, "world_inactive" end
if W.MISSIONS[W.mission] == nil or W.get(vars, "missionCode") ~= W.mission then return false, "mission_mismatch" end
if not W.inGame() then return false, "not_in_game" end
if W.api.buddyDemo() ~= false then return false, "demo_active" end
if W.api.buddyBlock() ~= true then return false, "buddy_block_inactive" end
return true
end
function B.context() return B.guard(true) end
function B.dog(cleanup)
if W.api.buddyType() ~= W.api.buddyDog then return end
local s, now = B.s, W.now()
local function diagnostic(id, p, why)
 local short = {number="n",table="t",userdata="u",string="s",boolean="b",["function"]="f",["nil"]="-"}
 local value = integer(id,0,65535) and tostring(id) or "?"
 local code = "q" .. (short[type(id)] or "x") .. value .. "/" .. (short[type(p)] or "x") .. "/" .. why
 s.probe = {code=code, at=now, idType=type(id), id=id, positionType=type(p), reason=why}
 if why ~= "ok" and (s.probeLast ~= code or not s.probeLogAt or now-s.probeLogAt >= 5000) then
  W.log("event=buddy_read;probe=" .. code)
  s.probeLast, s.probeLogAt = code, now
 end
end
local ok, id = pcall(W.api.buddyId, "TppBuddyDog2", 0)
if not ok then diagnostic(nil,nil,"iderr"); return end
if id ~= 9728 then diagnostic(id,nil,"idbad"); return nil,nil,nil,nil,id end
local mode = cleanup and s.owned and s.ownedMission == W.mission and 1 or 0
local epoch = mode == 1 and s.ownedEpoch or W.epoch
if not integer(epoch,1,65535) then diagnostic(id,nil,"epoch"); return nil,nil,nil,nil,id end
local readable, r = pcall(W.receipt,string.format("return 'MGS_COOP_BUDDY_POSE_V1' -- %d %d %d %d",
 W.mission,epoch,id,mode))
if not readable then diagnostic(id,nil,"nerr"); return nil,nil,nil,nil,id end
if type(r) ~= "table" then diagnostic(id,r,"reply"); return nil,nil,nil,nil,id end
if r.ready ~= true then
 local reasons = {admission="admit",scope="scope",player_scope="player",dog_selection="select",dog_registry="reg",
  dog_primary_vt="pvt",dog_secondary="kind",dog_unrealized="real",dog_position="pos",dog_changed="changed"}
 diagnostic(id,r.pos,"n_" .. (reasons[r.reason] or "refused")); return nil,nil,nil,nil,id
end
local p = r.pos
if r.mission ~= W.mission or r.epoch ~= epoch or r.id ~= id or type(p) ~= "table" or #p ~= 3 then
 diagnostic(id,p,"reply"); return nil,nil,nil,nil,id
end
for i=1,3 do if not integer(p[i],-1000000000,1000000000) then
 diagnostic(id,p,"pos"); return nil,nil,nil,nil,id
end end
local x,y,z = p[1]/1000,p[2]/1000,p[3]/1000
diagnostic(id,p,"ok")
s.localPos = {x=x,y=y,z=z,id=id,at=now}
return id,x,y,z,id
end
function B.log(reason, errorCm)
local now, s = W.now(), B.s
if s.reason ~= reason or not s.logAt or now - s.logAt >= 5000 then
 W.log(string.format("event=buddy;role=%s;result=%s;error_cm=%d", W.host and "host" or "guest", reason, errorCm or 65535))
 s.reason, s.logAt = reason, now
end
end
function B.clearOwnership(s)
s.owned, s.original, s.id, s.pending, s.nextRelease, s.ownedMission, s.ownedEpoch, s.aiHeld = nil, nil, nil, nil, nil, nil, nil, nil
end
function B.yieldForRide()
local s,a=B.s,W.api;if not s then return false end
-- M2: the guest copy's dog is the host's; its hold is never cleared for a ride lever.
if companion() and not W.host then return false end
if s.owned and s.aiHeld then
 if s.ownedMission~=W.mission or s.ownedEpoch~=W.epoch or not B.guard(true) then return false end
 local id=B.dog(true)
 if not id or (s.id and id~=s.id) then return false end
 local ok,result=pcall(a.send,id,{id="LuaAiStayAndSnarl"})
 if not ok or result==false then return false end
 s.aiHeld=nil;B.log("target_cleared_for_ride")
end
if s.owned then s.yieldedOwned={original=s.original,id=s.id,pending=s.pending,ownedMission=s.ownedMission,ownedEpoch=s.ownedEpoch,aiHeld=s.aiHeld};B.clearOwnership(s) end
s.rideYield=true;return true
end
function B.keepRideYield()
local s=B.s;if not s then return false end
s.yieldedOwned,s.rideYield=nil,true;return true
end
function B.resumeFollow()
local s=B.s;if not s then return false end
local saved=s.yieldedOwned;s.rideYield,s.yieldedOwned=nil,nil
if not saved or not B.guard(true) then return true end
local t=W.api.buddyType()
if t==W.api.buddyHorse or t==W.api.buddyWalker or t==W.api.buddyQuiet then return true end
s.owned,s.original,s.id,s.pending,s.ownedMission,s.ownedEpoch,s.aiHeld=true,saved.original,saved.id,saved.pending,saved.ownedMission,saved.ownedEpoch,saved.aiHeld
return true
end
function B.release(cleanup)
local s, a, now = B.s, W.api, W.now()
if not s or not s.owned then return true end
-- M2: the host's dog on the guest's copy is never released to the guest's AI (its native hold stays).
if companion() and not W.host then B.clearOwnership(s); return true end
if not B.guard(not cleanup) then return false end
if s.nextRelease and now < s.nextRelease then return false end
local t = a.buddyType()
if t ~= a.buddyDog then
 if t == a.buddyNone and s.pending and s.original == a.buddyNone then
  s.nextRelease = now + 250
  if a.buddyCall(a.buddyNone, a.buddyVector(0, 0, 0), 0) == false then return false end
 end
 B.clearOwnership(s); return true
end
local id, x, y, z, currentId = B.dog(true)
if s.id and currentId and currentId ~= s.id then B.clearOwnership(s); return true end
if not id then return false end
s.nextRelease = now + 250
if s.aiHeld then
 if a.send(id, {id="LuaAiStayAndSnarl"}) == false then return false end
 s.aiHeld = nil
end
local t, p, releaseYaw = a.buddyNone, a.buddyVector(0, 0, 0), 0
if s.original ~= a.buddyNone then
 t, p, releaseYaw = a.buddyDog, a.buddyVector(x, y, z), yaw()
end
if a.buddyCall(t, p, releaseYaw) == false then B.log("release_refused"); return false end
B.clearOwnership(s)
B.log("released")
return true
end
function B.cleanupPending()
local s = B.s
if not s or not s.owned then return true end
if s.ownedMission ~= W.mission then B.clearOwnership(s); s.releaseOnly = nil; return true end
if W.active and not s.releaseOnly then return true end
s.rx, s.rxSeq, s.releaseOnly = nil, nil, true
local ok, released = pcall(B.release, true)
if ok and released then s.releaseOnly = nil; return true end
return false
end
function B.reset(keepOwned)
local old = B.s
B.disabled = nil
B.s = {seq = 0, rxSeq = nil, nextTx = 0, nextApply = 0, nextAck = 0}
if keepOwned and old and old.owned then
 for _, k in ipairs({"owned", "original", "id", "pending", "nextRelease", "ownedMission", "ownedEpoch", "aiHeld"}) do B.s[k] = old[k] end
 B.s.releaseOnly = true
end
end
B.reset()
W.startHooks[#W.startHooks + 1] = function() B.reset() end
W.stopHooks[#W.stopHooks + 1] = function(full)
if full then B.reset(not B.release()) end
end
W.unpairedHooks[#W.unpairedHooks + 1] = function()
B.s.rx, B.s.rxSeq = nil, nil
B.s.releaseOnly = not B.release()
end
W.pairedHooks[#W.pairedHooks + 1] = function(changed)
if changed then B.reset(not B.release()) end
B.s.nextTx = 0
end
function B.ack(sample, result, errorCm, now)
local s = B.s
if now < s.nextAck and s.ackResult == result then return end
if W.tx(ACK, sample.seq, sample.kind, result, min(errorCm or 65535, 65535)) then
 s.nextAck, s.ackResult = now + 1000, result
end
end
W.handlers[STATE] = function(v, now)
if W.host or not W.paired or W.eff < 3 or #v ~= 9 then return end
if not integer(v[1], 1, 65535) or not integer(v[2], 0, 4) or not integer(v[3], 0, 1)
 or not integer(v[4], -1000000000, 1000000000) or not integer(v[5], -1000000000, 1000000000)
 or not integer(v[6], -1000000000, 1000000000) or not integer(v[7], -3142, 3142)
 or not integer(v[8], 0, 4294967295) or not integer(v[9], 0, 65535) then return end
local old = B.s.rxSeq
local d = old and (v[1] - old) % 65536 or 1
if d == 0 or d >= 32768 then return end
B.s.rxSeq = v[1]
B.s.rx = {seq = v[1], kind = v[2], present = v[3] == 1, x = v[4] / 1000, y = v[5] / 1000,
 z = v[6] / 1000, yaw = v[7] / 1000, at = now - v[9]}
if v[2] == 1 and v[3] == 1 then B.s.hostPos = B.s.rx end
end
W.handlers[ACK] = function(v)
if not W.host or not W.paired or W.eff < 3 or #v ~= 4 then return end
if integer(v[1],1,65535) and integer(v[2],0,4) and integer(v[3],0,4) and integer(v[4],0,65535)
 and v[1] == B.s.seq then B.s.remoteResult, B.s.remoteError = v[3], v[4] end
end
function B.host(now)
local s, a = B.s, W.api
if now < s.nextTx then return end
s.nextTx = now + 250
local k, present, x, y, z = kind(a.buddyType()), 0, 0, 0, 0
if k == 1 then
 local id; id, x, y, z = B.dog()
 if id then present = 1; s.hostPos = s.localPos else x, y, z = 0, 0, 0 end
end
local seq = s.seq % 65535 + 1
if W.tx(STATE, seq, k, present, mm(x), mm(y), mm(z), mm(yaw()), W.tick32(now)) then s.seq = seq end
end
function B.guest(now)
local s, a, r = B.s, W.api, B.s.rx
if not r or now - r.at > 1000 or now < r.at then
 B.release()
 if r then B.ack(r, 3, 65535, now); B.log("expired") end
 return
end
if r.kind ~= 1 or not r.present then
 B.release(); B.ack(r, (r.kind == 2 or r.kind == 3) and 2 or 4, 65535, now)
 B.log((r.kind == 2 or r.kind == 3) and "second_ride_unavailable" or "no_host_dog")
 return
end
local localType = a.buddyType()
if s.rideYield then B.ack(r,0,s.errorCm or 65535,now);B.log("selector_yielded");return end
if localType ~= a.buddyNone and localType ~= a.buddyDog then
 B.clearOwnership(s); B.ack(r, 2, 65535, now); B.log("guest_buddy_busy"); return
end
if localType == a.buddyNone and s.pending and now - s.pending >= 5000 then
 B.ack(r, 2, 65535, now); B.log("dog_load_timeout"); return
end
if localType == a.buddyNone then
 if not s.pending and (not s.refusedAt or now - s.refusedAt >= 5000) then
  s.original, s.owned, s.pending, s.ownedMission, s.ownedEpoch = localType, true, now, W.mission, W.epoch
  if a.buddyCall(a.buddyDog, a.buddyVector(r.x, r.y, r.z), r.yaw) == false then
   s.owned, s.pending, s.refusedAt, s.ownedMission, s.ownedEpoch = nil, nil, now, nil, nil; B.ack(r,2,65535,now); B.log("dog_load_refused"); return
  end
 end
 B.ack(r,1,65535,now); B.log("loading"); return
end
local id, x, y, z = B.dog()
if not id then B.ack(r,1,65535,now); B.log("dog_unreadable"); return end
if not s.rideYield and not s.owned then s.original, s.owned, s.ownedMission, s.ownedEpoch = localType, true, W.mission, W.epoch end
s.id, s.pending = id, nil
local dx, dy, dz = r.x - x, r.y - y, r.z - z
local distance = sqrt(dx * dx + dy * dy + dz * dz)
local errorCm = min(floor(distance * 100 + 0.5), 65535)
s.errorCm = errorCm
if now >= s.nextApply then
 s.nextApply = now + 250
 if distance > 15 then
  a.send(id, {id = "SetWarpPosition", pos = a.buddyVector(r.x, r.y, r.z)})
 end
 s.aiHeld = true
 if a.send(id, {id="LuaAiStayAndSnarl", position=a.buddyVector(r.x,r.y,r.z),
  look=a.buddyVector(r.x+math.sin(r.yaw)*2,r.y,r.z+math.cos(r.yaw)*2),
  useMarker=false, dash=distance>2 and 1 or 0, warp=0, ignoreMarker=true}) == false then
  B.ack(r,2,errorCm,now); B.log("target_refused",errorCm); return
 end
end
B.ack(r,0,errorCm,now); B.log("target_submitted",errorCm)
end
-- M2 host: the dog read every 100 ms (the receipt no longer needs body 1); sent when it moved 5 cm, its kind or
-- presence changed, or 500 ms passed. present=0 only when the dog itself is unreadable.
function B.hostCompanion(now)
local s, a = B.s, W.api
if now < (s.nextRead or 0) then return end
s.nextRead = now + 100
local k, present, x, y, z = kind(a.buddyType()), 0, 0, 0, 0
if k == 1 then
 local id; id, x, y, z = B.dog()
 if id then present = 1; s.hostPos = s.localPos else x, y, z = 0, 0, 0 end
end
local last = s.sent
local moved = last and present == 1 and (x - last.x) ^ 2 + (y - last.y) ^ 2 + (z - last.z) ^ 2 >= 0.0025
if last and last.k == k and last.present == present and not moved and now < (s.nextIdle or 0) then return end
if W.tx(CPOSE, W.tick32(now), k, 0, present, mm(x), mm(y), mm(z), mm(yaw()), 0) then
 s.sent, s.nextIdle, s.poseTx = {k = k, present = present, x = x, y = y, z = z, yaw = yaw()}, now + 500, (s.poseTx or 0) + 1
end
end
W.handlers[CPOSE] = function(v, now)
if W.host or not W.paired or W.eff < 3 or #v ~= 10 then return end
if not integer(v[1], 0, 4294967295) or not integer(v[2], 0, 4) or not integer(v[3], 0, 1) or not integer(v[4], 0, 1)
 or not integer(v[5], -1000000000, 1000000000) or not integer(v[6], -1000000000, 1000000000)
 or not integer(v[7], -1000000000, 1000000000) or not integer(v[8], -3142, 3142) or not integer(v[9], 0, 255)
 or not integer(v[10], 0, 65535) then return end
local s, p = B.s, B.s.cpose
if p then
 local d = (v[1] - p.tick) % 4294967296
 if d == 0 or d >= 2147483648 then return end
end
local r = {tick = v[1], kind = v[2], present = v[4] == 1, x = v[5] / 1000, y = v[6] / 1000, z = v[7] / 1000, yaw = v[8] / 1000,
 at = now - v[10], vx = 0, vz = 0, speed = 0}
if r.present and p and p.present and p.kind == r.kind then
 local dt = ((r.tick - p.tick) % 4294967296) / 1000
 if dt > 0 and dt <= 2 then
  r.vx, r.vz = (r.x - p.x) / dt, (r.z - p.z) / dt
  r.speed = sqrt(r.vx * r.vx + r.vz * r.vz)
 end
end
s.cpose, s.cposeN = r, (s.cposeN or 0) + 1
if r.kind == 1 and r.present then s.hostPos = r end
end
W.handlers[CACK] = function(v, now)
if not W.host or not W.paired or W.eff < 3 or #v ~= 4 then return end
if not (integer(v[1], 1, 65535) and integer(v[2], 0, 4) and integer(v[3], 0, 7) and integer(v[4], 0, 65535)) then return end
local s = B.s
if s.remoteResult ~= v[3] or now == nil or now >= (s.remoteLogAt or 0) then
 s.remoteLogAt = (now or 0) + 10000
 W.log(fmt("event=companion_ack;role=host;result=%d;error_cm=%d", v[3], v[4]))
end
s.remoteResult, s.remoteError = v[3], v[4]
end
-- Guest COMPANION_ACK results: 0 holding at the host's pose, 1 loading or unreadable, 2 refused or another kind,
-- 3 stale (held at the last pose), 5 held where it stands (no host pose yet).
function B.cack(result, errorCm, now)
local s = B.s
if now < (s.nextCack or 0) and s.cackResult == result then return end
s.cackSeq = (s.cackSeq or 0) % 65535 + 1
if W.tx(CACK, s.cackSeq, 1, result, min(errorCm or 65535, 65535)) then s.nextCack, s.cackResult = now + 1000, result end
end
-- The proof's tracking: 2 s windows of the host dog's speed (walk below 3.5 m/s: 3 m; run and dash: 5 m) and the error.
function B.track(now, errorCm, speed)
local s = B.s
local t = s.track or {from = now, n = 0, max = 0, sum = 0, speed = 0, errs = {}}
s.track = t
t.n, t.max, t.sum, t.speed = t.n + 1, math.max(t.max, errorCm), t.sum + errorCm, t.speed + speed
t.errs[#t.errs + 1] = errorCm
if now - t.from < 2000 then return end
s.track = nil
table.sort(t.errs)
local v = t.speed / t.n
local band = v < 0.5 and "idle" or v < 3.5 and "walk" or "run"
if band == "idle" and now < (s.idleTrackAt or 0) then return end
if band == "idle" then s.idleTrackAt = now + 10000 end
W.log(fmt("event=companion_track;band=%s;n=%d;speed_cms=%d;err_max_cm=%d;err_p90_cm=%d;err_mean_cm=%d;within=%d;leg=%s", band, t.n,
 floor(v * 100 + 0.5), t.max, t.errs[math.max(1, math.ceil(#t.errs * 0.9))], floor(t.sum / t.n + 0.5),
 t.max <= (band == "run" and 500 or 300) and 1 or 0, s.leg or "-") .. fmt(";mode=%s;warps=%d", B.holdMode, s.warps or 0))
end
-- Lab lever companion_lab mark <label>: names the following track windows (the proof's legs) and starts a new one.
function B.mark(label)
local s = B.s
s.leg, s.track, s.idleTrackAt = label, nil, nil
W.log("event=companion_mark;role=" .. (W.host and "host" or "guest") .. ";leg=" .. label)
return true
end
-- E3 / E19 watch (guest, lab): the nearest soldier to the held dog (8 of the roster per hold, the ones within
-- 30 m re-read every second), seconds with one within 3 m / 5 m, and the guest's own buddy vars on change.
local function soldier(id)
local ok, p = pcall(W.api.send, id, {id = "GetPosition"})
if ok then return vector(p) end
end
function B.watch(now, x, y, z)
local s = B.s
local w = s.watch or {i = 0, near = {}, s3 = 0, s5 = 0, min = 65535}
s.watch = w
local order, ent = W.entOrder or {}, W.ent or {}
local key = fmt("dead=%s;flag=%s;active=%s", tostring(W.get(vars, "deadBuddyType")), tostring(W.get(vars, "buddyCommonFlag")),
 tostring(W.api.buddyType()))
if key ~= w.vars then w.vars = key; W.log("event=mirror_vars;" .. key) end
for _ = 1, min(8, #order) do
 w.i = w.i % #order + 1
 local h = order[w.i]
 local e = ent[h]
 if e and type(e.id) == "number" then
  local sx, sy, sz = soldier(e.id)
  if sx then w.near[h] = (sx - x) ^ 2 + (sy - y) ^ 2 + (sz - z) ^ 2 <= 900 or nil end
 end
end
if now < (w.nextNear or 0) then return end
local dt = w.lastNear and (now - w.lastNear) / 1000 or 0
w.nextNear, w.lastNear = now + 1000, now
local best, bestH = 65535, nil
for h in pairs(w.near) do
 local e = ent[h]
 local sx, sy, sz
 if e and type(e.id) == "number" then sx, sy, sz = soldier(e.id) end
 if sx then
  local d = floor(sqrt((sx - x) ^ 2 + (sy - y) ^ 2 + (sz - z) ^ 2) * 100 + 0.5)
  if d < best then best, bestH = d, h end
 end
end
if best <= 300 then w.s3 = w.s3 + dt end
if best <= 500 then w.s5 = w.s5 + dt end
w.min = min(w.min, best)
if bestH and best <= 1000 and now >= (w.logAt or 0) then
 w.logAt = now + 5000
 W.log(fmt("event=mirror_near;ent=%.0f;d_cm=%d;s3=%d;s5=%d;engaged=%d;hurt=%d", bestH, best, floor(w.s3), floor(w.s5),
  W.mirrorCount or 0, s.hurt or 0))
end
end
-- E19: a damage message whose victim is this copy's dog (world_sync.lua W.observe, unmapped ids); both roles.
function W.companionHurt(msg, id, cls)
if id ~= 9728 or not companion() then return end
local s = B.s
s.hurt = (s.hurt or 0) + 1
if s.hurt <= 60 then W.log(fmt("event=companion_hurt;role=%s;msg=%s;att=%s;n=%d", W.host and "host" or "guest", tostring(msg), tostring(cls), s.hurt)) end
end
-- E19 protection candidate (guest lab lever companion_lab protect): SetDisableDamage on the held dog, once per dog.
function B.protect(on)
local s = B.s
if on ~= nil then s.protectOn, s.protected = on == true, nil end
if not s.protectOn or s.protected == s.id or not s.id then return s.protected ~= nil end
local ok, r = pcall(W.api.send, s.id, {id = "SetDisableDamage", life = true, faint = true, sleep = true})
s.protected = s.id
W.log(fmt("event=companion_protect;role=guest;id=%d;ok=%d;result=%s", s.id, ok and 1 or 0, tostring(r)))
return ok
end
-- Hold modes. The dog command LuaAiStayAndSnarl (dispatcher 0xC7D220 -> vt+0x98 -> 0xC7DA10) sets the persistent
-- mode: 1/2 (look given, useMarker false: the retail s10040 "stop AI"; the dog never moved toward the target in run
-- 8c0b425f, the Dog AI 0xC7E650 skips its update for them), 3/4 (useMarker true or no look), +1 with dash; warp=1
-- places the dog at the position facing the look point. marker (default): modes 3/4, re-sent when the target moves
-- 0.75 m or every 1 s; stop: modes 1/2; warp: modes 1/2 placed by warp=1 every 100 ms (lab lever companion_lab hold).
B.HOLD, B.holdMode = {marker = true, stop = true, warp = true}, "marker"
function B.setHold(mode)
if not B.HOLD[mode] then return false end
B.holdMode, B.s.sentHold, B.s.farSince = mode, nil, nil
W.log("event=companion_hold;role=" .. (W.host and "host" or "guest") .. ";mode=" .. mode)
return true
end
-- M2 guest: the hold. Target: the host dog's latest pose led by its age (at most 300 ms) while fresh (1 s);
-- else the last target (hold_last); with no host pose yet, where the dog stands (hold_here). Never its own AI.
function B.guestCompanion(now)
local s, a, r = B.s, W.api, B.s.cpose
if a.buddyType() ~= a.buddyDog then return end
if now < s.nextApply then return end
s.nextApply = now + 100
local id, x, y, z = B.dog()
if not id then B.cack(1, 65535, now); B.log("dog_unreadable"); return end
s.owned, s.id, s.ownedMission, s.ownedEpoch = true, id, W.mission, W.epoch
local fresh = r and r.kind == 1 and r.present and now >= r.at and now - r.at <= 1000
local why, result
if fresh then
 local age = min((now - r.at) / 1000, 0.3)
 s.hold = {x = r.x + r.vx * age, y = r.y, z = r.z + r.vz * age, yaw = r.yaw}
 why, result = "target_submitted", 0
elseif s.hold then why, result = "hold_last", 3
else s.hold, why, result = {x = x, y = y, z = z, yaw = yaw()}, "hold_here", 5 end
local t = s.hold
local dx, dy, dz = t.x - x, t.y - y, t.z - z
local distance = sqrt(dx * dx + dy * dy + dz * dz)
local errorCm = min(floor(distance * 100 + 0.5), 65535)
local mode = B.holdMode
-- SetWarpPosition beyond 15 m, or when the dog has not closed a 6 m gap for 2 s (not in warp mode: it places itself).
local stuck = mode ~= "warp" and fresh and distance > 6
if stuck then s.farSince = s.farSince or now else s.farSince = nil end
if fresh and (distance > 15 or (stuck and now - s.farSince >= 2000)) then
 a.send(id, {id = "SetWarpPosition", pos = a.buddyVector(t.x, t.y, t.z)})
 s.warps, s.farSince = (s.warps or 0) + 1, nil
end
s.aiHeld = true
local dash = (distance > 2 or (fresh and r.speed > 3)) and 1 or 0
local last = s.sentHold
if mode == "warp" or not last or now >= last.at + 1000 or last.dash ~= dash or
 (t.x - last.x) ^ 2 + (t.y - last.y) ^ 2 + (t.z - last.z) ^ 2 >= 0.5625 then
 s.sentHold = {x = t.x, y = t.y, z = t.z, at = now, dash = dash}
 if a.send(id, {id = "LuaAiStayAndSnarl", position = a.buddyVector(t.x, t.y, t.z),
  look = a.buddyVector(t.x + math.sin(t.yaw) * 2, t.y, t.z + math.cos(t.yaw) * 2), useMarker = mode == "marker",
  dash = dash, warp = mode == "warp" and 1 or 0, ignoreMarker = true}) == false then
  B.cack(2, errorCm, now); B.log("target_refused", errorCm); return
 end
end
if fresh then B.track(now, errorCm, r.speed) end
B.protect()
if W.mode == 1 then pcall(B.watch, now, x, y, z) end
B.cack(result, errorCm, now); B.log(why, errorCm)
end
function B.status()
local s, now, nativeKind = B.s, W.now(), "unknown"
local ok, live, contextReason = pcall(B.context)
live = ok and live == true
if live then
 local good, t = pcall(W.api.buddyType)
 if good then
  nativeKind = ({[0]="none", "dog", "horse", "walker", "quiet"})[kind(t)]
  if kind(t) == 1 then pcall(B.dog) end
 end
end
local function pose(p)
 if not p or not finite(p.x, 1000000) or not finite(p.y, 1000000) or not finite(p.z, 1000000) then return "?", "-1" end
 local age = math.max(0, floor(now - p.at))
 return string.format("%.2f,%.2f,%.2f", p.x, p.y, p.z), age > 99999 and "99999+" or tostring(age)
end
local hp, ha = pose(s.hostPos)
local lp, la = pose(s.localPos)
local errorCm = s.remoteError or 65535
if not W.host and s.hostPos and s.localPos then
 local dx, dy, dz = s.hostPos.x - s.localPos.x, s.hostPos.y - s.localPos.y, s.hostPos.z - s.localPos.z
 errorCm = min(floor(sqrt(dx*dx + dy*dy + dz*dz)*100 + 0.5), 65535)
end
return string.format("buddy:%s a%d p%d k%s v%d r%s e%d H=%s@%s L=%s@%s d%d",
 W.host and "H" or "G", live and 1 or 0, W.paired and 1 or 0, nativeKind,
 W.host and s.seq or s.rxSeq or 0, string.sub((not live and (ok and contextReason or "context_error"))
  or (s.probe and s.probe.reason ~= "ok" and s.probe.code) or s.reason or "idle", 1, 22), errorCm,
 hp, ha, lp, la, B.disabled and 1 or 0) .. (W.companion and W.companion.tail() or "") .. (companion() and
 fmt(" m2 %s w%d tx%d rx%d ack%s/%s near%s s3=%d hurt%d eng%d", B.holdMode, s.warps or 0, s.poseTx or 0, s.cposeN or 0, tostring(s.cackResult or s.remoteResult),
  tostring(s.remoteError or "-"), tostring(s.watch and s.watch.min or "-"), s.watch and floor(s.watch.s3) or 0, s.hurt or 0,
  W.mirrorCount or 0) or "")
end
function B.tick(now)
if B.disabled then pcall(B.cleanupPending); return end
local ok = pcall(B.update, now)
if not ok then
 local clean, released = pcall(B.release)
 B.s.rx, B.s.rxSeq, B.s.releaseOnly = nil, nil, not (clean and released)
 B.disabled = true
 pcall(W.log, "event=buddy;result=error;scope=buddy_only")
end
end
function B.update(now)
if not B.context() then return end
if companion() then
 if W.paired and W.eff >= 3 then if W.host then B.hostCompanion(now) else B.guestCompanion(now) end end
 return
end
if B.s.releaseOnly then
 if not B.release() then return end
 B.s.releaseOnly = nil
end
if not W.paired or W.eff < 3 then B.release(); return end
if W.host then B.host(now) else B.guest(now) end
end
end
end
