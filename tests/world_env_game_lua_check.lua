-- World environment against pinned retail Clock/Weather modules. No game,
-- network, UI, packages or saves. Args: world_env, TppClock, TppWeather.
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ': ' .. why, 2) end end
local function read(p) local f = assert(io.open(p, 'rb')); local s = f:read('*a'); f:close(); return s end
local source = read(arg[1])
check(#source:gsub('\r', '') <= 16000, 'world environment literal stays within 16K')
local OP = {CLOCK = 32, WEATHER = 33, DIGEST = 21, GDIGEST = 22, RESYNC = 5, RESYNC_END = 6, RESYNC_NEEDED = 118}
local function fixture(o)
    o = o or {}
    local c = {now = 100, demo = false, inGame = true, requests = {}, cancelled = {}, paused = {}, tx = {}, logs = {}, accept = true}
    local env = setmetatable({}, {__index = _G})
    env.e, env.vars = {}, {clock = 43200, weather = 0}
    env.Tpp = {IsTypeTable = function(t) return type(t) == 'table' end,
        SplitString = function(s, d) local a = {}; for v in s:gmatch('[^' .. d .. ']+') do a[#a + 1] = v end; return a end}
    env.Fox = {StrCode32 = function() return 1 end}
    env.TppDefine = {WEATHER = {SUNNY = 0, CLOUDY = 1, RAINY = 2, SANDSTORM = 3, FOGGY = 4}}
    env.WeatherManager = {NIGHT_START_CLOCK = 64800, NIGHT_END_CLOCK = 21600,
        REQUEST_PRIORITY_FORCE = 7, REQUEST_PRIORITY_NORMAL = 3,
        RequestWeather = function(v) c.requests[#c.requests + 1] = v end,
        CancelRequestWeather = function(v) c.cancelled[#c.cancelled + 1] = v end,
        PauseNewWeatherChangeRandom = function(v) c.paused[#c.paused + 1] = v end}
    env.DemoDaemon = {IsDemoPlaying = function() return c.demo end}
    local function load(s) local f = assert(loadstring(s)); setfenv(f, env); return f() end
    env.TppClock, env.TppWeather = load(read(arg[2])), load(read(arg[3]))
    if o.original then o.original(env, c) end
    c.originalClock, c.originalForce, c.originalCancel = env.TppClock.SetTime, env.TppWeather.ForceRequestWeather, env.TppWeather.CancelForceRequestWeather
    local W = {OP = OP, handlers = {}, bindHooks = {}, startHooks = {}, stopHooks = {}, pairedHooks = {}, unpairedHooks = {},
        active = o.level ~= 0, host = o.host ~= false, level = o.level or 3, eff = o.level or 3, paired = true,
        feature = {clock = true, weather = o.weather or 'all'}, api = {}, cpOrder = {}, entOrder = {}, ent = {},
        enemy = {s = {seq = 0, sum = 0, count = 0, seen = 0, ledger = {}, req = {}}}, t0 = 100}
    W.get = function(t, k) return t and t[k] end
    W.now = function() return c.now end
    W.inGame = function() return c.inGame end
    W.tick32 = function(n) return n % 4294967296 end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op = op, v = {...}, accepted = c.accept}; return c.accept end
    W.log = function(s) c.logs[#c.logs + 1] = s end
    env.e.MgsCoopWorld = W
    load(source)
    local missing = {}
    for _, fn in ipairs(W.bindHooks) do fn(W.api, function(t, _, k) return t[k] end, function(t, _, k) return t[k] end, missing) end
    for _, fn in ipairs(W.startHooks) do fn() end
    c.W, c.V, c.H = W, W.env, W.handlers
    function c.tick(ms) c.now = c.now + (ms or 33); c.V.tick(c.now) end
    function c.last(op) for i = #c.tx, 1, -1 do if c.tx[i].op == op then return c.tx[i] end end end
    function c.stop() for _, fn in ipairs(W.stopHooks) do fn(true) end; W.active = false end
    return env, c
end

-- The executed retail SetTime assigns vars.clock; it is not a guessed setter.
local env, h = fixture()
env.TppClock.SetTime('12:00:30')
check(env.vars.clock == 43230, 'retail SetTime writes clock')
h.tick()
check(h.last(OP.CLOCK).v[1] == 43230 and h.last(OP.CLOCK).v[3] == 1, 'explicit small host clock change sent immediately')
check(h.last(OP.CLOCK).v[2] == 2000, 'explicit clock change does not contaminate rate with jump delta')
local eh, paused = fixture()
paused.tick(); paused.tick(1000)
check(paused.V.s.rate100 == 0, 'unchanged host clock is measured as stopped')
eh.TppClock.SetTime('12:00:05'); paused.tick()
local stopped = paused.last(OP.CLOCK)
check(stopped.v[1] == 43205 and stopped.v[2] == 0, 'explicit setter preserves measured stopped rate')
check(paused.V.predict(stopped.v[1], stopped.v[2], 1500) == 43205, 'stopped clock never extrapolates a fabricated running rate')
paused.stop()
env.TppWeather.ForceRequestWeather(2, 1)
local request = h.requests[#h.requests]
check(request.weatherType == 2 and request.interpTime == 1 and request.priority == 7 and request.userId == 'Script',
    'executed retail force contract and original parameters')
check(env.vars.weather == 0, 'retail weather request is asynchronous, without fabricated readback')
h.tick()
local weather = h.last(OP.WEATHER)
check(weather.v[1] == 2 and weather.v[2] == 1, 'host force intent precedes vars readback and preserves one-second interpolation')
h.tick()
check(h.last(OP.WEATHER) == weather, 'old vars weather does not overwrite pending authoritative intent')
h.accept = false
env.TppClock.SetTime('12:00:31'); h.tick()
check(h.V.s.clockDirty == true, 'queue pressure retains explicit clock intent')
env.TppWeather.ForceRequestWeather(3, 0); h.tick()
check(h.W.weatherDirty == true and h.last(OP.WEATHER).v[2] == 0, 'queue pressure retains zero-second weather intent')
h.accept = true; h.tick()
check(not h.V.s.clockDirty and not h.W.weatherDirty and h.last(OP.WEATHER).accepted, 'both intents retry after queue pressure')
env.vars.weather = 3; h.tick(6000)
check(h.V.s.intent == nil and h.V.hostWeather() == 3, 'temporary intent yields to authoritative actual weather')
env.TppWeather.CancelForceRequestWeather(); h.tick()
check(#h.cancelled == 1 and h.cancelled[1].priority == 7 and h.cancelled[1].userId == 'Script', 'original retail cancel preserved')
check(h.last(OP.WEATHER).v[1] == 3, 'auto mode continues mirroring actual host weather')
h.stop()
check(env.TppClock.SetTime == h.originalClock and env.TppWeather.ForceRequestWeather == h.originalForce and
    env.TppWeather.CancelForceRequestWeather == h.originalCancel, 'teardown restores exact owned originals')
env.TppClock.SetTime('13:00:00')
check(not h.V.s.clockDirty, 'persistent disabled owner cannot capture later vanilla calls')

-- Wrapper identity, all returns, original errors and yields are preserved.
local er, r = fixture({original = function(e) e.TppClock.SetTime = function() e.vars.clock = 20; return 1, nil, 3 end end})
local a, b, d = er.TppClock.SetTime('unused')
check(a == 1 and b == nil and d == 3, 'wrapper preserves return tuple with nil')
local foreign = function() return 'foreign' end
er.TppClock.SetTime = foreign; r.stop()
check(er.TppClock.SetTime == foreign, 'teardown leaves a later foreign wrapper untouched')
local ey, y = fixture({original = function(e) e.TppClock.SetTime = function() coroutine.yield('original'); e.vars.clock = 25; return 9 end end})
local co = coroutine.create(function() return ey.TppClock.SetTime('unused') end)
local ok, value = coroutine.resume(co)
check(ok and value == 'original' and not y.V.s.clockDirty, 'original yield occurs before observation')
ok, value = coroutine.resume(co)
check(ok and value == 9 and y.V.s.clockDirty, 'observation runs after original resumes')
local sentinel = {}
local ee, e = fixture({original = function(t) t.TppClock.SetTime = function() error(sentinel) end end})
ok, value = pcall(ee.TppClock.SetTime, 'unused')
check(not ok and value == sentinel and not e.V.s.clockDirty, 'original error propagates without false intent')
local eo, owner = fixture()
local previous = owner.V
owner.W.active = false
local replacement = {}
for k, v in pairs(owner.W) do replacement[k] = v end
for _, k in ipairs({'handlers', 'bindHooks', 'startHooks', 'stopHooks', 'pairedHooks', 'unpairedHooks'}) do replacement[k] = {} end
replacement.active, replacement.env = true, nil
eo.e.MgsCoopWorld = replacement
local reload = assert(loadfile(arg[1])); setfenv(reload, eo); reload()
for _, fn in ipairs(replacement.bindHooks) do fn(replacement.api, function(t, _, k) return t[k] end, function(t, _, k) return t[k] end, {}) end
for _, fn in ipairs(replacement.startHooks) do fn() end
check(previous.hooks == nil and eo.TppWeather.MgsCoopWorldEnv == replacement.env, 'new mission owner retires previous library owner')
check(replacement.env.hooks[2][3] == owner.originalForce, 'new owner wraps the retail force rather than nesting a stale wrapper')
eo.TppWeather.ForceRequestWeather(2, 1)
check(not previous.s.intent and replacement.env.s.intent.t == 2, 'only current active owner captures the request')
for _, fn in ipairs(replacement.stopHooks) do fn(true) end
check(eo.TppWeather.ForceRequestWeather == owner.originalForce, 'replacement owner teardown restores retail force')

-- Guest retains the latest controls through a demo or unavailable gameplay.
local eg, g = fixture({host = false})
check(eg.TppClock.SetTime == g.originalClock and eg.TppWeather.ForceRequestWeather == g.originalForce, 'guest installs no host API wrappers')
g.demo = true
g.H[OP.CLOCK]({43000, 2000, 1, 0, 100}, g.now)
g.H[OP.WEATHER]({2, 1, 65535, 255, 1}, g.now)
g.H[OP.WEATHER]({3, 0, 65535, 255, 1}, g.now)
check(eg.vars.clock == 43200 and #g.requests == 0, 'demo causes no world application')
g.now = g.now + 2000
g.H[OP.DIGEST]({g.now, 43040, 2000, 3, 0, 0, 0, 0, 1, 100}, g.now)
check(#g.paused == 0, 'digest does not pause or force weather during a demo')
g.demo = false; g.tick(100)
check(eg.vars.clock == 43044, 'fresh digest replaces deferred clock and includes elapsed latency')
check(#g.requests == 1 and g.requests[1].weatherType == 3 and g.requests[1].interpTime == 0, 'latest deferred weather applied once with original interpolation')
eg.vars.weather = 3; g.tick()
check(g.logs[#g.logs]:find('event=weather_readback;type=3;matches=1', 1, true), 'actual weather arrival recorded separately from request')
g.inGame = false
g.H[OP.WEATHER]({2, 1, 65535, 255, 1}, g.now); g.tick()
check(#g.requests == 1, 'weather held outside gameplay')
g.inGame = true; g.tick()
check(#g.requests == 2 and g.requests[2].interpTime == 1, 'weather resumes when gameplay returns')
g.demo = true
g.H[OP.CLOCK]({1, 2000, 1, 0, 0}, g.now); g.H[OP.WEATHER]({1, 1, 65535, 255, 1}, g.now)
g.W.paired, g.W.eff = false, 0
for _, fn in ipairs(g.W.unpairedHooks) do fn(1) end
check(not g.V.s.pendingClock and not g.V.s.pendingWeather and not g.V.s.forced and #g.cancelled == 1,
    'unpair discards stale controls and releases only our force')
g.demo = false; g.W.paired, g.W.eff = true, 3; g.tick()
check(#g.requests == 2, 're-pair cannot replay discarded old-session weather')
local ef, fresh = fixture({host = false})
fresh.demo = true
fresh.H[OP.CLOCK]({1000, 0, 1, 0, 0}, fresh.now)
fresh.H[OP.WEATHER]({1, 20, 65535, 255, 1}, fresh.now)
fresh.demo = false
fresh.H[OP.CLOCK]({2000, 0, 1, 0, 0}, fresh.now)
fresh.H[OP.WEATHER]({2, 1, 65535, 255, 1}, fresh.now)
check(ef.vars.clock == 2000 and not fresh.V.s.pendingClock, 'healthy fresh clock supersedes an older held clock')
check(#fresh.requests == 1 and fresh.requests[1].weatherType == 2 and fresh.requests[1].interpTime == 1 and
    not fresh.V.s.pendingWeather, 'healthy fresh weather supersedes held weather with original interpolation')
fresh.tick()
check(ef.vars.clock == 2000 and #fresh.requests == 1, 'next tick cannot roll back newer accepted clock or weather')
local ei, idle = fixture({host = false})
idle.demo = true; idle.H[OP.WEATHER]({2, 1, 65535, 255, 1}, idle.now)
idle.demo = false; idle.H[OP.WEATHER]({255, 20, 65535, 255, 1}, idle.now); idle.tick()
check(not idle.V.s.pendingWeather and #idle.requests == 0 and #idle.cancelled == 0,
    'healthy release supersedes held weather even when no force is owned')
local ep, p = fixture({host = false})
p.H[OP.WEATHER]({2, 1, 65535, 255, 1}, p.now)
p.demo = true; p.H[OP.WEATHER]({255, 20, 65535, 255, 1}, p.now)
check(#p.cancelled == 0, 'release packet is retained during a demo')
p.demo = false; p.tick()
check(#p.cancelled == 1 and p.paused[#p.paused] == false and not p.V.s.forced, 'deferred release cancels owned force after the demo')

-- Reliable controls can be delivered before an older cached datagram. The
-- guest must compare host ticks, not their local dequeue order. WEATHER has
-- no host tick, so a digest cannot replace its reliable type/interpolation.
local cases = {
    {200, 100, false, 'older digest'},
    {200, 200, false, 'equal host ticks'},
    {200, 2147483848, false, 'exact half-range ambiguity'},
    {200, 2147483847, true, 'strictly below half range'},
    {200, 2147483849, false, 'strictly above half range'},
    {4294967294, 3, true, 'newer tick across uint32 wrap'},
    {3, 4294967294, false, 'older tick before uint32 wrap'},
    {4294967295, 0, true, 'zero after max uint32 tick'},
}
for _, case in ipairs(cases) do
    local ev, v = fixture({host = false})
    v.demo = true
    v.H[OP.CLOCK]({5000, 2000, 1, case[1], 0}, v.now)
    v.H[OP.WEATHER]({0, 1, 65535, 255, 1}, v.now)
    v.H[OP.DIGEST]({case[2], 1000, 2000, 2, 0, 0, 0, 0, 1, 50}, v.now)
    check(ev.vars.clock == 43200 and #v.requests == 0, case[4] .. ': still suspended')
    check(v.V.s.pendingClock[1][1] == (case[3] and 1000 or 5000) and
        v.V.s.pendingClock[1][4] == (case[3] and case[2] or case[1]), case[4] .. ': strict serial comparison')
    check(v.V.s.pendingWeather[1] == 0 and v.V.s.pendingWeather[2] == 1,
        case[4] .. ': reliable weather intent and interpolation retained')
    v.demo = false; v.tick(50)
    check(ev.vars.clock == (case[3] and 1002 or 5001), case[4] .. ': resumed clock includes held elapsed latency')
    check(#v.requests == 1 and v.requests[1].weatherType == 0 and v.requests[1].interpTime == 1,
        case[4] .. ': resumed actual retail weather request remains sunny over stale rainy digest')
    check(not v.V.s.pendingClock and not v.V.s.pendingWeather, case[4] .. ': applied controls leave no stale work')
    ev.vars.weather = 0; v.tick()
    check(not v.V.s.readback and table.concat(v.logs, '\n'):find('event=weather_readback;type=0;matches=1', 1, true),
        case[4] .. ': resumed real weather readback is recorded')
end
local ev, v = fixture({host = false})
v.demo = true
v.H[OP.CLOCK]({5000, 0, 1, 100, 0}, v.now)
v.H[OP.DIGEST]({101, 1000, 0, 0, 0, 0, 0, 0, 1, 0}, v.now)
v.H[OP.DIGEST]({100, 9000, 0, 0, 0, 0, 0, 0, 1, 0}, v.now)
check(v.V.s.pendingClock[1][1] == 1000 and v.V.s.pendingClock[1][4] == 101,
    'replacement digest retains its host tick so a later older digest cannot roll it back')
v.demo = false; v.tick()
check(ev.vars.clock == 1000, 'newest held digest remains authoritative at resume')

for _, level in ipairs({0, 2}) do
    local ev, v = fixture({level = level})
    check(ev.TppClock.SetTime == v.originalClock and ev.TppWeather.ForceRequestWeather == v.originalForce, 'level ' .. level .. ' is vanilla')
    ev.TppClock.SetTime('10:00:00'); ev.TppWeather.ForceRequestWeather(2, 1); v.tick()
    check(not v.last(OP.CLOCK) and not v.last(OP.WEATHER), 'level ' .. level .. ' sends no environment controls')
end
local em, m = fixture(); m.W.eff = 2
em.TppClock.SetTime('10:00:00'); em.TppWeather.ForceRequestWeather(2, 1); m.tick()
check(not m.V.s.clockDirty and not m.V.s.intent and not m.last(OP.WEATHER), 'mixed effective level 2 stays vanilla')
local en, n = fixture({weather = 'nofog'})
en.TppWeather.ForceRequestWeather(4, 1); en.vars.weather = 4; n.tick()
check(n.last(OP.WEATHER).v[1] == 255 and not n.V.s.intent, 'Africa fog remains clean release, never mirrored')
print('world_env_game_lua_check: PASS ' .. checks .. ' checks; exact retail APIs, no game/network/save actions')
