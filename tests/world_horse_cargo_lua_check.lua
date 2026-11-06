-- Horse plan M2, a downed partner on the shared D-Horse (lua/coop/world_horse_cargo.lua). No game process.
-- Usage: lua51 world_horse_cargo_lua_check.lua <world_horse_cargo.lua>
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ': ' .. why, 2) end end
local HORSE, OP = 6656, 80
local function fixture(o)
 o = o or {}
 local f = {now = 1000, tx = {}, logs = {}, cargo = {}, warps = {}, places = {}, holds = {}, hostage = false, mounted = false, role = 0,
  physical = {ready = true, carrying = false, carried = false, localCarried = false, peerCarrying = false},
  horse = {ready = true, id = HORSE, realized = true, occupied = false, pending = 0, x = 101000, y = 3000, z = 300000, yaw = 0, gen = 5},
  share = o.share ~= false, key = o.key ~= false, stanceReq = {}}
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
  -- hold0 (round 2): this copy's body 0 moved natively; recorded as a body-0 warp (via hold0) unless refused.
  if verb and verb:find("^free0 ") then f.frees = (f.frees or 0) + 1; return {ready = true, freed = true} end
  if verb and verb:find("^hold1 ") then
   local x, y, z, yaw = verb:match("^hold1 %d+ %d+ (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%d+)$")
   f.holds1 = (f.holds1 or 0) + 1
   f.lastHold1 = verb
   if f.holdOff or not x then return {ready = false, reason = f.holdOff or "request"} end
   f.warps[#f.warps + 1] = {index = 1, x = tonumber(x) / 1000, y = tonumber(y) / 1000, z = tonumber(z) / 1000, rotY = tonumber(yaw) / 1000,
    via = "hold1"}
   return {ready = true, held = true}
  end
  if verb and verb:find("^hold0 ") then
   local x, y, z, yaw, rs = verb:match("^hold0 %d+ %d+ (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%d+)$")
   f.holds[#f.holds + 1] = verb
   if f.holdOff or not x then return {ready = false, reason = f.holdOff or "request"} end
   f.warps[#f.warps + 1] = {index = 0, x = tonumber(x) / 1000, y = tonumber(y) / 1000, z = tonumber(z) / 1000, rotY = tonumber(yaw) / 1000,
    via = "hold0"}
   return {ready = true, held = true}
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
  PlayerInfo = {OrCheckStatus = function(s)
   if s[1] == 25 then return f.mounted end
   return f.stance ~= nil and s[1] == ({STAND = 31, SQUAT = 32, CRAWL = 33})[f.stance]
  end}, PlayerStatus = {ON_HORSE = 25, STAND = 31, SQUAT = 32, CRAWL = 33}, PlayerStance = {STAND = 1, SQUAT = 2, CRAWL = 3},
  Player = {RequestToSetTargetStance = function(id) f.stanceReq[#f.stanceReq + 1] = id end},
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
f = fixture(); f.role = 1; f.env.vars.playerRotY = -90; f.horse.x = 102000; f.hold("E", 600)
check(f.C.state == "off" and f.logged("step=refuse;why=facing"), "the horse 2 m behind the carrier: facing")
-- Round 2 (run 20aef564: the guest carrier 0.85 m from the horse's root, facing away, refused facing): within 1.2 m
-- the carrier is against the horse and the facing check is skipped.
f = fixture(); f.role = 1; f.env.vars.playerRotY = -90; f.horse.x = 100850; f.hold("E", 600)
check(f.C.state == "loading", "within 1.2 m of the horse: loads whichever way the carrier faces")
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
check(not f.C.downedPosture(), "the carrier's own downed posture is never the cargo's")
local w = f.lastWarp(1)
check(w and math.abs(w.x - 101.0) < 1e-6 and math.abs(w.y - (3.0 + f.C.RISE_MM / 1000)) < 1e-6 and
 math.abs(w.z - (300.0 - f.C.BACK_MM / 1000)) < 1e-6, "body 1 on the cargo point (behind the root, up)")
-- M2g (runs 770368d0 / d4aec0a2 host-h2-loaded, 4585cc5a host-h2-ride-walk: on the carrier's screen body 1 lay ALONG the horse
-- over the saddle, held at the horse's yaw; body 0 at a quarter turn lies across it on the victim's own screen): body 1 at a
-- quarter turn from the horse's yaw too.
check(math.abs(w.rotY - math.pi / 2) < 1e-3, "body 1 a quarter turn from the horse's yaw (it lies across the horse)")
check(w.via == "hold1" and f.lastHold1:find(" 14$"), "the carrier's copy holds body 1 by hold1 (C0 + watch + pin)")
-- M2f (runs 144d99bb / 4585cc5a frames: on the carrier's screen the body sat a little high over the rump at the load, the walk
-- and after the dismount): 0.1 m lower over the census, the rider and the carrier's ground.
check(f.C.RISE_MM == 450 and f.C.GROUND_MM == 1350, "body 1 0.45 m over the census, kept 1.35 m over the ground")
-- M2g (runs 770368d0 host-h2-mounted / -ride-walk, 4585cc5a host-h2-ride-walk: riding, its back at the rider's mid-back, about
-- 0.3 m over the rump): 0.15 m over the rider's own position.
check(f.C.RIDE_RISE_MM == 150, "riding: body 1 0.15 m over the rider")
f.horse.x, f.horse.yaw = 102000, 1571
f.mounted = true
f.env.vars.playerPosX, f.env.vars.playerPosY = 102.1, 2.5
f.tick(50)
w = f.lastWarp(1)
check(math.abs(w.x - (102.1 - f.C.RIDE_BACK_MM / 1000)) < 1e-3 and math.abs(w.z - 300.0) < 1e-3 and
 math.abs(w.y - (2.5 + f.C.RIDE_RISE_MM / 1000)) < 1e-3, "riding: body 1 behind and over the rider's own position (no census lag)")
check(f.logged("step=pin;how=rider;mounted=1"), "the pin stats line names the rider anchor")
check(math.abs(w.rotY - (1.571 + math.pi / 2)) < 2e-3, "riding: body 1 a quarter turn from the horse's yaw")
-- Round 2 (runs 83ac3950 / 547b431e: body 1 trailing low behind the rider at a gallop): while riding, body 1 leads by the
-- rider's last per-tick move (it is drawn after this tick's own move), never by a jump over 0.6 m.
f.env.vars.playerPosX = 102.4
f.tick(50)
w = f.lastWarp(1)
check(math.abs(w.x - (102.4 + 0.3 - f.C.RIDE_BACK_MM / 1000)) < 1e-3 and math.abs(w.z - 300.0) < 1e-3,
 "riding: body 1 leads by the rider's last move (0.3 m a tick)")
check(f.C.LEAD_MAX_MM == 600, "the lead is capped at 0.6 m")
f.env.vars.playerPosX = 104.4
f.tick(50)
w = f.lastWarp(1)
check(math.abs(w.x - (104.4 - f.C.RIDE_BACK_MM / 1000)) < 1e-3, "a 2 m jump between ticks is no motion: no lead")
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
-- Round 2 (run a49c4d76: the coop pickup shouldered them off the horse before a 0.5 s hold): the take-down is 0.3 s.
check(f.C.TAKE_MS == 300, "the take-down hold is 0.3 s")
f.hold("R", 150)
check(f.C.state == "carrier", "R under 0.3 s: still on")
f.tick(50)
f.hold("R", 350)
check(f.C.state == "off" and f.cargo[#f.cargo] == "000" and f.logged("step=take_down;key=r"),
 "R held 0.3 s: laid down at once (an R held on picks them up from the ground), the hold cleared (CARGO_V2 0 0 0)")
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
-- Round 2 (run a49c4d76: the E hold that loaded them, held on, took them down 0.5 s later): after the load the
-- take-down waits until E and R have been up once.
f = fixture(); f.role = 1
for i = 1, 10 do f.holdE(true); f.tick(50) end
check(f.C.state == "loading", "one long E hold: loading")
f.role, f.physical.carrying = 0, false
for i = 1, 16 do f.holdE(true); f.tick(50) end
check(f.C.state == "carrier" and not f.logged("step=take_down"), "the E that loaded them, held on 0.8 s: still on")
f.holdE(false); f.tick(50)
f.hold("E", 400)
check(f.C.state == "off" and f.logged("step=take_down;key=e"), "released once, a new 0.3 s E hold takes them down")
f = fixture(); load(f); f.hold("R", 2000)
check(f.C.state == "off" and f.count(5) == 1 and f.cargo[#f.cargo] == "000", "R held on after the lay-down: one UNLOADED, nothing more here")
-- On foot by the horse: an unridden census far over the carrier's ground is kept 1.45 m over that ground.
f = fixture(); load(f); f.horse.y = 4000; f.tick(50)
w = f.lastWarp(1)
check(math.abs(w.y - (2.0 + f.C.GROUND_MM / 1000)) < 1e-6 and f.logged("how=ground"), "a census 2 m high: the body kept 1.45 m over the ground")
f.horse.y = 3000; f.tick(50)
check(math.abs(f.lastWarp(1).y - (2.0 + f.C.RISE_MM / 1000 + 1.0)) < 1e-6, "a census in the window: the census cargo point")
-- M2f (run 5a92c73f (c), the carrier's copy after the dismount: census root 0.52 m over the carrier's ground, so 0.45 m over it
-- fell under the old 1.0 m edge and the body went 1.35 m over the ground, 0.38 m high over the rump): the window's low
-- edge is 0.8 m.
check(f.C.GROUND_LO_MM == 800, "the ground window's low edge is 0.8 m")
f.horse.y = 2520; f.tick(50)
check(math.abs(f.lastWarp(1).y - (2.52 + f.C.RISE_MM / 1000)) < 1e-6,
 "a census 0.52 m over the carrier's ground: still the census cargo point")
f.horse.y = 2300; f.tick(50)
check(math.abs(f.lastWarp(1).y - (2.0 + f.C.GROUND_MM / 1000)) < 1e-6, "a census 0.3 m over the ground: kept 1.35 m over the ground")
f.horse.y = 3000; f.tick(50)
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
 math.abs(w.z - (310.0 - f.C.BACK_MM / 1000)) < 1e-6 and math.abs(w.rotY - math.pi / 2) < 1e-3,
 "body 0 warped to its own horse's cargo point, a quarter turn (across the back)")
check(f.C.busy(), "the victim's copy is busy too")
-- Round 2: hold0 with no reset (a242d867, c2927350) or the A0 reset (8cfe8e97, 7f3155c8) kept body 0 with the horse but
-- in the airborne pose, also on the carrier's screen; the Lua Warp keeps the lying pose but drops under the horse.
-- BODY0 round: the C0 reset alone (C0.vt+0x90 sets the 0x580 record's +0x2C0 bit 0x20, which C0.vt+0xD0 0x14128E010
-- reads, clears and answers "no fall" for that frame) keeps the lying pose but the body still dropped under the horse
-- (run cfd2b604, victim_pin drop 136-288 cm); with the native write watch (4) and pin (8) on body 0's transform the
-- default is 14; the lab lever picks 0..15 (4 the Lua Warp).
check(w.via == "hold0" and #f.holds == 1 and f.holds[1]:find(" 14$"), "by default body 0 moves by hold0 + C0 + watch + pin")
check(f.C.RESET == 14, "the default is hold0 + C0 + watch + pin (14)")
check(f.C.downedPosture() == true, "loaded: the victim's downed controls take the held posture (world_downed's posture field)")
check(f.C.RESET1 == 14, "the carrier's copy holds body 1 by hold1 with C0 + watch + pin (14) by default")
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
-- Round 2 (body 0 slips off at a gallop on the victim's screen): the next placement within 200 ms leads body 0 by the
-- horse's last per-placement move (0.3 m here), capped at 0.6 m; a slower placement or a jump has no lead.
f.C.placed(170300, 2400, 310000, 0)
w = f.lastWarp(0)
check(math.abs(w.x - (170.3 + 0.3)) < 1e-6, "C.placed: body 0 leads by the horse's last move")
f.now = f.now + 50
f.C.placed(172300, 2400, 310000, 0)
check(math.abs(f.lastWarp(0).x - 172.3) < 1e-6, "a 2 m jump is no motion: no lead")
f.now = f.now + 400
f.C.placed(172600, 2400, 310000, 0)
check(math.abs(f.lastWarp(0).x - 172.6) < 1e-6, "a placement 400 ms after the last: no lead")
-- Round 2 (run a49c4d76: body 0 on the ground under the moving horse for a second or more at a time on the victim's
-- screen, right after the mount and at a gallop): body 0's target never drops faster than 1 m/s (a sudden drop of
-- the placed pose); a rise is immediate.
local y0 = f.lastWarp(0).y
f.now = f.now + 50
f.C.placed(172700, 2000, 310000, 0)
check(math.abs(f.lastWarp(0).y - (y0 - 0.05)) < 1e-6, "a 0.4 m drop of the placed pose: body 0 down 5 cm in 50 ms")
f.now = f.now + 50
f.C.placed(172800, 2600, 310000, 0)
check(math.abs(f.lastWarp(0).y - (2.6 + f.C.VICTIM_PLACED_MM / 1000)) < 1e-6, "a rise: at once")
check(f.C.DOWN_MMPS == 1000, "the drop rate is 1 m/s")
-- The lab lever horse_probe cargo_pose: the placed / census heights, the offset behind the root, the drop rate.
check(f.C.setVictimPose(1700, 1300, 200, 0, 15) and not f.C.setVictimPose(9000, 1300, 200, 0, 1) and
 not f.C.setVictimPose(1700, 1300, 200, 0, 16) and f.C.setVictimPose(1700, 1300, 200, 0, 2),
 "setVictimPose: in range only (the reset 0..15, 4 the Lua Warp)")
f.now = f.now + 50
f.C.placed(172900, 2600, 310000, 0)
w = f.lastWarp(0)
check(math.abs(w.y - 4.3) < 1e-6 and math.abs(w.z - 309.8) < 1e-6, "setVictimPose: body 0 1.7 m over the placed pose, 0.2 m behind")
check(f.holds[#f.holds]:find(" 2$"), "setVictimPose: the reset goes to hold0")
check(f.C.setVictimPose(f.C.VICTIM_PLACED_MM, f.C.VICTIM_RISE_MM, f.C.BACK_MM, f.C.DOWN_MMPS, f.C.RESET), "setVictimPose back to the defaults")
check(f.logged("step=victim_pin;want_cm=") and f.logged(";lag_cm=") and f.logged(";src="),
 "the victim_pin line carries the body's lag behind its last target and the target's source")
-- M2g (runs 5a92c73f (c), 4585cc5a (a): right at M1's final placement body 0 went into fall / land, coop_carry=watch a=19 /
-- 1a every frame, and ended crouched): the cargo tick held it again only 200 ms after the last placement, while hold0's C0
-- reset ("no fall" for that frame) is what keeps it lying. Once M1's track has ended body 0 is held every tick.
f.C.placed(173000, 2600, 310000, 0)
f.W.horseShare = {}
nw = #f.warps
f.tick(50)
check(#f.warps == nw + 1 and f.lastWarp(0).via == "hold0", "the tick right after M1's final placement holds body 0 (no 200 ms gap)")
f.tick(50)
check(#f.warps == nw + 2, "and every tick after it")
-- M2f (run 131f88c6 (c): 1 ms after the final placement the victim's copy re-placed its horse on a KEEP pose sent before
-- it, 11 m back, and that horse fell Unconscious 0.25 s later; three more 1 Hz jumps followed while the carrier's horse
-- slowed): for 2 s after M1's track ended nothing is re-placed, then only on a KEEP pose sent after those 2 s.
f.W.horseShare = {}
f.tick(50)
check(#f.places == 1, "M1's track just ended: the KEEP pose sent before it is not placed")
for i = 1, 38 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(#f.places == 1, "for 2 s after the track ended: nothing re-placed")
for i = 1, 4 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(#f.places == 2, "then a KEEP pose sent after those 2 s, drifted over 0.5 m: re-placed")
for i = 1, 25 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(#f.places == 2, "on the KEEP pose: not re-placed again")
-- A refused hold0 (an old native, the cargo key off while cargo_test runs it): the Lua Warp as before, one line a reason.
f.C.setVictimPose(f.C.VICTIM_PLACED_MM, f.C.VICTIM_RISE_MM, f.C.BACK_MM, f.C.DOWN_MMPS, 1)
f.tick(50)
check(f.lastWarp(0).via == "hold0" and f.holds[#f.holds]:find(" 1$"), "the lever's reset 1: hold0 with the A0 reset")
f.holdOff = "off"
f.tick(50); f.tick(50)
w = f.lastWarp(0)
check(w.via == nil and f.count and f.logged("step=hold0_refused;reason=off"), "hold0 refused: body 0 by Lua Warp")
-- Run 144d99bb (b): one transient refusal (body, at the mount) Lua-Warped body 0 once; Warp's A0 reset turned it prone for
-- the rest of the ride. A transient refusal leaves body 0 to the pin (it holds 300 ms); only off / request / pins Warp.
f.holdOff = "body"
local nwb = #f.warps
f.tick(50)
check(#f.warps == nwb and f.logged("step=hold0_refused;reason=body"), "hold0 refused body (transient): no Lua Warp")
f.holdOff = nil
f.C.setVictimPose(f.C.VICTIM_PLACED_MM, f.C.VICTIM_RISE_MM, f.C.BACK_MM, f.C.DOWN_MMPS, f.C.RESET)
-- The take-down from the carrier: body 0 at the exact point.
f.rx(5, 4, 33, 151000, 2400, 309000, 1571)
check((f.frees or 0) >= 1, "the take-down frees the pin (free0) before body 0's lay-down Warp")
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

-- M2f (BODY0 runs 911ce293 .. 4585cc5a: lying on the horse through the ride, crouched on it on the victim's own screen once
-- the carrier got off, both orders): body 0's stance (PlayerStance STAND 1 / SQUAT 2 / CRAWL 3) is logged on each change
-- with the target's source and the time since M1's track ended; a SQUAT or STAND body held as cargo is asked back to
-- CRAWL by the stock stance request at most every 500 ms; the lever's 0 only logs.
f = fixture({host = false})
f.D.records[2].down, f.D.records[2].generation = true, 33
f.D.records[1].down = false
f.stance = "CRAWL"
f.rx(4, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
check(f.C.state == "cargo" and f.logged("step=stance;s=CRAWL;was=none;") and #f.stanceReq == 0, "cargo: the lying stance logged, no request")
f.W.horseShare = {track = {}}
f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
f.W.horseShare, f.stance = {}, "SQUAT"
f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
check(f.logged("step=stance;s=SQUAT;was=CRAWL;src=census;tracked=0;off_ms=0"), "the crouch logged with the time since M1's track ended")
check(#f.stanceReq == 1 and f.stanceReq[1] == 3 and f.logged("step=relie;from=SQUAT;n=1;ok=1"), "a crouched cargo body: asked back to CRAWL")
f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
check(#f.stanceReq == 1, "at most every 500 ms")
for i = 1, 10 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(#f.stanceReq == 2, "still crouched 500 ms later: asked again")
f.stance = "CRAWL"
f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
check(f.logged("step=stance;s=CRAWL;was=SQUAT;"), "lying again: logged")
for i = 1, 12 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(#f.stanceReq == 2 and f.C.status():find(";stance=CRAWL;relie=1$"), "lying: no request; cargo_status carries the stance and the lever")
check(f.C.setRelie(0) and not f.C.setRelie(2) and not f.C.setRelie(0.5) and not f.C.setRelie(nil), "setRelie: 0 or 1 only")
f.stance = "STAND"
for i = 1, 12 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(#f.stanceReq == 2 and f.logged("step=stance;s=STAND;was=CRAWL;"), "the lever's 0: a standing cargo body logged only")
check(f.C.setRelie(1), "setRelie 1")
f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50)
check(#f.stanceReq == 3 and f.logged("step=relie;from=STAND;n=3;ok=1"), "the lever's 1 again: a standing cargo body asked to CRAWL")
for i = 1, 40 do f.rx(6, 0, 33, 150000, 2500, 310000, 0); f.tick(50) end
check(f.logged(";stance=STAND") and f.logged("step=victim_pin;"), "the victim_pin line carries the stance")
f.rx(5, 4, 33, 151000, 2400, 309000, 1571)
local nr = #f.stanceReq
for i = 1, 12 do f.tick(50) end
check(f.C.state == "off" and #f.stanceReq == nr, "off: never asked")
f = fixture(); load(f); f.stance = "SQUAT"
for i = 1, 12 do f.tick(50) end
check(#f.stanceReq == 0 and not f.logged("step=stance;"), "the carrier's own stance is never touched")

-- Automatic lay-downs.
local function loaded(o) local x = fixture(o); load(x); return x end
f = loaded(); f.horse.gen = 6; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 5 and f.lastWarp(1), "a new horse generation (dead / despawned): horse_lost, laid down")
check((f.frees or 0) >= 1 and f.lastWarp(1).via == nil, "the carrier's lay-down frees the pin, then body 1 by the Lua Warp")
f = loaded(); f.horse = nil; for i = 1, 40 do f.tick(50) end
check(f.C.state == "off" and f.sent(5)[6] == 5, "the horse unreadable for 1.5 s: horse_lost")
f = loaded(); f.horse.pending = 1; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 11, "a call pending while on foot: call_away")
f = loaded(); f.horse.x = 105000; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 11, "the unridden horse 4 m from where it stood: call_away")
f = loaded(); f.mounted = true; f.tick(50); f.horse.x = 140000; f.tick(50)
check(f.C.state == "carrier", "ridden away: still on")
-- Round 2 (run 883dc6ab: the horse still slowing from a gallop right after the dismount moved 3 m, call_away): for 2 s
-- after a dismount the stand point follows the horse; after that a 4 m move is a call away.
f.mounted = false; f.tick(50)
f.horse.x = 143500; f.tick(50)
check(f.C.state == "carrier", "the horse slides 3.5 m right after the dismount: still on")
for i = 1, 45 do f.tick(50) end
check(f.C.state == "carrier", "settled: still on")
f.horse.x = 147500; f.tick(50)
check(f.C.state == "off" and f.sent(5)[6] == 11, "settled, then 4 m away: call_away")
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
-- Round 2 (runs 83ac3950 / 547b431e: victim_gone left body 0 on the horse on the victim's own screen): a new downed
-- generation lays body 0 beside its horse while it is still at the horse (within 5 m), never a body that is elsewhere.
f = fixture({host = false}); f.D.records[2].down = true; f.D.records[2].generation = 33
f.rx(4, 0, 33, 101000, 2000, 300000, 0); f.tick(50); f.tick(50)
local nv = #f.warps
f.env.vars.playerPosX, f.env.vars.playerPosY, f.env.vars.playerPosZ = 101.0, 3.4, 299.6
f.D.records[2].generation = 34; f.tick(50)
w = f.lastWarp(0)
check(f.C.state == "off" and f.sent(5)[6] == 8 and #f.warps == nv + 1 and math.abs(w.z - (300.0 - f.C.BACK_MM / 1000)) < 1e-6 and
 math.abs(w.x - (101.0 - f.C.SIDE_MM / 1000)) < 1e-6 and f.logged("why=victim_gone;side=cargo;placed=1"),
 "victim_gone at the horse: body 0 laid beside it")
f = fixture({host = false}); f.D.records[2].down = true; f.D.records[2].generation = 33
f.rx(4, 0, 33, 101000, 2000, 300000, 0); f.tick(50); f.tick(50)
nv = #f.warps
f.env.vars.playerPosX, f.env.vars.playerPosZ = 160.0, 380.0
f.D.records[2].generation = 34; f.tick(50)
check(f.C.state == "off" and #f.warps == nv and f.logged("why=victim_gone;side=cargo;placed=0"),
 "victim_gone with body 0 elsewhere (a respawn): left where it is")

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
