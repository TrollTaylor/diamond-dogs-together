-- MGS co-op world sync: digests, the resync stream, clock and weather
-- (original; docs/world-sync-protocol.md). DIGEST/GUEST_DIGEST and the
-- phase vectors go out once a second while paired; 3 outcome mismatches ask
-- for a RESYNC. W2 (eff >= 3): clock jumps and fast samples, a guest clock
-- more than 60 game s off on 2 digests is corrected, and the host weather is
-- forced with TppWeather.ForceRequestWeather (30050: clock only; 30020: FOGGY
-- is never mirrored, 255 = release). Forces are released at stop, after W.fail too.
do
  local W = e.MgsCoopWorld
  if type(W) == "table" then
    local V, OP, H = {}, W.OP, W.handlers
    W.env = V
    local fmt, floor, abs, min = string.format, math.floor, math.abs, math.min
    local CAUSE = {"jump", "fast", "resync", "fast_end"}
    W.bindHooks[#W.bindHooks + 1] = function(a, fn, num, missing)
      local before = #missing
      a.force = fn(TppWeather, "TppWeather", "ForceRequestWeather")
      a.cancel = fn(TppWeather, "TppWeather", "CancelForceRequestWeather")
      a.pause = fn(WeatherManager, "WeatherManager", "PauseNewWeatherChangeRandom")
      a.demo = fn(DemoDaemon, "DemoDaemon", "IsDemoPlaying")
      a.foggy = num(W.get(TppDefine, "WEATHER"), "TppDefine.WEATHER", "FOGGY", true)
      W.w2ok = #missing == before and type(W.get(vars, "clock")) == "number" and type(W.get(vars, "weather")) == "number"
    end
    function V.reset() V.s = {mism = 0, over = 0, wmis = 0, rate100 = 2000} end
    V.reset()
    function V.restore()
      for _, h in ipairs(V.hooks or {}) do if W.get(h[1], h[2]) == h[4] then h[1][h[2]] = h[3] end end
      if type(TppWeather) == "table" and TppWeather.MgsCoopWorldEnv == V then TppWeather.MgsCoopWorldEnv = nil end
      V.hooks = nil
    end
    function V.install()
      if not W.active or not W.host or W.level < 3 or not W.feature.clock then return end
      if type(TppClock) ~= "table" or type(TppWeather) ~= "table" then return end
      local old = W.get(TppWeather, "MgsCoopWorldEnv")
      if type(old) == "table" and type(old.restore) == "function" then old.restore() end
      V.hooks, TppWeather.MgsCoopWorldEnv = {}, V
      local function live() return W.active and W.host and W.paired and V.clockOn() end
      local function hook(t, key, changed)
        local original = W.get(t, key)
        if type(original) ~= "function" then return end
        local wrapper = function(...)
          local args = {...}
          local function after(...)
            if select(1, ...) ~= false then pcall(changed, args) end
            return ...
          end
          return after(original(...))
        end
        V.hooks[#V.hooks + 1] = {t, key, original, wrapper}
        t[key] = wrapper
      end
      hook(TppClock, "SetTime", function()
        if live() then V.s.clockDirty, V.s.lastClock, V.s.dWall = true, nil, nil end
      end)
      hook(TppWeather, "ForceRequestWeather", function(a)
        local t, n = a[1], type(a[2]) == "number" and a[2] or 20
        if live() and V.weatherOn() and type(t) == "number" and t == floor(t) and t >= 0 and t <= 5 and
          n == n and n >= 0 and n <= 600 and not (W.feature.weather == "nofog" and t == W.api.foggy) then
          V.s.intent = {t = t, interp = floor(n + 0.5), at = W.now()}
          W.weatherDirty = true
        end
      end)
      hook(TppWeather, "CancelForceRequestWeather", function()
        if live() then V.s.intent = nil; W.weatherDirty = true end
      end)
    end
    W.startHooks[#W.startHooks + 1] = function() V.restore(); V.reset(); V.install() end
    function V.clockOn() return W.eff >= 3 and W.w2ok and W.feature.clock == true end
    function V.weatherOn() return V.clockOn() and W.feature.weather ~= nil end
    function V.clock() return floor(vars.clock) % 86400 end
    -- A clock difference in [-43200, 43200).
    function V.wrap(d) return (d + 43200) % 86400 - 43200 end
    function V.suspended()
      local d = V.s.digest
      return W.api.demo() == true or not W.inGame() or (d ~= nil and d.inGame == 0)
    end
    function V.hostWeather()
      local t = W.get(vars, "weather")
      if not V.weatherOn() or type(t) ~= "number" then return 255 end
      local i = V.s.intent
      if i and W.now() - i.at <= i.interp * 1000 + 5000 then return i.t end
      V.s.intent = nil
      if W.feature.weather == "nofog" and t == W.api.foggy then return 255 end
      if t < 0 or t > 5 then return nil end
      return t
    end
    function V.txClock(cause)
      local s = V.s
      local ok = W.tx(OP.CLOCK, V.clock(), s.rate100, cause, W.tick32(W.now()))
      if ok and cause ~= 2 then W.log(fmt("event=clock_tx;cause=%s;clock=%d", CAUSE[cause], V.clock())) end
      return ok
    end
    function V.txWeather(cause)
      local t = V.hostWeather()
      if t == nil then return false end
      local interp = V.s.intent and V.s.intent.interp or 20
      local ok = W.tx(OP.WEATHER, t, interp, 65535, 255, cause)
      if ok then
        V.s.lastWeather = t
        W.log(fmt("event=weather;type=%d;cause=%d;interp=%d", t, cause, interp))
      end
      return ok
    end
    -- Host: per-frame clock delta against 20 x min(dtWall, 0.25) (no false
    -- jump after a frame gap); fast mode above 40x over 250 ms windows.
    function V.hostClock(now)
      local s = V.s
      local c = V.clock()
      if not s.lastClock then
        s.lastClock, s.lastWall, s.winT, s.winC = c, now, now, c
        return
      end
      local dt = (now - s.lastWall) / 1000
      local d = V.wrap(c - s.lastClock)
      s.lastClock, s.lastWall = c, now
      if now - s.winT >= 250 then
        local rate = V.wrap(c - s.winC) / ((now - s.winT) / 1000)
        s.winT, s.winC = now, c
        if rate > 40 then s.fast, s.slow = true, nil
        elseif s.fast and rate <= 30 then
          s.slow = s.slow or now
          if now - s.slow >= 1000 then
            s.fast, s.slow = false, nil
            V.txClock(4)
          end
        else s.slow = nil end
      end
      if s.fast then
        if now - (s.fastAt or -1e9) >= 250 then
          s.fastAt = now
          V.txClock(2)
        end
      elseif abs(d - 20 * min(dt, 0.25)) > 120 then
        V.txClock(1)
      end
    end
    function V.applyWeather(t, interp, cause, now)
      local s, a = V.s, W.api
      if V.suspended() then s.pendingWeather = {t, interp, cause}; return end
      s.pendingWeather = nil
      if t <= 5 then
        a.force(t, interp)
        s.forced, s.lastForce, s.interp = true, now, interp
      elseif s.forced then
        a.cancel()
        a.pause(false)
        s.forced, s.paused = false, false
      else
        return
      end
      s.readback = t <= 5 and t or nil
      W.log(fmt("event=weather;type=%d;cause=%d;applied=1;interp=%d", t, cause, interp))
    end
    function V.predict(clock, rate100, latency)
      return (clock + rate100 / 100 * (latency or 0) / 1000) % 86400
    end
    function V.tick(now)
      local s = V.s
      if not W.host and W.paired and V.clockOn() and not V.suspended() then
        local c, w = s.pendingClock, s.pendingWeather
        s.pendingClock, s.pendingWeather = nil, nil
        if c then c[1][5] = (c[1][5] or 0) + now - c[2]; H[OP.CLOCK](c[1], now) end
        if w and V.weatherOn() then V.applyWeather(w[1], w[2], w[3], now) end
        if s.readback and W.get(vars, "weather") == s.readback then
          W.log(fmt("event=weather_readback;type=%d;matches=1", s.readback)); s.readback = nil
        end
      end
      if W.paired and W.eff >= 2 and now - (s.digestAt or -1e9) >= 1000 then
        local c = W.get(vars, "clock")
        c = type(c) == "number" and floor(c) % 86400 or 0
        if s.dWall and now > s.dWall then
          local r = floor(V.wrap(c - s.dClock) / ((now - s.dWall) / 1000) * 100 + 0.5)
          s.rate100 = r < 0 and 0 or (r > 65535 and 65535 or r)
        end
        s.digestAt, s.dClock, s.dWall = now, c, now
        local es, inGame, tick = W.enemy.s, W.inGame() and 1 or 0, W.tick32(now)
        if W.host then
          W.tx(OP.DIGEST, tick, c, s.rate100, V.hostWeather() or 255, es.seq, es.sum, es.count, #W.cpOrder, inGame)
        else
          local pending = 0
          for _ in pairs(es.ledger) do pending = pending + 1 end
          for _ in pairs(es.req) do pending = pending + 1 end
          local w = W.get(vars, "weather")
          W.tx(OP.GDIGEST, tick, pending, es.seen, es.sum, es.count, #W.cpOrder, inGame, c,
            type(w) == "number" and w >= 0 and w <= 5 and w or 255)
        end
        if W.phase then W.phase.txVector() end
      end
      V.resyncTick(now)
      if now - (V.s.statsAt or W.t0) >= 60000 then
        V.s.statsAt = now
        local t = W.stats
        W.log(fmt("event=stats;commits=%d;reqs=%d;applies=%d;noops=%d;ledger=%d;div=%d;obs=%d;resend=%d;unmapped=%d;bad_value=%d",
          t.commits, t.reqs, t.applies, t.noops, t.ledger, t.div, t.obs, t.resend, t.unmapped, t.bad_value))
      end
      if W.host and W.paired and V.clockOn() then
        V.hostClock(now)
        if s.clockDirty and V.txClock(1) then s.clockDirty = nil end
        if V.weatherOn() and (V.hostWeather() ~= s.lastWeather or W.weatherDirty) then
          if V.txWeather(1) then W.weatherDirty = false end
        end
      end
    end
    -- Host resync stream: at most 32 TX per frame, then RESYNC_END.
    function V.resyncTick(now)
      local r = V.s.resync
      if not r then return end
      for _ = 1, 32 do
        local item = r.items[r.i]
        if not item then
          if not W.tx(OP.RESYNC_END, r.rid, r.what, r.count, W.enemy.s.seq) then return end
          W.log(fmt("event=resync;what=%d;count=%d", r.what, r.count))
          V.s.resync = nil
          return
        end
        local k, h = item[1], item[2]
        local ok
        if k == 1 then
          local e = W.ent[h]
          local x, y, z = W.enemy.pos(e.id)
          ok = W.tx(OP.OUTCOME, h, 1, e.committed, e.ver, 8, 0, 0, 1, x, y, z)
        elseif k == 2 then ok = W.phase.txPhase(h, 1)
        elseif k == 3 then ok = V.txClock(3)
        else ok = V.txWeather(2) end
        if not ok then return end
        r.i, r.count = r.i + 1, r.count + 1
      end
    end
    H[OP.RESYNC] = function(v)
      local items, what = {}, v[2]
      if what % 2 == 1 then
        for _, h in ipairs(W.entOrder) do if W.ent[h].ver > 0 then items[#items + 1] = {1, h} end end
      end
      if floor(what / 2) % 2 == 1 and W.phase and W.w1 then
        for _, h in ipairs(W.cpOrder) do if W.cp[h].localPhase > 0 then items[#items + 1] = {2, h} end end
      end
      if what >= 4 and W.eff >= 3 and W.env and W.feature.clock then
        items[#items + 1] = {3}
        if W.feature.weather then items[#items + 1] = {4} end
      end
      V.s.resync = {rid = v[1], what = what, items = items, i = 1, count = 0}
    end
    H[OP.RESYNC_END] = function(v)
      local s = W.enemy.s
      s.seen, V.s.mism = v[4], 0
      local held = s.held
      s.held = {}
      for _, r in ipairs(held) do
        local st = W.w1 and W.enemy.localState(W.ent[r.h])
        if st and W.enemy.forward(r.h, st) then W.enemy.request(r.h, st, r.cause, r.attack, W.now()) end
      end
      W.log(fmt("event=resync;what=%d;count=%d;src=end;held=%d", v[2], v[3], #held))
    end
    H[OP.RESYNC_NEEDED] = function(v)
      local now = W.now()
      if W.host then W.log("event=resync_needed;what=" .. v[1])
      elseif not W.resyncAt or now - W.resyncAt >= 10000 then
        W.resyncAt = now
        W.tx(OP.RESYNC, W.nextRid(), 7)
      end
    end
    H[OP.CLOCK] = function(v, now)
      if W.host or not W.paired or not V.clockOn() then return end
      if V.suspended() then V.s.pendingClock = {v, now or W.now()}; return end
      V.s.pendingClock = nil
      local pred = V.predict(v[1], v[2], v[5])
      local diff = V.wrap(V.clock() - pred)
      vars.clock = floor(pred) % 86400
      W.log(fmt("event=clock;cause=%s;diff=%d;applied=1", CAUSE[v[3]] or "?", floor(diff)))
    end
    H[OP.WEATHER] = function(v, now)
      if not W.host and V.weatherOn() then V.applyWeather(v[1], v[2], v[5], now) end
    end
    H[OP.DIGEST] = function(v, now)
      local s = V.s
      s.digest = {inGame = v[9], weather = v[4]}
      if s.pendingClock then
        local delta = (v[1] - s.pendingClock[1][4]) % 4294967296
        if delta > 0 and delta < 2147483648 then s.pendingClock = {{v[2], v[3], 3, v[1], v[10]}, now} end
      end
      -- WEATHER has no host tick: retain reliable intent and its interpolation.
      if W.w1 then
        local es = W.enemy.s
        if es.seen == v[5] then
          s.seqSince = nil
          s.mism = (es.sum ~= v[6] or es.count ~= v[7]) and s.mism + 1 or 0
        else
          s.seqSince = s.seqSince or now
          if now - s.seqSince > 10000 then s.mism = s.mism + 1 end
        end
        if s.mism >= 3 and now - (s.resyncAt or -1e9) >= 10000 then
          s.mism, s.resyncAt = 0, now
          W.log("event=digest_mismatch;what=outcomes;n=3")
          W.tx(OP.RESYNC, W.nextRid(), 1)
        end
      end
      if V.clockOn() and not V.suspended() then
        local pred = V.predict(v[2], v[3], v[10])
        local diff = V.wrap(V.clock() - pred)
        s.over = abs(diff) > 60 and s.over + 1 or 0
        if s.over >= 2 then
          s.over = 0
          vars.clock = floor(pred) % 86400
          W.log(fmt("event=clock;cause=digest;diff=%d;applied=1", floor(diff)))
        end
        if now - (s.checkAt or -1e9) >= 10000 then
          s.checkAt = now
          W.log(fmt("event=clock_check;diff=%d", floor(diff)))
        end
      end
      if V.weatherOn() and v[4] <= 5 and not V.suspended() then
        local a, w = W.api, W.get(vars, "weather")
        a.pause(true)
        s.paused = true
        s.wmis = w ~= v[4] and s.wmis + 1 or 0
        if s.wmis >= 3 and now - (s.lastForce or -1e9) > (s.interp or 20) * 1000 + 5000 then
          s.wmis = 0
          V.applyWeather(v[4], 20, 3, now)
        end
        if now - (s.wcheckAt or -1e9) >= 10000 then
          s.wcheckAt = now
          W.log(fmt("event=weather_check;local=%s;host=%d", tostring(w), v[4]))
        end
      end
    end
    H[OP.GDIGEST] = function(v)
      V.s.guest = v
    end
    W.pairedHooks[#W.pairedHooks + 1] = function()
      if not W.host and V.weatherOn() then
        W.api.pause(true)
        V.s.paused = true
      end
    end
    -- Release only our applied forces, including link loss and LEVEL 0.
    function V.release()
      local s, a = V.s, W.api
      if not a then return end
      if (s.forced or s.probeForced) and a.cancel then pcall(a.cancel) end
      if s.paused and a.pause then pcall(a.pause, false) end
      s.forced, s.probeForced, s.paused = false, false, false
      s.pendingClock, s.pendingWeather, s.readback, s.digest = nil, nil, nil, nil
    end
    W.unpairedHooks[#W.unpairedHooks + 1] = V.release
    W.stopHooks[#W.stopHooks + 1] = function(full) if full then V.release(); V.restore() end end
  end
end
