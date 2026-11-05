-- Horse plan M2, a downed partner on the shared D-Horse (lua/coop/world_horse_cargo.lua). No game process.
-- Usage: lua51 world_horse_cargo_lua_check.lua <world_horse_cargo.lua>
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ': ' .. why, 2) end end
local HORSE, OP = 6656, 80
local function fixture(o)
 o = o or {}
 local f = {now = 1000, tx = {}, logs = {}, cargo = {}, warps = {}, places = {}, hostage = false, mounted = false, role = 0,
  physical = {ready = true, carrying = false, carried = false, localCarried = false, peerCarrying = false},
  horse = {ready = true, id = HORSE, realized = true, occupied = false, pending = 0, x = 101000, y = 3000, z = 300000, yaw = 0, gen = 5},
  share = o.share ~= false, key = o.key ~= false}
 local host = o.host ~= false
 local D = {records = {}, target = 0, progress = 0}
 local W = {OP = {}, handlers = {}, startHooks = {}, unpairedHooks = {}, paired = true, eff = 3, mission = 30010, epoch = 7,
  role = host and 1 or 2, host = host, downed = D, active = true}
 W.get = function(t, k) if t == nil then return nil end return t[k] end
 W.now = function() return f.now end
 W.tick = function() end
 W.log = function(s) f.logs[#f.logs + 1] = s end
 W.tx = function(...) f.tx[#f.tx + 1] = {...}; return true end
 W.receipt = function(text)
  local verb = text:match("MGS_COOP_HORSE_DRIVE_V1' %-%- (.*)$")
  if verb == "cfg" then return {on = f.share and 1 or 0, cargo = f.key and 1 or 0} end
  if verb and verb:find("^read ") then
   if not f.horse then return {ready = false, reason = "unrealized"} end
   local r = {}; for k, v in pairs(f.horse) do r[k] = v end; return r
  end
  if verb then
   local x, y, z, yaw = verb:match("^place %d+ %d+ (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+) 0$")
   if x then
    f.places[#f.places + 1] = {x = tonumber(x), y = tonumber(y), z = tonumber(z), yaw = tonumber(yaw)}
    f.horse.x, f.horse.y, f.horse.z, f.horse.yaw = tonumber(x), tonumber(y), tonumber(z), tonumber(yaw)
    return {ready = true, placed = true, gen = f.horse.gen}
   end
  end
  local h, p, l = text:match("MGS_COOP_CARGO_V2' %-%- ([01]) ([01]) ([01])$")
  if h then
   f.cargo[#f.cargo + 1] = h .. p .. l
   local r = {}; for k, v in pairs(f.physical) do r[k] = v end; r.role = f.role; return r
  end
 end
 local env = setmetatable({e = {MgsCoopWorld = W}, vars = {playerPosX = 100, playerPosY = 2, playerPosZ = 300, playerRotY = 90},
  PlayerInfo = {OrCheckStatus = function(s) return s[1] == 25 and f.mounted end}, PlayerStatus = {ON_HORSE = 25},
  GameObject = {SendCommand = function(a, b)
   if b.id == "Warp" then f.warps[#f.warps + 1] = {index = a.index, x = b.pos[1], y = b.pos[2], z = b.pos[3], rotY = b.rotY}; return true end
   if b.id == "IsCarriedHostage" then return f.hostage end
  end}}, {__index = _G})
 local chunk = assert(loadfile(assert(arg[1])))
 setfenv(chunk, env)
 chunk()
 f.W, f.D, f.env, f.C = W, D, env, W.horseCargo
 local me, other = W.role, 3 - W.role
 D.records[me] = {generation = 11, down = false, at = f.now, pos = {100000, 2000, 300000}}
 D.records[other] = {generation = 22, down = true, cause = 1, at = f.now, pos = {100500, 2000, 300000}}
 function f.tick(dt, keep)
  f.now = f.now + (dt or 50)
  if keep ~= false then for _, r in pairs(D.records) do r.at = f.now end end
  W.tick()
 end
 function f.holdE(on) D.rawHeld, D.rawHeldAt = on, on and f.now or nil end
 function f.holdR(on) D.rawCarryHeld, D.rawCarryHeldAt = on, on and f.now or nil end
 -- Hold a key for ms, refreshing the raw sample each tick as the revive poll does.
 function f.hold(key, ms)
  local t = 0
  while t < ms do
   if key == "E" then f.holdE(true) else f.holdR(true) end
   f.tick(50); t = t + 50
   if key == "E" then D.rawHeldAt = f.now else D.rawCarryHeldAt = f.now end
  end
  if key == "E" then f.holdE(false) else f.holdR(false) end
 end
 function f.rx(action, cause, gen, x, y, z, yaw, seat, role) W.handlers[OP]({9, seat or 2, role or other, action, cause or 0, gen, x or 0, y or 0, z or 0, yaw or 0}) end
 function f.logged(s) for _, l in ipairs(f.logs) do if l:find(s, 1, true) then return true end end return false end
 function f.sent(action) for i = #f.tx, 1, -1 do local t = f.tx[i]; if t[1] == OP and t[5] == action then return t end end end
 function f.count(action) local n = 0; for _, t in ipairs(f.tx) do if t[1] == OP and t[5] == action then n = n + 1 end end return n end
 function f.lastWarp(index) for i = #f.warps, 1, -1 do if f.warps[i].index == index then return f.warps[i] end end end
 function f.called(s) for _, c in ipairs(f.cargo) do if c == s then return true end end return false end
 return f
end
-- Carry the downed partner to the horse and load them (host carrier unless o.host == false: then the host grants).
local function load(f)
 f.role = 1
 f.hold("E", 500)
 if not f.W.host then
  local c = f.sent(1)
  check(c and c[3] == 2 and c[4] == 2 and c[7] == 22, "the guest carrier claims seat 2 against the downed host's generation")
  f.rx(2, 0, 22, 0, 0, 0, 0)
 end
 check(f.C.state == "loading" and f.called("010"), "loading: the stock put-down is requested (CARGO_V2 0 1 0)")
 f.role, f.physical.carrying = 0, false
 f.tick(50)
 check(f.C.state == "carrier" and f.called("100"), "the attachment clear: SetCargoHold(1) (CARGO_V2 1 0 0)")
end

-- Binding, keys.
local f = fixture()
check(type(f.C) == "table" and f.W.OP.HORSE_SEAT == OP and type(f.W.handlers[OP]) == "function", "HORSE_SEAT 0x50 bound")
check(f.W.OP.CARGO == nil and f.W.handlers[43] == nil, "the offset prototype's op 43 is retired")
check(f.C.on() and f.C.keyOn(), "both keys on, paired, free roam: on")
local off = fixture({key = false})
check(not off.C.on(), "the cargo key off: off")
off.role = 1; off.hold("E", 600)
check(#off.tx == 0 and #off.cargo == 0 and off.C.state == "off", "key off: an E hold while carrying does nothing")
check(off.C.setTesting(true) and off.C.on(), "cargo_test 1 runs it in the lab without the cargo key")
local noShare = fixture({share = false})
check(not noShare.C.on(), "world_horse_share off: off")
f.W.mission = 30050
check(not f.C.on(), "Mother Base has no horse: off")

-- Trigger: CarryRole 1, the downed partner, E 450 ms, the horse within 2.5 m in front, no guard on it.
f = fixture()
f.role = 1
f.hold("E", 300)
check(f.C.state == "off" and not f.called("010"), "E under 450 ms: nothing")
f.hold("E", 500)
check(f.C.state == "loading" and f.logged("event=horse_cargo;step=loading;seat=2;gen=22"), "host carrier: 450 ms E hold loads at once")
f = fixture(); f.role = 0; f.hold("E", 600)
check(f.C.state == "off" and not f.called("010"), "not carrying: another E use, nothing")
f = fixture(); f.role = 1; f.D.records[2].down = false; f.hold("E", 600)
check(f.C.state == "off" and f.logged("step=refuse;why=awake"), "a partner who is not down is never loaded")
f = fixture(); f.role = 1; f.horse.x = 103000; f.hold("E", 600)
check(f.C.state == "off" and f.logged("step=refuse;why=far;dist_cm=300"), "the horse 3 m away: far")
f = fixture(); f.role = 1; f.env.vars.playerRotY = -90; f.hold("E", 600)
check(f.C.state == "off" and f.logged("step=refuse;why=facing"), "the horse behind the carrier: facing")
f = fixture(); f.role = 1; f.hostage = true; f.hold("E", 600)
check(f.C.state == "off" and f.logged("step=refuse;why=npc_cargo"), "a guard already on the horse: npc_cargo")
f = fixture(); f.role = 1; f.horse.occupied = true; f.hold("E", 600)
check(f.C.state == "off" and f.logged("step=refuse;why=busy"), "a ridden horse is never loaded")
f = fixture(); f.role = 1; f.mounted = true; f.hold("E", 600)
check(f.C.state == "off" and not f.called("010"), "mounted: nothing")

-- Host carrier: put-down, hold, LOADED with the horse pose, body 1 on the cargo point every tick, KEEP at 1 Hz.
f = fixture()
load(f)
local l = f.sent(4)
check(l and l[3] == 2 and l[4] == 1 and l[6] == 0 and l[7] == 22 and l[8] == 101000 and l[9] == 3000 and l[10] == 300000 and l[11] == 0,
 "LOADED: seat 2, the host, the victim's generation, this copy's horse pose")
check(f.logged("event=horse_cargo;step=loaded;seat=2;gen=22;role=carrier"), "step=loaded seat=2")
local w = f.lastWarp(1)
check(w and math.abs(w.x - 101.0) < 1e-6 and math.abs(w.y - (3.0 + f.C.RISE_MM / 1000)) < 1e-6 and
 math.abs(w.z - (300.0 - f.C.BACK_MM / 1000)) < 1e-6 and w.rotY == 0, "body 1 on the cargo point (behind the root, up), the horse's yaw")
f.horse.x, f.horse.yaw = 102000, 1571
f.mounted = true
f.env.vars.playerPosX, f.env.vars.playerPosY = 102.1, 2.5
f.tick(50)
w = f.lastWarp(1)
check(math.abs(w.x - (102.1 - f.C.RIDE_BACK_MM / 1000)) < 1e-3 and math.abs(w.z - 300.0) < 1e-3 and
 math.abs(w.y - (2.5 + f.C.RIDE_RISE_MM / 1000)) < 1e-3, "riding: body 1 behind and over the rider's own position (no census lag)")
check(f.logged("step=pin;how=rider;mounted=1"), "the pin stats line names the rider anchor")
f.env.vars.playerPosX, f.env.vars.playerPosY = 100, 2
local keeps = f.count(6)
for i = 1, 22 do f.tick(50) end
check(f.count(6) - keeps >= 1 and f.count(6) - keeps <= 2, "KEEP at 1 Hz")
check(f.C.busy(), "busy while loaded (Travel, Bring partner here, the guest's checkpoint)")
-- Riding: the R hold does nothing while mounted; the body stays on.
f.hold("R", 1200)
check(f.C.state == "carrier", "mounted: the take-down hold does nothing")
-- Dismounted beside the horse: R held 1 s takes them down at the carrier's side, on both copies.
f.mounted = false
f.tick(50)
f.env.vars.playerPosX, f.env.vars.playerPosZ, f.env.vars.playerPosY = 101.6, 301.2, 1.9
f.hold("R", 300)
check(f.C.state == "carrier", "R under 0.5 s: still on")
f.tick(50)
f.hold("R", 550)
check(f.C.state == "off" and f.cargo[#f.cargo] == "000" and f.logged("step=take_down;key=r"),
 "R held 0.5 s: laid down at once (an R held on picks them up from the ground), the hold cleared (CARGO_V2 0 0 0)")
local u = f.sent(5)
check(u and u[6] == 4 and u[7] == 22, "UNLOADED cause taken_down (4) with the victim's generation")
w = f.lastWarp(1)
check(w and math.abs(w.x * 1000 - u[8]) < 1 and math.abs(w.z * 1000 - u[10]) < 1 and math.abs(w.y - 1.9) < 1e-6,
 "body 1 laid at the sent point, on the carrier's ground height")
check(math.abs(u[10] - (300000 + f.C.SIDE_MM)) < 2 and math.abs(u[8] - (102000 - f.C.BACK_MM)) < 2,
 "on the carrier's side of the horse (+z, the carrier stands there), beside the body")
check(f.logged("step=unloaded;seat=2;why=taken_down;side=carrier"), "step=unloaded why=taken_down")
check(not f.C.busy(), "not busy after the take-down")
-- An E hold also takes them down (where E does not mount).
f = fixture(); load(f); f.hold("E", 1100); f.tick(50)
check(f.C.state == "off" and f.logged("step=take_down;key=e"), "an E hold 1 s takes them down too")
f = fixture(); load(f); f.hold("R", 2000)
check(f.C.state == "off" and f.count(5) == 1 and f.cargo[#f.cargo] == "000", "R held on after the lay-down: one UNLOADED, nothing more here")
-- On foot by the horse: an unridden census far over the carrier's ground is kept 1.45 m over that ground.
f = fixture(); load(f); f.horse.y = 4000; f.tick(50)
w = f.lastWarp(1)
check(math.abs(w.y - (2.0 + f.C.GROUND_MM / 1000)) < 1e-6 and f.logged("how=ground"), "a census 2 m high: the body kept 1.45 m over the ground")
f.horse.y = 3000; f.tick(50)
check(math.abs(f.lastWarp(1).y - (2.0 + f.C.RISE_MM / 1000 + 1.0)) < 1e-6, "a census in the window: the census cargo point")
-- Out of reach: nothing.
f = fixture(); load(f); f.env.vars.playerPosX = 110; f.hold("R", 1200)
check(f.C.state == "carrier", "the R hold out of reach: still on")

-- Guest carrier: the host grants against its own downed generation; a deny or a timeout ends the claim.
f = fixture({host = false})
load(f)
check(f.C.state == "carrier" and f.sent(4)[4] == 2 and f.sent(4)[7] == 22, "guest carrier: loaded after the grant (LOADED role 2, gen 22)")
f = fixture({host = false}); f.role = 1; f.hold("E", 500)
check(f.C.state == "claim", "the guest waits for the host")
f.rx(3, 1, 22)
check(f.C.state == "off" and f.logged("step=refuse;why=awake;state=claim"), "the host's deny ends the claim")
f = fixture({host = false}); f.role = 1; f.hold("E", 500)
for i = 1, 35 do f.tick(50) end
check(f.C.state == "off" and f.logged("why=claim_timeout"), "no answer in 1.5 s: the claim ends")

-- The host arbitrates a guest's claim against its own downed generation.
f = fixture()
f.D.records[1].down = true
f.rx(1, 0, 11, 101000, 2000, 300000, 0)
local g = f.sent(2)
check(g and g[3] == 2 and g[7] == 11 and f.logged("step=grant;seat=2;gen=11"), "the host, down under gen 11: GRANT")
f.rx(1, 0, 10)
check(f.sent(3) and f.sent(3)[6] == 2, "another generation: DENY generation")
f.D.records[1].down = false
f.rx(1, 0, 11)
check(f.sent(3)[6] == 1, "the host is not down: DENY awake")
f.D.records[1].down, f.D.target, f.D.progress = true, 1, 1200
f.rx(1, 0, 11)
check(f.sent(3)[6] == 12, "the host is being revived: DENY mid_revive")
local gf = fixture({host = false}); gf.D.records[2].down = true; gf.rx(1, 0, 11)
check(gf.count(2) == 0 and gf.count(3) == 0, "a guest never grants")

-- Victim's copy (here the guest, carried by the host): LOADED -> cargo, its horse put on the carrier's pose, body 0 on it.
f = fixture({host = false})
f.D.records[2].down, f.D.records[2].generation = true, 33
f.D.records[1].down = false
f.rx(4, 0, 32, 150000, 2500, 310000, 0)
check(f.C.state == "off" and f.sent(5) and f.sent(5)[6] == 2, "LOADED for another generation: UNLOADED generation, never cargo")
f.rx(4, 0, 33, 150000, 2500, 310000, 0)
check(f.C.state == "cargo" and f.logged("step=cargo;seat=2;gen=33"), "LOADED for its own generation: cargo")
f.physical.localCarried = true
f.tick(50)
check(f.called("001") and not f.lastWarp(0), "SetCargoLocal(1); no warp while the replica put-down still plays")
f.physical.localCarried = false
f.tick(50)
check(#f.places == 1 and f.places[1].x == 150000 and f.places[1].z == 310000, "its own horse put on the carrier's pose (unridden)")
w = f.lastWarp(0)
check(w and math.abs(w.x - 150.0) < 1e-6 and math.abs(w.y - (2.5 + f.C.VICTIM_RISE_MM / 1000)) < 1e-6 and
 math.abs(w.z - (310.0 - f.C.BACK_MM / 1000)) < 1e-6 and math.abs(w.rotY - math.pi / 2) < 1e-6,
 "body 0 warped to its own horse's cargo point, a quarter turn (across the back)")
check(f.C.busy(), "the victim's copy is busy too")
check(f.logged("step=victim_pin;want_cm="), "the victim's copy logs the wanted height against its body's")
-- M1 moves the horse while the carrier rides (a live track): no re-place, body 0 follows the placed horse.
f.W.horseShare = {track = {}}
f.horse.x = 160000
f.rx(6, 0, 33, 150000, 2500, 310000, 0)
f.tick(50)
check(#f.places == 1 and math.abs(f.lastWarp(0).x - 160.0) < 1e-6, "while tracked: body 0 on the horse M1 placed, no re-place")
-- Right after M1 places the horse (the same frame), body 0 goes on the pose just written; the next tick does not move it.
local nw = #f.warps
f.C.placed(170000, 2400, 310000, 0)
w = f.lastWarp(0)
check(#f.warps == nw + 1 and math.abs(w.x - 170.0) < 1e-6 and math.abs(w.y - (2.4 + f.C.VICTIM_PLACED_MM / 1000)) < 1e-6 and
 math.abs(w.z - (310.0 - f.C.BACK_MM / 1000)) < 1e-6, "C.placed: body 0 over the pose M1 just wrote")
f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
check(#f.warps == nw + 1, "the tick after a post-placement pin leaves body 0 there")
f.W.horseShare = {}
for i = 1, 25 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(#f.places == 2, "unridden and drifted over 0.5 m from the KEEP pose: re-placed (at most 1 Hz)")
-- The take-down from the carrier: body 0 at the exact point.
f.rx(5, 4, 33, 151000, 2400, 309000, 1571)
check(f.C.state == "off" and f.called("000") and f.logged("step=unloaded;seat=2;why=taken_down;side=cargo;placed=1") and
 f.logged("from=partner"), "UNLOADED taken_down: off, the local hold cleared")
w = f.lastWarp(0)
check(math.abs(w.x - 151.0) < 1e-6 and math.abs(w.y - 2.4) < 1e-6 and math.abs(w.z - 309.0) < 1e-6 and
 math.abs(w.rotY - (1.571 + math.pi / 2)) < 1e-6, "body 0 at the carrier's point")
-- A zero point (the carrier picked them up off the horse): nothing placed, the carry replica takes over.
f.rx(4, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
nw = #f.warps
f.rx(5, 4, 33, 0, 0, 0, 0)
check(f.C.state == "off" and #f.warps == nw and f.logged("why=taken_down;side=cargo;placed=0"), "UNLOADED without a point: body 0 left to the carry")
check(f.count(5) == 1, "a remote end sends nothing back")

-- Automatic lay-downs.
local function loaded(o) local x = fixture(o); load(x); return x end
f = loaded(); f.horse.gen = 6; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 5 and f.lastWarp(1), "a new horse generation (dead / despawned): horse_lost, laid down")
f = loaded(); f.horse = nil; for i = 1, 40 do f.tick(50) end
check(f.C.state == "off" and f.sent(5)[6] == 5, "the horse unreadable for 1.5 s: horse_lost")
f = loaded(); f.horse.pending = 1; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 11, "a call pending while on foot: call_away")
f = loaded(); f.horse.x = 105000; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 11, "the unridden horse 4 m from where it stood: call_away")
f = loaded(); f.mounted = true; f.tick(50); f.horse.x = 140000; f.tick(50)
check(f.C.state == "carrier", "ridden away: still on")
f = loaded(); f.D.records[2].down = false; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 1, "the victim up: awake")
f = loaded(); f.D.records[2].generation = 23; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 8, "the victim's generation changed (dead, respawned): victim_gone")
f = loaded(); for i = 1, 80 do f.tick(50, false) end
check(f.C.state == "off" and f.sent(5)[6] == 6, "the victim's status stale 3.5 s: stream_lost")
f = loaded(); f.W.mission = 30020; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 7, "a context change (mission end): context, laid down")
f = loaded(); f.W.paired = false; f.tick(50)
check(f.C.state == "off" and f.lastWarp(1) and f.logged("why=stream_lost"), "unpaired (link drop): laid down locally")
f = loaded(); f.rx(5, 6, 22, 0, 0, 0, 0)
check(f.C.state == "off" and f.logged("why=stream_lost;side=carrier;placed=0") and f.count(5) == 0,
 "the victim's own end without a point: body 1 back to the partner driver, nothing sent back")
f = loaded(); f.rx(5, 6, 22, 101500, 2000, 299000, 0)
check(f.C.state == "off" and f.logged("why=stream_lost;side=carrier;placed=1") and math.abs(f.lastWarp(1).x - 101.5) < 1e-6,
 "the victim's own end with its point: body 1 laid there")
-- The carry key beside the loaded horse picks them up (the coop pickup): off at once, nothing placed, UNLOADED without a point.
f = loaded(); local nw2 = #f.warps; f.role = 1; f.tick(50)
local up = f.sent(5)
check(f.C.state == "off" and up and up[6] == 4 and (up[8] ~= 0 or up[10] ~= 0) and #f.warps == nw2 and
 f.logged("step=take_down;key=r;how=pickup") and f.logged("why=taken_down;side=carrier;placed=0"),
 "picked up off the horse: taken_down, body 1 left to the carry, the ground point sent for body 0")
-- The victim's copy without KEEP for 3.5 s lays itself down beside its horse.
f = fixture({host = false}); f.D.records[2].down = true; f.D.records[2].generation = 33
f.rx(4, 0, 33, 101000, 2000, 300000, 0)
for i = 1, 80 do f.tick(50) end
check(f.C.state == "off" and f.sent(5) and f.sent(5)[6] == 6 and f.lastWarp(0), "no KEEP 3.5 s on the victim's copy: stream_lost")
f = fixture({host = false}); f.D.records[2].down = true; f.D.records[2].generation = 33
f.rx(4, 0, 33, 101000, 2000, 300000, 0); f.D.records[2].generation = 34; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 8, "the victim respawned: victim_gone")

-- Wire hygiene.
f = fixture({host = false}); f.D.records[2].down = true; f.D.records[2].generation = 33
f.rx(4, 0, 33, 101000, 2000, 300000, 0, 1)
check(f.C.state == "off", "another seat is not cargo")
f.rx(4, 0, 33, 101000, 2000, 300000, 0, 2, 2)
check(f.C.state == "off", "a message claiming this copy's own role is ignored")
f.rx(4, 0, 33, 101000.5, 2000, 300000, 0)
check(f.C.state == "off", "a fractional position is ignored")
f.rx(4, 0, 33, 101000, 2000, 300000, 40000)
check(f.C.state == "off", "a yaw out of range is ignored")
f = fixture(); f.W.paired = false; f.D.records[1].down = true; f.rx(1, 0, 11)
check(#f.tx == 0, "unpaired: nothing answered")
check(type(f.C.status()) == "string" and #f.C.status() <= 160 and f.C.status():find("^cargo;state=off"), "cargo_status fits the debug reply")

print('world_horse_cargo_lua_check: ' .. checks .. ' checks passed; no game process')
