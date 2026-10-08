-- Original host guard placement and per-soldier debug actions on the owner update.
-- Placement reuses a living guard: "here" moves him 5 m in front of the player by
-- the native set-transform message (lab.ini debug_soldier_warp=1); else, and for
-- camp/route modes, an authored patrol route snap (retail TppDebug sequence).
do
  local D, A = e.MgsCoopDebug, e.MgsCoopDebug.api
  local E = D.enemy
  local R = E.api
  local get, call, integer, number, pose = A.get, A.call, A.integer, A.number, A.pose
  local fmt, floor, sqrt, min = string.format, math.floor, math.sqrt, math.min
  local S = {handles = {spawn_enemy = true, enemy_list = true, enemy = true, enemy_unmark = true, cp_phase = true}}
  D.spawn = S
  function S.stop() end
  local function send(id, t) return call(GameObject, "SendCommand", id, t) end
  local function dist(a, b) return sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
  local function mm(v) return floor(v * 1000 + 0.5) end
  -- Wire codes by enum name: 0 normal, 2 sleep, 3 faint, 4 dying, 5 dead, 8 disabled.
  local function wire(s)
    local disabled = get(TppGameObject, "NPC_STATE_DISABLE")
    if disabled ~= nil and send(s.id, {id = "GetStatus"}) == disabled then return 8 end
    local n = send(s.id, {id = "GetLifeStatus"})
    for _, k in ipairs({{"DEAD", 5}, {"DYING", 4}, {"SLEEP", 2}, {"FAINT", 3}}) do
      local v = get(TppGameObject, "NPC_LIFE_STATE_" .. k[1])
      if v ~= nil and n == v then return k[2] end
    end
    return 0
  end
  local function flags(s)
    return (E.marked[s.name] and 1 or 0) + (E.ignored[s.name] and 2 or 0) + (E.used[s.name] and 4 or 0)
  end
  -- Eligible: unused, living NORMAL, readable, 30 m to 500 m from the player.
  local function eligible(s, normal)
    return not E.used[s.name] and s.distance and s.distance > 900 and s.distance <= 250000
      and send(s.id, {id = "GetLifeStatus"}) == normal
  end
  local function pick(c, list, normal)
    for _, s in ipairs(c.guards) do if eligible(s, normal) then return s, 0 end end
    local best
    for _, o in ipairs(list) do
      if o ~= c then
        for _, s in ipairs(o.guards) do
          if eligible(s, normal) and (not best or s.distance < best.distance) then best = s end
        end
      end
    end
    return best, 1
  end
  -- Route score in metres from the player, at most 192 SendCommands per request.
  local function score(c, route, p, budget)
    local cached = E.routePos[route]
    if cached then return dist(cached, p) end
    if budget.n < 192 then
      budget.n = budget.n + 1
      local ok, best = pcall(function()
        local ids = send({type = "TppSoldier2"}, {id = "GetGameObjectIdUsingRoute", route = route})
        if type(ids) ~= "table" then ids = {ids} end
        local best
        for i = 1, 3 do
          local v = get(ids, i)
          if type(v) == "string" then v = call(GameObject, "GetGameObjectId", "TppSoldier2", v) end
          if integer(v, 0, 65534) and budget.n < 192 then
            budget.n = budget.n + 1
            local at = R.position({id = v})
            if at and (not best or dist(at, p) < best) then best = dist(at, p) end
          end
        end
        return best
      end)
      if ok and best then return best end
    end
    return (c.distance < math.huge and sqrt(c.distance) or 1e6) + 50
  end
  local function cachedRoute(c, p)
    local best, route
    for _, r in ipairs(c.routes) do
      local v = E.routePos[r]
      if v and (not best or dist(v, p) < best) then best, route = dist(v, p), r end
    end
    return route or c.routes[1]
  end
  local function place(s, route, id, o)
    E.used[s.name] = true
    -- Retail TppDebug's route placement, never an unknown XYZ Warp/Spawn.
    local ok = pcall(function()
      call(TppEnemy, "SetDisable", s.id)
      send(s.id, {id = "SetSneakRoute", route = route, point = 0})
      send(s.id, {id = "SetCautionRoute", route = route, point = 0})
      send(s.id, {id = "SetAlertRoute", enabled = false, route = "", point = 0})
      call(TppEnemy, "SetEnable", s.id)
      send(s.id, {id = "SetForceRealize"})
    end)
    if not ok then pcall(call, TppEnemy, "SetEnable", s.id); return false, "spawn_binding_error" end
    local q = {kind = "spawn", id = id, mission = D.mission, frames = 0}
    q.poll = function()
      if q.frames < 2 then return end
      if not R.usable(s) then D.pending = nil; A.note(id, false, "spawn_terminal_unknown"); return end
      local v = R.position(s)
      if v then
        D.pending = nil
        local d = dist(v, pose())
        if o.come then pcall(send, s.id, {id = "SetDiscoveryPlayer"}) end
        E.last, E.routePos[route] = {position = v, mission = D.mission}, v
        R.menu(id, fmt("spawned %.0f %.0f %.0f %.0f", mm(v[1]), mm(v[2]), mm(v[3]), s.hash))
        A.note(id, true, fmt("%s;ent=%.0f;d=%.1f;moved=%.1f;far=%d;%s;come=%d;sync=none;route=%s", o.prefix, s.hash, d,
          dist(v, s.position), d > 60 and 1 or 0, o.tag, o.come and 1 or 0, route))
      elseif q.frames >= 120 then D.pending = nil; A.note(id, false, "spawn_position_timeout") end
    end
    D.pending = q
    return true, fmt("%s;ent=%.0f;scope=local", o.submitted, s.hash)
  end
  -- WARP_V1: {r=,h=,leaf=} from the native engine message, else nil and why.
  local function warp(s, id)
    local c = pose()
    local y = math.rad(c[4])
    local r = A.receipt(fmt("return 'MGS_COOP_DEBUG_WARP_V1' -- %.0f %.0f %.0f %.0f %.0f %.0f", id, s.id, mm(c[1] + 5 * math.sin(y)),
      mm(c[2] + .2), mm(c[3] + 5 * math.cos(y)), mm(math.atan2(-math.sin(y), -math.cos(y)))))
    if type(r) ~= "table" then return nil, "native_unavailable" end
    local why = get(r, "refused")
    if why ~= nil then return nil, R.name(why) and why or "refused" end
    return r
  end
  -- Nearest unused NORMAL, enabled, on-foot guard 15-500 m away.
  local function nearest(list, normal)
    local null, off, best = get(GameObject, "NULL_ID"), get(TppGameObject, "NPC_STATE_DISABLE")
    for _, c in ipairs(list) do
      for _, s in ipairs(c.guards) do
        if not E.used[s.name] and s.distance > 225 and s.distance <= 250000 and (not best or s.distance < best.distance)
          and send(s.id, {id = "GetLifeStatus"}) == normal and (off == nil or send(s.id, {id = "GetStatus"}) ~= off) then
          local v = send(s.id, {id = "GetVehicleGameObjectId"})
          if type(v) ~= "number" or v == null then best = s end
        end
      end
    end
    return best
  end
  local function here(s, list, id, come)
    E.used[s.name] = true
    local notice = come and "SetDisableRestrictNotice" or "SetEnableRestrictNotice"
    if (not come or E.ignored[s.name]) and pcall(call, TppEnemy, notice, s.id) then E.ignored[s.name] = not come or nil end
    local q, forced = {kind = "spawn", id = id, mission = D.mission, frames = 0}
    local function release() if forced then forced = nil; pcall(send, s.id, {id = "SetForceRealize", forceRealize = false}) end end
    -- Route snap fallback when the native placement refuses or never lands.
    local function route(why)
      release()
      local best, br, budget, p = nil, nil, {n = 0}, pose()
      for i = 1, min(4, #list) do
        for _, r in ipairs(list[i].routes) do
          local value = score(list[i], r, p, budget)
          if not best or value < best then best, br = value, r end
        end
      end
      if not br then return false, "spawn_no_route;why=" .. why end
      local ok, msg = place(s, br, id, {prefix = "guard_here", submitted = "submitted_guard_here", come = come, tag = "mode=route"})
      return ok, msg .. ";mode=route;why=" .. why
    end
    local r, why = warp(s, id)
    if not r then return route(why) end
    local function landed() if get(r, "h") == 1 then q.done = q.frames end end
    landed()
    if not q.done then forced = true; pcall(send, s.id, {id = "SetForceRealize"}) end
    q.poll = function()
      if not q.done then
        if not R.usable(s) then release(); D.pending = nil; A.note(id, false, "spawn_terminal_unknown"); return end
        r, why = warp(s, id)
        if r then landed() end
        if not r or (not q.done and q.frames >= 150) then
          D.pending = nil
          local ok, msg = route(why or "realize_timeout")
          A.note(id, ok, msg, ok)
        end
        return
      end
      if q.frames < q.done + 2 then return end
      local v = R.position(s)
      if not v then if q.frames > q.done + 60 then release(); D.pending = nil; A.note(id, false, "spawn_position_timeout") end; return end
      D.pending = nil
      release()
      if come then pcall(send, s.id, {id = "SetDiscoveryPlayer"}) end
      local d = dist(v, pose())
      E.last = {position = v, mission = D.mission}
      R.menu(id, fmt("spawned %.0f %.0f %.0f %.0f", mm(v[1]), mm(v[2]), mm(v[3]), s.hash))
      A.note(id, true, fmt("guard_here;ent=%.0f;d=%.1f;moved=%.1f;far=%d;mode=warp;come=%d;sync=none;frames=%d;leaf=%s", s.hash, d,
        dist(v, s.position), d > 10 and 1 or 0, come and 1 or 0, q.done, R.name(get(r, "leaf")) and r.leaf or "unknown"))
    end
    D.pending = q
    return true, fmt("submitted_guard_here;ent=%.0f;scope=local;mode=warp", s.hash)
  end
  local function spawn(a, id)
    if not A.hostOnly() then return false, "host_only" end
    local mode = a[2]
    local come = mode == "here" and a[3] == "come" and #a == 3
    if not ((mode == "here" and (#a == 2 or come)) or (mode == "closest" and #a == 2)
      or (#a == 3 and mode ~= "here" and mode ~= "closest" and R.name(mode))) then return false, "spawn_args" end
    local list, byName, err = R.camps()
    if err then return false, err end
    local count = 0
    for _ in pairs(E.used) do count = count + 1 end
    if count >= 8 then return false, "spawn_pool_limit_8" end
    local normal = get(get(TppEnemy, "LIFE_STATUS"), "NORMAL")
    if not integer(normal, 0, 4) then return false, "missing_normal_enum" end
    local p, readback = pose(), {prefix = "guard_position_readback", submitted = "submitted_patrol_guard"}
    if mode == "here" then
      local s = nearest(list, normal)
      if not s then return false, "spawn_no_guard" end
      return here(s, list, id, come)
    elseif mode == "closest" then
      for i = 1, min(6, #list) do
        for _, s in ipairs(list[i].guards) do
          if eligible(s, normal) then readback.tag = "borrowed=0"; return place(s, cachedRoute(list[i], p), id, readback) end
        end
      end
      if not list[1] then return false, "spawn_no_route" end
      local s, borrowed = pick(list[1], list, normal)
      if not s then return false, "spawn_no_guard" end
      readback.tag = "borrowed=" .. borrowed
      return place(s, cachedRoute(list[1], p), id, readback)
    end
    local index, c = number(a[3], 1, 64, true), byName[mode]
    if not c or not index or not c.routes[index] then return false, "spawn_camp_route" end
    local s, borrowed = pick(c, list, normal)
    if not s then return false, "spawn_no_guard" end
    readback.tag = "borrowed=" .. borrowed
    return place(s, c.routes[index], id, readback)
  end
  local function nearbyList(a, id)
    local radius = 80
    if #a == 2 then radius = number(a[2], 10, 300, true) elseif #a ~= 1 then radius = nil end
    if not radius then return false, "radius_args" end
    local near = R.nearby(radius)
    if not R.menu(id, fmt("nearby %.0f %d", D.mission, radius)) then return false, "enemy_menu_unavailable" end
    local n = min(#near, 12)
    for i = 1, n do
      local s = near[i]
      R.menu(id, fmt("soldier %d %.0f %.0f %d %d %s", i, s.hash, floor(sqrt(s.distance) * 1000 + 0.5), wire(s), flags(s), s.name))
    end
    R.menu(id, "done")
    return true, fmt("nearby=%d;radius=%d", n, radius)
  end
  local LIFE = {kill = "DEAD", sleep = "SLEEP", faint = "FAINT"}
  local ANY = {enable = true, info = true, unmark = true, notice = true}
  local OPS = {kill = 1, sleep = 1, faint = 1, fulton = 1, come = 1, ignore = 1, notice = 1, disable = 1, enable = 1,
    mark = 1, unmark = 1, info = 1}
  local function target(o, t)
    if t == "nearest" then
      for _, s in ipairs(R.nearby(150)) do if ANY[o] or R.usable(s) then return s end end
      return nil, "no_readable_soldier"
    end
    local hash = number(t, 0, 4294967295, true)
    if not hash then return nil, "enemy_args" end
    for _, s in ipairs(R.soldiers()) do if s.hash == hash then return s end end
    return nil, "enemy_not_authored"
  end
  local function op(a)
    local o = a[2]
    if #a ~= 3 or not OPS[o] then return false, "enemy_args" end
    if o ~= "info" and not A.hostOnly() then return false, "host_only" end
    local s, why = target(o, a[3])
    if not s then return false, why end
    local head, tag = fmt("enemy_%s;ent=%.0f", o, s.hash), A.sync("enemy")
    if LIFE[o] then
      local k = "NPC_LIFE_STATE_" .. LIFE[o]
      local v = get(TppGameObject, k)
      if v == nil then return false, "missing_" .. k end
      send(s.id, {id = "ChangeLifeState", state = v})
      return true, fmt("%s;life_readback=%d%s;%s", head, wire(s), o == "faint" and ";unverified" or "", tag)
    elseif o == "fulton" then
      send(s.id, {id = "RequestForceFulton"})
      return true, head .. ";" .. tag
    elseif o == "come" then
      send(s.id, {id = "SetDiscoveryPlayer"})
      return true, head .. ";hostile;unverified;" .. tag
    elseif o == "ignore" or o == "notice" then
      call(TppEnemy, o == "ignore" and "SetEnableRestrictNotice" or "SetDisableRestrictNotice", s.id)
      E.ignored[s.name] = o == "ignore" or nil
      return true, head .. ";sync=none"
    elseif o == "disable" or o == "enable" then
      if o == "enable" and not R.usable(s) then return false, "enemy_terminal_unknown" end
      call(TppEnemy, o == "enable" and "SetEnable" or "SetDisable", s.id)
      return true, head .. ";sync=none"
    elseif o == "mark" or o == "unmark" then
      if o == "mark" then call(TppMarker, "Enable", s.id, 0, "moving", "map_and_world_only_icon", 0, false, false)
      else call(TppMarker, "Disable", s.id) end
      E.marked[s.name] = o == "mark" and s.id or nil
      return true, head .. ";sync=none;unverified"
    end
    local v = R.position(s)
    return true, fmt("enemy_info;ent=%.0f;name=%s;cp=%s;state=%d;distance_m=%s", s.hash, s.name, tostring(s.cp), wire(s),
      v and fmt("%.1f", dist(v, pose())) or "unknown")
  end
  local PHASES = {sneak = "SNEAK", caution = "CAUTION", evasion = "EVASION", alert = "ALERT"}
  local function phase(a)
    if not A.hostOnly() then return false, "host_only" end
    if #a ~= 3 or not PHASES[a[3]] then return false, "phase_args" end
    local k = "PHASE_" .. PHASES[a[3]]
    local v = get(TppGameObject, k)
    if v == nil then return false, "missing_" .. k end
    local cp = a[2]
    if cp == "nearest" then
      local list = R.camps()
      cp = list and list[1] and list[1].name
    end
    if not R.name(cp) or type(get(get(mvars, "ene_soldierDefine"), cp)) ~= "table" then return false, "cp_unknown" end
    local cid = call(GameObject, "GetGameObjectId", cp)
    if not integer(cid, 0, 65534) then return false, "cp_unknown" end
    send(cid, {id = "SetPhase", phase = v})
    return true, fmt("submitted_phase;cp=%s;phase=%s;%s", cp, a[3], A.sync("enemy"))
  end
  function S.run(a, id)
    local cmd = a[1]
    if cmd == "spawn_enemy" then return spawn(a, id)
    elseif cmd == "enemy_list" then return nearbyList(a, id)
    elseif cmd == "enemy" then return op(a)
    elseif cmd == "cp_phase" then return phase(a)
    elseif cmd == "enemy_unmark" and #a == 1 then
      if not A.hostOnly() then return false, "host_only" end
      local n = 0
      for k, v in pairs(E.marked) do pcall(call, TppMarker, "Disable", v); E.marked[k] = nil; n = n + 1 end
      return true, fmt("unmarked=%d", n)
    end
    return false, "command_args"
  end
end
