-- Offset transport test; disabled in normal play.
do
local W = e.MgsCoopWorld
if type(W) == "table" and type(W.handlers) == "table" and type(W.OP) == "table" then
local C = {testing = false, seq = 0, state = "off", txAt = 0, rxAt = 0, maskOwned = false}
W.horseCargo = C
local OP = 43
W.OP.CARGO = OP
local HORSE = 6656
local floor, sin, cos, sqrt, atan2, fmt = math.floor, math.sin, math.cos, math.sqrt, math.atan2, string.format
local HOLD_MS, NEAR_M, LOAD_MS, STALE_MS, RIDE_MS = 450, 3.2, 6000, 1500, 900
local OFFSET = {horse = {back = 0.55, rise = 1.55}, vehicle = {back = 1.25, rise = 1.25}}
local function finite(n) return type(n) == "number" and n == n and n > -1e9 and n < 1e9 end
local function milli(v) return floor(v * 1000 + (v >= 0 and 0.5 or -0.5)) end
local function integer(v,lo,hi) return finite(v) and v==floor(v) and v>=lo and v<=hi end
local function D() return W.downed end
local function context()
  return W.paired and W.eff and W.eff>=3 and integer(W.mission,1,99999) and integer(W.epoch,1,65535)
    and integer(W.role,1,2) and type(D())=="table"
end
local function fresh(r,now)
  return type(r)=="table" and finite(r.at) and now>=r.at and now-r.at<1000 and type(r.pos)=="table"
    and integer(r.pos[1],-1000000000,1000000000) and integer(r.pos[2],-1000000000,1000000000)
    and integer(r.pos[3],-1000000000,1000000000)
end
local function current(now)
  return context() and C.mission==W.mission and C.epoch==W.epoch and C.role==W.role and finite(now)
    and (not C.lastNow or now>=C.lastNow)
end
function C.horsePose()
  local x, y, z, rot
  if type(W.receipt) == "function" and W.mission and W.epoch then
    local ok, r = pcall(W.receipt, fmt("return 'MGS_COOP_RIDE_POSE_V1' -- %d %d 1", W.mission, W.epoch))
    if ok and type(r) == "table" and r.ready == true and type(r.actors) == "table" then
      for i = 1, #r.actors do
        local a = r.actors[i]
        if type(a) == "table" and a.realized == true and a.id == HORSE and finite(a.x) and finite(a.y) and finite(a.z) then
          x, y, z, rot = a.x / 1000, a.y / 1000, a.z / 1000, finite(a.yaw) and a.yaw / 1000 or nil
        end
      end
    end
  end
  if not x then return nil end
  local h = C.horse
  local yaw = rot
  if not yaw and h then
    local dx, dz = x - h.x, z - h.z
    yaw = (dx * dx + dz * dz > 0.04) and atan2(dx, dz) or h.yaw
  end
  C.horse = {x = x, y = y, z = z, yaw = yaw or (h and h.yaw) or 0}
  return C.horse
end
local function mounted()
  if type(PlayerInfo) ~= "table" or type(PlayerStatus) ~= "table" then return false end
  local ok, yes = pcall(PlayerInfo.OrCheckStatus, {PlayerStatus.ON_HORSE})
  return ok and yes == true
end
local function localPos()
  local x, y, z = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
  if finite(x) and finite(y) and finite(z) then return x, y, z end
end
local function near(px, pz, radius)
  local x, _, z = localPos()
  return x and ((px - x) ^ 2 + (pz - z) ^ 2 <= radius * radius) or false
end
local function point(h)
  local o = OFFSET[h.kind or "horse"]
  return h.x - o.back * sin(h.yaw), h.y + o.rise, h.z - o.back * cos(h.yaw), h.yaw
end
local function drivenVehicle()
  if type(PlayerInfo) ~= "table" or type(PlayerStatus) ~= "table" or type(Tpp) ~= "table" then return nil end
  local ok, on = pcall(PlayerInfo.OrCheckStatus, {PlayerStatus.ON_VEHICLE})
  local id = W.get(vars, "playerVehicleGameObjectId")
  if not ok or on ~= true or type(id) ~= "number" or id < 0 or id > 65534 or id == HORSE then return nil end
  local typed, yes = pcall(Tpp.IsVehicle, id)
  return typed and yes == true and id or nil
