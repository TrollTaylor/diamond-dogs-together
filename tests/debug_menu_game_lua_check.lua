-- Debug actions against pinned, extracted retail function bodies. No game,
-- external service or save files are used. Inputs are local and never bundled.
-- Args: core, equipment, TppPlayer, TppClock, TppWeather, TppEnemy, TppQuest,
--       s10260_sequence, s10121_sequence, TppMission, s10030_sequence, enemy helper, TppDebug, TppCheckPoint.
local checks = 0
local function check(ok, why) checks = checks + 1; if not ok then error(checks .. ': ' .. why, 2) end end
local function read(p) local f = assert(io.open(p, 'rb')); local s = f:read('*a'); f:close(); return s end
local function slice(s, a, b)
    local first = assert(s:find(a, 1, true), a)
    local last = assert(s:find(b, first + #a, true), b)
    return s:sub(first, last - 1)
end
local env = setmetatable({}, {__index = _G})
local calls, logs, command, id, stock, stockSub = {}, {}, nil, 0, 0, 0
local function record(name, value) calls[#calls + 1] = {name, value} end
local function load(s) local f = assert(loadstring(s)); setfenv(f, env); return f() end
env.vars = {missionCode = 30020, playerPosX = -10, playerPosY = 20, playerPosZ = 30, playerRotY = -60,
    playerVehicleGameObjectId = 65535, weapons = {[0] = 301, [1] = 501, [2] = 101},
    ammoInWeapons = {[0] = 2, [1] = 1, [2] = 3}, ammoSubInWeapons = {[0] = 0, [1] = 0, [2] = 0},
    ammoStockIds = {[0] = 11, [1] = 12, [2] = 13}, ammoStockCounts = {[0] = 40, [1] = 8, [2] = 12}}
env.gvars = {sav_varRestoreForContinue = true, initAmmoStockIds = {}, initAmmoStockCounts = {}, initAmmoInWeapons = {}, initAmmoSubInWeapons = {}}
env.mvars = {ene_soldierDefine = {cp = {'sol_a'}}, ene_recoverdStateIndexByName = {sol_a = 0}}
env.svars = {seq_sequence = 2, ene_isRecovered = {[0] = false}}
env.Tpp = {IsTypeTable = function(v) return type(v) == 'table' end,
    IsTypeString = function(v) return type(v) == 'string' end, IsTypeNumber = function(v) return type(v) == 'number' end,
    IsHelicopter = function(v) return v == 42 end, SplitString = function(s, delimiter)
        local result = {}; for v in s:gmatch('[^' .. delimiter .. ']+') do result[#result + 1] = v end; return result
    end}
env.TppDemo = {IsNotPlayable = function() return false end}
env.DemoDaemon = {IsDemoPlaying = function() return false end}
env.PlayerStatus = {STAND = 1, SQUAT = 2, CRAWL = 3}
env.PlayerSlotType = {PRIMARY_1 = 5, PRIMARY_2 = 6, SECONDARY = 7}
env.PlayerInfo = {OrCheckStatus = function(a) return true end}
env.TppGameObject = {NPC_LIFE_STATE_DEAD = 3}
env.TppDefine = {WEAPONSLOT = {PRIMARY_HIP = 0, PRIMARY_BACK = 1, SECONDARY = 2, SUPPORT_0 = 4, SUPPORT_7 = 11, MAX = 12},
    WEATHER = {SUNNY = 0, CLOUDY = 1, RAINY = 2, SANDSTORM = 3, FOGGY = 4}, INITIAL_PLAYER_STATE = {RIDEON_HELICOPTER = 2}}
env.TppEquip = {EQP_WP_30101 = 301, EQP_WP_10101 = 101, EQP_WP_East_ms_010 = 501, EQP_IT_DevelopmentFile = 999,
    BL_None = 0, GetAmmoInfo = function(v) return 11, 30, 90, 0, 0, 0 end}
env.TppScriptVars = {PLAYER_AMMO_STOCK_TYPE_COUNT = 3}
env.foxmath = {DegreeToRadian = function(deg) return deg * math.pi / 180 end,
    NormalizeRadian = function(rad) return (rad + math.pi) % (2 * math.pi) - math.pi end}
env.Fox = {StrCode32 = function(s) return -1 end, Log = function() end}
env.Vector3 = setmetatable({}, {__call = function(_, x, y, z) return {x, y, z} end})
env.Quat = {RotationY = function(y) return {yaw = y} end}
local life, guardPosition = 0, {-9, 20, 30}
env.GameObject = {NULL_ID = 65535, GetGameObjectId = function(kind, name)
        check(type(kind) == 'string' and (name == nil or type(name) == 'string'), 'native locator lookup accepts names, not numeric IDs')
        return (name or kind) == 'sol_a' and 8 or 65535
    end,
    SendCommand = function(target, a)
        record(a.id, {target = target, args = a})
        if a.id == 'WarpAndWaitBlock' then
            check(type(target) == 'table' and target.type == 'TppPlayer2' and target.index == 0, 'retail warp targets local player zero')
            env.vars.playerPosX, env.vars.playerPosY, env.vars.playerPosZ = a.pos[1], a.pos[2], a.pos[3]
        elseif a.id == 'GetLifeStatus' then return life
        elseif a.id == 'GetActionStatus' then check(type(target) == 'number', 'retail action wrapper preserves numeric ID'); return 0
        elseif a.id == 'GetPosition' then
            local v = {}
            for i, axis in ipairs({'GetX', 'GetY', 'GetZ'}) do
                local component = i
                v[axis] = function(self) check(self == v, 'exact source Vector3 method requires receiver'); return guardPosition[component] end
            end
            return v
        elseif a.id == 'ChangeLifeState' then life = a.state end
        if a.id == 'SetSneakRoute' then guardPosition = {-12, 20, 35} end
    end}
env.Player = {IsInstanceValid = function() return true end,
    SetInfiniteAmmoFromScript = function(v) record('InfiniteAmmo', v) end,
    ChangeEquip = function(a)
        record('ChangeEquip', a); stock, stockSub = a.stock, a.stockSub
        local slot = a.equipId == 301 and 0 or a.equipId == 501 and 1 or 2
        env.vars.weapons[slot], env.vars.ammoInWeapons[slot], env.vars.ammoSubInWeapons[slot] = a.equipId, a.ammo, a.ammoSub
    end, GetAmmoStockBySlot = function(slot, index)
        check(slot == 5 and index == 0, 'native inventory query uses actual PlayerSlotType'); return stock, stockSub
    end}
env.WeatherManager = {NIGHT_START_CLOCK = 64800, NIGHT_END_CLOCK = 21600, REQUEST_PRIORITY_FORCE = 7, REQUEST_PRIORITY_NORMAL = 3,
    RequestWeather = function(a) record('RequestWeather', a) end, CancelRequestWeather = function(a) record('CancelWeather', a) end}
env.TppPickable = {DropItem = function(a) record('DropItem', a) end}
local playerSource, enemySource, missionSource = read(arg[3]), read(arg[6]), read(arg[10])
env.TppPlayer = load('local e={};local t=Tpp.IsTypeTable;' ..
    slice(playerSource, 'function e.Warp(', 'function e.SetForceFultonPercent(') ..
    slice(playerSource, 'function e.SetWeapons(', 'function e.SaveWeaponsToUsingTemp(') .. 'return e')
env.TppClock, env.TppWeather = load(read(arg[4])), load(read(arg[5]))
env.TppEnemy = load('local e={};local o=Tpp.IsTypeString;local l=Tpp.IsTypeNumber;local a=GameObject.NULL_ID;' ..
    'local r=GameObject.GetGameObjectId;local t=GameObject.SendCommand;' ..
    slice(enemySource, 'function e.SetEnable(', 'function e.SetEnableRestrictNotice(') ..
    slice(enemySource, 'function e.GetLifeStatus(', 'function e.ChangeLifeState(') .. 'return e')
env.TppEnemy.ACTION_STATUS = {NORMAL = 0, FULTON_RECOVERD = 100}
env.TppEnemy.LIFE_STATUS = {NORMAL = 0}
env.TppMission = load('local e={};' .. slice(missionSource, 'function e.IsMissionStart(', 'function e.IsSysMissionId(') ..
    slice(missionSource, 'function e.CheckMissionState(', 'function e.CheckMissionClearOnOutOfMissionArea(') .. 'return e')
-- Regression: IsMissionStart is false after Continue while actual readiness
-- remains true. The debug executor must accept this normal gameplay.
check(env.TppMission.IsMissionStart() == false and env.TppMission.CheckMissionState() == true, 'retail Continue readiness semantics')
local activeSource = read(arg[11])
local activeAssignment = 'vars.currentInventorySlot = TppDefine.WEAPONSLOT.SECONDARY'
check(activeSource:find(activeAssignment, 1, true) ~= nil, 'exact retail active inventory slot contract')
load(activeAssignment)
check(env.vars.currentInventorySlot == 2, 'retail active slot uses inventory enum rather than native PlayerSlotType')
-- The exact SetWeapons body only assigns vars and initial ammo metadata.
env.TppPlayer.SetWeapons({{primaryHip = 'EQP_WP_30101'}})
check(env.vars.ammoInWeapons[0] == 2 and env.gvars.initAmmoInWeapons[0] == 30, 'SetWeapons is not current runtime ammo/equip')
check(#calls == 0, 'SetWeapons calls no Player runtime equip API')
check(env.TppEnemy.GetActionStatus(8) == 0 and env.TppEnemy.GetActionStatus('sol_a') == 0,
    'unmodified retail action wrapper accepts numeric IDs and resolves authored names')
local retailVector = env.GameObject.SendCommand(8, {id = 'GetPosition'})
check(not pcall(retailVector.GetX) and retailVector:GetX() == guardPosition[1],
    'native-vector fixture rejects an unbound getter and accepts retail colon call')
-- Execute the retail gameplay launcher function to establish the real binding
-- and complete field names, without copying game-derived source into this repo.
local launcher = load('local this={};' .. slice(read(arg[8]), 'function this.GetRocketLuncher()', 'function this.UnsetEquipments()') .. 'return this')
launcher.GetRocketLuncher()
local retailEquip = calls[#calls][2]
check(retailEquip.equipId == 501 and retailEquip.stock == 8 and retailEquip.ammo == 1 and retailEquip.toActive == true, 'retail gameplay ChangeEquip contract')
check(retailEquip.dropPrevEquip == true and retailEquip.suppressorLife == 0, 'retail equip option fields')
local pickup = load('local this={};local SUB_TASK_BLUE_PRINT_NUMBER=3;' ..
    slice(read(arg[9]), 'function this.HeliItemDrop(', 'this.OffHighInterrogation =') .. 'return this')
pickup.HeliItemDrop(env.Vector3(-10, 21, 30))
local retailDrop = calls[#calls][2]
check(retailDrop.equipId == 999 and retailDrop.number == 3 and retailDrop.position[2] == 21, 'retail pickup request contract')
check(retailDrop.rotation.yaw == 0 and retailDrop.linearVelocity[2] == 2 and retailDrop.angularVelocity[2] == 2, 'retail pickup physics fields')
local questSource = read(arg[7])
for _, flag in ipairs({'true', 'false'}) do
    local statement = 'Player.SetInfiniteAmmoFromScript(' .. flag .. ')'
    local p = assert(questSource:find(statement, 1, true), 'retail shooting-practice ammo flag')
    load(questSource:sub(p, p + #statement - 1))
    check(calls[#calls][1] == 'InfiniteAmmo' and calls[#calls][2] == (flag == 'true'), 'retail infinite ammo boolean')
end
env.e = {OnUpdate = function() return 'original', nil, 77 end, OnTerminate = function() return 'term', nil, 88 end}
env.loadstring = function(s)
    check(#s < 256, 'bounded native receipt')
    if s == "return 'MGS_COOP_DEBUG_CMD_V1'" then local c = command; command = nil; return function() return c end end
    local i, ok, message = s:match("^return 'MGS_COOP_DEBUG_LOG_V1' %-%- (%d+) ([01]) ([%w_=,.;:%-]+)$")
    if i then logs[#logs + 1] = {tonumber(i), tonumber(ok), message} end
    return function() end
end
load(read(arg[1])); load(read(arg[2])); load(read(arg[12]))
local function tick() local a, b, c = env.e.OnUpdate(); check(a == 'original' and b == nil and c == 77, 'exact API fixture preserves owner return tuple') end
local function issue(text) id = id + 1; command = {id = id, text = text, mission = env.vars.missionCode, host = true}; tick(); return logs[#logs] end
check(issue('bookmark 0')[2] == 1, 'debug available after checkpoint Continue')
check(issue('teleport -4 21 37 -60')[3] == 'submitted_warp', 'exact TppPlayer.Warp issued')
local warp = calls[#calls][2].args
check(warp.id == 'WarpAndWaitBlock' and warp.pos[1] == -4 and warp.pos[3] == 37 and math.abs(warp.rotY + math.pi / 3) < 1e-9, 'retail warp degrees converted to radians')
tick(); check(logs[#logs][3]:find('warp_position_readback'), 'exact warp position accepted')
check(issue('weapon hip current')[3] == 'submitted_equip;equip=301;native_slot=5;ammo=30;stock=90', 'actual resident equip requested after Continue with numeric diagnostics')
local equip = calls[#calls][2]
for key in pairs(retailEquip) do check(equip[key] ~= nil, 'actual equip field present: ' .. key) end
check(equip.equipId == 301 and equip.stock == 90 and equip.ammo == 30 and equip.toActive and not equip.dropPrevEquip, 'refill/activate uses current resident gun and ammo metadata')
tick(); check(logs[#logs][3] == 'equip_inventory_readback;equip=301;native_slot=5;ammo=30;stock=90;active=2;render_unverified', 'native stock and magazine readback verified')
local callCount = #calls
check(issue('inventory')[3] == 'inventory;hip=301;back=501;secondary=101;active=2' and #calls == callCount, 'retail weapon vars and active slot current inventory read is read-only')
check(issue('drop_item EQP_WP_30101 2')[3]:find('submitted_pickup', 1, true), 'current-resident weapon pickup issued')
local drop = calls[#calls][2]
for key in pairs(retailDrop) do check(drop[key] ~= nil, 'pickup exact field present: ' .. key) end
check(drop.number == 2 and drop.equipId == 301 and drop.position[1] == -4 and drop.position[2] == 22 and drop.position[3] == 37, 'pickup request uses actual current position')
check(issue('time 45296')[2] == 1 and env.vars.clock == 45296, 'retail Clock.SetTime/ParseTimeString')
check(issue('weather rainy')[2] == 1, 'retail weather force wrapper')
local weather = calls[#calls][2]
check(weather.priority == 7 and weather.userId == 'Script' and weather.weatherType == 2 and weather.interpTime == 1, 'retail weather request exact priority/user/interpolation')
check(issue('weather auto')[2] == 1 and calls[#calls][1] == 'CancelWeather' and calls[#calls][2].priority == 7, 'retail weather cancel wrapper')
check(issue('enemy_enable 4294967295')[2] == 1 and calls[#calls][1] == 'SetEnabled', 'retail authored soldier enable wrapper')
local enabled = calls[#calls][2]
check(enabled.target == 8 and enabled.args.enabled == true, 'retail soldier enable target/boolean')
check(issue('enemy_disable 4294967295')[2] == 1 and calls[#calls][2].args.enabled == false, 'retail soldier disable wrapper')
env.svars.ene_isRecovered[0] = true
check(issue('enemy_enable 4294967295')[3] == 'enemy_terminal_unknown', 'retail recovered-state getter protects Fulton')
env.svars.ene_isRecovered[0] = false
env.mvars.mis_isReserveGameOver = true
check(issue('infinite_ammo 1')[3] == 'gameplay', 'actual mission GameOver gate')
env.mvars.mis_isReserveGameOver = false; env.svars.seq_sequence = 1
check(issue('infinite_ammo 1')[3] == 'gameplay', 'actual mission prepare gate')
env.svars.seq_sequence = 2
check(issue('infinite_ammo 1')[2] == 1 and calls[#calls][1] == 'InfiniteAmmo' and calls[#calls][2] == true, 'actual ammo toggle after Continue')
local a, b, c = env.e.OnTerminate()
check(a == 'term' and b == nil and c == 88, 'termination owner return tuple')
check(calls[#calls][1] == 'InfiniteAmmo' and calls[#calls][2] == false, 'terminate releases script ammo force')

-- Execute retail's authored-route placement verbatim, then compare the new
-- pooled spawner's actual command sequence against those original contracts.
env.gvars.sav_varRestoreForContinue = true
local retailDebug = load('local o={};local n=Tpp.IsTypeTable;' ..
    slice(read(arg[13]), 'function o.DEBUG_WarpVehicleAndSoldier(', 'o.DEBUG_SkipOnChangeSVarsLog=') .. 'return o')
local start = #calls
retailDebug.DEBUG_WarpVehicleAndSoldier({'sol_a'}, 'vehicle_test', 'rt_test', env.Vector3(0, 0, 0), 0, false)
local retailPlacement = {}
for i = start + 1, #calls do if calls[i][2].target == 8 then retailPlacement[#retailPlacement + 1] = calls[i][2].args end end
check(#retailPlacement == 5 and retailPlacement[1].id == 'SetEnabled' and not retailPlacement[1].enabled
    and retailPlacement[5].id == 'SetEnabled' and retailPlacement[5].enabled, 'exact retail route snap disables then re-enables guard')
local force = 't(e,{id="SetForceRealize"})'
local forceAt = assert(enemySource:find(force, 1, true), 'retail force-realize contract')
load('local t,e=GameObject.SendCommand,8;' .. enemySource:sub(forceAt, forceAt + #force - 1))
check(calls[#calls][1] == 'SetForceRealize' and calls[#calls][2].target == 8, 'exact retail force-realize public command')
env.mvars.ene_soldierDefine = {mafr_test_cp = {'sol_a'}}
env.mvars.ene_routeSetsDefine = {mafr_test_cp = {sneak_day = {priority_group = {'rt_test'}}}}
env.loadstring = function(s)
    check(#s < 256, 'bounded spawn native receipt')
    if s == "return 'MGS_COOP_DEBUG_CMD_V1'" then local c = command; command = nil; return function() return c end end
    if s:match("^return 'MGS_COOP_DEBUG_ENEMY_V1' %-%- %d+ [a-z]+ .*") or s:match("^return 'MGS_COOP_DEBUG_ENEMY_V1' %-%- %d+ done$") then return function() return true end end
    local i, ok, message = s:match("^return 'MGS_COOP_DEBUG_LOG_V1' %-%- (%d+) ([01]) ([%w_=,.;:%-]+)$")
    if i then logs[#logs + 1] = {tonumber(i), tonumber(ok), message} end
    return function() end
end
check(issue('enemy_camps')[3] == 'camps=1;more=0;local_pool=1', 'current authored route definitions feed camp UI')
start = #calls
check(issue('spawn_enemy mafr_test_cp 1')[3]:find('submitted_patrol_guard', 1, true), 'exact-source pooled guard placement requested')
local placed = {}
for i = start + 1, #calls do
    local cmd = calls[i][2].args
    if cmd.id == 'SetEnabled' or cmd.id == 'SetSneakRoute' or cmd.id == 'SetCautionRoute' or cmd.id == 'SetAlertRoute' then placed[#placed + 1] = cmd end
end
check(#placed == #retailPlacement, 'new actor placement matches exact retail command count')
for i, cmd in ipairs(retailPlacement) do for key, value in pairs(cmd) do check(placed[i][key] == value, 'exact retail placement field: ' .. key) end end
tick(); tick()
check(logs[#logs][3]:find('guard_position_readback', 1, true) and logs[#logs][3]:find('visible_unverified', 1, true), 'guard position readback does not invent live renderer proof')
check(issue('goto_spawn')[3] == 'submitted_warp' and calls[#calls][2].args.pos[1] == -9, 'exact retail player Warp near confirmed guard')
tick(); check(logs[#logs][3]:find('warp_position_readback', 1, true), 'guard teleport verifies player position separately')
local beforeCamp = #calls
check(issue('goto_camp mafr_test_cp')[3] == 'submitted_warp' and calls[#calls][2].args.pos[1] == -9,
    'camp travel calls exact retail player Warp near current authored guard')
for i = beforeCamp + 1, #calls do check(calls[i][1] ~= 'SetEnabled' and calls[i][1] ~= 'SetSneakRoute', 'camp travel has no actor mutation') end
tick(); check(logs[#logs][3]:find('warp_position_readback', 1, true), 'camp travel exact player position readback')
env.TppCheckPoint = load('local e={};local t=Tpp.IsTypeString;' ..
    slice(read(arg[14]), 'function e.GetCheckPointLocator(', 'function e.FindNearestCheckPoint(') .. 'return e')
env.Tpp.GetLocator = function(identifier, key)
    check(identifier == 'CheckPointIdentifier' and key == 'CHK_factory_Player', 'unmodified retail player checkpoint identifier and suffix')
    return {-400, 17, 800}, -90
end
env.mvars.ene_soldierDefine = {mafr_factory_cp = {'sol_a'}}
env.mvars.ene_routeSetsDefine = {mafr_factory_cp = {sneak_day = {priority_group = {'rt_test'}}}}
env.mvars.loc_locationCommonCheckPointList = {factory = {'CHK_factory'}}
life = 255
beforeCamp = #calls
check(issue('goto_camp mafr_factory_cp')[3] == 'submitted_warp;via=checkpoint', 'unmodified retail checkpoint fallback works without readable actors')
local campWarp = calls[#calls][2].args
check(campWarp.pos[1] == -400 and campWarp.pos[2] == 17 and campWarp.pos[3] == 800 and math.abs(campWarp.rotY + math.pi / 2) < 1e-9,
    'authored checkpoint coordinates and degree yaw pass through exact retail Warp conversion')
for i = beforeCamp + 1, #calls do check(calls[i][1] ~= 'SetEnabled' and calls[i][1] ~= 'SetSneakRoute', 'unloaded camp travel never mutates unreadable actor') end
tick(); check(logs[#logs][3]:find('warp_position_readback', 1, true), 'checkpoint fallback actual player readback')
print('debug_menu_game_lua_check: PASS ' .. checks .. ' checks')
