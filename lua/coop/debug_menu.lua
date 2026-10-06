-- Original copied-save/offline actions on natural Lua updates, never a console.
-- Public game contracts are exercised by debug_menu_game_lua_check.lua.
do
  local update, terminate = e.OnUpdate, e.OnTerminate
  local D = {bookmarks = {}, infinite = false}
  e.MgsCoopDebug = D
  local fmt, floor, abs = string.format, math.floor, math.abs
  local function get(t, k)
    if t == nil then return nil end
    local ok, v = pcall(function() return t[k] end)
    if ok then return v end
  end
  local function call(t, k, ...)
    local f = get(t, k)
    if type(f) ~= "function" then error("missing_" .. k, 0) end
    return f(...)
  end
  local function finite(n) return type(n) == "number" and n == n and abs(n) < 1e15 end
  local function integer(n, lo, hi) return finite(n) and n == floor(n) and n >= lo and n <= hi end
  local function receipt(text)
    local f = loadstring(text)
    if type(f) == "function" then return f() end
  end
  local function note(id, ok, msg)
    msg = tostring(msg):gsub("[^%w_=,.;:%-]", "_"):sub(1, 160)
    receipt("return 'MGS_COOP_DEBUG_LOG_V1' -- " .. fmt("%.0f %d %s", id, ok and 1 or 0, msg))
  end
  local function state()
    local mask = 0
    for i = 0, 7 do if D.bookmarks[i] then mask = mask + 2 ^ i end end
    receipt("return 'MGS_COOP_DEBUG_STATE_V1' -- " .. fmt("%d %d", D.infinite and 1 or 0, mask))
  end
  function D.stop()
    if D.ammoTouched then pcall(call, Player, "SetInfiniteAmmoFromScript", false) end
    if D.weatherTouched then pcall(call, TppWeather, "CancelForceRequestWeather") end
    D.bookmarks, D.infinite, D.ammoTouched, D.weatherTouched, D.pending, D.mission = {}, false, nil, nil, nil, nil
    pcall(state)
  end
  local function guard()
    local mission = get(vars, "missionCode")
    if mission ~= 30010 and mission ~= 30020 and mission ~= 30050 then return false, "mission" end
    if call(TppMission, "CheckMissionState") ~= true then
      return false, "gameplay"
    end
    if call(DemoDaemon, "IsDemoPlaying") ~= false then return false, "demo" end
    if call(Player, "IsInstanceValid") ~= true then return false, "player" end
    if call(Tpp, "IsHelicopter", get(vars, "playerVehicleGameObjectId")) == true then return false, "helicopter" end
    local statuses = {get(PlayerStatus, "STAND"), get(PlayerStatus, "SQUAT"), get(PlayerStatus, "CRAWL")}
    for i = 1, 3 do if type(statuses[i]) ~= "number" then return false, "missing_posture" end end
    if call(PlayerInfo, "OrCheckStatus", statuses) ~= true then return false, "on_foot" end
    return true, mission
  end
  local function hostOnly()
    return D.host == true
  end
  local function pose()
    local p = {get(vars, "playerPosX"), get(vars, "playerPosY"), get(vars, "playerPosZ"), get(vars, "playerRotY")}
    for i = 1, 4 do if not finite(p[i]) then error("position_unreadable", 0) end end
    return p
  end
  local function number(s, lo, hi, whole)
    if type(s) ~= "string" or not s:match("^%-?%d+%.?%d*$") then return nil end
    local n = tonumber(s)
    if not finite(n) or n < lo or n > hi or (whole and n ~= floor(n)) then return nil end
    return n
  end
  local function tokens(text)
    if type(text) ~= "string" or #text == 0 or #text > 200 or text:find("[^ -~]") then return nil end
    local a = {}
    for s in text:gmatch("%S+") do a[#a + 1] = s; if #a > 16 then return nil end end
    return a
  end
  D.api = {get = get, call = call, number = number, integer = integer, pose = pose, note = note}
  local function warp(p, id)
    call(TppPlayer, "Warp", {pos = {p[1], p[2], p[3]}, rotY = p[4]})
    D.pending = {kind = "warp", id = id, pose = p, mission = D.mission, frames = 0}
    return true, "submitted_warp"
  end
  -- Only authored soldiers; nearest actions require position/life readback.
  local function soldiers()
    local defs, list, seen = get(mvars, "ene_soldierDefine"), {}, {}
    if type(defs) ~= "table" then return list end
    local null = get(GameObject, "NULL_ID")
    if type(null) ~= "number" then return list end
    for _, group in pairs(defs) do
      if type(group) == "table" then
        for _, name in ipairs(group) do
          if type(name) == "string" and not seen[name] and #list < 512 then
            seen[name] = true
            local id = call(GameObject, "GetGameObjectId", "TppSoldier2", name)
            if integer(id, 0, 65535) and id ~= null then
              local h = call(Fox, "StrCode32", name)
              if integer(h, -2147483648, 4294967295) then
                if h < 0 then h = h + 4294967296 end
                list[#list + 1] = {id = id, name = name, hash = h}
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
  local WEATHER = {sunny = "SUNNY", cloudy = "CLOUDY", rainy = "RAINY", sandstorm = "SANDSTORM", foggy = "FOGGY"}
  local function run(a, id)
    local cmd = a[1]
    if cmd == "position" and #a == 1 then
      local p = pose()
      return true, fmt("position=%.3f,%.3f,%.3f;yaw=%.3f", p[1], p[2], p[3], p[4])
    elseif (cmd == "bookmark" or cmd == "return") and #a == 2 then
      local slot = number(a[2], 0, 7, true)
      if not slot then return false, "slot" end
      if cmd == "bookmark" then D.bookmarks[slot] = pose(); return true, "bookmarked_" .. a[2] end
      if not D.bookmarks[slot] then return false, "empty_bookmark" end
      return warp(D.bookmarks[slot], id)
    elseif cmd == "teleport" and #a == 5 then
      local p = {}
      for i = 1, 3 do p[i] = number(a[i + 1], -100000, 100000) end
      p[4] = number(a[5], -36000, 36000)
      for i = 1, 4 do if not p[i] then return false, "position_args" end end
      return warp(p, id)
    elseif cmd == "step" and #a == 4 then
      local p = pose()
      for i = 1, 3 do
        local n = number(a[i + 1], -100, 100)
        if not n then return false, "step_args" end
        p[i] = p[i] + n
      end
      return warp(p, id)
    elseif cmd == "infinite_ammo" and #a == 2 then
      local n = number(a[2], 0, 1, true)
      if not n then return false, "ammo_args" end
      D.ammoTouched = true
      if call(Player, "SetInfiniteAmmoFromScript", n == 1) == false then return false, "ammo_refused" end
      D.infinite = n == 1
      return true, "infinite_ammo_" .. a[2]
    elseif cmd == "inventory" or cmd == "weapon" or cmd == "drop_item" then
      if not D.equipment then return false, "equipment_unavailable" end
      return D.equipment.run(a, id)
    elseif cmd == "time" and #a == 2 then
      if not hostOnly() then return false, "host_only" end
      local t = number(a[2], 0, 86399, true)
      if not t then return false, "time_args" end
      call(TppClock, "SetTime", fmt("%02d:%02d:%02d", floor(t / 3600), floor(t / 60) % 60, t % 60))
      return get(vars, "clock") == t, "time_readback"
    elseif cmd == "weather" and #a == 2 then
      if not hostOnly() then return false, "host_only" end
      if a[2] == "auto" then
        call(TppWeather, "CancelForceRequestWeather")
        D.weatherTouched = nil
        return true, "submitted_weather_auto"
      end
      local t = get(get(TppDefine, "WEATHER"), WEATHER[a[2]])
      if not integer(t, 0, 5) then return false, "weather_args" end
      if D.mission == 30020 and a[2] == "foggy" then return false, "africa_fog_unsupported" end
      D.weatherTouched = true
      call(TppWeather, "ForceRequestWeather", t, 1)
      return true, "submitted_weather_" .. a[2]
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
    elseif cmd == "partner" and #a >= 2 then
      local keys = {stage = true, source = true, apply = true, anim = true, aset = true, reset = true,
        look = true, dm = true, input = true, dmplay = true, dmstop = true}
      local seen = {}
      local fields = {}
      for i = 2, #a do
        local key, value = a[i]:match("^([a-z_]+)=([a-z0-9_.%,%-]+)$")
        if not key or not keys[key] or seen[key] then return false, "partner_args" end
        seen[key] = true
        fields[#fields + 1] = key .. "=" .. value
      end
      local payload = table.concat(fields, " ")
      if #payload > 160 then return false, "partner_args" end
      local ok = receipt("return 'MGS_COOP_DEBUG_PARTNER_V1' -- " .. fmt("%.0f %s", id, payload)) == true
      return ok, ok and "partner_applied" or "partner_refused"
    elseif cmd == "heal" or cmd == "godmode" or cmd == "spawn_enemy" then
      return false, "unsupported_public_api"
    end
    return false, "command_args"
  end
  local function pending()
    local q = D.pending
    if not q then return end
    local ok, mission = guard()
    if not ok or mission ~= q.mission then D.pending = nil; note(q.id, false, "action_context_changed"); return end
    q.frames = q.frames + 1
    if q.kind ~= "warp" then q.poll(); return end
    local p, target = pose(), q.pose
    local d = (p[1] - target[1]) ^ 2 + (p[2] - target[2]) ^ 2 + (p[3] - target[3]) ^ 2
    if d <= 1 then
      D.pending = nil
      note(q.id, true, fmt("warp_position_readback;error_m=%.3f", math.sqrt(d)))
    elseif q.frames >= 300 then D.pending = nil; note(q.id, false, "warp_readback_timeout") end
  end
  function D.update()
    local mission = get(vars, "missionCode")
    if D.mission and D.mission ~= mission then D.stop() end
    D.mission = mission
    local ok, failure = pcall(pending)
    if not ok then D.pending = nil; pcall(note, 0, false, "pending_binding_error") end
    local command = receipt("return 'MGS_COOP_DEBUG_CMD_V1'")
    if type(command) ~= "table" then return end
    local id = get(command, "id")
    if not integer(id, 1, 4294967295) then return end
    if D.lastId then
      local delta = (id - D.lastId) % 4294967296
      if delta == 0 or delta >= 2147483648 then note(id, false, "stale_command"); return end
    end
    D.lastId = id
    D.host = get(command, "host") == true
    local a = tokens(get(command, "text"))
    if not a then note(id, false, "command_text"); return end
    if get(command, "mission") ~= nil and get(command, "mission") ~= mission then
      note(id, false, "stale_mission"); return
    end
    if D.pending then note(id, false, "busy_action"); return end
    local good, why
    ok, failure = pcall(function()
      good, why = guard()
      if good then good, why = run(a, id) end
    end)
    if not ok then
      good = false
      why = type(failure) == "string" and failure:match("^missing_%w+$") and failure or "binding_error"
    end
    note(id, good, why)
    state()
  end
  local function updated(...)
    pcall(D.update)
    return ...
  end
  if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
  if type(terminate) == "function" then e.OnTerminate = function(...) pcall(D.stop); return terminate(...) end end
end