end
local function vehiclePose(id)
  if type(GameObject) ~= "table" or type(GameObject.SendCommand) ~= "function" then return nil end
  local ok, pos, rot = pcall(GameObject.SendCommand, id, {id = "GetPosition"})
  if not ok or pos == nil or (type(pos) ~= "userdata" and type(pos) ~= "table") then return nil end
  local read, x, y, z = pcall(function() return pos:GetX(), pos:GetY(), pos:GetZ() end)
  if not read or not finite(x) or not finite(y) or not finite(z) then return nil end
  C.horse = {x = x, y = y, z = z, yaw = finite(rot) and rot or 0, kind = "vehicle"}
  return C.horse
end
local function mountPose()
  if C.mount and C.mount ~= HORSE then return vehiclePose(C.mount) end
  local h = C.horsePose()
  if h then h.kind = "horse" end
  return h
end
local function warp(index, x, y, z, yaw)
  if type(GameObject) ~= "table" or type(GameObject.SendCommand) ~= "function" then return false end
  return pcall(GameObject.SendCommand, {type = "TppPlayer2", index = index}, {id = "Warp", pos = {x, y, z}, rotY = yaw})
end
local function native(hold, putdown, mine)
  if type(W.receipt)~="function" then C.physical=nil;C.nativeRole=-1;return -1 end
  local ok,r=pcall(W.receipt,"return 'MGS_COOP_CARGO_V2' -- "..(hold and "1" or "0").." "..(putdown and "1" or "0").." "..(mine and "1" or "0"))
  C.physical=nil;C.nativeRole=-1
  if ok and type(r)=="table" and r.ready==true and integer(r.role,0,2)
    and type(r.carrying)=="boolean" and type(r.carried)=="boolean" and type(r.localCarried)=="boolean" and type(r.peerCarrying)=="boolean" then
    C.physical=r;C.nativeRole=r.role
  end
  return C.nativeRole
end
local function send(state, x, y, z, yaw)
  if type(W.tx) ~= "function" then return false end
  C.seq = C.seq % 65535 + 1
  local yawm = milli(yaw or 0)
  if yawm > 31416 or yawm < -31416 then yawm = 0 end
  return W.tx(OP, C.seq, state, milli(x or 0), milli(y or 0), milli(z or 0), yawm)
end
local function log(text) if type(W.log) == "function" then pcall(W.log, "horse_cargo;" .. text) end end
local function padMask(on)
  local f = W.get(Player, on and "SetPadMask" or "ResetPadMask")
  if type(f) ~= "function" then return false end
  local stick = W.get(PlayerPad, "STICK_L")
  local ok,result
  if on then ok,result=pcall(f,{settingName="MgsCoopCargo",except=false,buttons=0xFFFFFFA7,sticks=type(stick)=="number" and stick or 0,triggers=0})
  else ok,result=pcall(f,{settingName="MgsCoopCargo"}) end
  if ok and result~=false then C.maskOwned=on;return true end
  return false
end
local function stance(name)
  local f, s = W.get(Player, "RequestToSetTargetStance"), W.get(PlayerStance, name)
  if type(f) == "function" and s ~= nil then pcall(f, s) end
end
local function besidePoint(x, y, z, yaw)
  local s, d = yaw + math.pi / 2, (C.horse and C.horse.kind == "vehicle") and 2.2 or 1.3
  return x + d * sin(s), y, z + d * cos(s), yaw
end
local function stop(now, why, place, remote)
  if C.state == "carrier" or C.state == "loading" then
    local h = C.horse
    if C.state == "carrier" and place and h then local x, y, z, r = besidePoint(h.x, h.y + 0.3, h.z, h.yaw); warp(1, x, y, z, r) end
    if not remote then send(0,0,0,0,0) end
  elseif C.state == "cargo" then
    local p = C.rx
    if place and p then local x, y, z, r = besidePoint(p.x, p.y - 1.1, p.z, p.yaw); warp(0, x, y, z, r) end
    if C.awake then stance("STAND") end
    if not remote then send(0,0,0,0,0) end
  end
  if C.maskOwned then padMask(false) end
  if C.state ~= "off" then log("stop;why=" .. tostring(why)) end
  C.state, C.heldSince, C.rx, C.loadAt, C.awake, C.unloadSince, C.hopSince = "off", nil, nil, nil, nil, nil, nil
  C.mount,C.horse,C.offer,C.sentOffer,C.offerShown,C.requestAt,C.pullSince,C.lostAt,C.startAt,C.why=nil,nil,nil,nil,nil,nil,nil,nil,nil,nil
  C.ownerGeneration,C.localGeneration=nil,nil
  C.txAt=now
  native(false,false,false)
end
C.stop=stop
function C.setTesting(on)
  if type(on)~="boolean" then return false end
  if not on then stop(W.now(),"test_disabled",false) end
  C.testing=on;return true
