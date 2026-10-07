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
  function E.stop() E.used, E.last = {}, nil end
  local function spawn(c, route, id)
    if not c or not route then return false, "spawn_camp_route" end
    local count = 0
    for _ in pairs(E.used) do count = count + 1 end
    if count >= 8 then return false, "spawn_pool_limit_8" end
    local s
    local normal = get(get(TppEnemy, "LIFE_STATUS"), "NORMAL")
    if not integer(normal, 0, 4) then return false, "missing_normal_enum" end
    for _, candidate in ipairs(c.guards) do
      if not E.used[candidate.name] and candidate.distance <= 150 * 150
        and call(GameObject, "SendCommand", candidate.id, {id = "GetLifeStatus"}) == normal then s = candidate; break end
    end
    if not s then return false, "spawn_no_loaded_living_guard" end
    E.used[s.name] = true
    -- Retail TppDebug's route placement, never an unknown XYZ Warp/Spawn.
    local ok = pcall(function()
      call(TppEnemy, "SetDisable", s.id)
      call(GameObject, "SendCommand", s.id, {id = "SetSneakRoute", route = route, point = 0})
      call(GameObject, "SendCommand", s.id, {id = "SetCautionRoute", route = route, point = 0})
      call(GameObject, "SendCommand", s.id, {id = "SetAlertRoute", enabled = false, route = "", point = 0})
      call(TppEnemy, "SetEnable", s.id)
      call(GameObject, "SendCommand", s.id, {id = "SetForceRealize"})
    end)
    if not ok then pcall(call, TppEnemy, "SetEnable", s.id); return false, "spawn_binding_error" end
    local q = {kind = "enemy", id = id, mission = D.mission, frames = 0}
    q.poll = function()
      if q.frames < 2 then return end
      if not usable(s) then D.pending = nil; A.note(id, false, "spawn_terminal_unknown"); return end
      local v = position(s)
      if v then
        local p, before = pose(), s.position
        local distance = math.sqrt((v[1] - p[1]) ^ 2 + (v[2] - p[2]) ^ 2 + (v[3] - p[3]) ^ 2)
        local moved = math.sqrt((v[1] - before[1]) ^ 2 + (v[2] - before[2]) ^ 2 + (v[3] - before[3]) ^ 2)
        E.last, D.pending = {position = v, mission = D.mission}, nil
        menu(id, fmt("spawned %.0f %.0f %.0f %.0f", math.floor(v[1] * 1000 + .5), math.floor(v[2] * 1000 + .5), math.floor(v[3] * 1000 + .5), s.hash))
        A.note(id, true, fmt("guard_position_readback;ent=%.0f;reuse=1;distance_m=%.3f;moved_m=%.3f;visible_unverified", s.hash, distance, moved))
      elseif q.frames >= 120 then D.pending = nil; A.note(id, false, "spawn_position_timeout") end
    end
    D.pending = q
    return true, fmt("submitted_patrol_guard;ent=%.0f;reuse=1;scope=local", s.hash)
  end
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
    elseif cmd == "spawn_enemy" then
      if not hostOnly() then return false, "host_only" end
      local list, byName, err = camps()
      if err then return false, err end
      if #a == 2 and a[2] == "closest" then
        return spawn(list[1], list[1] and list[1].routes[1], id)
      elseif #a == 3 and name(a[2]) then
        local index, c = number(a[3], 1, 64, true), byName[a[2]]
        return spawn(c, c and index and c.routes[index], id)
      end
      return false, "spawn_args"
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
    elseif cmd == "goto_spawn" and #a == 1 then
      local last = E.last
      if not last or last.mission ~= D.mission then return false, "spawn_position_unavailable" end
      local p = last.position
      return A.warp({p[1] + 3, p[2] + .5, p[3], pose()[4]}, id)
    elseif cmd == "enemy_enable" or cmd == "enemy_disable" then
      if #a ~= 2 then return false, "enemy_args" end
      if not hostOnly() then return false, "host_only" end
      local hash = number(a[2], 0, 4294967295, true)
      if not hash then return false, "enemy_args" end
      for _, s in ipairs(soldiers()) do if s.hash == hash then
        if cmd == "enemy_enable" and not usable(s) then return false, "enemy_terminal_unknown" end
        call(TppEnemy, cmd == "enemy_enable" and "SetEnable" or "SetDisable", s.id)
          return true, "submitted_local;ent=" .. fmt("%.0f", hash)
      end end
      return false, "enemy_not_authored"
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
        return true, "submitted_local;ent=" .. fmt("%.0f", list[1].hash)
      end
      local dead = get(TppGameObject, "NPC_LIFE_STATE_DEAD")
      if not integer(dead, 1, 4) then return false, "missing_dead_enum" end
      local n = cmd == "clear_enemies" and math.min(#list, 16) or 1
      local readback = 0
      for i = 1, n do
        call(GameObject, "SendCommand", list[i].id, {id = "ChangeLifeState", state = dead})
        if call(GameObject, "SendCommand", list[i].id, {id = "GetLifeStatus"}) == dead then readback = readback + 1 end
      end
      return true, fmt("submitted=%d;dead_readback=%d;scope=local;max=16", n, readback)
    end
    return false, "command_args"
  end
end
