-- Debug menu arms (docs/debug-menu.md "Give weapon" and "Friendly fire"); loaded after debug_boot.lua.
-- Give weapon: Player.ChangeEquip, the retail s10260.GetRocketLuncher call, with full ammo, only for a gun the
-- game already loaded in this mission (a protected entry of the equip writer's last native census, read through
-- MGS_COOP_ARMS_RESIDENT_V1) or a stock support item. Development is not checked: like a gun taken from a soldier.
-- Friendly fire: the host decides for both copies (ARMS 0x3F: kind 1 a guest's ask, 2 the host's state). Off, the
-- partner's hits are dropped at both ends of FRIENDLY_HIT (world_friendly.lua reads W.arms.ff).
do
  local D = e.MgsCoopDebug
  local A = D.api
  local get, call, integer, number, fmt = A.get, A.call, A.integer, A.number, string.format
  local W = get(e, "MgsCoopWorld")
  local R = {handles = {give_weapon = true, give_pick = true, give_list = true, friendly_fire = true}, ff = true, list = {}, seq = 0}
  D.arms = R
  local OP, NOTE, ALIAS = 63, 18, 559
  if type(W) == "table" then
    W.arms = R
    if type(W.OP) == "table" then W.OP.ARMS = OP end
  end
  -- ChangeEquip's slot of a gun type (partner_slots TypeSlot): 1/5/7 hip, 4/6/8 back, 2/3 secondary.
  local SLOT = {"hip", "secondary", "secondary", "back", "hip", "back", "hip", "back"}
  local SLOTKEY = {hip = "PRIMARY_HIP", back = "PRIMARY_BACK", secondary = "SECONDARY"}
  -- The stock base support items (the common equip block, no mission-block load).
  local SUPPORT = {"Grenade", "StunGrenade", "SmokeGrenade", "SleepingGusGrenade", "Decoy", "C4", "Magazine"}
  local function wlog(s) if type(W) == "table" and type(W.log) == "function" then pcall(W.log, s) end end
  local function say(text)
    local f = get(TppUiCommand, "AnnounceLogView")
    if type(f) == "function" then pcall(f, text) end
  end
  -- The census: {mission, id, type, ...} of the protected stock guns, nil without one.
  local function resident()
    local r = A.receipt("return 'MGS_COOP_ARMS_RESIDENT_V1'")
    if type(r) ~= "table" or r[1] ~= get(vars, "missionCode") then return nil end
    local out = {}
    for i = 2, 32, 2 do
      local id, t = r[i], r[i + 1]
      if id == nil then break end
      if integer(id, 1, ALIAS) and integer(t, 1, 8) then out[#out + 1] = {id = id, type = t} end
    end
    return out
  end
  -- TppEquip names, once per mission: the shortest EQP_WP_ key of each id.
  local names, namesMission = {}, nil
  local function code(id)
    if namesMission ~= D.mission then
      names, namesMission = {}, D.mission
      pcall(function()
        for k, v in pairs(TppEquip) do
          if type(k) == "string" and k:sub(1, 7) == "EQP_WP_" and #k <= 31 and not k:find("[^%w_]") and integer(v, 1, ALIAS) then
            local s = k:sub(8)
            if not names[v] or #s < #names[v] then names[v] = s end
          end
        end
      end)
    end
    return names[id] or fmt("id_%d", id)
  end
  local function publish(force)
    local parts = {R.ff and 1 or 0, D.mission or 0, #R.list}
    for _, g in ipairs(R.list) do parts[#parts + 1] = fmt("%d.%d.%s", g.id, g.type, g.code) end
    local text = table.concat(parts, " ")
    if force or text ~= R.sent then
      R.sent = text
      A.receipt("return 'MGS_COOP_ARMS_STATE_V1' -- " .. text)
    end
  end
  local function send(kind, on)
    if type(W) ~= "table" or type(W.tx) ~= "function" then return false end
    R.seq = R.seq % 65535 + 1
    local ok, sent = pcall(W.tx, OP, R.seq, kind, on and 1 or 0)
    return ok and sent == true
  end
  -- by: self (this copy's press), partner (the host took a guest's ask), host (the guest took the host's state).
  local function apply(on, by)
    local changed = R.ff ~= on
    R.ff = on
    if (changed or by ~= "host") and type(W) == "table" and type(W.note) == "function" then pcall(W.note, NOTE, 1, on and 1 or 0) end
    if changed or by ~= "host" then wlog(fmt("event=arms;step=ff;on=%d;by=%s", on and 1 or 0, by)) end
    pcall(publish, true)
  end
  if type(W) == "table" and type(W.handlers) == "table" then
    W.handlers[OP] = function(v)
      if type(v) ~= "table" or (v[3] ~= 0 and v[3] ~= 1) then return end
      local on = v[3] == 1
      if v[2] == 1 and W.host == true then
        apply(on, "partner"); send(2, on)
      elseif v[2] == 2 and W.host ~= true then
        apply(on, "host")
      end
    end
  end
  local function give(token, id)
    if type(token) ~= "string" then return false, "give_args" end
    if type(get(Player, "ChangeEquip")) ~= "function" then return false, "missing_ChangeEquip" end
    local equip, gun, short = nil, nil, nil
    local n = token:match("^%d+$") and number(token, 1, ALIAS, true)
    if n then
      local list = resident()
      for _, g in ipairs(list or {}) do if g.id == n then gun = g end end
      if not gun then return false, list and "give_refused;reason=not_loaded" or "give_refused;reason=no_census" end
      equip = n
    else
      short = token:match("^EQP_SWP_(%w+)$")
      local known = false
      for _, s in ipairs(SUPPORT) do if s == short then known = true end end
      if not known then return false, "give_args" end
      equip = get(TppEquip, token)
      if not integer(equip, 1, 65535) then return false, "give_refused;reason=item_unknown" end
    end
    local ok, bullet, ammo, stock, sub, ammoSub, stockSub = pcall(call, TppEquip, "GetAmmoInfo", equip)
    local meta = ok and integer(bullet, 0, 65535) and integer(ammo, 0, 65535) and integer(stock, 0, 65535) and
      integer(sub, 0, 65535) and integer(ammoSub, 0, 65535) and integer(stockSub, 0, 65535)
    local label, slot, index
    if gun then
      if not meta then return false, "give_refused;reason=metadata" end
      slot = SLOT[gun.type]
      index = get(get(TppDefine, "WEAPONSLOT"), SLOTKEY[slot])
      if not integer(index, 0, 15) then return false, "weapon_slot_unavailable" end
      label = fmt("kind=gun;type=%d;slot=%s;name=%s", gun.type, slot, gun.code or code(equip))
    else
      local has, free = false, false
      for i = 0, 7 do
        local v = get(get(vars, "supportWeapons"), i)
        if v == equip then has = true elseif v == 0 then free = true end
      end
      if not has and not free then return false, "give_refused;reason=support_full" end
      -- The table's count when it has one, else 8 (unverified for support items).
      if not meta or stock == 0 then ammo, stock, ammoSub, stockSub = 0, 8, 0, 0 end
      slot, label = "support", "kind=support;name=" .. short
    end
    call(Player, "ChangeEquip", {equipId = equip, stock = stock, stockSub = stockSub, ammo = ammo, ammoSub = ammoSub,
      suppressorLife = 0, isSuppressorOn = false, isLightOn = false, toActive = gun ~= nil, dropPrevEquip = gun ~= nil})
    local q = {kind = "give", id = id, mission = D.mission, frames = 0}
    q.poll = function()
      local done = false
      if gun then done = get(get(vars, "weapons"), index) == equip
      else for i = 0, 7 do if get(get(vars, "supportWeapons"), i) == equip then done = true end end end
      if done then
        D.pending = nil
        wlog(fmt("event=arms;step=give;equip=%d;slot=%s;ok=1", equip, slot))
        A.note(id, true, fmt("give_readback;equip=%d;slot=%s", equip, slot))
      elseif q.frames >= 300 then
        D.pending = nil
        wlog(fmt("event=arms;step=give;equip=%d;slot=%s;ok=0", equip, slot))
        A.note(id, false, fmt("give_readback_timeout;equip=%d", equip))
      end
    end
    D.pending = q
    return true, fmt("submitted_give;equip=%d;%s", equip, label)
  end
  function R.run(a, id)
    local cmd = a[1]
    if cmd == "friendly_fire" and #a == 2 and (a[2] == "0" or a[2] == "1") then
      local on = a[2] == "1"
      local active = type(W) == "table" and W.active == true
      if not active or W.host == true then
        apply(on, "self")
        local sent = active and W.paired == true and send(2, on)
        return true, fmt("friendly_fire;on=%d;by=host;sent=%d", on and 1 or 0, sent and 1 or 0)
      end
      if W.paired ~= true then return false, "friendly_fire_refused;reason=not_paired" end
      if not send(1, on) then return false, "friendly_fire_refused;reason=tx" end
      wlog(fmt("event=arms;step=ff_ask;want=%d", on and 1 or 0))
      return true, fmt("friendly_fire_asked;want=%d", on and 1 or 0)
    elseif cmd == "give_list" and #a == 1 then
      local ids = {}
      for i, g in ipairs(R.list) do ids[i] = fmt("%d", g.id) end
      return true, fmt("give_list;guns=%d;ids=%s;support=%d", #R.list, table.concat(ids, ","), #SUPPORT)
    elseif cmd == "give_pick" and #a == 2 then
      local n = a[2]:match("^%d+$") and number(a[2], 1, 32, true)
      if not n then return false, "give_args" end
      if n <= #R.list then return give(fmt("%d", R.list[n].id), id) end
      local s = SUPPORT[n - #R.list]
      if not s then return false, "give_refused;reason=pick_range" end
      return give("EQP_SWP_" .. s, id)
    elseif cmd == "give_weapon" and #a == 2 then
      return give(a[2], id)
    end
    return false, "command_args"
  end
  function R.stop()
    R.list, R.sent, R.paired, R.frames = {}, nil, nil, 0
  end
  -- Every update: the host repeats its switch to a newly paired guest; every 30 updates in a supported
  -- mission the census is read again and the menu state is sent when it changed.
  function R.tick()
    R.frames = (R.frames or 0) + 1
    local paired = type(W) == "table" and W.paired == true
    -- A guest that loses its host goes back to ON (the default); the host resends its switch at the next pairing.
    if not paired and R.paired and not R.ff and W.host ~= true then R.ff = true; pcall(publish, true) end
    local m = get(vars, "missionCode")
    if m ~= 30010 and m ~= 30020 and m ~= 30050 then return end
    -- On pairing (until it went out), and every 600 updates while off.
    local ok = true
    if paired and W.host == true and (not R.paired or (not R.ff and R.frames % 600 == 0)) then ok = send(2, R.ff) end
    R.paired = paired and ok
    if R.frames % 30 ~= 1 then return end
    local list = resident()
    if list then
      for _, g in ipairs(list) do g.code = code(g.id) end
      R.list = list
    end
    publish()
  end
  local update = D.update
  D.update = function(...)
    local result = update(...)
    pcall(R.tick)
    return result
  end
  -- The menu routes a command to the first of D.player, D.spawn, D.buddy and D.vehicle whose handles name it; the
  -- arms commands ride on D.player's entry (and its stop), so debug_menu.lua's module list is unchanged.
  local P = D.player
  if type(P) == "table" and type(P.handles) == "table" and type(P.run) == "function" then
    for k in pairs(R.handles) do P.handles[k] = true end
    local run, stop = P.run, P.stop
    P.run = function(a, id)
      if type(a) == "table" and R.handles[a[1]] then return R.run(a, id) end
      return run(a, id)
    end
    P.stop = function(...)
      pcall(R.stop)
      if type(stop) == "function" then return stop(...) end
    end
  end
end