end
function C.status()
  local p=C.physical
  return fmt("cargo;state=%s;awake=%d;mask=%d;mount=%s;role=%d;physical=%s;attachment=offset",C.state,C.awake and 1 or 0,C.maskOwned and 1 or 0,
    C.mount==HORSE and "horse" or C.mount and "vehicle" or "none",C.nativeRole or -1,p and ((p.carrying or p.carried) and "busy" or "clear") or "unknown")
end
local function records()
  local d = D()
  if type(d) ~= "table" or type(d.records) ~= "table" or not W.role then return nil end
  return d.records[3 - W.role], d.records[W.role]
end
local function held(d, key, now)
  local at,on
  if key=="E" then at,on=d.rawHeldAt,d.rawHeld else at,on=d.rawCarryHeldAt,d.rawCarryHeld end
  return on==true and finite(at) and now>=at and now-at<300
end
local function offTick(now, d, other, me)
  local role = native(false, false, false)
  local vehicle = role == 0 and not mounted() and drivenVehicle() or nil
  if role == 0 and (mounted() or vehicle) then
    C.mount = vehicle or HORSE
    local h = mountPose()
    if not h then return end
    if now>=C.txAt then
      local x,y,z,r=point(h)
      if send(2,x,y,z,r) then C.sentOffer={x=x,y=y,z=z,yaw=r,at=now,mount=C.mount} end
      C.txAt=now+250
    end
    local downed=fresh(other,now) and other.down==true
    local reach = downed and near(other.pos[1] / 1000, other.pos[3] / 1000, NEAR_M + 1)
    C.pullSince = reach and held(d, "R", now) and (C.pullSince or now) or nil
    if C.pullSince and now - C.pullSince >= 1000 then
      C.state,C.awake,C.pullSince,C.unloadSince,C.ownerGeneration="carrier",false,nil,nil,other.generation
      native(true, false, false)
      
      log("carrier;pulled=1;mount=" .. tostring(C.mount))
    end
    return
  end
  C.pullSince = nil
  if role == 1 then
    C.heldSince = held(d, "E", now) and (C.heldSince or now) or nil
    if not C.heldSince or now - C.heldSince < HOLD_MS then return end
    C.mount = HORSE
    local h = mountPose()
    if not h or not near(h.x, h.z, NEAR_M) then return end
    C.state,C.loadAt,C.heldSince,C.awake,C.ownerGeneration="loading",now,nil,false,other and other.generation
    native(false, true, false)
    log("loading")
    return
  end
  C.heldSince = nil
  local o=C.offer
  if C.requestAt and now-C.requestAt>3000 then C.requestAt=nil end
  local why = (not o or now<o.at or now-o.at>1000) and "no_offer" or role ~= 0 and "role" or mounted() and "mounted" or
    (me and me.down == true) and "down" or not near(o.x, o.z, NEAR_M) and "far" or held(d, "R", now) and "holding" or "ready"
  C.why=why
  if why ~= "far" and why ~= "ready" and why ~= "holding" then C.hopSince = nil; return end
  local close = near(o.x, o.z, NEAR_M)
  if close and not C.offerShown then C.offerShown = true;  end
  if not close then C.offerShown = nil end
  C.hopSince = close and held(d, "R", now) and (C.hopSince or now) or nil
  if C.hopSince and now-C.hopSince>=RIDE_MS and not C.requestAt then
    if send(3,o.x,o.y,o.z,o.yaw) then C.requestAt=now;log("ride_request") end
    C.hopSince=nil
  end
