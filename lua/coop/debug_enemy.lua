-- Original owner-thread authored guard actions; never allocates unknown assets.
do
  local D, A = e.MgsCoopDebug, e.MgsCoopDebug.api
  local get, call, integer, number, pose = A.get, A.call, A.integer, A.number, A.pose
  local finite, hostOnly, fmt = A.finite, A.hostOnly, string.format
  local E = {used = {}}
  D.enemy = E
  -- Only authored soldiers; nearest actions require position/life readback.
  local function soldiers()
    local defs, list, seen = get(mvars, "ene_soldierDefine"), {}, {}
    if type(defs) ~= "table" then return list end
    local null = get(GameObject, "NULL_ID")
    if type(null) ~= "number" then return list end
    for cp, group in pairs(defs) do
      if type(group) == "table" then
        for _, name in ipairs(group) do
          if type(name) == "string" and not seen[name] and #list < 512 then
            seen[name] = true
            local id = call(GameObject, "GetGameObjectId", "TppSoldier2", name)
            if integer(id, 0, 65535) and id ~= null then
              local h = call(Fox, "StrCode32", name)
              if integer(h, -2147483648, 4294967295) then
                if h < 0 then h = h + 4294967296 end
                list[#list + 1] = {id = id, name = name, hash = h, cp = cp}
              end
            end
          end
        end
      end
    end
    table.sort(list, function(a, b) return a.name < b.name end)
    return list
  end
  local function nearby(radius)
    local p, list = pose(), {}
    for _, s in ipairs(soldiers()) do
      local life = call(GameObject, "SendCommand", s.id, {id = "GetLifeStatus"})
      if integer(life, 0, 4) then
        local v = call(GameObject, "SendCommand", s.id, {id = "GetPosition"})
        if v and type(get(v, "GetX")) == "function" and type(get(v, "GetY")) == "function" and type(get(v, "GetZ")) == "function" then
          local x, y, z = v:GetX(), v:GetY(), v:GetZ()
          if finite(x) and finite(y) and finite(z) then
            s.distance = (x - p[1]) ^ 2 + (y - p[2]) ^ 2 + (z - p[3]) ^ 2
            if s.distance <= radius * radius then list[#list + 1] = s end
          end
        end
      end
    end
    table.sort(list, function(a, b) return a.distance == b.distance and a.name < b.name or a.distance < b.distance end)
    return list
  end
  local function usable(s)
    local n = call(GameObject, "SendCommand", s.id, {id = "GetLifeStatus"})
    local dead, status = get(TppGameObject, "NPC_LIFE_STATE_DEAD"), get(TppEnemy, "ACTION_STATUS")
    if not integer(dead, 1, 4) or not integer(n, 0, 4) or n == dead or type(status) ~= "table" then return false end
    local action, known = call(TppEnemy, "GetActionStatus", s.id), false
    for _, value in pairs(status) do if type(value) == "number" and action == value then known = true end end
    return known and type(status.FULTON_RECOVERD) == "number" and action ~= status.FULTON_RECOVERD
      and call(TppEnemy, "IsRecovered", s.name) ~= true
  end
  local function name(s)
    return type(s) == "string" and #s > 0 and #s <= 64 and s:match("^[%w_.%-]+$") ~= nil
  end
  local function position(s)
    local v = call(GameObject, "SendCommand", s.id, {id = "GetPosition"})
    if not v then return nil end
    local p = {call(v, "GetX", v), call(v, "GetY", v), call(v, "GetZ", v)}
    for i = 1, 3 do if not finite(p[i]) or math.abs(p[i]) > 100000 then return nil end end
    return p
  end
  local function routes(cp)
    local set = get(get(mvars, "ene_routeSetsDefine"), cp)
    local list, seen = {}, {}
    if type(set) ~= "table" then return list end
    for _, mode in ipairs({"sneak_day", "sneak_night", "caution"}) do
      local groups = set[mode]
      if type(groups) == "table" then
        for _, group in pairs(groups) do
          if type(group) == "table" then
            for _, route in ipairs(group) do
              if name(route) and not seen[route] then
                if #list >= 512 then return nil end
                list[#list + 1], seen[route] = route, true
              end
            end
          end
        end
      end
    end
    table.sort(list)
    for i = #list, 65, -1 do list[i] = nil end
    return list
  end
  local function checkpoint(cp)
    local prefix = D.mission == 30010 and "afgh" or D.mission == 30020 and "mafr"
    if not prefix then return nil end
    local base = cp:match("^" .. prefix .. "_(.-)_cp$") or cp:match("^" .. prefix .. "_(.-)_ob$")
    local points = base and get(get(mvars, "loc_locationCommonCheckPointList"), base)
    if type(points) ~= "table" then return nil end
    -- Retail's locator returns an authored player spawn table and degree yaw.
    for i, key in ipairs(points) do
      if i > 8 then break end
      if name(key) then
        local ok, p, yaw = pcall(call, TppCheckPoint, "GetCheckPointLocator", key)
        if ok and type(p) == "table" and finite(yaw) and math.abs(yaw) <= 36000 then
          local valid = true
          for axis = 1, 3 do if not finite(p[axis]) or math.abs(p[axis]) > 100000 then valid = false end end
          if valid then return {p[1], p[2], p[3], yaw} end
        end
      end
    end
  end
  local function camps()
    local list, byName, p = {}, {}, pose()
    for _, s in ipairs(soldiers()) do
      local cp = s.cp
      if name(cp) and cp ~= "quest_cp" and (cp:match("_cp$") or cp:match("_ob$")) then
        if not byName[cp] then
          local r = routes(cp)
          if not r then return nil, nil, "spawn_route_limit_512" end
          if #r > 0 then
            byName[cp] = {name = cp, routes = r, guards = {}, distance = math.huge}
            list[#list + 1] = byName[cp]
          end
        end
        local c = byName[cp]
        if c and usable(s) then
          local v = position(s)
          if v then
            s.position, s.distance = v, (v[1] - p[1]) ^ 2 + (v[2] - p[2]) ^ 2 + (v[3] - p[3]) ^ 2
            c.guards[#c.guards + 1] = s
            c.distance = math.min(c.distance, s.distance)
          end
        end
      end
    end
    for _, c in ipairs(list) do
      table.sort(c.guards, function(a, b) return a.distance == b.distance and a.name < b.name or a.distance < b.distance end)
      if #c.guards == 0 then
        c.checkpoint = checkpoint(c.name)
        local v = c.checkpoint
        if v then c.distance = (v[1] - p[1]) ^ 2 + (v[2] - p[2]) ^ 2 + (v[3] - p[3]) ^ 2 end
      end
    end
    table.sort(list, function(a, b) return a.distance == b.distance and a.name < b.name or a.distance < b.distance end)
    return list, byName
  end
  local function menu(id, text)
    return A.receipt("return 'MGS_COOP_DEBUG_ENEMY_V1' -- " .. fmt("%.0f %s", id, text)) == true
  end
  local function dist(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
  -- Route score in metres from the player, at most 192 SendCommands per request.
  local function score(c, route, p, budget)
    local cached = E.routePos[route]
    if cached then return dist(cached, p) end
    if budget.n < 192 then
      budget.n = budget.n + 1
      local ok, best = pcall(function()
        local ids = call(GameObject, "SendCommand", {type = "TppSoldier2"}, {id = "GetGameObjectIdUsingRoute", route = route})
        if type(ids) ~= "table" then ids = {ids} end
        local best
        for i = 1, 3 do
          local v = get(ids, i)
          if type(v) == "string" then v = call(GameObject, "GetGameObjectId", "TppSoldier2", v) end
          if integer(v, 0, 65534) and budget.n < 192 then
            budget.n = budget.n + 1
            local at = position({id = v})
            if at and (not best or dist(at, p) < best) then best = dist(at, p) end
          end
        end
        return best
      end)
      if ok and best then return best end
    end
    return (c.distance < math.huge and math.sqrt(c.distance) or 1e6) + 50
  end
  -- Spawn sync tag (lua/coop/world_place.lua): the host announces the action for the
  -- guest's copy; "sync=world;seq=n" or "sync=none;why=<reason>".
  local function share(kind, s, x, y, z, yaw, flags, route, dy)
    local P = get(get(e, "MgsCoopWorld"), "place")
    if type(get(P, "share")) ~= "function" then return "sync=none;why=world_off" end
    local ok, tag = pcall(P.share, kind, s.hash, x, y, z, yaw, flags, route, dy)
    return ok and type(tag) == "string" and tag or "sync=none;why=world_error"
  end
  -- Spawn/list/op commands live in debug_spawn.lua and reuse these readers.
  E.api = {soldiers = soldiers, nearby = nearby, usable = usable, position = position, camps = camps, name = name, menu = menu,
    dist = dist, score = score, share = share}
  function E.stop() E.used, E.last, E.routePos, E.marked, E.ignored = {}, nil, {}, {}, {} end
  E.stop()
  function E.run(a, id)
    local cmd = a[1]
    if cmd == "enemy_camps" and #a == 1 then
      local list, _, err = camps()
      if err then return false, err end
      E.last = nil
      if not menu(id, "clear " .. fmt("%.0f", D.mission)) then return false, "enemy_menu_unavailable" end
      for i = 1, math.min(#list, 32) do
        local c = list[i]
        local distance = c.distance < math.huge and math.min(1e9, math.floor(math.sqrt(c.distance) * 1000 + .5)) or -1
        menu(id, fmt("camp %s %.0f %d", c.name, distance, #c.routes))
      end
      menu(id, "done")
      return true, fmt("camps=%d;more=%d;local_pool=1", math.min(#list, 32), math.max(0, #list - 32))
    elseif cmd == "enemy_routes" and #a == 2 then
      local _, byName, err = camps()
      if err then return false, err end
      local c = byName[a[2]]
      if not c then return false, "spawn_camp_route" end
      if not menu(id, "routes " .. c.name) then return false, "enemy_menu_unavailable" end
      for i, route in ipairs(c.routes) do menu(id, fmt("route %d %s", i, route)) end
      menu(id, "done")
      return true, fmt("routes=%d;point=0", #c.routes)
    elseif cmd == "goto_camp" and #a == 2 and name(a[2]) then
      local _, byName, err = camps()
      if err then return false, err end
      local c = byName[a[2]]
      local p = c and c.guards[1] and c.guards[1].position
      if not p and c and c.checkpoint then
        local ok, message = A.warp(c.checkpoint, id)
        return ok, message .. ";via=checkpoint"
      end
      if not p then return false, "camp_position_unavailable" end
      if math.abs(p[1] + 3) > 100000 or math.abs(p[2] + .5) > 100000 then return false, "camp_position_bounds" end
      return A.warp({p[1] + 3, p[2] + .5, p[3], pose()[4]}, id)
    elseif cmd == "goto_spawn" and (#a >= 1 and #a <= 3) then
      local distance = #a >= 2 and number(a[2], 1, 5, false) or 3
      local yaw = #a == 3 and number(a[3], -360, 360, false) or (#a == 2 and -90 or pose()[4])
      if not distance or not yaw then return false, "spawn_distance_args" end
      local last = E.last
      if not last or last.mission ~= D.mission then return false, "spawn_position_unavailable" end
      local p = last.position
      if #a >= 2 then
        if not integer(last.actor, 0, 65534) then return false, "spawn_actor_unavailable" end
        p = position({id = last.actor})
        if not p then return false, "spawn_current_position_unavailable" end
      end
      return A.warp({p[1] + distance, p[2] + .5, p[3], yaw}, id)
    elseif cmd == "enemy_enable" or cmd == "enemy_disable" then
      if #a ~= 2 then return false, "enemy_args" end
      if not hostOnly() then return false, "host_only" end
      local hash = number(a[2], 0, 4294967295, true)
      if not hash then return false, "enemy_args" end
      for _, s in ipairs(soldiers()) do if s.hash == hash then
        if cmd == "enemy_enable" and not usable(s) then return false, "enemy_terminal_unknown" end
        call(TppEnemy, cmd == "enemy_enable" and "SetEnable" or "SetDisable", s.id)
        return true, "submitted_local;ent=" .. fmt("%.0f", hash) .. ";" .. share(cmd == "enemy_enable" and 8 or 7, s)
      end end
      return false, "enemy_not_authored"
    elseif cmd == "enemy_aim_nearest" then
      -- M7 test lever (docs/native-puppet.md "Seat gun"): this copy's camera toward the nearest living soldier within
      -- 60 m (chest 1.1 m up, from a 1.5 m eye) through the stock Player.RequestToSetCameraRotation; nothing else.
      if #a ~= 1 then return false, "enemy_args" end
      if type(get(Player, "RequestToSetCameraRotation")) ~= "function" then return false, "camera_unavailable" end
      local p, target = pose(), nil
      for _, s in ipairs(nearby(60)) do if usable(s) then target = s; break end end
      local v = target and position(target)
      if not v then return false, "no_readable_soldier" end
      local dx, dz, dy = v[1] - p[1], v[3] - p[3], (v[2] + 1.1) - (p[2] + 1.5)
      local yaw, pitch = math.deg(math.atan2(dx, dz)), math.deg(math.atan2(-dy, math.sqrt(dx * dx + dz * dz)))
      call(Player, "RequestToSetCameraRotation", {rotX = pitch, rotY = yaw, interpTime = 0.2})
      return true, fmt("aim_nearest;ent=%.0f;d=%.1f;rotY=%.1f;rotX=%.1f;%s", target.hash, math.sqrt(target.distance), yaw, pitch, A.sync("enemy"))
    elseif cmd == "enemy_nearest" or cmd == "enemy_enable_nearest" or cmd == "enemy_disable_nearest" or cmd == "enemy_kill_nearest" or cmd == "clear_enemies" then
      if cmd == "clear_enemies" then
        if #a ~= 2 then return false, "radius_args" end
      elseif #a ~= 1 then return false, "enemy_args" end
      if cmd ~= "enemy_nearest" and not hostOnly() then return false, "host_only" end
      local radius = 150
      if cmd == "clear_enemies" then radius = number(a[2], 1, 100) end
      if not radius then return false, "radius_args" end
      local list = nearby(radius)
      if cmd == "enemy_kill_nearest" or cmd == "clear_enemies" or cmd == "enemy_enable_nearest" then
        local living = {}
        for _, s in ipairs(list) do if usable(s) then living[#living + 1] = s end end
        list = living
      end
      if #list == 0 then return false, "no_readable_soldier" end
      if cmd == "enemy_nearest" then return true, fmt("nearest_ent=%.0f;distance_m=%.3f", list[1].hash, math.sqrt(list[1].distance)) end
      if cmd == "enemy_enable_nearest" or cmd == "enemy_disable_nearest" then
        if cmd == "enemy_enable_nearest" and not usable(list[1]) then return false, "enemy_terminal_unknown" end
        call(TppEnemy, cmd == "enemy_enable_nearest" and "SetEnable" or "SetDisable", list[1].id)
        return true, "submitted_local;ent=" .. fmt("%.0f", list[1].hash) .. ";" .. share(cmd == "enemy_enable_nearest" and 8 or 7, list[1])
      end
      local dead = get(TppGameObject, "NPC_LIFE_STATE_DEAD")
      if not integer(dead, 1, 4) then return false, "missing_dead_enum" end
      local n = cmd == "clear_enemies" and math.min(#list, 16) or 1
      local readback = 0
      for i = 1, n do
        call(GameObject, "SendCommand", list[i].id, {id = "ChangeLifeState", state = dead})
        if call(GameObject, "SendCommand", list[i].id, {id = "GetLifeStatus"}) == dead then readback = readback + 1 end
      end
      return true, fmt("submitted=%d;dead_readback=%d;%s;max=16", n, readback, A.sync("enemy"))
    end
    return false, "command_args"
  end
end
