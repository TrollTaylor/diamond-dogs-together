-- Lua 5.1 debug executor regression: the owner update is the only dispatcher.
-- Args: debug_menu.lua, debug_equipment.lua, debug_enemy.lua, debug_player.lua,
-- debug_spawn.lua (splice order). No game or saves are used.
local checks = 0
local function check(ok, why) checks = checks + 1; if not ok then error(checks .. ': ' .. why, 2) end end
local chunks = {}
for i = 1, 5 do
    chunks[i] = assert(io.open(assert(arg[i]), 'rb')):read('*a')
    check(#chunks[i]:gsub('\r\n', '\n') <= 16000, 'fragment native LF byte limit ' .. arg[i])
end
local function fixture(options)
    options = options or {}
    local c = {calls = {}, logs = {}, state = {}, menu = {}, id = 1, updates = 0, terms = 0, host = options.host ~= false}
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
    env.TppCheckPoint = {GetCheckPointLocator = function(key)
        check(type(key) == 'string', 'retail checkpoint lookup requires authored name')
        record('checkpoint', key)
        if c.checkpointError then error('locator unloaded') end
        local p = c.checkpoints and c.checkpoints[key]
        if p then return {p[1], p[2], p[3]}, p[4] end
    end}
    -- Distinct raw values: the chunks must map enums by name, never by number.
    env.TppGameObject = {NPC_LIFE_STATE_DEAD = 3, NPC_LIFE_STATE_SLEEP = 1, NPC_LIFE_STATE_FAINT = 4, NPC_LIFE_STATE_DYING = 2,
        PHASE_SNEAK = 0, PHASE_CAUTION = 1, PHASE_EVASION = 2, PHASE_ALERT = 3}
    env.HighSpeedCamera = {RequestEvent = function(a) record('slowmo', a) end, RequestToCancel = function() record('slowmo_cancel') end}
    env.TppMarker = {Enable = function(...) record('mark', ...) end, Disable = function(id) record('unmark', id) end}
    env.TppDefine = {WEAPONSLOT = {PRIMARY_HIP = 0, PRIMARY_BACK = 1, SECONDARY = 2},
        WEATHER = {SUNNY = 0, CLOUDY = 1, RAINY = 2, SANDSTORM = 3, FOGGY = 4}}
    env.TppEquip = {EQP_WP_30101 = 301, EQP_WP_10101 = 101, EQP_WP_10004 = 104,
        GetAmmoInfo = function(id) record('metadata', id); return 11, 30, 90, 0, 0, 0 end}
    env.TppPlayer = {Warp = function(a)
        record('warp', a); if c.warpError then error(c.warpFailure or 'warp error') end
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
        if name == nil then record('cp_lookup', kind); return c.cpIds and c.cpIds[kind] or 65535 end
        check(kind == 'TppSoldier2', 'soldier typed locator lookup'); return ids[name] or 65535
    end, SendCommand = function(id, a)
        record(a.id, id, a)
        if type(id) == 'table' then
            check(id.type == 'TppSoldier2' and a.id == 'GetGameObjectIdUsingRoute', 'typed route query only')
            if c.routeError then error('route query failed') end
            return c.routeIds and c.routeIds[a.route]
        end
        if a.id == 'GetLifeStatus' then return life[id] end
        if a.id == 'GetStatus' then return c.status and c.status[id] end
        if a.id == 'GetVehicleGameObjectId' then return c.vehicle and c.vehicle[id] end
        if a.id == 'GetPosition' then
            if c.positionUnknown then return nil end
            local p = positions[id]
            if not p then return nil end
            local v = {}
            for i, axis in ipairs({'GetX', 'GetY', 'GetZ'}) do
                local component = i
                v[axis] = function(self) check(self == v, 'native Vector3 method requires receiver'); return p[component] end
            end
            return v
        end
        if a.id == 'ChangeLifeState' and not c.deferDead then life[id] = a.state end
        if a.id == 'SetSneakRoute' and not c.routeRefuse then positions[id] = {20, 20, 40} end
        if c.spawnError and a.id == 'SetCautionRoute' then error('route failed') end
    end}
    env.Fox = {StrCode32 = function(s) return s == 'sol_a' and -1 or s == 'sol_b' and 2147483648 or 99 end}
    env.TppEnemy = {SetEnable = function(id) record('enable', id) end, SetDisable = function(id) record('disable', id) end,
        IsRecovered = function(name) return c.recovered == name end,
        GetActionStatus = function(id) check(type(id) == 'number', 'retail action state accepts numeric game object ID'); if c.actionUnknown then return nil end; return c.fultoned == id and 123 or 0 end,
        ACTION_STATUS = {NORMAL = 0, FULTON_RECOVERD = 123}, LIFE_STATUS = {NORMAL = 0}}
    env.e = {OnUpdate = function(...) c.updates = c.updates + 1; c.args = {...}; return 'original', nil, 'tail' end,
        OnTerminate = function(...) c.terms = c.terms + 1; return 'terminated', nil, ... end}
    env.loadstring = function(s)
        check(#s < 256, 'receipt byte limit')
        if s == "return 'MGS_COOP_EQUIPMENT_OBSERVATION_QUERY_V1'" then
            c.observationQueries = (c.observationQueries or 0) + 1
            return function() return c.observationValue end
        end
        if s:find("return 'MGS_COOP_EQUIPMENT_OBSERVATION_RECEIPT_V1' -- ", 1, true) == 1 then
            c.observationReceipts = (c.observationReceipts or 0) + 1
            return function() return c.observationAccepted end
        end
        if s == "return 'MGS_COOP_DEBUG_CMD_V1'" then
            if c.literal then return function() return 'MGS_COOP_DEBUG_CMD_V1' end end
            local command = c.command; c.command = nil; return function() return command end
        end
        local request, payload = s:match("^return 'MGS_COOP_DEBUG_PARTNER_V1' %-%- (%d+) (.+)$")
        if request then record('partner', payload); return function() return c.partnerRefuse ~= true end end
        request, payload = s:match("^return 'MGS_COOP_DEBUG_ENEMY_V1' %-%- (%d+) (.+)$")
        if request then c.menu[#c.menu + 1] = {tonumber(request), payload}; return function() return not c.menuRefuse end end
        if s == "return 'MGS_COOP_DEBUG_POLL_V1'" then c.polls = (c.polls or 0) + 1; return function() end end
        -- Native set-transform receipt (native/soldier_warp.cpp); default: lab key off.
        local w = {s:match("^return 'MGS_COOP_DEBUG_WARP_V1' %-%- (%d+) (%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+)$")}
        if #w == 6 then
            for i = 1, 6 do w[i] = tonumber(w[i]) end
            c.warps = c.warps or {}; c.warps[#c.warps + 1] = w
            local answer = c.warp and c.warp(w) or {refused = 'disabled'}
            if answer.h == 1 then positions[w[2]] = {w[3] / 1000, w[4] / 1000, w[5] / 1000} end
            return function() return answer end
        end
        check(not s:find('MGS_COOP_DEBUG_WARP_V1', 1, true), 'strict warp receipt grammar: ' .. s)
        local id, ok, msg = s:match("^return 'MGS_COOP_DEBUG_LOG_V1' %-%- (%d+) ([012]) ([%w_=,.;:%-]+)$")
        if id then c.logs[#c.logs + 1] = {tonumber(id), tonumber(ok), msg}; return function() end end
        local text = s:match("^return 'MGS_COOP_DEBUG_STATE_V1' %-%- (.+)$")
        local fields = {}
        for v in (text or ''):gmatch('%S+') do fields[#fields + 1] = tonumber(v) or v end
        check(#fields == 13 and text:match('^[01] %d+ %d+ [a-z]+ %d+ [01] [01] [01] %d+ %-?%d+ %-?%d+ [a-z_]+ [0-3]$'),
            'strict result/state receipt: ' .. tostring(text))
        c.state = fields; c.stateCount = (c.stateCount or 0) + 1; return function() end
    end
    for i = 1, 5 do local chunk = assert(loadstring(chunks[i])); setfenv(chunk, env); chunk() end
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
check(c.logs[#c.logs][2] == 2, 'submitted warp is an interim ok receipt')
local polls = c.polls or 0
c:send('infinite_ammo 1')
check(c:count('infinite') == 0 and c.polls == polls + 1 and c.command ~= nil, 'pending warp polls; the next command waits in the queue')
check(not c.logs[#c.logs][3]:find('busy_action', 1, true), 'no busy_action refusal')
c.env.vars.playerPosX = 10; c:tick()
check(c.logs[#c.logs - 1][3]:find('warp_position_readback', 1, true) and c.logs[#c.logs - 1][2] == 1, 'warp checks actual position (final)')
check(c.logs[#c.logs][3] == 'infinite_ammo_1' and c:count('infinite') == 1, 'queued command runs after the readback')
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
    'enemy_enable 4294967296', 'enemy_enable -1', 'clear_enemies 0', 'clear_enemies 101', 'heal', 'godmode 2', 'slowmo 2 1', 'forward 101', 'time_add 1.5',
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
check(f:send('teleport 1 2 3 0')[3] == 'binding_error;detail=warp_error', 'binding exception reports bounded sanitized detail')
f:tick(); check(f.updates == 2, 'original update survives debug error')
f = fixture(); f.warpError, f.warpFailure = true, 'bad "argument"\n' .. string.rep('x', 300)
local detail = f:send('teleport 1 2 3 0')[3]
check(detail:find('binding_error;detail=bad__argument__', 1, true) == 1 and #detail <= 160 and not detail:find('["\n]'),
    'long binding detail stays bounded and cannot inject receipt syntax')
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
local function spawnFixture(options)
    local s = fixture(options)
    -- Guards 40 m and 50 m away: inside the 30-500 m placement band.
    s.positions[5], s.positions[6] = {50, 20, 30}, {60, 20, 30}
    s.env.mvars.ene_soldierDefine = {afgh_test_cp = {'sol_a', 'sol_b', 'sol_u'}, quest_cp = {'sol_quest'}}
    s.env.mvars.ene_routeSetsDefine = {afgh_test_cp = {sneak_day = {group_b = {'rt_second', 'rt_first'}, group_a = {'rt_first'}},
        caution = {group_c = {'rt_caution'}}}, quest_cp = {sneak_day = {group = {'rt_quest'}}}}
    return s
end
local function routeDefinitions(count, reverse)
    local groups = {}
    for offset = 1, count do
        local i = reverse and count + 1 - offset or offset
        local key = 'group_' .. i % 3
        groups[key] = groups[key] or {}
        groups[key][#groups[key] + 1] = string.format('rt_%04d', i)
    end
    return {afgh_test_cp = {sneak_day = groups, caution = {duplicate = {'rt_0001', 'rt_0001', 'bad/name'}}}}
end
local function selectedRoutes(s)
    local result = {}
    for _, receipt in ipairs(s.menu) do
        local index, route = receipt[2]:match('^route (%d+) (.+)$')
        if index then result[tonumber(index)] = route end
    end
    return result
end
for _, count in ipairs({65, 83, 512}) do
    local ordered = spawnFixture()
    ordered.env.mvars.ene_routeSetsDefine = routeDefinitions(count, false)
    check(ordered:send('enemy_camps')[3] == 'camps=1;more=0;local_pool=1'
        and ordered.menu[2][2] == 'camp afgh_test_cp 40000 64', 'route menu truncates only after validating and sorting')
    check(ordered:send('enemy_routes afgh_test_cp')[3] == 'routes=64;point=0', 'bounded sorted route menu')
    local before = selectedRoutes(ordered)
    ordered.env.mvars.ene_routeSetsDefine = routeDefinitions(count, true)
    ordered.menu = {}
    check(ordered:send('enemy_routes afgh_test_cp')[3] == 'routes=64;point=0', 'reconstructed route groups retain selected indices')
    local after = selectedRoutes(ordered)
    for i = 1, 64 do
        check(before[i] == string.format('rt_%04d', i) and after[i] == before[i], 'group/array reconstruction cannot change the first64 sorted routes')
    end
    check(ordered:send('spawn_enemy afgh_test_cp 64')[2] == 2, 'selected route remains usable after reconstruction')
    for _, entry in ipairs(ordered.calls) do
        if entry[1] == 'SetSneakRoute' then check(entry[3].route == 'rt_0064', 'spawn resolves the displayed sorted route index') end
    end
end
for _, reverse in ipairs({false, true}) do
    local overflow = spawnFixture()
    overflow.env.mvars.ene_routeSetsDefine = routeDefinitions(513, reverse)
    for _, command in ipairs({'enemy_camps', 'enemy_routes afgh_test_cp', 'spawn_enemy closest', 'goto_camp afgh_test_cp'}) do
        check(overflow:send(command)[3] == 'spawn_route_limit_512', 'oversized route source refuses both reconstruction orders')
    end
    check(#overflow.menu == 0 and overflow:count('disable') == 0 and overflow:count('SetSneakRoute') == 0
        and overflow:count('warp') == 0 and overflow.D.pending == nil, 'overflow emits no arbitrary subset or gameplay mutation')
end
local sp = spawnFixture()
check(sp:send('enemy_camps')[3] == 'camps=1;more=0;local_pool=1', 'bounded authored camp listing')
check(sp.menu[1][2] == 'clear 30010' and sp.menu[2][2] == 'camp afgh_test_cp 40000 3' and sp.menu[3][2] == 'done', 'camp metadata uses loaded distance and deduplicated routes')
check(sp:send('enemy_routes afgh_test_cp')[3] == 'routes=3;point=0', 'authored route selector lists safe point0')
check(sp.menu[5][2] == 'route 1 rt_caution' and sp.menu[7][2] == 'route 3 rt_second', 'route indices sorted and stable')
check(sp:send('spawn_enemy afgh_test_cp 2')[3] == 'submitted_patrol_guard;ent=4294967295;scope=local' and sp.logs[#sp.logs][2] == 2, 'selected camp spawns from existing living pool')
check(sp:count('disable') == 1 and sp:count('SetSneakRoute') == 1 and sp:count('SetCautionRoute') == 1 and sp:count('SetAlertRoute') == 1 and sp:count('enable') == 1 and sp:count('SetForceRealize') == 1, 'exact retail disable/routes/enable then force-realize calls')
for _, entry in ipairs(sp.calls) do if entry[1] == 'SetSneakRoute' then check(entry[3].route == 'rt_first' and entry[3].point == 0, 'only resident chosen route point0') end end
sp:tick(); check(sp.D.pending ~= nil, 'actor enable is not immediate placement proof')
sp:tick(); check(sp.logs[#sp.logs][3]:find('guard_position_readback', 1, true) and sp.logs[#sp.logs][3]:find(';far=0;borrowed=0;come=0;sync=none;why=world_off;route=rt_first', 1, true) and sp.logs[#sp.logs][2] == 1, 'actual guard position final receipt')
check(sp.menu[#sp.menu][2] == 'spawned 20000 20000 40000 4294967295', 'actual spawn location metadata unsigned ID')
check(sp:send('goto_spawn')[3] == 'submitted_warp' and sp.calls[#sp.calls][2].pos[1] == 23 and sp.calls[#sp.calls][2].pos[2] == 20.5, 'go to confirmed spawn uses owner-thread player Warp nearby')
sp.env.vars.playerPosX, sp.env.vars.playerPosY, sp.env.vars.playerPosZ = 23, 20.5, 40; sp:tick()
check(sp:send('spawn_enemy closest')[3]:find('ent=2147483648', 1, true), 'second placement does not recycle prior selected guard')
sp = spawnFixture({host = false})
check(sp:send('spawn_enemy closest')[3] == 'host_only' and sp:count('disable') == 0, 'guest cannot mutate pool even unpaired')
sp = spawnFixture(); sp.life[5], sp.life[6] = 3, 255
check(sp:send('spawn_enemy closest')[3] == 'spawn_no_guard' and sp:count('disable') == 0, 'dead/unknown pool never revived')
sp = spawnFixture(); sp.life[5], sp.life[6] = 2, 4
check(sp:send('spawn_enemy closest')[3] == 'spawn_no_guard', 'unconscious/dying pool not used as combat guard')
sp = spawnFixture(); sp.recovered, sp.fultoned = 'sol_a', 6
check(sp:send('spawn_enemy closest')[3] == 'spawn_no_guard', 'saved/action Fulton pool never reused')
sp = spawnFixture(); sp.positions[5], sp.positions[6] = {1000, 20, 30}, {2000, 20, 30}
check(sp:send('spawn_enemy closest')[3] == 'spawn_no_guard' and sp:count('disable') == 0, 'distant actor packs cannot be assumed resident')
sp = spawnFixture(); sp.spawnError = true
check(sp:send('spawn_enemy closest')[3] == 'spawn_binding_error' and sp:count('enable') == 1, 'partial route failure restores enabled guard instead of leaving disabled')
sp = spawnFixture(); sp:send('spawn_enemy closest'); sp.positionUnknown = true
for i = 1, 120 do sp:tick() end
check(sp.logs[#sp.logs][3] == 'spawn_position_timeout' and sp.D.enemy.last == nil, 'unreadable placement cannot invent a location')
sp = spawnFixture(); for i = 1, 8 do sp.D.enemy.used['old_' .. i] = true end
check(sp:send('spawn_enemy closest')[3] == 'spawn_pool_limit_8' and sp:count('disable') == 0, 'bounded pool avoids unbounded force-realized guard lifetime')
sp = spawnFixture(); sp:send('spawn_enemy closest'); sp.env.vars.missionCode = 30020; sp:tick()
check(sp.D.pending == nil and sp.D.enemy.last == nil and next(sp.D.enemy.used) == nil, 'travel clears pool reservations and old spawn coordinates')
sp = spawnFixture(); sp.menuRefuse = true
check(sp:send('enemy_camps')[3] == 'enemy_menu_unavailable', 'missing native metadata handler refuses camp UI cleanly')
for _, text in ipairs({'spawn_enemy unknown_cp 1', 'spawn_enemy afgh_test_cp 0', 'spawn_enemy afgh_test_cp 65',
    'spawn_enemy afgh_test_cp 1.5', 'spawn_enemy quest_cp 1', 'spawn_enemy closest extra', 'enemy_routes nope',
    'spawn_enemy afgh_test_cp 1;evil', 'enemy_camps extra', 'goto_spawn extra'}) do
    sp = spawnFixture(); check(sp:send(text)[2] == 0 and sp:count('disable') == 0, 'invalid pool placement has no actor mutation')
end
sp = spawnFixture(); check(sp:send('goto_spawn')[3] == 'spawn_position_unavailable', 'no invented spawn coordinates')
sp = spawnFixture(); sp.warpNow = true; sp.positions[5], sp.positions[6] = {1000, 20, 30}, {2000, 20, 30}
check(sp:send('goto_camp afgh_test_cp')[3] == 'submitted_warp' and sp:count('disable') == 0 and sp:count('SetSneakRoute') == 0,
    'camp travel uses readable distant guard position without enemy mutation')
check(sp.env.vars.playerPosX == 1003 and sp.env.vars.playerPosY == 20.5 and sp.env.vars.playerPosZ == 30,
    'camp travel preserves current yaw and offsets confirmed actor position')
sp:tick(); check(sp.logs[#sp.logs][3]:find('warp_position_readback', 1, true), 'camp travel verifies actual player position')
sp = spawnFixture({host = false}); check(sp:send('goto_camp afgh_test_cp')[3] == 'submitted_warp' and sp:count('disable') == 0,
    'guest can use local camp travel without mutating guards')
sp = spawnFixture(); sp.life[5], sp.life[6] = 3, 255
check(sp:send('goto_camp afgh_test_cp')[3] == 'camp_position_unavailable' and sp:count('warp') == 0,
    'dead or unknown camp positions never authorize travel')
sp = spawnFixture(); sp.positions[5], sp.positions[6] = {99999, 20, 30}, {100000, 20, 30}
check(sp:send('goto_camp afgh_test_cp')[3] == 'camp_position_bounds' and sp:count('warp') == 0, 'offset remains inside teleport bounds')
sp = spawnFixture(); sp.life[5], sp.life[6] = 255, 255; sp.warpNow = true
sp.env.mvars.loc_locationCommonCheckPointList = {test = {'CHK_authored_test'}}
sp.checkpoints = {CHK_authored_test = {1010, 20, 30, 90}}
check(sp:send('enemy_camps')[3] == 'camps=1;more=0;local_pool=1' and sp.menu[2][2] == 'camp afgh_test_cp 1000000 3',
    'unloaded guard camp distance uses actual declared player checkpoint')
check(sp:send('goto_camp afgh_test_cp')[3] == 'submitted_warp;via=checkpoint' and sp:count('disable') == 0,
    'wilderness camp travel falls back without enabling an unknown soldier')
check(sp.env.vars.playerPosX == 1010 and sp.env.vars.playerPosY == 20 and sp.env.vars.playerPosZ == 30 and sp.calls[#sp.calls][2].rotY == 90,
    'checkpoint travel preserves authored player XYZ and degree yaw')
sp:tick(); check(sp.logs[#sp.logs][3]:find('warp_position_readback', 1, true), 'checkpoint fallback verifies actual player arrival')
sp = spawnFixture(); sp.env.mvars.loc_locationCommonCheckPointList = {test = {'CHK_authored_test'}}
sp.checkpoints = {CHK_authored_test = {1000, 20, 30, 90}}
check(sp:send('goto_camp afgh_test_cp')[3] == 'submitted_warp' and sp:count('checkpoint') == 0,
    'current readable guard remains preferred over checkpoint')
for _, bad in ipairs({{100001, 20, 30, 90}, {10, 20, 30, 36001}, {10, 20, 0/0, 90}, {10, 20, 30}}) do
    sp = spawnFixture(); sp.life[5], sp.life[6] = 255, 255
    sp.env.mvars.loc_locationCommonCheckPointList = {test = {'CHK_authored_test'}}; sp.checkpoints = {CHK_authored_test = bad}
    check(sp:send('goto_camp afgh_test_cp')[3] == 'camp_position_unavailable' and sp:count('warp') == 0,
        'unreadable/invalid checkpoint coordinates or yaw cannot authorize teleport')
end
sp = spawnFixture(); sp.life[5], sp.life[6] = 255, 255; sp.checkpointError = true
sp.env.mvars.loc_locationCommonCheckPointList = {test = {'CHK_authored_test'}}
check(sp:send('goto_camp afgh_test_cp')[3] == 'camp_position_unavailable' and sp:count('warp') == 0,
    'unloaded throwing locator refuses without guessing a camp position')
sp = spawnFixture(); sp.life[5], sp.life[6] = 255, 255
sp.env.mvars.loc_locationCommonCheckPointList = {test = {'CHK_authored_test'}}; sp.env.TppCheckPoint = nil
check(sp:send('goto_camp afgh_test_cp')[3] == 'camp_position_unavailable', 'missing retail checkpoint binding refuses cleanly')
sp = spawnFixture(); sp.life[5], sp.life[6] = 255, 255; sp.env.vars.missionCode = 30020
sp.env.mvars.loc_locationCommonCheckPointList = {test = {'CHK_authored_test'}}
sp.checkpoints = {CHK_authored_test = {1000, 20, 30, 90}}
check(sp:send('goto_camp afgh_test_cp')[3] == 'camp_position_unavailable' and sp:count('checkpoint') == 0,
    'different location prefix cannot use stale camp checkpoint metadata')
sp = spawnFixture(); sp.life[5], sp.life[6] = 255, 255
sp.ids.sol_n, sp.life[8] = 8, 255
sp.env.mvars.ene_soldierDefine.afgh_bridgeNorth_ob = {'sol_n'}
sp.env.mvars.ene_routeSetsDefine.afgh_bridgeNorth_ob = {sneak_day = {group = {'rt_outpost'}}}
sp.env.mvars.loc_locationCommonCheckPointList = {test = {'CHK_test'}, bridgeNorth = {'CHK_bridgeNorth'}}
sp.checkpoints = {CHK_test = {1010, 20, 30, 0}, CHK_bridgeNorth = {13, 24, 30, -180}}
check(sp:send('enemy_camps')[3] == 'camps=2;more=0;local_pool=1' and sp.menu[2][2] == 'camp afgh_bridgeNorth_ob 5000 1',
    'wilderness distance sorts an actual authored outpost checkpoint before a distant camp')
check(sp:send('goto_camp afgh_bridgeNorth_ob')[3] == 'submitted_warp;via=checkpoint' and sp.calls[#sp.calls][2].rotY == -180,
    'outpost suffix maps through declared base key and preserves negative yaw')
for _, text in ipairs({'goto_camp unknown_cp', 'goto_camp quest_cp', 'goto_camp', 'goto_camp afgh_test_cp extra', 'goto_camp bad/name'}) do
    sp = spawnFixture(); check(sp:send(text)[2] == 0 and sp:count('warp') == 0 and sp:count('disable') == 0, 'invalid camp travel has no mutation')
end
f = fixture(); f.warpNow = true; check(f:send('step -5 2 0')[3] == 'submitted_warp', 'bounded relative teleport')
f:tick(); check(f.env.vars.playerPosX == 5 and f.env.vars.playerPosY == 22 and f.logs[#f.logs][3]:find('readback'), 'relative teleport actual position readback')
-- Read-only equipment observation can begin before the player is ready. A
-- transient refusal must not permanently consume the mission's observation.
local ob = fixture()
ob:tick(); ob:tick()
check(ob.observationQueries == 1 and ob.D.equipmentObservationMission == 30010,
    'disabled observation is queried once and remains inert')
ob = fixture(); ob.observationValue = false
ob:tick()
check(ob.observationQueries == 1 and ob.D.equipmentObservationMission == nil,
    'loading observation refusal retains a later attempt')
for i = 1, 29 do ob:tick() end
check(ob.observationQueries == 1, 'observation retry is throttled')
ob.observationValue = 'MGS_COOP_EQUIPMENT_OBSERVATION_V1;mission=30010;read_only=1;'
ob.observationAccepted = false; ob:tick()
check(ob.observationQueries == 2 and ob.observationReceipts == 1 and ob.D.equipmentObservationMission == nil,
    'refused native receipt does not report completion')
ob.observationAccepted = true
for i = 1, 30 do ob:tick() end
check(ob.observationQueries == 3 and ob.observationReceipts == 2 and ob.D.equipmentObservationMission == 30010,
    'accepted observation completes only after owner receipt')
for i = 1, 40 do ob:tick() end
check(ob.observationQueries == 3 and #ob.calls == 0, 'completed observation causes no API mutations or repeat queries')
ob.env.vars.missionCode = 30020; ob.observationValue = false; ob:tick()
check(ob.observationQueries == 4 and ob.D.equipmentObservationAttempts == 1,
    'mission transition clears observation retry history')
for i = 1, 3600 do ob:tick() end
check(ob.observationQueries == 123 and ob.D.equipmentObservationMission == 30020,
    'persistent refusal stops at exactly 120 attempts per mission')
-- Overhaul: dispatch, STATE-12, POLL, interim receipts, player and spawn chunks.
f = fixture()
check(f.D.player and f.D.player.handles.heal and f.D.spawn and f.D.spawn.handles.spawn_enemy and f.D.spawn.handles.enemy,
    'player and spawn modules registered for dispatch')
for i = 1, 40 do f:tick() end
check(f.stateCount == 2 and #f.state == 13 and f.state[4] == 'none' and f.state[12] == 'ok' and f.state[13] == 0,
    'idle STATE is 13 fields and rate limited')
f.env.vars.clock = 3600; f:tick()
check(f.stateCount == 3 and f.state[9] == 3600, 'changed STATE emits after 5 updates')
-- Name tag HUD bits (field 13): demo 1, iDroid 2 (TppUiCommand query), absent binding 0.
for i = 1, 5 do f:tick() end
f.demo = true; for i = 1, 6 do f:tick() end
check(f.state[13] == 1, 'a playing demo sets HUD bit 1')
f.demo = nil; f.env.TppUiCommand = {IsMbDvcTerminalOpened = function() return true end}; for i = 1, 6 do f:tick() end
check(f.state[13] == 2, 'an open iDroid sets HUD bit 2')
f.env.TppUiCommand = {IsMbDvcTerminalOpened = function() error('ui torn down') end}; for i = 1, 6 do f:tick() end
check(f.state[13] == 0, 'a failing iDroid query counts as closed')
f.env.TppUiCommand = nil; for i = 1, 6 do f:tick() end
check(f.state[13] == 0, 'no TppUiCommand: HUD bits 0')
f = fixture(); f:send('teleport 1 2 3 0'); local warpId = f.logs[#f.logs][1]
check(f.state[3] == warpId and f.state[4] == 'warp', 'STATE reports the pending action')
f.env.vars.playerPosX = nil; f:tick()
check(f.logs[#f.logs][1] == warpId and f.logs[#f.logs][3]:find('pending_binding_error', 1, true) == 1 and f.D.pending == nil,
    'pending binding error is reported against the pending command id')
f = fixture(); f.sitting = true
local st = f:send('status')[3]
check(st:find('status;mission=30010;guard=on_foot;host=1;pending=none;god=0;ammo=0;fulton=0;slowmo=0;clock=0;life=-1,-1', 1, true) == 1,
    'status answers outside on-foot gameplay with the guard reason')
check(fixture({mission = 40010}):send('status')[3] == 'mission', 'status needs a supported mission')
-- heal / godmode
f = fixture(); check(f:send('heal')[3] == 'life_vars_unreadable', 'heal refuses unreadable life')
f.env.vars.playerLife, f.env.vars.playerLifeMax = 0, 100
check(f:send('heal')[3] == 'life_dead' and f.env.vars.playerLife == 0, 'heal never revives')
f.env.vars.playerLife = 40
check(f:send('heal')[3] == 'heal;life=100,100;unverified' and f.env.vars.playerLife == 100, 'heal writes max and reads it back')
check(f:send('godmode 1')[3] == 'godmode_1;autoheal;lethal_unprotected;unverified' and f.state[6] == 1, 'godmode on')
f.env.vars.playerLife = 40; f:tick(); check(f.env.vars.playerLife == 100, 'auto-heal refills before intake')
f.loading = true; f.env.vars.playerLife = 40; f:tick(); check(f.env.vars.playerLife == 40, 'no auto-heal outside CheckMissionState')
f.loading = nil; f.demo = true; f:tick(); check(f.env.vars.playerLife == 40, 'no auto-heal during a demo')
f.demo = nil; f.env.vars.playerLife = 0; f:tick(); check(f.env.vars.playerLife == 0, 'auto-heal never writes at 0 life')
check(f:send('godmode 0')[3] == 'godmode_0', 'godmode off')
f.env.vars.playerLife = 40; f:tick(); check(f.env.vars.playerLife == 40, 'godmode off stops refills')
f:send('godmode 1'); f.env.vars.missionCode = 30020; f:tick(); check(not f.D.god, 'mission change clears godmode')
-- fulton, slowmo, forward, goto_partner, time_add
f = fixture(); check(f:send('fulton_infinite 1')[3] == 'missing_SetFultonCountInfinity', 'missing Fulton API is named')
f.env.Player.SetFultonCountInfinity = function(v) f.calls[#f.calls + 1] = {'fulton', v} end
check(f:send('fulton_infinite 1')[3] == 'fulton_infinite_1;unverified' and f.calls[#f.calls][2] == true and f.state[7] == 1, 'Fulton infinity')
f.env.vars.missionCode = 30020; f:tick(); check(f.calls[#f.calls][1] == 'fulton' and f.calls[#f.calls][2] == false, 'stop restores Fulton count')
f = fixture()
check(f:send('slowmo 0.25 3')[3] == 'submitted_slowmo;rate=0.25;seconds=3.0;experimental', 'slowmo submits')
local ev, keys = f.calls[#f.calls][2], 0
for _ in pairs(ev) do keys = keys + 1 end
check(keys == 3 and ev.continueTime == 3 and ev.worldTimeRate == 0.25 and ev.localPlayerTimeRate == 0.25, 'slowmo uses exactly the retail keys')
check(f.state[8] == 1 and f:send('slowmo off')[3] == 'slowmo_off' and f:count('slowmo_cancel') == 1 and f.state[8] == 0, 'slowmo off cancels')
f = fixture(); f.env.vars.playerRotY = 90
check(f:send('forward 5')[3] == 'submitted_warp', 'forward warps')
local w = f.calls[#f.calls][2]
check(math.abs(w.pos[1] - 15) < 1e-6 and math.abs(w.pos[3] - 30) < 1e-6 and w.rotY == 90, 'forward at yaw 90 moves +x')
f = fixture(); check(f:send('goto_partner')[3] == 'partner_pose_unavailable' and f:count('warp') == 0, 'goto_partner needs pp')
check(f:send('goto_partner', {pp = {20000, 20000, 30000, 40}})[3] == 'submitted_warp;via=partner;age_ms=40', 'goto_partner submits')
w = f.calls[#f.calls][2]
check(math.abs(w.pos[1] - 18) < 1e-6 and math.abs(w.pos[2] - 20.3) < 1e-6 and math.abs(w.pos[3] - 30) < 1e-6 and math.abs(w.rotY - 90) < 1e-6,
    'goto_partner lands 2 m from the partner, facing him')
f = fixture(); check(f:send('goto_partner', {pp = {1e9, 0, 0, 40}})[3] == 'partner_pose_bounds', 'partner pose bounds')
f = fixture(); f.env.vars.clock = 86000
check(f:send('time_add 1000')[3] == 'time_readback;sync=none' and f.env.vars.clock == 600, 'time_add wraps')
f.env.e.MgsCoopWorld = {paired = true, eff = 3}
check(f:send('time 43200')[3] == 'time_readback;sync=world', 'clock sync tag at world level 3')
check(fixture({host = false}):send('time_add 60')[3] == 'host_only', 'guest time_add refused')
-- spawn_enemy here / closest / route
local function guardsFixture(options)
    local s = spawnFixture(options)
    s.env.mvars.ene_soldierDefine.afgh_other_cp = {'sol_c'}
    s.env.mvars.ene_routeSetsDefine.afgh_other_cp = {sneak_day = {group = {'rt_other'}}}
    s.ids.sol_c, s.life[9], s.positions[9] = 9, 0, {100, 20, 30}
    return s
end
sp = guardsFixture(); sp.routeIds = {rt_second = {6}}
check(sp:send('spawn_enemy here')[3] == 'submitted_guard_here;ent=4294967295;scope=local;mode=route;why=disabled' and sp.logs[#sp.logs][2] == 2,
    'spawn here falls back to the route snap while the native key is off')
check(#sp.warps == 1 and sp.warps[1][2] == 5, 'one native attempt for the nearest guard')
for _, entry in ipairs(sp.calls) do if entry[1] == 'SetSneakRoute' then check(entry[3].route == 'rt_second' and entry[3].point == 0, 'nearest scored route') end end
check(sp.state[4] == 'spawn', 'spawn pending kind')
sp:tick(); sp:tick()
local final = sp.logs[#sp.logs][3]
check(final == 'guard_here;ent=4294967295;d=14.1;moved=31.6;far=0;mode=route;why=disabled;come=0;sync=none;why=world_off;route=rt_second' and sp.logs[#sp.logs][2] == 1,
    'spawn here final readback: ' .. final)
check(sp.D.enemy.routePos.rt_second ~= nil and sp.D.enemy.last ~= nil, 'route position cached')
sp.routeError = true
local queries = sp:count('GetGameObjectIdUsingRoute')
check(sp:send('spawn_enemy here')[3] == 'submitted_guard_here;ent=2147483648;scope=local;mode=route;why=disabled', 'cached route reused with a failing route query')
check(sp:count('GetGameObjectIdUsingRoute') == queries + 3, 'cached route is not queried again')
for _, entry in ipairs(sp.calls) do if entry[1] == 'SetSneakRoute' then check(entry[3].route == 'rt_second', 'cache wins') end end
sp = guardsFixture(); sp.positions[5] = {12, 20, 30}
check(sp:send('spawn_enemy here')[3]:find('ent=2147483648', 1, true), 'guards within 15 m are skipped')
sp = guardsFixture(); sp.life[5], sp.life[6] = 3, 3
check(sp:send('spawn_enemy afgh_test_cp 1')[3] == 'submitted_patrol_guard;ent=99;scope=local', 'own camp dead: nearest other guard borrowed')
sp:tick(); sp:tick(); check(sp.logs[#sp.logs][3]:find(';borrowed=1;', 1, true), 'borrowed flag in the final result')
sp = guardsFixture(); sp.positions[9] = {600, 20, 30}; sp.life[5], sp.life[6] = 3, 3
check(sp:send('spawn_enemy afgh_test_cp 1')[3] == 'spawn_no_guard', 'borrowing is capped at 500 m')
sp = guardsFixture(); sp:send('spawn_enemy here come'); sp:tick(); sp:tick()
check(sp:count('SetDiscoveryPlayer') == 1 and sp.logs[#sp.logs][3]:find(';come=1;', 1, true), 'come sends discovery once')
local long = string.rep('r', 64)
sp = guardsFixture(); sp.env.mvars.ene_routeSetsDefine.afgh_test_cp = {sneak_day = {group = {long}}}
sp:send('spawn_enemy afgh_test_cp 1'); sp:tick(); sp:tick()
final = sp.logs[#sp.logs][3]
check(#final <= 160 and final:find('guard_position_readback;', 1, true) == 1 and final:find(';route=rrrr', 1, true), '64-character route stays within 160')
sp = guardsFixture(); for i = 1, 8 do sp.D.enemy.used['old_' .. i] = true end
check(sp:send('spawn_enemy here')[3] == 'spawn_pool_limit_8' and sp:count('disable') == 0, 'pool cap applies to spawn here')
sp = fixture(); check(sp:send('spawn_enemy here')[3] == 'spawn_no_guard', 'no camp guard pool refuses spawn here')
sp = guardsFixture(); sp.positions[5], sp.positions[6] = {11, 20, 30}, {12, 20, 30}
check(sp:send('spawn_enemy closest')[3] == 'submitted_patrol_guard;ent=99;scope=local', 'closest falls through to the next camp')
for _, entry in ipairs(sp.calls) do if entry[1] == 'SetSneakRoute' then check(entry[3].route == 'rt_other', 'closest uses the camp route') end end
sp:tick(); sp:tick(); check(sp.logs[#sp.logs][3]:find(';borrowed=0;', 1, true), 'fall-through guard is the camp own guard')
sp = guardsFixture({host = false}); check(sp:send('spawn_enemy here')[3] == 'host_only' and sp:count('disable') == 0, 'guest spawn refused')
check(sp.warps == nil, 'guest never asks for the native placement')
-- Native placement (lab.ini debug_soldier_warp=1): 5 m ahead, facing the player.
local function warpFixture(answers, options)
    local s = guardsFixture(options)
    s.env.TppEnemy.SetEnableRestrictNotice = function(id) s.calls[#s.calls + 1] = {'restrict', id, true} end
    s.env.TppEnemy.SetDisableRestrictNotice = function(id) s.calls[#s.calls + 1] = {'restrict', id, false} end
    local n = 0
    s.warp = function() n = n + 1; return answers[math.min(n, #answers)] end
    return s
end
local function forced(s, value)
    local n = 0
    for _, v in ipairs(s.calls) do if v[1] == 'SetForceRealize' and v[3].forceRealize == value then n = n + 1 end end
    return n
end
sp = warpFixture({{r = 0, h = 1, leaf = 'pinned'}})
check(sp:send('spawn_enemy here')[3] == 'submitted_guard_here;ent=4294967295;scope=local;mode=warp' and sp.logs[#sp.logs][2] == 2, 'warp interim')
local w = sp.warps[1]
check(w[1] == sp.id - 1 and w[2] == 5 and w[3] == 5670 and w[4] == 20200 and w[5] == 32500 and w[6] == 2094,
    'receipt: command id, soldier id, 5 m along the -60 degree heading, yaw turned to face the player')
check(sp:count('SetSneakRoute') == 0 and sp:count('disable') == 0 and forced(sp, nil) == 0, 'landed at once: no route snap, no force realize')
check(sp:count('restrict') == 1 and sp.D.enemy.ignored.sol_a, 'not hostile without come')
sp:tick(); sp:tick()
final = sp.logs[#sp.logs][3]
check(final == 'guard_here;ent=4294967295;d=5.0;moved=44.4;far=0;mode=warp;come=0;sync=none;why=world_off;frames=0;leaf=pinned' and sp.logs[#sp.logs][2] == 1,
    'warp final readback: ' .. final)
check(sp.menu[#sp.menu][2] == 'spawned 5670 20200 32500 4294967295' and sp:count('SetDiscoveryPlayer') == 0, 'spawn metadata at the warped position')
check(sp:send('goto_spawn')[3] == 'submitted_warp', 'go to spawned guard after a warp')
sp = warpFixture({{r = -1, h = 0, leaf = 'pinned'}, {r = -1, h = 0, leaf = 'pinned'}, {r = 0, h = 1, leaf = 'engine'}})
check(sp:send('spawn_enemy here come')[3]:find(';mode=warp', 1, true) and forced(sp, nil) == 1, 'unrealized guard is force-realized once')
check(sp:count('restrict') == 0, 'come keeps noticing')
sp:tick(); sp:tick(); check(#sp.warps == 3 and sp.D.pending ~= nil, 'retries each update until handled')
sp:tick(); sp:tick()
final = sp.logs[#sp.logs][3]
check(final:find('guard_here;ent=4294967295;d=5.0;', 1, true) == 1 and final:find(';mode=warp;come=1;sync=none;why=world_off;frames=2;leaf=engine', 1, true),
    'come warp final: ' .. final)
check(forced(sp, false) == 1 and sp:count('SetDiscoveryPlayer') == 1, 'force realize released, then discovery')
sp = warpFixture({{r = -1, h = 0}, {refused = 'dispatch'}})
sp:send('spawn_enemy here'); sp:tick()
check(sp.logs[#sp.logs][3] == 'submitted_guard_here;ent=4294967295;scope=local;mode=route;why=dispatch' and sp.logs[#sp.logs][2] == 2,
    'native refusal mid-retry falls back to the route snap')
check(forced(sp, false) == 1 and sp:count('SetSneakRoute') == 1, 'warp force realize released before the route snap')
sp:tick(); sp:tick(); check(sp.logs[#sp.logs][3]:find(';mode=route;why=dispatch;come=0;', 1, true), 'route fallback final keeps why')
sp = warpFixture({{r = -1, h = 0}})
sp:send('spawn_enemy here')
for _ = 1, 149 do sp:tick() end
check(sp:count('SetSneakRoute') == 0, 'retry budget not yet spent')
sp:tick(); check(sp.logs[#sp.logs][3]:find(';mode=route;why=realize_timeout', 1, true) and #sp.warps == 151, '150-update realize budget')
sp = warpFixture({{r = 0, h = 1}}); sp.vehicle = {[5] = 42}
check(sp:send('spawn_enemy here')[3]:find('ent=2147483648', 1, true) and sp.warps[1][2] == 6, 'riding guard skipped')
sp = warpFixture({{r = 0, h = 1}}); sp.vehicle = {[5] = 65535}
check(sp:send('spawn_enemy here')[3]:find('ent=4294967295', 1, true), 'NULL_ID vehicle means on foot')
sp = warpFixture({{r = 0, h = 1}}); sp.env.TppGameObject.NPC_STATE_DISABLE = 9; sp.status = {[5] = 9}
check(sp:send('spawn_enemy here')[3]:find('ent=2147483648', 1, true), 'disabled guard skipped')
sp = warpFixture({{r = 0, h = 1}}); sp.positions[5] = {26, 20, 30}
check(sp:send('spawn_enemy here')[3]:find('ent=4294967295', 1, true), 'a guard 16 m away is eligible')
sp = warpFixture({{r = 0, h = 1}})
local rawLoad = sp.env.loadstring
sp.env.loadstring = function(text)
    if text:find('MGS_COOP_DEBUG_WARP_V1', 1, true) then return function() return 'MGS_COOP_DEBUG_WARP_V1' end end
    return rawLoad(text)
end
check(sp:send('spawn_enemy here')[3]:find(';mode=route;why=native_unavailable', 1, true), 'old loader (literal receipt) falls back')
sp = warpFixture({{r = -1, h = 0}}); sp:send('spawn_enemy here'); sp.life[5] = 3; sp:tick()
check(sp.logs[#sp.logs][3] == 'spawn_terminal_unknown' and forced(sp, false) == 1 and sp.D.pending == nil, 'guard died while waiting')
-- Native context refusals end the command: no route snap once the command or mission ended natively.
for _, why in ipairs({'command', 'mission'}) do
    sp = warpFixture({{refused = why}})
    check(sp:send('spawn_enemy here')[3] == 'spawn_context;why=' .. why and sp.logs[#sp.logs][2] == 0, 'context refusal is final: ' .. why)
    check(sp:count('SetSneakRoute') == 0 and sp:count('disable') == 0 and forced(sp, nil) == 0 and sp.D.pending == nil, 'no route snap: ' .. why)
end
sp = warpFixture({{r = -1, h = 0}, {refused = 'mission'}})
sp:send('spawn_enemy here'); sp:tick()
check(sp.logs[#sp.logs][3] == 'spawn_context;why=mission' and sp.logs[#sp.logs][2] == 0 and sp.D.pending == nil, 'mid-retry mission end is final')
check(forced(sp, false) == 0 and sp:count('SetSneakRoute') == 0, 'mid-retry context end: no guard command at all during teardown')
-- A pending warp dropped by the context check releases force realize only while its mission is in gameplay.
sp = warpFixture({{r = -1, h = 0}}); sp:send('spawn_enemy here'); sp.sitting = true; sp:tick()
check(sp.logs[#sp.logs][3] == 'action_context_changed' and forced(sp, false) == 1 and sp.D.pending == nil, 'off foot: force realize released')
sp = warpFixture({{r = -1, h = 0}}); sp:send('spawn_enemy here'); sp.loading = true; sp:tick()
check(sp.logs[#sp.logs][3] == 'action_context_changed' and forced(sp, false) == 0, 'mission not in gameplay: no guard command')
sp = warpFixture({{r = -1, h = 0}}); sp:send('spawn_enemy here'); sp.env.vars.missionCode = 30020; sp:tick()
check(sp.D.pending == nil and forced(sp, false) == 0, 'mission change: no guard command to the old mission')
sp = warpFixture({{r = 0, h = 1}}); for i = 1, 7 do sp.D.enemy.used['old_' .. i] = true end
sp:send('spawn_enemy here'); sp:tick(); sp:tick()
check(sp:send('spawn_enemy here')[3] == 'spawn_pool_limit_8' and #sp.warps == 1, 'eight per mission includes warped guards')
-- enemy_list and enemy <op>
f = fixture(); f.life[6] = 1
check(f:send('enemy_list 50')[3] == 'nearby=2;radius=50', 'nearby list result')
check(f.menu[1][2] == 'nearby 30010 50' and f.menu[2][2] == 'soldier 1 4294967295 1000 0 0 sol_a'
    and f.menu[3][2] == 'soldier 2 2147483648 3000 2 0 sol_b' and f.menu[4][2] == 'done', 'nearby wire codes by enum name')
check(fixture():send('enemy_list 301')[3] == 'radius_args', 'nearby radius bounds')
f = fixture()
check(f:send('enemy sleep nearest')[3] == 'enemy_sleep;ent=4294967295;life_readback=2;sync=none', 'sleep nearest')
check(f.calls[#f.calls - 1][1] == 'ChangeLifeState' and f.calls[#f.calls - 1][3].state == 1, 'sleep enum by name')
f.env.e.MgsCoopWorld = {paired = true, w1 = true}
check(f:send('enemy kill nearest')[3] == 'enemy_kill;ent=4294967295;life_readback=5;sync=world', 'kill nearest with W1 sync tag')
check(f:send('enemy faint 2147483648')[3] == 'enemy_faint;ent=2147483648;life_readback=3;unverified;sync=world', 'faint by hash')
check(f:send('enemy fulton 2147483648')[3] == 'enemy_fulton;ent=2147483648;sync=world' and f:count('RequestForceFulton') == 1, 'fulton')
f = fixture()
check(f:send('enemy come nearest')[3] == 'enemy_come;ent=4294967295;hostile;unverified;sync=none;why=world_off' and f:count('SetDiscoveryPlayer') == 1, 'come')
f.env.TppEnemy.SetEnableRestrictNotice = function(id) f.calls[#f.calls + 1] = {'restrict', id, true} end
f.env.TppEnemy.SetDisableRestrictNotice = function(id) f.calls[#f.calls + 1] = {'restrict', id, false} end
check(f:send('enemy ignore nearest')[3] == 'enemy_ignore;ent=4294967295;sync=none;why=world_off' and f.calls[#f.calls][2] == 5, 'ignore uses the game object id')
check(f:send('enemy mark 2147483648')[3] == 'enemy_mark;ent=2147483648;sync=none;unverified' and f:count('mark') == 1, 'mark')
local mark = f.calls[#f.calls]
check(mark[2] == 6 and mark[3] == 0 and mark[4] == 'moving' and mark[5] == 'map_and_world_only_icon' and mark[7] == false, 'retail marker form')
f:send('enemy_list 50')
check(f.menu[#f.menu - 1][2] == 'soldier 2 2147483648 3000 0 1 sol_b' and f.menu[#f.menu - 2][2] == 'soldier 1 4294967295 1000 0 2 sol_a', 'marked and ignoring flags')
check(f:send('enemy_unmark')[3] == 'unmarked=1' and f:count('unmark') == 1, 'unmark all tracked markers')
f.env.TppGameObject.NPC_LIFE_STATE_SLEEP = nil
check(f:send('enemy sleep nearest')[3] == 'missing_NPC_LIFE_STATE_SLEEP', 'missing enum named')
check(f:send('enemy kill 123')[3] == 'enemy_not_authored' and f:send('enemy jump nearest')[3] == 'enemy_args', 'op/target validation')
f = fixture({host = false})
check(f:send('enemy info nearest')[3] == 'enemy_info;ent=4294967295;name=sol_a;cp=cp_a;state=0;distance_m=1.0', 'guest info')
check(f:send('enemy kill nearest')[3] == 'host_only' and f:count('ChangeLifeState') == 0, 'guest ops refused')
f = fixture(); f.env.e.MgsCoopWorld = {paired = true, w1 = true}
check(f:send('enemy_kill_nearest')[3] == 'submitted=1;dead_readback=1;sync=world;max=16', 'kill alias keeps text with the sync tag')
-- cp_phase
f = fixture(); f.cpIds = {cp_a = 300}
check(f:send('cp_phase cp_a alert')[3] == 'submitted_phase;cp=cp_a;phase=alert;sync=none', 'phase by name')
check(f.calls[#f.calls][1] == 'SetPhase' and f.calls[#f.calls][2] == 300 and f.calls[#f.calls][3].phase == 3, 'SetPhase enum by name')
check(f:send('cp_phase cp_x alert')[3] == 'cp_unknown' and f:send('cp_phase cp_a bogus')[3] == 'phase_args', 'phase refusals')
sp = spawnFixture(); sp.cpIds = {afgh_test_cp = 301}
check(sp:send('cp_phase nearest sneak')[3] == 'submitted_phase;cp=afgh_test_cp;phase=sneak;sync=none', 'nearest command post')
check(fixture({host = false}):send('cp_phase cp_a alert')[3] == 'host_only', 'guest phase refused')
-- Spawn sync (lua/coop/world_place.lua): the host announces through MgsCoopWorld.place.share.
local function shareStub(s, answer)
    s.shares = {}
    s.env.e.MgsCoopWorld = {place = {share = function(...)
        s.shares[#s.shares + 1] = {...}
        if answer == 'error' then error('boom') end
        return answer or ('sync=world;seq=' .. #s.shares)
    end}}
end
sp = warpFixture({{r = 0, h = 1, leaf = 'pinned'}}); shareStub(sp)
sp:send('spawn_enemy here'); sp:tick(); sp:tick()
w = sp.warps[1]
local sh = sp.shares[1]
check(sh and sh[1] == 1 and sh[2] == 4294967295 and sh[3] == w[3] and sh[4] == w[4] and sh[5] == w[5] and sh[6] == w[6] and
    sh[7] == 2 and sh[8] == nil and sh[9] == 0, 'warp announce: the exact native request, restrict-notice flag, body offset 0')
check(sp.logs[#sp.logs][3] == 'guard_here;ent=4294967295;d=5.0;moved=44.4;far=0;mode=warp;come=0;sync=world;seq=1;frames=0;leaf=pinned',
    'warp result carries sync=world;seq')
sp = warpFixture({{r = 0, h = 1, leaf = 'engine'}}); shareStub(sp)
sp:send('spawn_enemy here come'); sp:tick(); sp:tick()
check(sp.shares[1][7] == 1 and sp.logs[#sp.logs][3]:find(';mode=warp;come=1;sync=world;seq=1;frames=0;', 1, true), 'come flag announced')
sp = guardsFixture(); shareStub(sp)
sp:send('spawn_enemy here'); sp:tick(); sp:tick()
sh = sp.shares[1]
check(#sp.shares == 1 and sh[1] == 2 and sh[2] == 4294967295 and sh[7] == (sp.D.enemy.ignored.sol_a and 2 or 0) and sh[8] == 'rt_caution' and sh[3] == 20000,
    'route fallback announces kind 2 with the readback, flags and route name')
check(sp.logs[#sp.logs][3]:find(';mode=route;why=disabled;come=0;sync=world;seq=1;route=rt_caution', 1, true), 'route result tag')
sp = spawnFixture(); shareStub(sp)
sp:send('spawn_enemy afgh_test_cp 2'); sp:tick(); sp:tick()
check(sp.shares[1] and sp.shares[1][1] == 2 and sp.shares[1][7] == 0 and
    sp.logs[#sp.logs][3]:find(';come=0;sync=world;seq=1;route=' .. tostring(sp.shares[1][8]), 1, true), 'camp route placement announced')
sp = warpFixture({{r = 0, h = 1}}); shareStub(sp, 'error')
sp:send('spawn_enemy here'); sp:tick(); sp:tick()
check(sp.logs[#sp.logs][3]:find(';come=0;sync=none;why=world_error;frames=0;', 1, true) and sp.logs[#sp.logs][2] == 1,
    'a failing announce never fails the placement')
f = fixture(); shareStub(f)
f.env.TppEnemy.SetEnableRestrictNotice = function() end
f.env.TppEnemy.SetDisableRestrictNotice = function() end
check(f:send('enemy come nearest')[3] == 'enemy_come;ent=4294967295;hostile;unverified;sync=world;seq=1', 'come announced (kind 4)')
check(f:send('enemy ignore 2147483648')[3] == 'enemy_ignore;ent=2147483648;sync=world;seq=2', 'ignore announced (kind 5)')
check(f:send('enemy notice 2147483648')[3] == 'enemy_notice;ent=2147483648;sync=world;seq=3', 'notice announced (kind 6)')
check(f:send('enemy disable 2147483648')[3] == 'enemy_disable;ent=2147483648;sync=world;seq=4', 'disable announced (kind 7)')
check(f:send('enemy enable 2147483648')[3] == 'enemy_enable;ent=2147483648;sync=world;seq=5', 'enable announced (kind 8)')
local kinds = {}
for i, s in ipairs(f.shares) do kinds[i] = s[1] .. ':' .. string.format('%.0f', s[2]) end
check(table.concat(kinds, ',') == '4:4294967295,5:2147483648,6:2147483648,7:2147483648,8:2147483648', 'kinds and hashes')
check(f:send('enemy mark 2147483648')[3] == 'enemy_mark;ent=2147483648;sync=none;unverified' and #f.shares == 5, 'mark stays local')
check(f:send('enemy sleep nearest')[3]:find(';sync=none$') and #f.shares == 5, 'outcomes use the W1 ledger, not spawn sync')
check(f:send('enemy_disable 2147483648')[3] == 'submitted_local;ent=2147483648;sync=world;seq=6' and f.shares[6][1] == 7 and
    f:send('enemy_enable_nearest')[3]:find(';sync=world;seq=7$') and f.shares[7][1] == 8, 'legacy enable/disable commands announce too')
-- world_status / spawn_sync: read-only reports on both roles.
for _, host in ipairs({true, false}) do
    f = fixture({host = host})
    check(f:send('world_status')[3] == 'world_unavailable' and f:send('spawn_sync')[3] == 'world_unavailable', 'no world link')
    local seen
    f.env.e.MgsCoopWorld = {place = {report = function(cmd, D) seen = {cmd, D}; return true, cmd == 'world_status' and 'world;paired=1' or 'remote_spawn;seq=1' end}}
    check(f:send('world_status')[3] == 'world;paired=1' and seen[1] == 'world_status' and seen[2] == f.D, 'world_status report')
    check(f:send('spawn_sync')[3] == 'remote_spawn;seq=1' and seen[1] == 'spawn_sync', 'spawn_sync report')
    check(f:send('spawn_sync now')[3] == 'command_args' and f:send('world_status 1')[3] == 'command_args', 'no arguments')
end
print('debug_menu_lua_check: PASS ' .. checks .. ' checks')
