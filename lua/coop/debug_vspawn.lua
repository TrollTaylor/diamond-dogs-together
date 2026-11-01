-- Spawn vehicle, synced (docs/debug-menu.md "Spawn vehicle"; lab key debug_vspawn, off unless named, lab and play).
-- F9 World > Vehicles: vspawn lv|trc brings a jeep (light vehicle) or a truck 6 m in front of whoever presses, facing
-- their way, on both screens; vspawn_status is read only. Afghanistan and Africa free roam only.
-- Free roam loads only the faction's light vehicle and truck bodies (docs/vehicles-companions-design.md 3.5, 3.6) and
-- the class command Spawn refuses a live name, so the host brings one of the area's own vehicles of that class: an
-- authored locator of the area VEHICLE_SPAWN_LIST (veh_lv_NNNN, veh_trc_NNNN) that is unridden, not an LRRP
-- (soldier-driven) vehicle and not the partner's ride (an unridden LRRP one only as the last resort: each copy's AI
-- drives those, and Afghanistan's six trucks are all LRRP); live before wrecked, never brought before (else the oldest),
-- then the farthest from the spot. The stock instance command SetPosition{position, rotY in degrees}
-- (s10050_sequence.lua) moves it; a wreck is first re-created by the class command Respawn{name} (0xB31710: the name
-- binding stays). The same locator at the same pose on both copies is the same vehicle for world_vehicle.lua (by
-- locator, nearest of its type): riding together, driving, damage, Fulton and VEHICLE_GONE as for any map vehicle.
-- VSPAWN 0x44 (docs/world-sync-protocol.md section 25): seq, kind (1 the guest's ask, 2 the host's press, 3 the host's
-- spawn for the guest's ask, 4 the host's refusal of it), class (1 lv, 2 trc), locator number (255 none), xyz mm, yaw
-- mdeg, reason. World note 20: a=1 spawned in front of you (b class), 2 your partner spawned one (b class), 3 refused
-- (b reason: 1 off, 2 not free roam, 3 none free, 4 not in this area, 5 not on foot, 6 not paired, 7 failed, 8 no answer).
do
 local D, W = e.MgsCoopDebug, e.MgsCoopWorld
 local P = type(D) == "table" and D.player
 if type(P) == "table" and type(P.handles) == "table" and type(P.run) == "function" and type(D.api) == "table" then
  local A = D.api
  local get, fmt, floor, sqrt = A.get, string.format, math.floor, math.sqrt
  local OP, NOTE, FRONT, UP, NULL = 68, 20, 6, 0.8, 65535
  local CLASS, CNAME, CWORD = {lv = 1, trc = 2}, {"lv", "trc"}, {"jeep", "truck"}
  local WHY = {off = 1, mission = 2, none_free = 3, not_loaded = 4, busy = 5, unpaired = 6, failed = 7, timeout = 8}
  local V = {seq = 0, tick = 0, recent = {}, moves = {}}
  D.vspawn = V
  if type(W) == "table" and type(W.OP) == "table" then W.OP.VSPAWN = OP end
  local function log(s) if type(W) == "table" and type(W.log) == "function" then pcall(W.log, "event=vspawn;" .. s) end end
  local function note(a, b) if type(W) == "table" and type(W.note) == "function" then pcall(W.note, NOTE, a, b) end end
  local function int(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
  local function mm(v) return floor(v * 1000 + 0.5) end
  local function mdeg(y) y = y % 360; if y > 180 then y = y - 360 end; return floor(y * 1000 + 0.5) end
  local function send(id, cmd)
   local f = get(GameObject, "SendCommand")
   if type(f) ~= "function" then return nil end
   local ok, r = pcall(f, id, cmd)
   if ok then return r end
  end
  -- The lab key, answered by native (an older loader leaves the receipt unanswered: off).
  function V.on() return A.receipt("return 'MGS_COOP_VSPAWN_CFG_V1'") == 1 end
  local function isHost()
   if type(W) == "table" and W.active == true then return W.host == true end
   return A.hostOnly() == true
  end
  local function paired() return type(W) == "table" and W.paired == true end
  local function freeRoam() local m = get(vars, "missionCode"); return m == 30010 or m == 30020 end
  -- Gameplay, on foot (never while riding, in a cutscene or loading).
  local function onFoot()
   local f, o = get(TppMission, "CheckMissionState"), get(PlayerInfo, "OrCheckStatus")
   if type(f) ~= "function" or type(o) ~= "function" or f() ~= true then return false end
   return o({get(PlayerStatus, "STAND"), get(PlayerStatus, "SQUAT"), get(PlayerStatus, "CRAWL")}) == true
  end
  local function vid(loc)
   local f = get(GameObject, "GetGameObjectId")
   if type(f) ~= "function" then return nil end
   local ok, v = pcall(f, "TppVehicle2", loc)
   -- Index 0 (25600) is the relief vehicle: never one of these.
   if ok and int(v, 25601, 26111) then return v end
  end
  local function pose(id)
   local p = send(id, {id = "GetPosition"})
   if p == nil then return nil end
   local ok, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end)
   if ok and type(x) == "number" and type(y) == "number" and type(z) == "number" then return x, y, z end
  end
  local function alive(id) return send(id, {id = "IsAlive"}) == true end
  -- Why `id` must not be taken (nil: free): world_vehicle.lua's verdict (the local rider, an occupied seat, the
  -- partner's ride), else the stock rider array.
  local function busy(id)
   local G = get(W, "vehicleGone")
   if type(G) == "table" and type(G.busy) == "function" then
    local ok, why = pcall(G.busy, id)
    if not ok then return "busy_error" end
    return why
   end
   local r = send(id, {id = "GetRiderId"})
   if type(r) ~= "table" then return "riders_unreadable" end
   for _, v in pairs(r) do if v ~= NULL then return "occupied" end end
  end
  -- The area's authored locators of one class: {loc, num}.
  local function authored(class)
   local m = get(get(W, "MISSIONS"), get(vars, "missionCode"))
   local name = get(m, "areaEnemy")
   local list = type(name) == "string" and get(get(_G, name), "VEHICLE_SPAWN_LIST")
   local out = {}
   if type(list) ~= "table" then return out end
   for i = 1, 128 do
    local q = get(list, i)
    if q == nil then break end
    local loc = type(q) == "table" and get(q, "locator")
    local n = type(loc) == "string" and loc:match("^veh_" .. CNAME[class] .. "_(%d%d%d%d)$")
    if n and tonumber(n) <= 254 then out[#out + 1] = {loc = loc, num = tonumber(n)} end
   end
   return out
  end
  local function lrrp()
   local s, t = {}, get(mvars, "ene_lrrpVehicle")
   if type(t) == "table" then for _, v in pairs(t) do if type(v) == "string" then s[v] = true end end end
   return s
  end
  local function before(a, b)
   for i = 1, #a do if a[i] ~= b[i] then return a[i] < b[i] end end
   return false
  end
  -- The host's pick for the spot (x, z), or nil, reason.
  function V.pick(class, x, z)
   local list = authored(class)
   if #list == 0 then return nil, "not_loaded" end
   local skip, best, key = lrrp(), nil, nil
   for _, c in ipairs(list) do
    local id = vid(c.loc)
    if id and not busy(id) then
     local live, px, _, pz = alive(id), pose(id)
     local d = px and sqrt((px - x) ^ 2 + (pz - z) ^ 2) or 0
     local k = {skip[c.loc] and 1 or 0, live and 0 or 1, V.recent[c.loc] or -1, -d}
     if not key or before(k, key) then best, key = {loc = c.loc, num = c.num, id = id, live = live, d = d, lrrp = k[1]}, k end
    end
   end
   if not best then return nil, "none_free" end
   return best
  end
  function V.place(m)
   local ok = pcall(function() send(m.id, {id = "SetPosition", position = Vector3(m.x, m.y, m.z), rotY = m.yaw}) end)
   m.placed = V.tick
   return ok
  end
  -- Both copies: locator `num` of `class` to (x, y, z) facing yaw degrees. A live one moves at once; a wreck is
  -- re-created first and moved once it answers IsAlive (90 updates at most).
  function V.apply(class, num, x, y, z, yaw, by)
   local loc = fmt("veh_%s_%04d", CNAME[class], num)
   local id = vid(loc)
   if not id then log(fmt("step=apply;loc=%s;ok=0;reason=no_id", loc)); return false end
   local why = busy(id)
   if why == "local_rider" then log(fmt("step=apply;loc=%s;id=%d;ok=0;reason=local_rider", loc, id)); return false end
   local m = {loc = loc, id = id, x = x, y = y, z = z, yaw = yaw, by = by, at = V.tick}
   if alive(id) then V.place(m)
   else
    m.respawn = true
    send({type = "TppVehicle2"}, {id = "Respawn", name = loc})
    -- world_vehicle.lua counts a lost id as gone for good: it is back now.
    local G = get(W, "vehicleGone")
    if type(G) == "table" and type(G.gone) == "table" then G.gone[id] = nil end
   end
   V.moves[id], V.recent[loc] = m, V.tick
   log(fmt("step=apply;loc=%s;id=%d;via=%s;by=%s;x=%.2f;y=%.2f;z=%.2f;yaw=%.1f;busy=%s", loc, id, m.respawn and "respawn" or "setpos",
    by, x, y, z, yaw, tostring(why or "none")))
   return true
  end
  local function tx(kind, class, num, x, y, z, yaw, reason)
   if not (type(W) == "table" and type(W.tx) == "function" and paired()) then return false end
   V.seq = V.seq % 65535 + 1
   local ok = W.tx(OP, V.seq, kind, class, num, x, y, z, yaw, reason) == true
   log(fmt("step=tx;kind=%d;seq=%d;class=%d;num=%d;reason=%d;sent=%d", kind, V.seq, class, num, reason, ok and 1 or 0))
   return ok
  end
  -- The host: pick and apply for a presser at (x, y, z) facing yaw degrees; the move, or nil, reason.
  local function hostSpawn(class, x, y, z, yaw, by)
   if not freeRoam() then return nil, "mission" end
   local r = math.rad(yaw)
   local sx, sz = x + FRONT * math.sin(r), z + FRONT * math.cos(r)
   local c, why = V.pick(class, sx, sz)
   if not c then return nil, why end
   log(fmt("step=pick;loc=%s;id=%d;alive=%d;d=%.0f;for=%s;lrrp=%d", c.loc, c.id, c.live and 1 or 0, c.d, by, c.lrrp))
   if not V.apply(class, c.num, sx, y + UP, sz, yaw, "self") then return nil, "failed" end
   return {num = c.num, loc = c.loc, x = sx, y = y + UP, z = sz, yaw = yaw}
  end
  local function refuse(why)
   note(3, WHY[why] or WHY.failed)
   log("step=refused;reason=" .. why)
   return false, "vspawn_refused;reason=" .. why
  end
  function V.press(class)
   if not V.on() then return refuse("off") end
   if not freeRoam() then return refuse("mission") end
   if not onFoot() then return refuse("busy") end
   local ok, p = pcall(A.pose)
   if not ok or type(p) ~= "table" then return refuse("busy") end
   log(fmt("step=press;class=%s;role=%s;x=%.2f;z=%.2f;yaw=%.1f", CNAME[class], isHost() and "host" or "guest", p[1], p[3], p[4]))
   if isHost() then
    local m, why = hostSpawn(class, p[1], p[2], p[3], p[4], "host")
    if not m then return refuse(why) end
    local sent = tx(2, class, m.num, mm(m.x), mm(m.y), mm(m.z), mdeg(m.yaw), 0)
    note(1, class)
    return true, fmt("vspawn;class=%s;loc=%s;sent=%d", CNAME[class], m.loc, sent and 1 or 0)
   end
   if not paired() then return refuse("unpaired") end
   if V.ask then return false, "vspawn_refused;reason=pending" end
   if not tx(1, class, 255, mm(p[1]), mm(p[2]), mm(p[3]), mdeg(p[4]), 0) then return refuse("failed") end
   V.ask = {seq = V.seq, class = class, at = V.tick}
   return true, fmt("vspawn_asked;class=%s;seq=%d", CNAME[class], V.seq)
  end
  if type(W) == "table" and type(W.handlers) == "table" then
   W.handlers[OP] = function(v)
    if type(v) ~= "table" then return end
    local kind, class, num, x, y, z, yaw, reason = v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]
    log(fmt("step=rx;kind=%s;seq=%s;class=%s;num=%s;reason=%s", tostring(kind), tostring(v[1]), tostring(class), tostring(num), tostring(reason)))
    if not CNAME[class] or not int(x, -1e9, 1e9) or not int(z, -1e9, 1e9) or not int(y, -1e9, 1e9) or not int(yaw, -360000, 360000) then return end
    if kind == 1 and W.host == true then
     local m, why
     if not V.on() then why = "off" end
     if not why then m, why = hostSpawn(class, x / 1000, y / 1000, z / 1000, yaw / 1000, "guest") end
     if not m then
      log("step=ask_refused;reason=" .. tostring(why))
      tx(4, class, 255, x, y, z, yaw, WHY[why] or WHY.failed)
      return
     end
     tx(3, class, m.num, mm(m.x), mm(m.y), mm(m.z), mdeg(m.yaw), 0)
     note(2, class)
    elseif W.host == false and (kind == 2 or kind == 3) and int(num, 0, 254) then
     if kind == 3 then V.ask = nil end
     if not freeRoam() then log("step=skip;reason=mission"); return end
     if V.apply(class, num, x / 1000, y / 1000, z / 1000, yaw / 1000, "host") then note(kind == 3 and 1 or 2, class) end
    elseif W.host == false and kind == 4 then
     V.ask = nil
     note(3, int(reason, 1, 8) and reason or WHY.failed)
    end
   end
  end
  -- Each update: re-created wrecks are moved once alive; every move is read back (within 3 m, or ok=0 after 90).
  local function poll()
   for id, m in pairs(V.moves) do
    if m.respawn and not m.placed then
     if alive(id) then V.place(m)
     elseif V.tick - m.at > 90 then
      V.moves[id] = nil
      log(fmt("step=placed;loc=%s;id=%d;ok=0;reason=respawn;by=%s", m.loc, id, m.by))
     end
    elseif m.placed and V.tick - m.placed >= 15 then
     local x, y, z = pose(id)
     local d = x and sqrt((x - m.x) ^ 2 + (z - m.z) ^ 2)
     if d and d <= 3 or V.tick - m.placed >= 90 then
      V.moves[id] = nil
      log(fmt("step=placed;loc=%s;id=%d;d_m=%s;dy_m=%s;ok=%d;by=%s", m.loc, id, d and fmt("%.2f", d) or "nil",
       y and fmt("%.2f", y - m.y) or "nil", d and d <= 3 and 1 or 0, m.by))
     end
    end
   end
  end
  local function tick()
   V.tick = V.tick + 1
   local m = get(vars, "missionCode")
   if m ~= V.mission then
    if V.mission ~= nil then V.recent, V.moves, V.ask = {}, {}, nil end
    V.mission = m
   end
   if V.ask and V.tick - V.ask.at > 150 then
    V.ask = nil
    note(3, WHY.timeout)
    log("step=ask_timeout")
   end
   if next(V.moves) then poll() end
  end
  function V.status()
   local r, n = {}, 0
   for loc in pairs(V.recent) do r[#r + 1] = loc end
   for _ in pairs(V.moves) do n = n + 1 end
   table.sort(r)
   return fmt("vspawn;on=%d;role=%s;mission=%s;recent=%s;moving=%d;ask=%d", V.on() and 1 or 0, isHost() and "host" or "guest",
    tostring(get(vars, "missionCode")), #r > 0 and table.concat(r, ",") or "none", n, V.ask and 1 or 0)
  end
  for k in pairs({vspawn = true, vspawn_status = true}) do P.handles[k] = true end
  local base, stop = P.run, P.stop
  P.run = function(a, id)
   local c = type(a) == "table" and a[1]
   if c == "vspawn_status" and #a == 1 then return true, V.status() end
   if c == "vspawn" then
    if #a ~= 2 or not CLASS[a[2]] then return false, "vspawn_args" end
    local ok, yes, why = pcall(V.press, CLASS[a[2]])
    if not ok then log("step=error;msg=" .. tostring(yes):sub(1, 80)); return false, "vspawn_binding" end
    return yes, why
   end
   return base(a, id)
  end
  P.stop = function(...)
   V.recent, V.moves, V.ask = {}, {}, nil
   if type(stop) == "function" then return stop(...) end
  end
  local update = e.OnUpdate
  if type(update) == "function" then
   e.OnUpdate = function(...)
    local r = {update(...)}
    pcall(tick)
    return unpack(r)
   end
  end
 end
end
