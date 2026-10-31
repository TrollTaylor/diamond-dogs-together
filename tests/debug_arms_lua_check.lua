-- Debug menu arms (lua/coop/debug_arms.lua; docs/debug-menu.md "Give weapon" and "Friendly fire").
-- Pure fixture: stubbed debug api, world link and game bindings; no native call, window or save.
local path = assert(arg[1], "debug_arms.lua path")
local f = assert(io.open(path, "rb")); local src = f:read("*a"); f:close()
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ": " .. why, 2) end end
check(#src:gsub("\r\n", "\n") <= 16000 and #src:gsub("\r?\n", "\r\n") <= 16000, "embedded fragment fits both checkout modes")
local SLOT = {PRIMARY_HIP = 0, PRIMARY_BACK = 1, SECONDARY = 2}
-- The 30010 census of the 0.14.5 session: protected stock guns (id, type) the game loaded.
local CENSUS = {30010, 39, 1, 24, 5, 52, 8, 6, 2, 51, 6, 16, 3}
local function F(host, opts)
  opts = opts or {}
  local c = {equips = {}, tx = {}, notes = {}, logs = {}, states = {}, results = {}, census = opts.census == nil and CENSUS or opts.census, paired = opts.paired ~= false}
  local env = setmetatable({}, {__index = _G}); env._G = env
  env.vars = {missionCode = 30010, weapons = {[0] = 871, [1] = 873, [2] = 875, [3] = 0}, supportWeapons = {[0] = 300, 0, 0, 0, 0, 0, 0, 0}}
  env.TppDefine = {WEAPONSLOT = SLOT}
  env.TppEquip = {EQP_WP_East_ar_010 = 39, EQP_WP_East_ar_010_FL = 39, EQP_WP_Com_sg_011 = 24, EQP_WP_East_ms_010 = 52,
    EQP_WP_West_hg_010 = 6, EQP_WP_East_mg_010 = 51, EQP_WP_East_sm_010 = 16, EQP_WP_West_ar_010 = 40,
    EQP_SWP_Grenade = 300, EQP_SWP_StunGrenade = 301, EQP_SWP_SmokeGrenade = 302, EQP_SWP_C4 = 391, EQP_SWP_Decoy = 304,
    EQP_SWP_SleepingGusGrenade = 305, EQP_SWP_Magazine = 306,
    GetAmmoInfo = function(id)
      if c.badMeta then return nil end
      if id >= 300 then return 0, 0, 0, 0, 0, 0 end
      return 1, 30, 270, 0, 0, 0
    end}
  local TYPE = {[39] = 1, [24] = 5, [52] = 8, [6] = 2, [51] = 6, [16] = 3}
  local TO = {[1] = 0, [5] = 0, [8] = 1, [6] = 1, [2] = 2, [3] = 2}
  env.Player = {ChangeEquip = function(t)
    c.equips[#c.equips + 1] = t
    if c.withhold then return true end
    if t.equipId >= 300 then
      for i = 0, 7 do if env.vars.supportWeapons[i] == t.equipId then return true end end
      for i = 0, 7 do if env.vars.supportWeapons[i] == 0 then env.vars.supportWeapons[i] = t.equipId; return true end end
      return true
    end
    env.vars.weapons[TO[TYPE[t.equipId]]] = t.equipId
    return true
  end}
  local D = {mission = 30010, host = host}
  -- The player module the arms commands ride on (debug_menu.lua routes to D.player first).
  D.player = {handles = {heal = true}, run = function(a) c.playerRuns = (c.playerRuns or 0) + 1; return true, "player_" .. a[1] end,
    stop = function() c.playerStops = (c.playerStops or 0) + 1 end}
  D.api = {
    get = function(t, k) if t == nil then return nil end; return t[k] end,
    call = function(t, k, ...) local fn = t and t[k]; if type(fn) ~= "function" then error("missing_" .. k, 0) end; return fn(...) end,
    integer = function(v, lo, hi) return type(v) == "number" and v == math.floor(v) and v >= lo and v <= hi end,
    number = function(s, lo, hi, whole)
      local n = tonumber(s); if not n or n < lo or n > hi or (whole and n ~= math.floor(n)) then return nil end; return n
    end,
    note = function(id, ok, msg) c.results[#c.results + 1] = {id, ok, msg} end,
    receipt = function(text)
      if text == "return 'MGS_COOP_ARMS_RESIDENT_V1'" then
        c.queries = (c.queries or 0) + 1
        if c.census == false then return nil end
        return c.census
      end
      local s = text:match("^return 'MGS_COOP_ARMS_STATE_V1' %-%- (.*)$")
      if s then c.states[#c.states + 1] = s; return true end
      error("unexpected receipt " .. text)
    end}
  D.update = function() c.updates = (c.updates or 0) + 1 end
  local W = {active = true, paired = c.paired, host = host, OP = {FRIENDLY_HIT = 41}, handlers = {}}
  W.tx = function(op, ...) c.tx[#c.tx + 1] = {op, ...}; return c.txFail ~= true end
  W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
  W.log = function(s) c.logs[#c.logs + 1] = s end
  env.e = {MgsCoopDebug = D, MgsCoopWorld = W}
  setfenv(assert(loadstring(src)), env)()
  c.env, c.D, c.W, c.R = env, D, W, D.arms
  function c:run(text, id)
    local a = {}
    for s in text:gmatch("%S+") do a[#a + 1] = s end
    return self.R.run(a, id or 7)
  end
  function c:tick(n) for _ = 1, n or 1 do self.D.update() end end
  function c:poll(n) for _ = 1, n or 1 do local q = self.D.pending; if not q then return end; q.frames = q.frames + 1; q.poll() end end
  function c:lasttx() return self.tx[#self.tx] end
  return c
end

-- Module shape: registered on the debug menu and the world link; friendly fire is ON by default.
local c = F(true)
check(c.R and c.R.handles.give_weapon and c.R.handles.give_pick and c.R.handles.give_list and c.R.handles.friendly_fire,
  "D.arms handles give_weapon, give_pick, give_list and friendly_fire")
check(c.R.ff == true and c.W.arms == c.R and c.W.OP.ARMS == 63 and type(c.W.handlers[63]) == "function",
  "friendly fire ON by default, W.arms is the module, ARMS is op 63 (0x3F)")

-- Routed through D.player: the menu's module list is unchanged; other commands still reach the player module.
check(c.D.player.handles.give_weapon and c.D.player.handles.friendly_fire and c.D.player.handles.heal, "arms commands ride on D.player's handles")
local okp, msgp = c.D.player.run({"give_list"}, 3)
check(okp and msgp:find("give_list;", 1, true) and not c.playerRuns, "an arms command reaches the arms module")
okp, msgp = c.D.player.run({"heal"}, 4)
check(okp and msgp == "player_heal" and c.playerRuns == 1, "a player command still reaches the player module")
c.R.list = {{id = 1, type = 1, code = "x"}}; c.D.player.stop()
check(c.playerStops == 1 and #c.R.list == 0, "the player module's stop also stops arms")
c = F(true)
-- The loaded-gun list: the native census, named from TppEquip, published to the menu.
c:tick()
check(c.queries == 1 and #c.R.list == 6 and c.R.list[1].id == 39 and c.R.list[1].type == 1 and c.R.list[1].code == "East_ar_010",
  "the first update reads the census and names each gun (the shortest TppEquip key)")
check(#c.states == 1 and c.states[1] == "1 30010 6 39.1.East_ar_010 24.5.Com_sg_011 52.8.East_ms_010 6.2.West_hg_010 51.6.East_mg_010 16.3.East_sm_010",
  "the state receipt carries friendly fire, the mission and every loaded gun")
c:tick(10)
check(c.queries == 1 and #c.states == 1, "the census is re-read every 30 updates and an unchanged state is not re-sent")
local ok, msg = c:run("give_list")
check(ok and msg == "give_list;guns=6;ids=39,24,52,6,51,16;support=7", "give_list names the loaded guns and the support count")

-- Give a loaded gun: the stock ChangeEquip with full ammo, active, the old gun dropped like a stock pickup swap.
ok, msg = c:run("give_weapon 39")
local q = c.equips[#c.equips]
check(ok and msg == "submitted_give;equip=39;kind=gun;type=1;slot=hip;name=East_ar_010", "give_weapon 39 is submitted for the hip slot")
check(q.equipId == 39 and q.ammo == 30 and q.stock == 270 and q.toActive == true and q.dropPrevEquip == true and q.isSuppressorOn == false,
  "ChangeEquip: full magazine and stock from GetAmmoInfo, made active, the previous gun dropped")
check(c.D.pending and c.D.pending.kind == "give", "a readback is pending")
c:poll()
check(c.D.pending == nil and c.results[#c.results][2] == true and c.results[#c.results][3] == "give_readback;equip=39;slot=hip",
  "vars.weapons readback confirms the gun")
check(c.logs[#c.logs] == "event=arms; step=give; equip=39; slot=hip; ok=1", "the give is logged for the run")
ok, msg = c:run("give_pick 2")
check(ok and msg:find("submitted_give;equip=24;kind=gun;type=5;slot=hip", 1, true), "give_pick 2 gives the second listed gun")
c:poll()
ok, msg = c:run("give_pick 4")
check(ok and msg:find("equip=6;kind=gun;type=2;slot=secondary", 1, true), "a handgun goes to the secondary slot")
c:poll()
ok, msg = c:run("give_pick 3")
check(ok and msg:find("equip=52;kind=gun;type=8;slot=back", 1, true), "a missile goes to the back slot")
c:poll()

-- Refusals: never a gun the census does not list, never without a census, never on bad metadata.
local n = #c.equips
ok, msg = c:run("give_weapon 40")
check(not ok and msg == "give_refused;reason=not_loaded" and #c.equips == n, "a gun the game did not load is refused (no ChangeEquip)")
c = F(true, {census = false})
ok, msg = c:run("give_weapon 39")
check(not ok and msg == "give_refused;reason=no_census" and #c.equips == 0, "no census (no partner body yet): guns refused")
c = F(true, {census = {30020, 39, 1}})
ok, msg = c:run("give_weapon 39")
check(not ok and msg == "give_refused;reason=no_census", "a census of another mission is not used")
c = F(true, {census = {30010, 900, 1, 39, 9, 39, 1}})
c:tick()
check(#c.R.list == 1 and c.R.list[1].id == 39 and c.R.list[1].type == 1, "aliases (>= 0x230) and non-gun types are never listed")
c = F(true); c.badMeta = true
ok, msg = c:run("give_weapon 39")
check(not ok and msg == "give_refused;reason=metadata" and #c.equips == 0, "unreadable ammo metadata refuses")
c = F(true); c.withhold = true
ok = c:run("give_weapon 39"); c:poll(300)
check(ok and c.results[#c.results][2] == false and c.results[#c.results][3] == "give_readback_timeout;equip=39",
  "no gun in the slot: a timeout, never a fake success")
for _, bad in ipairs({"give_weapon", "give_weapon x", "give_weapon 0", "give_weapon EQP_SWP_Kibidango", "give_pick 0", "give_pick 99"}) do
  c = F(true); ok, msg = c:run(bad)
  check(not ok and #c.equips == 0, "bad give arguments refused: " .. bad)
end

-- Support items: the stock base items (common block), into a free sub, never replacing a full list.
c = F(true)
ok, msg = c:run("give_weapon EQP_SWP_C4")
q = c.equips[#c.equips]
check(ok and msg == "submitted_give;equip=391;kind=support;name=C4" and q.equipId == 391 and q.toActive == false and q.dropPrevEquip == false
  and q.stock == 8, "a support item: ChangeEquip not active, nothing dropped, the default count when the table has none")
c:poll()
check(c.results[#c.results][3] == "give_readback;equip=391;slot=support" and c.env.vars.supportWeapons[1] == 391, "support readback")
ok, msg = c:run("give_weapon EQP_SWP_Grenade")
check(ok and c.equips[#c.equips].equipId == 300, "an item already listed is refilled")
c = F(true); for i = 0, 7 do c.env.vars.supportWeapons[i] = 290 + i end
ok, msg = c:run("give_weapon EQP_SWP_C4")
check(not ok and msg == "give_refused;reason=support_full" and #c.equips == 0, "a full support list is refused, never replaced")
c = F(true); c:tick()
ok, msg = c:run("give_pick 7")
check(ok and msg:find("kind=support;name=Grenade", 1, true), "give_pick past the guns picks the support items in order")

-- Friendly fire: the host decides for both copies.
c = F(true)
ok, msg = c:run("friendly_fire 0")
local t = c:lasttx()
check(ok and msg == "friendly_fire;on=0;by=host;sent=1" and c.R.ff == false, "the host turns friendly fire off")
check(t[1] == 63 and t[3] == 2 and t[4] == 0, "ARMS kind 2 (state) carries off to the guest")
check(c.notes[#c.notes][1] == 18 and c.notes[#c.notes][2] == 1 and c.notes[#c.notes][3] == 0, "note 18: the host's own toast")
check(c.logs[#c.logs] == "event=arms; step=ff; on=0; by=self", "the host logs the switch")
check(c.states[#c.states]:sub(1, 8) == "0 30010 ", "the menu state shows friendly fire off at once")
ok, msg = c:run("friendly_fire 1")
check(ok and c.R.ff == true and c:lasttx()[4] == 1, "and on again")
-- The guest asks; the host answers with its state.
local g = F(false)
ok, msg = g:run("friendly_fire 0")
t = g:lasttx()
check(ok and msg == "friendly_fire_asked;want=0" and g.R.ff == true and t[1] == 63 and t[3] == 1 and t[4] == 0,
  "a guest press only asks the host (ARMS kind 1)")
local h = F(true)
h.W.handlers[63]({t[2], t[3], t[4]})
check(h.R.ff == false and h:lasttx()[3] == 2 and h:lasttx()[4] == 0 and h.notes[#h.notes][3] == 0 and
  h.logs[#h.logs] == "event=arms; step=ff; on=0; by=partner", "the host applies the ask, toasts and sends its state")
g.W.handlers[63]({h:lasttx()[2], 2, 0})
check(g.R.ff == false and g.notes[#g.notes][1] == 18 and g.notes[#g.notes][3] == 0 and g.logs[#g.logs] == "event=arms; step=ff; on=0; by=host",
  "the guest takes the host's state and shows the toast")
local notes = #g.notes
g.W.handlers[63]({9, 2, 0})
check(#g.notes == notes, "a repeated state with no change shows no second toast")
g.W.handlers[63]({10, 1, 1})
check(g.R.ff == false, "a guest never takes an ask")
h.W.handlers[63]({11, 2, 1})
check(h.R.ff == false, "the host never takes a state")
for _, v in ipairs({{12, 2, 5}, {12, 3, 1}, {12}, "x"}) do g.W.handlers[63](v) end
check(g.R.ff == false, "malformed ARMS values are ignored")
g = F(false, {paired = false})
ok, msg = g:run("friendly_fire 0")
check(not ok and msg == "friendly_fire_refused;reason=not_paired" and #g.tx == 0, "an unpaired guest cannot ask")
-- The host repeats its state when a guest pairs (a guest that joins later learns it).
h = F(true, {paired = false})
h:run("friendly_fire 0")
check(#h.tx == 0, "an unpaired host keeps the switch locally")
h.W.paired = true; h:tick()
check(h:lasttx() and h:lasttx()[1] == 63 and h:lasttx()[3] == 2 and h:lasttx()[4] == 0, "the host sends its state on pairing")
local sends = #h.tx; h:tick(5)
check(#h.tx == sends, "once per pairing")
h:tick(600)
check(#h.tx == sends + 1 and h:lasttx()[3] == 2 and h:lasttx()[4] == 0, "while off, the host repeats its state every 600 updates")
h:run("friendly_fire 1"); sends = #h.tx; h:tick(1200)
check(#h.tx == sends, "while on (the guest's default), no repeats")
h:run("friendly_fire 0")
-- A mission change keeps the switch (session-wide) but drops the gun list.
h.R.stop()
check(h.R.ff == false and #h.R.list == 0, "stop keeps friendly fire and clears the gun list")
-- Bad switch arguments.
for _, bad in ipairs({"friendly_fire", "friendly_fire 2", "friendly_fire on"}) do
  c = F(true); ok = c:run(bad)
  check(not ok and #c.tx == 0, "bad friendly_fire arguments refused: " .. bad)
end
print("debug_arms_lua_check: " .. checks .. " checks passed")
