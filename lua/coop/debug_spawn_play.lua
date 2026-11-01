-- Spawn enemy here for players (docs/debug-menu.md "Spawn enemy here") and cp_phase; spliced after debug_spawn.lua.
-- Mother Base has no enemies: every spawn row refuses there in plain words (spawn_mother_base).
-- Recycled pool: before a host spawn, a placed guard who is dead, asleep, fainted, Fultoned,
-- disabled, gone or more than 150 m away frees his place (an awake one may be placed again),
-- so the cap of 8 counts only placed soldiers alive near you.
do
  local D = e.MgsCoopDebug
  local S, E = D and D.spawn, D and D.enemy
  if S and E and E.api and type(S.run) == "function" then
    local A, R = D.api, E.api
    local get, call, integer = A.get, A.call, A.integer
    local inner, NEAR = S.run, 150
    local function send(id, t) return call(GameObject, "SendCommand", id, t) end
    -- True while this placed guard still holds his place in the pool.
    local function holds(name, normal, p)
      local id = call(GameObject, "GetGameObjectId", "TppSoldier2", name)
      if not integer(id, 0, 65535) or id == get(GameObject, "NULL_ID") then return false end
      local s = {id = id, name = name}
      if not R.usable(s) or send(id, {id = "GetLifeStatus"}) ~= normal then return false end
      local off = get(TppGameObject, "NPC_STATE_DISABLE")
      if off ~= nil and send(id, {id = "GetStatus"}) == off then return false end
      local v = R.position(s)
      return v ~= nil and R.dist(v, p) <= NEAR
    end
    local function recycle()
      local normal = get(get(TppEnemy, "LIFE_STATUS"), "NORMAL")
      if not integer(normal, 0, 4) then return 0 end
      local p, freed = A.pose(), 0
      local W = get(e, "MgsCoopWorld")
      local pins, ent = get(W, "pinned"), get(W, "ent")
      for name in pairs(E.used) do
        local ok, held = pcall(holds, name, normal, p)
        if not (ok and held) then
          E.used[name] = nil; freed = freed + 1
          -- Release what the placement took: his force-realize and his area pin (bounded by the pool).
          local id = call(GameObject, "GetGameObjectId", "TppSoldier2", name)
          if integer(id, 0, 65535) and id ~= get(GameObject, "NULL_ID") then pcall(send, id, {id = "SetForceRealize", forceRealize = false}) end
          if type(pins) == "table" and type(ent) == "table" then
            for h in pairs(pins) do if get(ent[h], "name") == name then pins[h] = nil end end
          end
        end
      end
      return freed
    end
    S.recycle = recycle
    -- cp_phase (moved here unchanged from debug_spawn.lua, which is at its 16000-byte limit).
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
      return true, string.format("submitted_phase;cp=%s;phase=%s;%s", cp, a[3], A.sync("enemy"))
    end
    function S.run(a, id)
      if a[1] == "cp_phase" then return phase(a) end
      if a[1] == "spawn_enemy" then
        if D.mission == 30050 then return false, "spawn_mother_base" end
        if A.hostOnly() then recycle() end
      end
      return inner(a, id)
    end
  end
end