end
function C.tick(now)
  if not C.testing then return end
  local d = D()
  if not context() or not finite(now) then stop(finite(now) and now or 0,"unpaired",false);return end
  if not current(now) then
    stop(now,"context_changed",false)
    C.mission,C.epoch,C.role,C.rxSeq=W.mission,W.epoch,W.role,nil
  end
  C.lastNow=now
  local other,me=records()
  if C.maskOwned and C.state~="cargo" then padMask(false) end
  if C.state == "off" then return offTick(now, d, other, me) end
  if C.state == "loading" then
    local role=native(false,true,false)
    local p=C.physical
    if role==0 and p and not p.carrying and not p.carried then
      C.state, C.unloadSince = "carrier", nil
      native(true, false, false)
      log("carrier")
    elseif now - C.loadAt > LOAD_MS then stop(now, "load_timeout", false) end
    return
  end
  if C.state == "carrier" then
    native(true, false, false)
    if not fresh(other,now) or (C.ownerGeneration and C.ownerGeneration~=other.generation) then stop(now,"teammate_lost",true);return end
    if C.awake and (other.down or (not mounted() and not drivenVehicle())) then stop(now,"ride_ended",true);return end
    if not C.awake and other.down ~= true then stop(now, "teammate_up", true); return end
    local h = mountPose()
    if not h then
      C.lostAt = C.lostAt or now
      if now - C.lostAt > STALE_MS then stop(now, "horse_lost", false) end
      return
    end
    C.lostAt = nil
    local x, y, z, yaw = point(h)
    warp(1, x, y, z, yaw)
    if now >= C.txAt then send(1, x, y, z, yaw); C.txAt = now + 100 end
    C.unloadSince = (not mounted() and not drivenVehicle() and held(d, "R", now) and near(h.x, h.z, NEAR_M + 0.8)) and (C.unloadSince or now) or nil
    if C.unloadSince and now - C.unloadSince >= 1000 then stop(now, "taken_down", true) end
    return
  end
  if C.state == "cargo" then
    native(false, false, true)
    local p = C.rx
    if not p or now<C.rxAt or now-C.rxAt>STALE_MS then stop(now,"stream_lost",true);return end
    if not fresh(me,now) or C.localGeneration~=me.generation then stop(now,"player_changed",false);return end
    if C.awake and me.down then stop(now,"passenger_downed",true);return end
    if not C.awake and me and me.down ~= true then stop(now, "awake", true); return end
    warp(0, p.x, p.y, p.z, p.yaw)
    if C.awake then
      C.hopSince = (now - C.startAt > 1500 and held(d, "R", now)) and (C.hopSince or now) or nil
      if C.hopSince and now - C.hopSince >= RIDE_MS then stop(now, "hopped_off", true); return end
    end
  end
end
W.handlers[OP] = function(v)
  if not C.testing then return end
  if type(v)~="table" or #v~=6 or not integer(v[1],1,65535) or not integer(v[2],0,3)
    or not integer(v[3],-1000000000,1000000000) or not integer(v[4],-1000000000,1000000000)
    or not integer(v[5],-1000000000,1000000000) or not integer(v[6],-31416,31416) then return end
  local now=W.now and W.now() or 0
  if not current(now) then return end
  if C.rxSeq then local delta=(v[1]-C.rxSeq)%65535;if delta==0 or delta>32767 then return end end
  C.rxSeq=v[1]
  local p = {x = v[3] / 1000, y = v[4] / 1000, z = v[5] / 1000, yaw = v[6] / 1000, at = now}
  local state = v[2]
  if state == 2 then C.offer = p; return end
  if state == 3 then
    local other=records()
    local o=C.sentOffer
    if C.state=="off" and o and now>=o.at and now-o.at<1000 and fresh(other,now) and other.down~=true
      and (mounted() or drivenVehicle()) and near(other.pos[1]/1000,other.pos[3]/1000,NEAR_M+1)
      and (p.x-o.x)^2+(p.y-o.y)^2+(p.z-o.z)^2<4 then
      C.mount = mounted() and HORSE or drivenVehicle()
      C.state,C.awake,C.unloadSince,C.ownerGeneration="carrier",true,nil,other.generation
      native(true, false, false)
      
      log("carrier;awake=1")
    end
    return
  end
  if state == 1 then
    if C.state == "carrier" or C.state == "loading" then return end
    if C.state ~= "cargo" then
      local _, me = records()
      local awake = not (me and me.down == true)
      if not fresh(me,now) or (awake and not (C.requestAt and now>=C.requestAt and now-C.requestAt<3000)) then return end
      if not near(p.x,p.z,NEAR_M+1) then return end
      C.awake,C.startAt,C.localGeneration=awake,now,me.generation
      if awake then
        if not padMask(true) then stop(now,"pad_mask_refused",false);return end
        stance("SQUAT");
      end
      log("cargo;awake=" .. (awake and "1" or "0"))
    end
    C.state, C.rxAt, C.rx = "cargo", now, p
  elseif state == 0 and (C.state == "cargo" or C.state == "carrier") then
    stop(now,"remote_end",true,true)
  end
end
local tick = W.tick
function W.tick(...)
  if tick then tick(...) end
  local now = W.now and W.now() or 0
  local ok, err = pcall(C.tick, now)
  if not ok then stop(now,"error",false);log("error;"..tostring(err)) end
end
local function reset() stop(W.now and W.now() or 0,"reset",false);C.mission,C.epoch,C.role,C.rxSeq,C.lastNow=nil,nil,nil,nil,nil end
if type(W.startHooks) == "table" then W.startHooks[#W.startHooks + 1] = reset end
if type(W.unpairedHooks) == "table" then W.unpairedHooks[#W.unpairedHooks + 1] = reset end
end
end
