-- Original local player debug actions on the owner update. Unverified script
-- APIs are called through call() and say "unverified" in their results.
do
  local D, A = e.MgsCoopDebug, e.MgsCoopDebug.api
  local get, call, number, fmt, floor = A.get, A.call, A.number, string.format, math.floor
  local P = {handles = {heal = true, godmode = true, fulton_infinite = true, forward = true, goto_partner = true,
    slowmo = true, time_add = true}}
  D.player = P
  local function fill()
    local l, m = A.life()
    if l > 0 and l < m then vars.playerLife = m end
  end
  -- Auto-heal: before command intake, only in live supported gameplay, never at 0 life.
  function P.tick()
    if D.slowmo then D.slowmo = D.slowmo - 1; if D.slowmo <= 0 then D.slowmo = nil end end
    if not D.god then return end
    local m = get(vars, "missionCode")
    if (m ~= 30010 and m ~= 30020 and m ~= 30050) or call(TppMission, "CheckMissionState") ~= true
      or call(DemoDaemon, "IsDemoPlaying") ~= false or call(Player, "IsInstanceValid") ~= true then return end
    fill()
  end
  function P.stop()
    if D.fultonTouched then pcall(call, Player, "SetFultonCountInfinity", false) end
    if D.slowmo then pcall(call, HighSpeedCamera, "RequestToCancel") end
    D.god, D.fulton, D.fultonTouched, D.slowmo = nil, nil, nil, nil
  end
  local update = D.update
  D.update = function(...)
    pcall(P.tick)
    return update(...)
  end
  local function toggle(a)
    if #a ~= 2 then return nil end
    return number(a[2], 0, 1, true)
  end
  function P.run(a, id)
    local cmd = a[1]
    if cmd == "heal" and #a == 1 then
      local l, m = A.life()
      if l < 0 then return false, "life_vars_unreadable" end
      if l == 0 then return false, "life_dead" end
      vars.playerLife = m
      local r = get(vars, "playerLife")
      return r == m, fmt("heal;life=%d,%d;unverified", A.integer(r, 0, 1e8) and r or -1, m)
    elseif cmd == "godmode" then
      local n = toggle(a)
      if not n then return false, "godmode_args" end
      D.god = n == 1 or nil
      return true, D.god and "godmode_1;autoheal;lethal_unprotected;unverified" or "godmode_0"
    elseif cmd == "fulton_infinite" then
      local n = toggle(a)
      if not n then return false, "fulton_args" end
      D.fultonTouched = true
      call(Player, "SetFultonCountInfinity", n == 1)
      D.fulton = n == 1 or nil
      return true, "fulton_infinite_" .. a[2] .. ";unverified"
    elseif cmd == "slowmo" then
      if #a == 2 and a[2] == "off" then
        call(HighSpeedCamera, "RequestToCancel")
        D.slowmo = nil
        return true, "slowmo_off"
      end
      local r, s = number(a[2], 0.05, 1), number(a[3], 0.1, 30)
      if #a ~= 3 or not r or not s then return false, "slowmo_args" end
      call(HighSpeedCamera, "RequestEvent", {continueTime = s, worldTimeRate = r, localPlayerTimeRate = r})
      -- Indicator only: cleared after a generous update count or by slowmo off.
      D.slowmo = floor(s * 60) + 1
      return true, fmt("submitted_slowmo;rate=%.2f;seconds=%.1f;experimental", r, s)
    elseif cmd == "forward" and #a == 2 then
      local m = number(a[2], -100, 100)
      if not m then return false, "forward_args" end
      local p = A.pose()
      local y = math.rad(p[4])
      p[1], p[3] = p[1] + m * math.sin(y), p[3] + m * math.cos(y)
      return A.warp(p, id)
    elseif cmd == "goto_partner" and #a == 1 then
      local pp = D.partnerPose
      if type(pp) ~= "table" then return false, "partner_pose_unavailable" end
      local t = {}
      for i = 1, 3 do
        local v = get(pp, i)
        if not A.integer(v, -100000000, 100000000) then return false, "partner_pose_bounds" end
        t[i] = v / 1000
      end
      local age = get(pp, 4)
      if not A.integer(age, 0, 1000) then return false, "partner_pose_unavailable" end
      local me = A.pose()
      local dx, dz = me[1] - t[1], me[3] - t[3]
      local len = math.sqrt(dx * dx + dz * dz)
      if len < 0.1 then dx, dz, len = 1, 0, 1 end
      local x, z = t[1] + 2 * dx / len, t[3] + 2 * dz / len
      if math.abs(x) > 100000 or math.abs(t[2]) > 99999 or math.abs(z) > 100000 then return false, "partner_pose_bounds" end
      local ok, msg = A.warp({x, t[2] + 0.3, z, math.deg(math.atan2(t[1] - x, t[3] - z))}, id)
      return ok, fmt("%s;via=partner;age_ms=%d", msg, age)
    elseif cmd == "time_add" and #a == 2 then
      if not A.hostOnly() then return false, "host_only" end
      local s = number(a[2], -86400, 86400, true)
      if not s then return false, "time_args" end
      local c = get(vars, "clock")
      if not A.integer(c, 0, 86399) then return false, "clock_unreadable" end
      return A.setClock((c + s) % 86400)
    end
    return false, "command_args"
  end
end
