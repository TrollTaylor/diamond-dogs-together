-- Supply drops synced (r1101; docs/world-sync-protocol.md section 25). Spliced last, after world_relief_sync.lua.
-- With lab.ini world_supply=1 (lab and play; OFF in both unless named until a hidden run proves it) on both copies.
-- The game keeps ONE supply box per copy (the TppSupplyCboxSystem core). The native observer (native/supply_cbox.cpp,
-- MGS_COOP_SUPPLY_V1) records every box request (iDroid, script) and every box reset; this module:
--   drop:   a request this copy made itself (not inside our mirror window) is this copy's box: SUPPLY 0x45 kind 1 with
--           the request's drop type, equip, parts, aim point (this player's position when the request aims at an object
--           or at no point), level, wait level and flags. The partner requests the same box at the same aim point with
--           Player.RequestToSupplyCbox inside a mirror window (never sent back), with world note 21 a=1.
--   gone:   a synced box that was live (state not 0) and then reset or reads state 0, outside the 2 s after a request
--           (the game's own reset of the old box), is gone here: kind 2 with reason 1 (this player stood within NEAR_MM:
--           collected) or 2 (vanished). The partner removes its own copy with the lab remove lever (the box core's
--           reset bit) and shows note 21 a=2 (only when it removed one). A reset followed by a request in the same poll
--           is a replacement: no gone. A reset at state 14 is the game's own end on both copies (landed -> delivered ->
--           faded, run ddf44cee): step=delivered, nothing sent.
-- Each copy's request charges only its own player (the mirror goes straight to the box system, not the support
-- request that costs GMP). A soldier a falling box knocks out is mirrored by the world outcomes like any knockout.
-- The stock Player message OnPickUpSupplyCbox (this player took a supply box) ends the synced box at once: kind 2,
-- reason 1. Lab only: every game message is logged for a while around a box (step=msg).
-- Review r1101: only the proven drop / equip pairs sync (ALLOW; any other request stays this copy's own box, sync=0);
-- a partner's drop never replaces this copy's own live or just requested box (step=mirror_held: one box per copy, so
-- each player keeps the box they paid for); a request the box core refused (the observer's accepted=0) is no box; a
-- mirror is adopted only when the observer saw the box core take it; the ring is read before a partner's op is applied.
-- Debug rows (the player module's dispatch): supply state | drop [x y z] | bullet | remove.
do
local W = e.MgsCoopWorld
if type(W) == "table" and type(W.handlers) == "table" then
local S = {}
W.supply = S
local OP, NOTE = 69, 21
if type(W.OP) == "table" then W.OP.SUPPLY = OP end
local PRE = "return 'MGS_COOP_SUPPLY_V1' -- "
local POLL_MS, GRACE_MS, NEAR_MM, CENSUS_MS, CENSUS_N, STATE_N = 200, 2000, 6000, 15000, 300, 200
local ECHO_MS, ECHO_MM = 3000, 1000
-- The box core's state when the game delivered the box's contents (run ddf44cee: 8 landed -> 14, then a fade reset).
local DELIVERED = 14
-- The synced drop types, "drop:equip", each seen in a hidden run: the iDroid AMMO / FULTON drop (0:431, ddf44cee) and
-- the script AMMO box (SupplyCbox.DROP_TYPE_AMMO 4:0, c66d418b). Both are delivered by the game itself on landing
-- (state 14), so the 14 rule ends them. WEAPONS / ITEMS, SUIT, LOADOUT ... stay off until a run shows what the receiver
-- gets and how the box ends (a type that needs a pickup must not take the 14 rule).
local ALLOW = {["0:431"] = true, ["4:0"] = true}
local function allowed(drop, equip) return ALLOW[tostring(drop) .. ":" .. tostring(equip)] == true end
S.POLL_MS, S.GRACE_MS, S.NEAR_MM, S.allowed = POLL_MS, GRACE_MS, NEAR_MM, allowed
local fmt, floor, sqrt = string.format, math.floor, math.sqrt
local function L(...)
  local ok, text = pcall(fmt, ...)
  if ok and type(W.log) == "function" then pcall(W.log, "event=supply;" .. text) end
end
local function now() return W.now() end
local function int(v, lo, hi) return type(v) == "number" and v == floor(v) and v >= lo and v <= hi end
local function role() return W.host and 1 or 2 end
local function get(t, k)
  if t == nil then return nil end
  local ok, v = pcall(function() return t[k] end)
  if ok then return v end
end
function S.reset(keepAfter)
  if not keepAfter then S.after = 0 end
  S.cur, S.removing, S.lastReqAt, S.st, S.box, S.census, S.censusUntil, S.stateLines = nil, nil, -1e9, 0, 0, 0, nil, 0
  S.done = {}
end
S.reset()
S.seq, S.txSeq = 0, 0
S.n = {tx = 0, rx = 0, mirror = 0, gone = 0, removed = 0}
local function receipt(cmd)
  local ok, r = pcall(W.receipt, PRE .. cmd)
  if ok and type(r) == "table" then return r end
end
local function pos()
  local x, y, z = get(vars, "playerPosX"), get(vars, "playerPosY"), get(vars, "playerPosZ")
  if type(x) == "number" and type(y) == "number" and type(z) == "number" then return x, y, z end
end
local function mm(v) return floor(v * 1000 + 0.5) end
local function key(o, s) return o * 65536 + s end
local function censusOn(t) S.censusUntil = t + CENSUS_MS end
local function send(kind, b, reason)
  S.txSeq = S.txSeq % 65535 + 1
  local ok, sent = pcall(W.tx, OP, S.txSeq, kind, b.origin, b.oseq, b.drop, b.equip, b.parts, b.x, b.y, b.z, b.level, b.wait,
    b.flags, reason)
  sent = ok and sent == true
  if sent then S.n.tx = S.n.tx + 1 end
  return sent
end
-- Echo guard: a request outside the mirror window within ECHO_MS of a mirror and within ECHO_MM of its aim point is the
-- mirror's own (should the game ever run it outside Lua's call): never sent back, so two copies never ping-pong.
local function echo(ev, t)
  local m = S.cur
  if not (m and m.origin ~= role() and t - m.at <= ECHO_MS) then return false end
  local dx, dy, dz = (ev[7] or 0) - m.x, (ev[8] or 0) - m.y, (ev[9] or 0) - m.z
  return sqrt(dx * dx + dy * dy + dz * dz) <= ECHO_MM
end
-- A request this copy made: its box, sent to the partner.
function S.mine(ev, t)
  if echo(ev, t) then
    L("step=echo;seq=%s;origin=%d;oseq=%d;x_mm=%s;y_mm=%s;z_mm=%s", tostring(ev[1]), S.cur.origin, S.cur.oseq, tostring(ev[7]),
      tostring(ev[8]), tostring(ev[9]))
    return
  end
  S.seq = S.seq % 65535 + 1
  local b = {origin = role(), oseq = S.seq, drop = ev[4], equip = ev[5], parts = ev[6], x = ev[7], y = ev[8], z = ev[9],
    level = ev[10], wait = ev[11], flags = ev[12], at = t}
  local aim = "point"
  if ev[13] ~= 65535 or math.abs(b.x) + math.abs(b.y) + math.abs(b.z) < 1000 then
    local px, py, pz = pos()
    if px then b.x, b.y, b.z, aim = mm(px), mm(py), mm(pz), "player" end
  end
  S.cur = b
  b.sync = allowed(b.drop, b.equip)
  local sent = b.sync and W.paired == true and int(b.drop, 0, 255) and int(b.level, 0, 255) and send(1, b, 0) or false
  L("step=drop_local;origin=%d;oseq=%d;drop=%s;equip=%s;parts=%s;x_mm=%d;y_mm=%d;z_mm=%d;sent=%d;aim=%s;caller=0x%x;sync=%d", b.origin,
    b.oseq, tostring(b.drop), tostring(b.equip), tostring(b.parts), b.x, b.y, b.z, sent and 1 or 0, aim, ev[14] or 0, b.sync and 1 or 0)
end
-- This copy's own box is live or was just requested (its state may read 0 for a frame after the request).
local function ownBox(t)
  local b = S.cur
  return b and b.origin == role() and (S.st ~= 0 or t - b.at <= GRACE_MS) and b or nil
end
-- The synced box (origin, oseq): never a box that only this copy tracks (sync false).
local function is(b, origin, oseq) return b and b.sync and b.origin == origin and b.oseq == oseq end
local function near(b)
  local px, py, pz = pos()
  if not px or not b then return false end
  local dx, dy, dz = px * 1000 - b.x, py * 1000 - b.y, pz * 1000 - b.z
  return sqrt(dx * dx + dy * dy + dz * dz) <= NEAR_MM
end
-- A synced box is gone on this copy (reason: 1 collected, 2 vanished; nil = by this player's distance to the box).
function S.gone(t, why, reason)
  local b = S.cur
  if not b then return end
  S.cur = nil
  S.done[key(b.origin, b.oseq)] = true
  reason = reason or (near(b) and 1 or 2)
  local sent = b.sync and W.paired == true and send(2, b, reason) or false
  S.n.gone = S.n.gone + 1
  L("step=gone_local;origin=%d;oseq=%d;reason=%d;why=%s;sent=%d", b.origin, b.oseq, reason, why, sent and 1 or 0)
  censusOn(t)
end
function S.event(list, t)
  for i, ev in ipairs(list) do
    if type(ev) == "table" and type(ev[2]) == "number" then
      if ev[2] == 1 and ev[16] == 0 then
        -- The box core refused it (its first branch returns before touching the box): no box, nothing to send.
        if ev[3] == 1 then S.mirrorAcc = 0 end
        L("step=refused;seq=%d;mirror=%d;drop=%s;equip=%s", ev[1], ev[3] == 1 and 1 or 0, tostring(ev[4]), tostring(ev[5]))
      elseif ev[2] == 1 then
        S.lastReqAt = t
        censusOn(t)
        local later = false
        for j = i + 1, #list do
          local n = list[j]
          if type(n) == "table" and n[2] == 1 and n[16] ~= 0 then later = true end
        end
        if ev[3] == 1 then
          S.mirrorAcc = 1
          L("step=mirror_seen;seq=%d;x_mm=%s;y_mm=%s;z_mm=%s", ev[1], tostring(ev[7]), tostring(ev[8]), tostring(ev[9]))
        elseif later then
          -- One box per copy: a later request in the same poll replaced this one.
          L("step=superseded;seq=%d;drop=%s;x_mm=%s;y_mm=%s;z_mm=%s", ev[1], tostring(ev[4]), tostring(ev[7]), tostring(ev[8]),
            tostring(ev[9]))
        else
          S.mine(ev, t)
        end
      elseif ev[2] == 2 then
        local later = false
        for j = i + 1, #list do if type(list[j]) == "table" and list[j][2] == 1 and list[j][16] ~= 0 then later = true end end
        if S.removing and t - S.removing <= 5000 then
          S.removing = nil
          S.n.removed = S.n.removed + 1
          L("step=removed;state=%s", tostring(ev[15]))
        elseif later then
          L("step=reset_replaced;state=%s", tostring(ev[15]))
        elseif t - S.lastReqAt <= GRACE_MS then
          L("step=reset_request;state=%s;caller=0x%x", tostring(ev[15]), ev[14] or 0)
        elseif ev[15] == DELIVERED and S.cur and S.cur.sync then
          -- The game's own end of a synced (self-delivering, ALLOW) type (run ddf44cee): landed (8) -> delivered (14) -> the
          -- fade timer resets it, on both copies. Any other box reset at 14 falls through to the gone rule.
          local b = S.cur
          S.cur = nil
          S.done[key(b.origin, b.oseq)] = true
          L("step=delivered;origin=%d;oseq=%d;caller=0x%x", b.origin, b.oseq, ev[14] or 0)
        elseif S.cur and S.cur.live then
          S.gone(t, fmt("reset_0x%x", ev[14] or 0))
        else
          L("step=reset_idle;state=%s;caller=0x%x", tostring(ev[15]), ev[14] or 0)
        end
      end
    end
  end
end
function S.poll(t)
  local r = receipt("poll " .. S.after)
  if not r or r.on ~= 1 then
    if S.on ~= false then L("step=off") end
    S.on = false
    return
  end
  if S.on ~= true then L("step=on;inst=%s", tostring(r.inst)) end
  S.on = true
  local last = int(r.last, 0, 4294967295) and r.last or S.after
  if S.skipOld then
    S.skipOld = nil
  elseif type(r.ev) == "table" then
    if (r.lost or 0) > 0 then L("step=lost;n=%s", tostring(r.lost)) end
    S.event(r.ev, t)
  end
  S.after = last
  local st = int(r.st, 0, 4294967295) and r.st or 0
  S.box = r.box == 1 and 1 or 0
  if st ~= S.st and S.stateLines < STATE_N then
    S.stateLines = S.stateLines + 1
    L("step=state;from=%d;to=%d;fl=0x%x", S.st, st, int(r.fl, 0, 4294967295) and r.fl or 0)
  end
  S.st = st
  local b = S.cur
  if b then
    if st ~= 0 then
      if not b.live then b.live = true; L("step=live;origin=%d;oseq=%d;st=%d", b.origin, b.oseq, st) end
    elseif b.live and t - S.lastReqAt > GRACE_MS then
      S.gone(t, "state_0")
    end
  end
end
-- The partner's SUPPLY.
local function apply(v, t)
  -- The ring first (review r1101): this copy's newest request is S.cur before a partner's op is applied.
  S.poll(t)
  for i = 1, 14 do if type(v[i]) ~= "number" or v[i] ~= floor(v[i]) then L("step=rx_bad;field=%d", i); return end end
  local kind, origin, oseq = v[2], v[3], v[4]
  S.n.rx = S.n.rx + 1
  if S.on ~= true then L("step=rx_off;kind=%d", kind); return end
  if origin ~= 1 and origin ~= 2 then return end
  if kind == 1 then
    if origin == role() then L("step=rx_own;oseq=%d", oseq); return end
    if S.done[key(origin, oseq)] or is(S.cur, origin, oseq) then
      L("step=mirror_dup;origin=%d;oseq=%d", origin, oseq); return
    end
    if not allowed(v[5], v[6]) then
      S.done[key(origin, oseq)] = true
      L("step=mirror_skip;origin=%d;oseq=%d;reason=drop_type;drop=%d;equip=%d", origin, oseq, v[5], v[6]); return
    end
    -- One box per copy: a mirror request would reset this copy's own box (paid for here). Each player keeps theirs.
    local own = ownBox(t)
    if own then
      S.done[key(origin, oseq)] = true
      L("step=mirror_held;origin=%d;oseq=%d;own=%d.%d;st=%d", origin, oseq, own.origin, own.oseq, S.st); return
    end
    if not W.inGame() then L("step=mirror_skip;origin=%d;oseq=%d;reason=not_in_game", origin, oseq); return end
    local f = get(Player, "RequestToSupplyCbox")
    -- Vector3 is a callable object in the game (run c66d418b), not a function: built inside the pcall.
    if type(f) ~= "function" or Vector3 == nil then L("step=mirror_skip;reason=missing_api"); return end
    local b = {origin = origin, oseq = oseq, drop = v[5], equip = v[6], parts = v[7], x = v[8], y = v[9], z = v[10], level = v[11],
      wait = v[12], flags = v[13], at = t, sync = true}
    S.mirrorAcc = nil
    receipt("mirror 1")
    local ok, err = pcall(function()
      return f({dropType = b.drop, equipId = b.equip, partsType = b.parts, aimPosition = Vector3(b.x / 1000, b.y / 1000, b.z / 1000),
        level = b.level, waitLevel = b.wait, callHeliSound = floor(b.flags / 4) % 2 == 1})
    end)
    receipt("mirror 0")
    -- The mirror's own request event (the request runs synchronously inside the call): adopted only when the core took it.
    S.poll(t)
    local why = not ok and "err=" .. string.sub(tostring(err), 1, 60) or S.mirrorAcc == 0 and "refused" or S.mirrorAcc ~= 1 and "unseen"
    censusOn(t)
    if why then
      L("step=mirror;origin=%d;oseq=%d;ok=0;drop=%d;x_mm=%d;y_mm=%d;z_mm=%d;why=%s", origin, oseq, b.drop, b.x, b.y, b.z, why)
      return
    end
    S.cur, S.lastReqAt = b, t
    S.n.mirror = S.n.mirror + 1
    L("step=mirror;origin=%d;oseq=%d;ok=1;drop=%d;x_mm=%d;y_mm=%d;z_mm=%d", origin, oseq, b.drop, b.x, b.y, b.z)
    pcall(W.note, NOTE, 1, b.drop)
  elseif kind == 2 then
    local b = S.cur
    if not is(b, origin, oseq) then
      S.done[key(origin, oseq)] = true
      L("step=gone_unknown;origin=%d;oseq=%d", origin, oseq); return
    end
    S.cur = nil
    S.done[key(origin, oseq)] = true
    S.removing = t
    local r = receipt("remove " .. oseq) or {}
    if r.ok ~= 1 then S.removing = nil end
    L("step=remove;origin=%d;oseq=%d;ok=%d;why=%s;st=%s;reason=%d", origin, oseq, r.ok == 1 and 1 or 0, tostring(r.why or "none"),
      tostring(r.st), v[14])
    censusOn(t)
    -- The toast only when this screen changed (a refused remove, e.g. no_box, changed nothing).
    if r.ok == 1 then pcall(W.note, NOTE, 2, v[14]) end
  end
end
W.handlers[OP] = function(v)
  local ok, err = pcall(apply, v, now())
  if not ok then L("step=error;msg=%s", string.sub(tostring(err), 1, 100)) end
end
W.stopHooks = W.stopHooks or {}
-- A real stop only (true); W.w1off runs the stop hooks with false mid-mission, with the boxes still down.
W.stopHooks[#W.stopHooks + 1] = function(full) if full then S.reset(true) end end
-- A new pairing epoch (e.g. the partner restarted its game; its box seq starts at 1 again): forget the dedupe keys; the
-- old pairing's partner box is this copy's alone now (sync false: never matched, never sent).
W.pairedHooks = W.pairedHooks or {}
W.pairedHooks[#W.pairedHooks + 1] = function(changed)
  if not changed then return end
  S.done = {}
  if S.cur and S.cur.origin ~= role() then S.cur.sync = false end
  L("step=epoch;cur=%s", S.cur and fmt("%d.%d", S.cur.origin, S.cur.oseq) or "none")
end
W.startHooks = W.startHooks or {}
W.startHooks[#W.startHooks + 1] = function() S.reset(true); S.skipOld = true end
-- The stock Player messages (the game's s10030_sequence.lua): OnPickUpSupplyCbox (this player took a supply box: the
-- synced box is gone here, collected) and OnComeOutSupplyCbox (logged). Raw Fox.StrCode32 values, bound once.
local function ids()
  if S.ids == nil then
    local s = get(W.api, "str32")
    if type(s) ~= "function" then return nil end
    local ok, player, pick, come = pcall(function() return s("Player"), s("OnPickUpSupplyCbox"), s("OnComeOutSupplyCbox") end)
    S.ids = ok and {player = player, pick = pick, come = come} or false
  end
  return S.ids or nil
end
function S.stock(pick, a0, a1)
  if S.on == nil then S.poll(now()) end
  if S.on ~= true then return end
  local b = S.cur
  L("step=%s;a0=%s;a1=%s;cur=%s", pick and "pickup" or "come_out", tostring(a0), tostring(a1),
    b and fmt("%d.%d", b.origin, b.oseq) or "none")
  if pick and b then S.gone(now(), "pickup", 1) end
end
-- Lab census: every game message while a box is around.
local observe = W.observe
if type(observe) == "function" then
  function W.observe(class, msg, a0, a1, a2, a3)
    local I = ids()
    if I and class == I.player and (msg == I.pick or msg == I.come) then
      local ok, err = pcall(S.stock, msg == I.pick, a0, a1)
      if not ok then L("step=error;msg=%s", string.sub(tostring(err), 1, 100)) end
    end
    if W.mode == 1 and S.censusUntil and (S.cur or now() <= S.censusUntil) and S.census < CENSUS_N then
      S.census = S.census + 1
      L("step=msg;class=%s;msg=%s;a0=%s;a1=%s;a2=%s;a3=%s", tostring(class), tostring(msg), tostring(a0), tostring(a1),
        tostring(a2), tostring(a3))
    end
    return observe(class, msg, a0, a1, a2, a3)
  end
end
-- Debug rows.
function S.command(a)
  local c = a[2]
  if c == "state" and #a == 2 then
    local b = S.cur
    return true, fmt("on=%d;box=%d;st=%d;cur=%s;tx=%d;rx=%d;mirror=%d;gone=%d;removed=%d", S.on and 1 or 0, S.box or 0, S.st or 0,
      b and fmt("%d.%d", b.origin, b.oseq) or "none", S.n.tx, S.n.rx, S.n.mirror, S.n.gone, S.n.removed)
  end
  if S.on ~= true then return false, "supply_off" end
  local px, py, pz = pos()
  if c == "drop" and (#a == 2 or #a == 5) then
    if #a == 5 then px, py, pz = tonumber(a[3]), tonumber(a[4]), tonumber(a[5]) end
    if not (px and py and pz) then return false, "command_args" end
    local f, ammo = get(Player, "RequestToSupplyCbox"), get(get(_G, "SupplyCbox"), "DROP_TYPE_AMMO")
    if type(f) ~= "function" or type(ammo) ~= "number" then return false, "missing_api" end
    local ok = pcall(function() return f({dropType = ammo, aimPosition = Vector3(px, py, pz)}) end)
    L("step=debug_drop;ok=%d;drop=%d;x_mm=%d;y_mm=%d;z_mm=%d", ok and 1 or 0, ammo, mm(px), mm(py), mm(pz))
    return ok, fmt("submitted_supply_drop;drop=%d;x=%.1f;y=%.1f;z=%.1f", ammo, px, py, pz)
  elseif c == "bullet" and #a == 2 then
    local f = get(TppSupportRequest, "RequestDropBullet")
    if type(f) ~= "function" or not px then return false, "missing_api" end
    local ok = pcall(function() return f({pos = Vector3(px, py, pz)}) end)
    L("step=debug_bullet;ok=%d;x_mm=%d;y_mm=%d;z_mm=%d", ok and 1 or 0, mm(px), mm(py), mm(pz))
    return ok, "submitted_supply_bullet"
  elseif c == "remove" and #a == 2 then
    S.removing = now()
    local r = receipt("remove 0") or {}
    L("step=debug_remove;ok=%d;why=%s", r.ok == 1 and 1 or 0, tostring(r.why or "none"))
    return r.ok == 1, fmt("supply_remove;ok=%d;why=%s", r.ok == 1 and 1 or 0, tostring(r.why or "none"))
  end
  return false, "command_args"
end
local function hookDebug()
  local D = get(e, "MgsCoopDebug")
  local P = type(D) == "table" and D.player
  if type(P) ~= "table" or type(P.handles) ~= "table" or type(P.run) ~= "function" then return end
  S.debugHooked = true
  P.handles.supply = true
  local base = P.run
  P.run = function(a, id)
    if type(a) == "table" and a[1] == "supply" then return S.command(a) end
    return base(a, id)
  end
end
local tick = W.tick
function W.tick(...)
  if tick then tick(...) end
  if not S.debugHooked then pcall(hookDebug) end
  local t = now()
  if t < (S.nextPoll or 0) then return end
  S.nextPoll = t + POLL_MS
  local ok, err = pcall(S.poll, t)
  if not ok then L("step=error;msg=%s", string.sub(tostring(err), 1, 100)) end
end
end
end
