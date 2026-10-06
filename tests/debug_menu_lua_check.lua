-- Lua 5.1 debug executor regression: the owner update is the only dispatcher.
-- Args: debug_menu.lua, debug_equipment.lua. Engine fakes record calls; no game or saves are used.
local checks = 0
local function check(ok, why) checks = checks + 1; if not ok then error(checks .. ': ' .. why, 2) end end
local source = assert(io.open(assert(arg[1]), 'rb')):read('*a')
local equipment = assert(io.open(assert(arg[2]), 'rb')):read('*a')
check(#source:gsub('\r\n', '\n') <= 16000 and #equipment:gsub('\r\n', '\n') <= 16000, 'fragment native LF byte limits')
local function fixture(options)
    options = options or {}
    local c = {calls = {}, logs = {}, state = {}, id = 1, updates = 0, terms = 0, host = options.host ~= false}
    local env = setmetatable({}, {__index = _G})
    local function record(name, ...) c.calls[#c.calls + 1] = {name, ...} end
    env.vars = {missionCode = options.mission or 30010, playerPosX = 10, playerPosY = 20, playerPosZ = 30, playerRotY = -60,
        playerVehicleGameObjectId = 65535, clock = 0, currentInventorySlot = 1, weapons = {[0] = 301, [1] = 8, [2] = 101},
        ammoInWeapons = {[0] = 1, [1] = 2, [2] = 3}, ammoSubInWeapons = {[0] = 0, [1] = 0, [2] = 0}}
    env.mvars = {ene_soldierDefine = {cp_z = {'sol_b'}, cp_a = {'sol_a', 'sol_u'}, cp_meta = {lrrpVehicle = 'vehicle'}}}
    env.PlayerStatus = {STAND = 17, SQUAT = 31, CRAWL = 45}
    env.PlayerInfo = {OrCheckStatus = function(a)
        check(#a == 3 and a[1] == 17 and a[2] == 31 and a[3] == 45, 'actual posture constants')
        return not c.sitting
    end}
    env.Player = {IsInstanceValid = function() return not c.invalid end, SetInfiniteAmmoFromScript = function(v)
        record('infinite', v); if c.ammoError then error('game fault') end; if c.ammoRefuse then return false end
    end, ChangeEquip = function(a)
        record('equip', a)
        if c.equipRefuse then return false end
        if not c.weaponReject then
            local slot = a.equipId == 301 and 0 or 2
            env.vars.ammoInWeapons[slot], env.vars.ammoSubInWeapons[slot] = a.ammo, a.ammoSub
            c.stock, c.stockSub = a.stock, a.stockSub
        end
    end, GetAmmoStockBySlot = function(slot, sub)
        check((slot == 5 or slot == 7 or slot == 9) and sub == 0, 'native slot enum, not vars slot index')
        return c.stock, c.stockSub
    end}
    env.PlayerSlotType = {PRIMARY_1 = 5, PRIMARY_2 = 7, SECONDARY = 9}
    env.DemoDaemon = {IsDemoPlaying = function() return c.demo or false end}
    env.TppMission = {CheckMissionState = function() return not c.loading end, IsMissionStart = function() return not c.ended end}
    env.Tpp = {IsHelicopter = function(v) return c.heli or false end}
    env.TppGameObject = {NPC_LIFE_STATE_DEAD = 3}
    env.TppDefine = {WEAPONSLOT = {PRIMARY_HIP = 0, PRIMARY_BACK = 1, SECONDARY = 2},
        WEATHER = {SUNNY = 0, CLOUDY = 1, RAINY = 2, SANDSTORM = 3, FOGGY = 4}}
    env.TppEquip = {EQP_WP_30101 = 301, EQP_WP_10101 = 101, EQP_WP_10004 = 104,
        GetAmmoInfo = function(id) record('metadata', id); return 11, 30, 90, 0, 0, 0 end}
    env.TppPlayer = {Warp = function(a)
        record('warp', a); if c.warpError then error('warp error') end
        if c.warpNow then env.vars.playerPosX, env.vars.playerPosY, env.vars.playerPosZ = a.pos[1], a.pos[2], a.pos[3] end
    end, SavePlayerCurrentAmmoCount = function() record('ammo_save') end,
        SetWeapons = function(a)
            record('set_weapons', a)
            if c.weaponReject then return end
            local v = a[1]
            if v.primaryHip then env.vars.weapons[0] = env.TppEquip[v.primaryHip]
            elseif v.primaryBack then env.vars.weapons[1] = env.TppEquip[v.primaryBack]
            elseif v.secondary then env.vars.weapons[2] = env.TppEquip[v.secondary] end
        end, SetMissionStartAmmoCount = function() record('ammo_load') end}
    env.TppClock = {SetTime = function(s)
        record('time', s); local h, m, sec = s:match('^(%d+):(%d+):(%d+)$'); env.vars.clock = tonumber(h) * 3600 + tonumber(m) * 60 + tonumber(sec)
    end}
    env.TppWeather = {ForceRequestWeather = function(t, s) record('weather', t, s) end,
        CancelForceRequestWeather = function() record('weather_auto') end}
    env.Vector3 = function(x, y, z) return {x, y, z} end
    env.Quat = {RotationY = function(y) return {yaw = y} end}
    env.TppPickable = {DropItem = function(a) record('drop', a); if c.dropRefuse then return false end end}
    local ids, positions, life = {sol_a = 5, sol_b = 6, sol_u = 7}, {[5] = {11, 20, 30}, [6] = {13, 20, 30}, [7] = {10, 20, 30}}, {[5] = 0, [6] = 0, [7] = 255}
    c.life, c.positions, c.ids = life, positions, ids
    env.GameObject = {NULL_ID = 65535, GetGameObjectId = function(kind, name)
        check(kind == 'TppSoldier2', 'soldier typed locator lookup'); return ids[name] or 65535
    end, SendCommand = function(id, a)
        record(a.id, id, a)
        if a.id == 'GetLifeStatus' then return life[id] end
        if a.id == 'GetPosition' then
            local p = positions[id]
            if not p then return nil end
            return {GetX = function() return p[1] end, GetY = function() return p[2] end, GetZ = function() return p[3] end}
        end
        if a.id == 'ChangeLifeState' and not c.deferDead then life[id] = a.state end
    end}
    env.Fox = {StrCode32 = function(s) return s == 'sol_a' and -1 or s == 'sol_b' and 2147483648 or 99 end}
    env.TppEnemy = {SetEnable = function(id) record('enable', id) end, SetDisable = function(id) record('disable', id) end,
        IsRecovered = function(name) return c.recovered == name end,
        GetActionStatus = function(id) if c.actionUnknown then return nil end; return c.fultoned == id and 123 or 0 end,
        ACTION_STATUS = {NORMAL = 0, FULTON_RECOVERD = 123}}
    env.e = {OnUpdate = function(...) c.updates = c.updates + 1; c.args = {...}; return 'original', nil, 'tail' end,
        OnTerminate = function(...) c.terms = c.terms + 1; return 'terminated', nil, ... end}
    env.loadstring = function(s)
        check(#s < 256, 'receipt byte limit')
        if s == "return 'MGS_COOP_DEBUG_CMD_V1'" then
            if c.literal then return function() return 'MGS_COOP_DEBUG_CMD_V1' end end
            local command = c.command; c.command = nil; return function() return command end
        end
        local request, payload = s:match("^return 'MGS_COOP_DEBUG_PARTNER_V1' %-%- (%d+) (.+)$")
        if request then record('partner', payload); return function() return c.partnerRefuse ~= true end end
        local id, ok, msg = s:match("^return 'MGS_COOP_DEBUG_LOG_V1' %-%- (%d+) ([01]) ([%w_=,.;:%-]+)$")
        if id then c.logs[#c.logs + 1] = {tonumber(id), tonumber(ok), msg}; return function() end end
        local ammo, mask = s:match("^return 'MGS_COOP_DEBUG_STATE_V1' %-%- ([01]) (%d+)$")
        check(ammo ~= nil, 'strict result/state receipt'); c.state = {tonumber(ammo), tonumber(mask)}; return function() end
    end
    local chunk = assert(loadstring(source)); setfenv(chunk, env); chunk()
    chunk = assert(loadstring(equipment)); setfenv(chunk, env); chunk()
    c.env, c.D = env, env.e.MgsCoopDebug
    function c:tick()
        local a, b, d = env.e.OnUpdate('arg')
        check(a == 'original' and b == nil and d == 'tail' and self.args[1] == 'arg', 'original args/results preserved')
    end
    function c:send(text, extra)
        local a = {id = self.id, text = text, host = self.host}; for k, v in pairs(extra or {}) do a[k] = v end
        self.id = self.id + 1; self.command = a; self:tick(); return self.logs[#self.logs]
    end
    function c:count(name) local n = 0; for _, v in ipairs(self.calls) do if v[1] == name then n = n + 1 end end; return n end
    return c
end
local c = fixture()
c.literal = true; c:tick(); check(#c.calls == 0 and #c.logs == 0, 'literal bridge inert')
c.literal = false
check(c:send('position')[2] == 1 and c.logs[#c.logs][3]:find('yaw=%-60'), 'position/yaw output')
check(c:send('bookmark 7')[2] == 1 and c.state[2] == 128, 'bookmark mask')
c.env.vars.playerPosX = 80
check(c:send('return 7')[3] == 'submitted_warp' and c:count('warp') == 1, 'bookmark warp submits')
check(c.calls[#c.calls][2].pos[1] == 10 and c.calls[#c.calls][2].rotY == -60, 'bookmark captured pose')
check(c:send('infinite_ammo 1')[3] == 'busy_action' and c:count('infinite') == 0, 'pending warp serializes actions')
c.env.vars.playerPosX = 10; c:tick()
check(c.logs[#c.logs][3]:find('warp_position_readback', 1, true), 'warp checks actual position')
check(c:send('infinite_ammo 1')[2] == 1 and c.state[1] == 1, 'ammo force state')
check(c:send('infinite_ammo 0')[2] == 1 and c.state[1] == 0, 'ammo release state')
check(c:send('weapon hip EQP_WP_30101')[3] == 'submitted_equip;equip=301;native_slot=5;ammo=30;stock=90', 'resident weapon actual runtime API and diagnostic submitted')
local eq = c.calls[#c.calls][2]
check(eq.equipId == 301 and eq.ammo == 30 and eq.stock == 90 and eq.toActive and not eq.dropPrevEquip, 'native equip request schema')
check(c.env.vars.weapons[0] == 301 and c.env.vars.weapons[1] == 8 and c.env.vars.weapons[2] == 101, 'unrelated native weapon slots preserved')
check(c:count('ammo_save') == 0 and c:count('set_weapons') == 0 and c:count('ammo_load') == 0, 'no vars metadata pretending to equip')
c:tick(); check(c.logs[#c.logs][3] == 'equip_inventory_readback;equip=301;native_slot=5;ammo=30;stock=90;active=1;render_unverified', 'native inventory readback reports unchanged active slot without claiming render')
local readCalls = #c.calls
check(c:send('inventory')[3] == 'inventory;hip=301;back=8;secondary=101;active=1' and #c.calls == readCalls, 'read-only current inventory uses actual weapon slots')
check(c:send('drop_item EQP_WP_30101 1')[3]:find('submitted_pickup', 1, true) and c:count('drop') == 1, 'equipped pickup drop issued')
local item = c.calls[#c.calls][2]
check(item.equipId == 301 and item.number == 1 and item.position[1] == 10 and item.position[2] == 21 and item.position[3] == 30, 'pickup actual current equipment/position')
check(item.rotation.yaw == 0 and item.linearVelocity[2] == 2 and item.angularVelocity[2] == 2, 'exact retail pickup physics schema')
for _, pair in ipairs({{'hip', 301}, {'back', 8}, {'secondary', 101}}) do
    check(c:send('drop_item current_' .. pair[1] .. ' 2')[2] == 1, 'current gun pickup accepts ' .. pair[1])
    check(c.calls[#c.calls][2].equipId == pair[2] and c.calls[#c.calls][2].number == 2, 'pickup resolves actual ' .. pair[1] .. ' slot')
end
check(c:send('weapon secondary EQP_WP_10004')[3] == 'weapon_not_resident', 'new nonresident weapon cannot bypass loading')
check(c:send('time 86399')[2] == 1 and c.env.vars.clock == 86399, 'clock boundary')
check(c:send('weather rainy')[2] == 1 and c:count('weather') == 1, 'weather submits exact API')
check(c:send('weather auto')[2] == 1 and c:count('weather_auto') == 1, 'weather cancel')
check(c:send('enemy_nearest')[3]:find('nearest_ent=4294967295', 1, true), 'signed hash formatted unsigned')
check(c:send('enemy_enable 4294967295')[2] == 1 and c:count('enable') == 1, 'u32 max authored hash accepted')
check(c:send('enemy_disable 2147483648')[2] == 1 and c:count('disable') == 1, 'large unsigned authored hash')
check(c:send('enemy_enable 123')[3] == 'enemy_not_authored', 'arbitrary actor refused')
check(c:send('enemy_kill_nearest')[3]:find('dead_readback=1', 1, true) and c.life[5] == 3, 'nearest soldier exact dead enum/readback')
check(c.life[7] == 255, 'unknown soldier not mutated')
c.deferDead = true
check(c:send('clear_enemies 50')[3]:find('submitted=1;dead_readback=0', 1, true), 'dead nearest skipped and submitted batch does not fake readback')
check(c:send('partner stage=3 apply=xform look=net dm=map')[3] == 'partner_applied' and c:count('partner') == 1, 'partner action dispatched only after owner guard')
c.partnerRefuse = true
check(c:send('partner stage=3')[3] == 'partner_refused', 'native partner refusal explicit')
local controls = fixture()
local function partnerPreset(text)
    local n = controls:count('partner')
    check(controls:send(text)[3] == 'partner_applied' and controls:count('partner') == n + 1, 'UI preset dispatches: ' .. text)
    check(controls.calls[#controls.calls][2] == text:sub(9), 'partner payload preserved after owner guard')
end
for _, text in ipairs({'partner stage=0', 'partner stage=3 source=net look=net',
    'partner stage=3 source=net apply=xform anim=off input=off dm=map look=net',
    'partner stage=3 source=net apply=warp anim=off input=off dm=off look=net', 'partner dmstop=1'}) do partnerPreset(text) end
for _, move in ipairs({'xform', 'warp', 'hybrid', 'input'}) do
    for _, anim in ipairs({'off', 'net', 'mirror'}) do
        for _, dm in ipairs({'map', 'off', 'on'}) do
            partnerPreset('partner stage=3 source=net apply=' .. move .. ' input=' .. (move == 'input' and 'net' or 'off')
                .. ' anim=' .. anim .. ' aset=qs dm=' .. dm .. ' look=net')
        end
    end
end
for clip = 0, 12 do partnerPreset('partner dm=on dmplay=' .. clip) end
for _, text in ipairs({'teleport 1 2 3 nan', 'teleport 100001 2 3 0', 'teleport 1e2 2 3 0', 'step 101 0 0',
    'infinite_ammo 2', 'bookmark -1', 'bookmark 1.5', 'return 8', 'return 1', 'time 86400', 'time -1', 'time 1.1',
    'weapon hip EQP_ITEM_1', 'weapon hip EQP_WP_99999', 'weapon head EQP_WP_30101', 'weather unknown',
    'enemy_enable 4294967296', 'enemy_enable -1', 'clear_enemies 0', 'clear_enemies 101', 'heal', 'godmode 1',
    'drop_item EQP_WP_10004 1', 'drop_item EQP_WP_30101 0', 'drop_item EQP_WP_30101 11', 'drop_item EQP_WP_30101 1.5',
    'drop_item EQP_UNKNOWN 1', 'drop_item EQP_WP_30101;evil 1',
    'drop_item current_head 1', 'drop_item current_hip 11',
    'partner login=1', 'partner stage=3;evil', 'partner stage="3"', 'partner stage=3 steam_p2p=1',
    'partner stage=3 stage=0', 'partner rate=30', 'partner interp=1', 'partner offset=1', 'partner partner=1',
    'partner dmplay=1;evil', 'partner input="net"', 'partner dmstop=1 extra', 'inventory extra',
    'spawn_enemy soldier', 'os.execute("bad")', 'time 1;evil', 'time 1 extra', '', 'time\n1', string.rep('a', 201)}) do
    local count = c:count('warp') + c:count('infinite') + c:count('set_weapons') + c:count('time') + c:count('weather') + c:count('enable') + c:count('disable') + c:count('equip') + c:count('drop') + c:count('ChangeLifeState') + c:count('partner')
    check(c:send(text)[2] == 0, 'invalid command refused: ' .. text)
    check(count == c:count('warp') + c:count('infinite') + c:count('set_weapons') + c:count('time') + c:count('weather') + c:count('enable') + c:count('disable') + c:count('equip') + c:count('drop') + c:count('ChangeLifeState') + c:count('partner'), 'refusal has no game mutation')
end
local function gated(field, value, reason)
    local f = fixture(); f[field] = value; check(f:send('infinite_ammo 1')[3] == reason and #f.calls == 0, reason .. ' refuses before bindings')
end
gated('loading', true, 'gameplay'); gated('demo', true, 'demo')
gated('invalid', true, 'player'); gated('heli', true, 'helicopter'); gated('sitting', true, 'on_foot')
local restored = fixture(); restored.ended = true
check(restored:send('bookmark 0')[2] == 1, 'Continue/fresh-start flag is not a gameplay readiness gate')
for _, m in ipairs({30011, 40010, 40020, 40050, 10010, 0}) do local f = fixture({mission = m}); check(f:send('position')[3] == 'mission', 'unsupported mission ' .. m) end
local f = fixture({host = false}); f.env.e.MgsCoopWorld = {paired = true, host = false}
for _, text in ipairs({'time 1', 'weather rainy', 'enemy_enable 4294967295', 'enemy_kill_nearest', 'clear_enemies 50'}) do
    check(f:send(text)[3] == 'host_only' and #f.calls == 0, 'guest world mutation refused')
end
check(f:send('infinite_ammo 1')[2] == 1, 'guest local ammo allowed')
f.env.e.MgsCoopWorld = {paired = false, host = true}
check(f:send('weather rainy')[3] == 'host_only', 'native role enforced while world0/unpaired')
f = fixture({mission = 30020}); check(f:send('weather foggy')[3] == 'africa_fog_unsupported', 'Africa fog excluded')
f = fixture(); f.env.Player.GetAmmoStockBySlot = nil
check(f:send('weapon hip EQP_WP_30101')[3] == 'missing_GetAmmoStockBySlot' and #f.calls == 0, 'native weapon group preflight before mutation')
f = fixture(); f.env.TppEquip.GetAmmoInfo = function() return 'invalid' end
check(f:send('weapon hip EQP_WP_30101')[3] == 'weapon_metadata' and #f.calls == 0, 'invalid metadata refuses')
f = fixture(); f.env.TppEquip.GetAmmoInfo = function() return 11, nil, 90, 0, 0, 0 end
check(f:send('weapon hip current')[3] == 'weapon_metadata' and #f.calls == 0, 'metadata hole refuses before mutation')
f = fixture(); f.weaponReject = true; check(f:send('weapon hip current')[3]:find('submitted_equip;', 1, true), 'void native request not completion')
for i = 1, 300 do f:tick() end
check(f.logs[#f.logs][3] == 'equip_readback_timeout;equip=301;native_slot=5;ammo=30;stock=90', 'no engine inventory change is not fake success')
f = fixture(); f.equipRefuse = true; check(f:send('weapon hip current')[3] == 'equip_refused' and not f.D.pending, 'explicit native equip refusal')
f = fixture(); check(f:send('weapon secondary current')[3] == 'submitted_equip;equip=101;native_slot=9;ammo=30;stock=90', 'current secondary uses resident ID and native slot')
f:tick(); check(f.logs[#f.logs][3]:find('equip_inventory_readback'), 'current secondary native confirmation')
f = fixture(); f.env.vars.weapons[0] = nil
check(f:send('inventory')[3] == 'inventory_unreadable' and #f.calls == 0, 'unreadable inventory is refused without mutation')
f = fixture(); f.env.Vector3 = setmetatable({}, {__call = function(_, x, y, z) return {x, y, z} end})
check(f:send('drop_item current_hip 1')[2] == 1 and f:count('drop') == 1, 'callable native constructor table supports exact retail Vector3 syntax')
f = fixture(); f.env.Vector3 = newproxy(true); getmetatable(f.env.Vector3).__call = function(_, x, y, z) return {x, y, z} end
check(f:send('drop_item current_hip 1')[2] == 1 and f:count('drop') == 1, 'callable native constructor userdata supports exact retail Vector3 syntax')
f = fixture(); f.env.Vector3 = {}
check(f:send('drop_item current_hip 1')[3] == 'pickup_constructor_error;vector_type=table' and f:count('drop') == 0, 'noncallable constructor refuses before drop mutation')
f = fixture(); f.env.Vector3 = function() error('constructor failure') end
check(f:send('drop_item current_hip 1')[3] == 'pickup_constructor_error;vector_type=function' and f:count('drop') == 0, 'constructor exception is bounded and refuses pickup')
f = fixture(); f.env.Vector3 = nil
check(f:send('drop_item current_hip 1')[3] == 'missing_Vector3' and f:count('drop') == 0, 'missing constructor refuses pickup')
f = fixture(); f.env.TppDefine.WEAPONSLOT = {PRIMARY_HIP = 4, PRIMARY_BACK = 6, SECONDARY = 7}
f.env.vars.weapons = {[4] = 65535, [6] = 0, [7] = 104}
check(f:send('inventory')[3] == 'inventory;hip=65535;back=0;secondary=104;active=1', 'inventory honors engine slot enums including empty IDs')
f.env.vars.currentInventorySlot = nil
check(f:send('inventory')[3]:find(';active=unknown', 1, true), 'missing active slot is explicit rather than invented')
f = fixture(); f.life[5] = 3
check(f:send('enemy_enable 4294967295')[3] == 'enemy_terminal_unknown' and f:count('enable') == 0, 'dead soldier never revived')
f.life[5] = 255; check(f:send('enemy_enable 4294967295')[3] == 'enemy_terminal_unknown', 'unreadable actor not recycled')
f.life[5] = 0; f.recovered = 'sol_a'; check(f:send('enemy_enable 4294967295')[3] == 'enemy_terminal_unknown', 'saved Fulton never revived')
f.recovered = nil; f.fultoned = 5; check(f:send('enemy_enable 4294967295')[3] == 'enemy_terminal_unknown', 'Fulton action never recycled')
f.fultoned = nil; f.actionUnknown = true
check(f:send('enemy_enable 4294967295')[3] == 'enemy_terminal_unknown', 'unknown action slot never recycled')
f.actionUnknown = nil; f.env.TppGameObject.NPC_LIFE_STATE_DEAD = nil
check(f:send('enemy_enable 4294967295')[3] == 'enemy_terminal_unknown', 'missing terminal enum cannot revive actor')
f = fixture(); f.life[5] = 3
check(f:send('enemy_kill_nearest')[3]:find('submitted=1;dead_readback=1', 1, true) and f.life[6] == 3, 'kill nearest skips corpse for farther living guard')
f = fixture(); f.env.mvars.ene_soldierDefine.cp_a = {}
for i = 1, 20 do
    local name = 'corpse_' .. i; f.env.mvars.ene_soldierDefine.cp_a[i] = name
    f.ids[name], f.life[20 + i], f.positions[20 + i] = 20 + i, 3, {10, 20, 30}
end
check(f:send('clear_enemies 50')[3]:find('submitted=1;dead_readback=1', 1, true) and f.life[6] == 3, 'clear budget ignores over16 closer corpses')
f = fixture(); f.heli = true
check(f:send('partner stage=3')[3] == 'helicopter' and f:count('partner') == 0, 'partner handler cannot bypass gameplay guard')
f = fixture(); f.ammoRefuse = true; check(f:send('infinite_ammo 1')[3] == 'ammo_refused' and f.state[1] == 0, 'explicit ammo refusal')
f = fixture(); f.warpError = true
check(f:send('teleport 1 2 3 0')[3] == 'binding_error', 'binding exception reported')
f:tick(); check(f.updates == 2, 'original update survives debug error')
f = fixture(); f:send('infinite_ammo 1'); f:send('weather rainy'); f:send('bookmark 1')
f.env.vars.missionCode = 30020; f:tick()
check(not f.D.infinite and f.state[2] == 0 and f:count('infinite') == 2 and f:count('weather_auto') == 1, 'mission change releases toggles and bookmarks')
f:send('infinite_ammo 1'); f:send('bookmark 2')
local a, b, d = f.env.e.OnTerminate('term_arg')
check(a == 'terminated' and b == nil and d == 'term_arg' and f.terms == 1, 'terminate preserves varargs/results')
check(not f.D.infinite and f.state[2] == 0 and f:count('infinite') == 4, 'terminate releases ammo and bookmarks')
f = fixture(); f:send('infinite_ammo 1'); local id = f.logs[#f.logs][1]
check(f:send('infinite_ammo 0', {id = id})[3] == 'stale_command' and f:count('infinite') == 1, 'duplicate id no replay')
check(f:send('infinite_ammo 0', {id = id - 1})[3] == 'stale_command', 'older id no replay')
check(f:send('infinite_ammo 0', {mission = 30020})[3] == 'stale_mission' and f:count('infinite') == 1, 'queued old mission no mutation')
f = fixture(); f.id = 4294967295; check(f:send('position')[1] == 4294967295, 'u32 id max')
f.id = 1; check(f:send('position')[2] == 1, 'bounded sequence wrap')
f = fixture(); f:send('teleport 100 200 300 90')
for i = 1, 300 do f:tick() end
check(f.logs[#f.logs][3] == 'warp_readback_timeout', 'warp finite retry budget')
f = fixture(); f:send('teleport 100 200 300 90'); f.env.vars.missionCode = 30020; f:tick()
check(f.D.pending == nil and f:count('warp') == 1, 'mission change cancels old warp')
f = fixture(); f.warpNow = true; check(f:send('step -5 2 0')[3] == 'submitted_warp', 'bounded relative teleport')
f:tick(); check(f.env.vars.playerPosX == 5 and f.env.vars.playerPosY == 22 and f.logs[#f.logs][3]:find('readback'), 'relative teleport actual position readback')
print('debug_menu_lua_check: PASS ' .. checks .. ' checks')
