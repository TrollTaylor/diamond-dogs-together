-- Original local player debug actions on the owner update. Unverified script
-- APIs are called through call() and say "unverified" in their results.
do
  local D, A = e.MgsCoopDebug, e.MgsCoopDebug.api
  local get, call, number, fmt, floor = A.get, A.call, A.number, string.format, math.floor
  local P = {handles = {heal = true, godmode = true, fulton_infinite = true, forward = true, goto_partner = true,
    goto_carry = true, goto_horse = true, goto_vehicle = true, slowmo = true, time_add = true,
    overlay_state = true, overlay_stop = true}}
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
    elseif cmd == "goto_carry" and #a == 1 then
      -- 0.9 m in front of the teammate's stock carry point (native "cp"), facing it.
      local cp = D.carryPoint
      if type(cp) ~= "table" then return false, "carry_point_unavailable" end
      local t = {}
      for i = 1, 3 do
        local v = get(cp, i)
        if not A.integer(v, -100000000, 100000000) then return false, "carry_point_bounds" end
        t[i] = v / 1000
      end
      local me = A.pose()
      local dx, dz = me[1] - t[1], me[3] - t[3]
      local len = math.sqrt(dx * dx + dz * dz)
      if len < 0.1 then dx, dz, len = 1, 0, 1 end
      local x, z = t[1] + 0.9 * dx / len, t[3] + 0.9 * dz / len
      local ok, msg = A.warp({x, t[2] + 0.4, z, math.deg(math.atan2(t[1] - x, t[3] - z))}, id)
      return ok, fmt("%s;via=carry_point", msg)
    elseif cmd == "goto_horse" and #a == 1 then
      -- 1.6 m beside the local D-Horse, facing it: stock GetPosition on the
      -- horse, else the ride pose census, else the last D-Horse call goal.
      local W = e.MgsCoopWorld
      local t, via
      local okp, pos = pcall(GameObject.SendCommand, 6656, {id = "GetPosition"})
      if okp and pos ~= nil and (type(pos) == "userdata" or type(pos) == "table") then
        local r2, x, y, z = pcall(function() return pos:GetX(), pos:GetY(), pos:GetZ() end)
        if r2 and A.finite(x) and A.finite(y) and A.finite(z) and (x ~= 0 or z ~= 0) then t, via = {x, y, z}, "get_position" end
      end
      if not t and type(W) == "table" and type(W.receipt) == "function" then
        local ok, r = pcall(W.receipt, fmt("return 'MGS_COOP_RIDE_POSE_V1' -- %d %d 1", W.mission or 0, W.epoch or 0))
        if ok and type(r) == "table" and r.ready == true and type(r.actors) == "table" then
          for i = 1, #r.actors do
            local h = r.actors[i]
            if type(h) == "table" and h.realized == true and h.id == 6656 and A.integer(h.x, -1000000000, 1000000000) and
              A.integer(h.y, -1000000000, 1000000000) and A.integer(h.z, -1000000000, 1000000000) then t, via = {h.x / 1000, h.y / 1000, h.z / 1000}, "census" end
          end
        end
      end
      local B = D.buddy
      if not t and type(B) == "table" and type(B.lastGoal) == "table" then t, via = B.lastGoal, "call_goal" end
      if not t then return false, "horse_position_unknown" end
      local me = A.pose()
      local dx, dz = me[1] - t[1], me[3] - t[3]
      local len = math.sqrt(dx * dx + dz * dz)
      if len < 0.1 then dx, dz, len = 1, 0, 1 end
      local x, z = t[1] + 1.6 * dx / len, t[3] + 1.6 * dz / len
      local ok2, msg = A.warp({x, t[2] + 0.4, z, math.deg(math.atan2(t[1] - x, t[3] - z))}, id)
      return ok2, fmt("%s;via=%s;horse=%.2f,%.2f,%.2f", msg, via, t[1], t[2], t[3])
    elseif cmd == "goto_vehicle" and (#a == 1 or (#a == 2 and (a[2] == "jeep" or a[2] == "sortie"))) then
      -- 3 m beside the nearest authored free-roam vehicle (TppVehicle2 GetPosition);
      -- "sortie": the nearest deployed/sortie vehicle 25600..25607 instead (no
      -- authored locator: the WS3 seat-by-id case, docs/vehicle-ws3.md).
      local me, best, bestD, bestName = A.pose(), nil, nil, nil
      local names = {}
      if a[2] == "sortie" then
        for i = 0, 7 do names[#names + 1] = {fmt("sortie_%d", i), 25600 + i} end
      else
        for i = 0, 12 do names[#names + 1] = fmt("veh_lv_%04d", i) end
        if a[2] ~= "jeep" then for i = 0, 5 do names[#names + 1] = fmt("veh_trc_%04d", i) end end
      end
      for _, entry in ipairs(names) do
        local name, okId, vid = entry, false, nil
        if type(entry) == "table" then name, okId, vid = entry[1], true, entry[2]
        else okId, vid = pcall(GameObject.GetGameObjectId, "TppVehicle2", name) end
        if okId and type(vid) == "number" and vid >= 0 and vid < 65535 then
          local okp, pos, rot = pcall(GameObject.SendCommand, vid, {id = "GetPosition"})
          if okp and pos ~= nil then
            local r2, x, y, z = pcall(function() return pos:GetX(), pos:GetY(), pos:GetZ() end)
            if r2 and A.finite(x) and A.finite(y) and A.finite(z) then
              local d2 = (x - me[1]) ^ 2 + (z - me[3]) ^ 2
              if not bestD or d2 < bestD then best, bestD, bestName = {x, y, z, A.finite(rot) and rot or 0, vid}, d2, name end
            end
          end
        end
      end
      if not best then return false, "vehicle_none" end
      local s = best[4] + math.pi / 2
      local x, z = best[1] + 1.9 * math.sin(s), best[3] + 1.9 * math.cos(s)
      local W = e.MgsCoopWorld
      if type(W) == "table" and type(W.log) == "function" then pcall(W.log, fmt("goto_vehicle;name=%s;id=%d;dist=%.0f", bestName, best[5], math.sqrt(bestD))) end
      local ok2, msg = A.warp({x, best[2] + 0.5, z, math.deg(math.atan2(best[1] - x, best[3] - z))}, id)
      return ok2, fmt("%s;vehicle=%s;id=%d;dist=%.0f", msg, bestName, best[5], math.sqrt(bestD))
    elseif cmd == "overlay_state" and #a >= 2 and #a <= 5 then
      -- The partner's DM overlay lever (docs/anim-round1-actions.md): overlay_state <state> [[loop] 0|1] [ms].
      -- Native checks the catalogue and lab.ini partner_native_overlay=debug|map; the result lines are partner_overlay=.
      local name, i, fields = a[2], 3, nil
      if #name > 40 or not name:match("^[A-Za-z_][A-Za-z0-9_]*$") then return false, "overlay_args" end
      fields = "overlay=" .. name
      if a[i] == "loop" then i = i + 1 end
      if a[i] then
        local loop = number(a[i], 0, 1, true)
        if not loop then return false, "overlay_args" end
        fields, i = fields .. fmt(" oloop=%d", loop), i + 1
      end
      if a[i] then
        local ms = number(a[i], 100, 60000, true)
        if not ms then return false, "overlay_args" end
        fields, i = fields .. fmt(" oms=%d", ms), i + 1
      end
      if a[i] then return false, "overlay_args" end
      local ok = A.receipt("return 'MGS_COOP_DEBUG_PARTNER_V1' -- " .. fmt("%.0f %s", id, fields)) == true
      return ok, ok and ("overlay_queued;state=" .. name) or "overlay_refused"
    elseif cmd == "overlay_stop" and #a == 1 then
      local ok = A.receipt("return 'MGS_COOP_DEBUG_PARTNER_V1' -- " .. fmt("%.0f overlay=stop", id)) == true
      return ok, ok and "overlay_stop_queued" or "overlay_refused"
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
