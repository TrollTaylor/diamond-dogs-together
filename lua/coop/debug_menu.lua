-- Original copied-save/offline actions on natural Lua updates, never a console.
-- Public game contracts are exercised by debug_menu_game_lua_check.lua.
do
  local update, terminate = e.OnUpdate, e.OnTerminate
  local D = {bookmarks = {}, infinite = false, tick = 0}
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
  -- LOG_V1 codes: 0 refused (final), 1 ok (final), 2 ok (interim, a readback follows).
  local function note(id, ok, msg, interim)
    msg = tostring(msg):gsub("[^%w_=,.;:%-]", "_"):sub(1, 160)
    receipt("return 'MGS_COOP_DEBUG_LOG_V1' -- " .. fmt("%.0f %d %s", id, ok and (interim and 2 or 1) or 0, msg))
  end
  local function bindingError(prefix, failure)
    if type(failure) ~= "string" then return prefix end
    return prefix .. ";detail=" .. failure:gsub("^.-:%d+: ", ""):sub(1, 120)
  end
  local function supported(m) return m == 30010 or m == 30020 or m == 30050 end
  local function guard()
    local mission = get(vars, "missionCode")
    if not supported(mission) then return false, "mission" end
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
  local function reason()
    local ok, good, why = pcall(guard)
    if not ok then return "binding" end
    if good then return "ok" end
    return type(why) == "string" and #why < 24 and why:match("^[a-z_]+$") and why or "binding"
  end
  local function life()
    local a, b = get(vars, "playerLife"), get(vars, "playerLifeMax")
    if not integer(a, 0, 100000000) or not integer(b, 1, 100000000) then return -1, -1 end
    return a, b
  end
  local function clock()
    local c = get(vars, "clock")
    return integer(c, 0, 86399) and c or 0
  end
  -- STATE_V1, 12 fields: on change at most every 5 updates, forced every 30.
  local function state(force)
    D.tick = D.tick + 1
    if force or not D.reason or D.tick % 30 == 1 then D.reason = reason() end
    local mask = 0
    for i = 0, 7 do if D.bookmarks[i] then mask = mask + 2 ^ i end end
    local q, l, m = D.pending, life()
    local text = fmt("%d %d %.0f %s %d %d %d %d %d %d %d %s", D.infinite and 1 or 0, mask, q and q.id or 0, q and q.kind or "none",
      q and q.frames or 0, D.god and 1 or 0, D.fulton and 1 or 0, D.slowmo and 1 or 0, clock(), l, m, D.reason)
    local since = D.tick - (D.sent or -100)
    if force or since >= 30 or (text ~= D.last and since >= 5) then
      D.last, D.sent = text, D.tick
      receipt("return 'MGS_COOP_DEBUG_STATE_V1' -- " .. text)
    end
  end
  function D.stop()
    if D.ammoTouched then pcall(call, Player, "SetInfiniteAmmoFromScript", false) end
    if D.weatherTouched then pcall(call, TppWeather, "CancelForceRequestWeather") end
    for _, k in ipairs({"enemy", "player", "spawn"}) do if D[k] then pcall(D[k].stop) end end
    D.bookmarks, D.infinite, D.ammoTouched, D.weatherTouched, D.pending, D.mission = {}, false, nil, nil, nil, nil
    D.equipmentObservationMission = nil
    D.equipmentObservationFrames, D.equipmentObservationAttempts = nil, nil
    pcall(state, true)
  end
  local function hostOnly()
    return D.host == true
  end
  -- Sync tag: read-only view of the world link. Enemy outcomes need W1;
  -- clock/weather follow at an active world level of 3.
  local function sync(kind)
    local W = get(e, "MgsCoopWorld")
    local eff = tonumber(get(W, "eff")) or 0
    local on = get(W, "paired") == true and (kind == "env" and eff >= 3 or kind ~= "env" and get(W, "w1") == true)
    return on and "sync=world" or "sync=none"
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
  local function setClock(t)
    call(TppClock, "SetTime", fmt("%02d:%02d:%02d", floor(t / 3600), floor(t / 60) % 60, t % 60))
    return get(vars, "clock") == t, "time_readback;" .. sync("env")
  end
  D.api = {get = get, call = call, number = number, integer = integer, pose = pose, note = note,
    finite = finite, hostOnly = hostOnly, receipt = receipt, sync = sync, life = life, setClock = setClock}
  local function warp(p, id)
    call(TppPlayer, "Warp", {pos = {p[1], p[2], p[3]}, rotY = p[4]})
    D.pending = {kind = "warp", id = id, pose = p, mission = D.mission, frames = 0}
    return true, "submitted_warp"
  end
  D.api.warp = warp
  local WEATHER = {sunny = "SUNNY", cloudy = "CLOUDY", rainy = "RAINY", sandstorm = "SANDSTORM", foggy = "FOGGY"}
  local function run(a, id)
    local cmd = a[1]
    for _, m in ipairs({D.player, D.spawn}) do
      if m and m.handles[cmd] then return m.run(a, id) end
    end
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
      return setClock(t)
    elseif cmd == "weather" and #a == 2 then
      if not hostOnly() then return false, "host_only" end
      if a[2] == "auto" then
        call(TppWeather, "CancelForceRequestWeather")
        D.weatherTouched = nil
        return true, "submitted_weather_auto;" .. sync("env")
      end
      local t = get(get(TppDefine, "WEATHER"), WEATHER[a[2]])
      if not integer(t, 0, 5) then return false, "weather_args" end
      if D.mission == 30020 and a[2] == "foggy" then return false, "africa_fog_unsupported" end
      D.weatherTouched = true
      call(TppWeather, "ForceRequestWeather", t, 1)
      return true, "submitted_weather_" .. a[2] .. ";" .. sync("env")
    elseif cmd:match("^enemy_") or cmd == "clear_enemies" or cmd == "goto_spawn" or cmd == "goto_camp" then
      if not D.enemy then return false, "enemy_unavailable" end
      return D.enemy.run(a, id)
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
    end
    return false, "command_args"
  end
  -- Read-only report; needs only a supported mission, so it also answers while seated.
  local function status()
    if not supported(D.mission) then return false, "mission" end
    local q, l, m = D.pending, life()
    return true, fmt("status;mission=%d;guard=%s;host=%d;pending=%s;god=%d;ammo=%d;fulton=%d;slowmo=%d;clock=%d;life=%d,%d",
      D.mission, reason(), D.host and 1 or 0, q and q.kind or "none", D.god and 1 or 0, D.infinite and 1 or 0,
      D.fulton and 1 or 0, D.slowmo and 1 or 0, clock(), l, m)
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
    local q = D.pending
    local ok, failure = pcall(pending)
    if not ok then D.pending = nil; pcall(note, q and q.id or 0, false, bindingError("pending_binding_error", failure)) end
    -- A pending action polls: telemetry stays live and commands wait in the native queue.
    if D.pending then receipt("return 'MGS_COOP_DEBUG_POLL_V1'"); pcall(state); return end
    local command = receipt("return 'MGS_COOP_DEBUG_CMD_V1'")
    if type(command) ~= "table" then pcall(state); return end
    local id = get(command, "id")
    if not integer(id, 1, 4294967295) then return end
    if D.lastId then
      local delta = (id - D.lastId) % 4294967296
      if delta == 0 or delta >= 2147483648 then note(id, false, "stale_command"); return end
    end
    D.lastId = id
    D.host = get(command, "host") == true
    D.partnerPose = get(command, "pp")
    local a = tokens(get(command, "text"))
    if not a then note(id, false, "command_text"); return end
    if get(command, "mission") ~= nil and get(command, "mission") ~= mission then
      note(id, false, "stale_mission"); return
    end
    local good, why
    ok, failure = pcall(function()
      if a[1] == "status" and #a == 1 then good, why = status(); return end
      good, why = guard()
      if good then good, why = run(a, id) end
    end)
    if not ok then
      good = false
      why = type(failure) == "string" and failure:match("^missing_%w+$") and failure or bindingError("binding_error", failure)
    end
    note(id, good, why, good and D.pending ~= nil and D.pending.id == id)
    pcall(state, true)
  end
  local function updated(...)
    pcall(D.update)
    return ...
  end
  if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
  if type(terminate) == "function" then e.OnTerminate = function(...) pcall(D.stop); return terminate(...) end end
end
