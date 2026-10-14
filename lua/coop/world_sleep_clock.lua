-- Certified fresh paired sleep gameplay clock. Load after world_downed.
-- Passive: no native calls, control changes, status authority or wake grants.
do
local W=e.MgsCoopWorld
local D=W.downed
local C={}
D.sleepClock=C
local floor,fmt=math.floor,string.format
local FRESH,SLEEP=500,30000
local function clockTime(n)
  return type(n) == "number" and n == n and n >= 0 and n <= 9007199254740991
end
local function clockFresh(r, now)
  return clockTime(now) and r and r.ready and clockTime(r.at) and now >= r.at and
    now - r.at <= FRESH and r.clockMission == W.mission and r.clockEpoch == W.epoch
end
function C.Pause(reason)
  local changed = D.sleepPauseReason ~= reason
  for _, timer in pairs(D.sleepTimers or {}) do
    if not timer.paused or timer.intervalAt then changed = true end
    timer.intervalAt, timer.freshUntil, timer.paused = nil, nil, true
  end
  if changed then D.sleepPauseCount = (D.sleepPauseCount or 0) % 65535 + 1 end
  D.sleepPauseReason = reason
end
function C.Bind()
  if D.sleepClockMission ~= W.mission or D.sleepClockEpoch ~= W.epoch or
    D.sleepClockRole ~= W.role or D.sleepClockHost ~= W.host then
    D.sleepTimers = {}
    D.sleepClockMission, D.sleepClockEpoch, D.sleepClockRole, D.sleepClockHost = W.mission, W.epoch, W.role, W.host
    D.sleepPauseCount, D.sleepPauseReason = 0, "scope"
  end
end
-- Read-only diagnostic: generation, credited gameplay ms, paused for each role.
-- No clock sample, native call, grant, liveness renewal or timer mutation.
function D.sleepClockStatus()
  local function row(role)
    local t = D.sleepTimers and D.sleepTimers[role]
    if not t then return "0,0,1" end
    return fmt("%.0f,%d,%d", t.generation, floor(t.elapsed or 0), t.paused and 1 or 0)
  end
  return fmt("sleepclock;host=%d;t1=%s;t2=%s;why=%s;n=%d", W.host and 1 or 0,
    row(1), row(2), D.sleepPauseReason or "none", D.sleepPauseCount or 0)
end
function C.Reset()
  D.sleepTimers={}
  D.sleepClockMission,D.sleepClockEpoch,D.sleepClockRole,D.sleepClockHost=nil,nil,nil,nil
  D.sleepPauseCount,D.sleepPauseReason=0,"reset"
end
function C.Tick(now)
  local a,b=D.records[1],D.records[2]
  -- A fresh changed/healthy target retires only its own prior sleep cycle.
  -- A stale row cannot erase earned time or create a replacement timer.
  for role = 1, 2 do
    local r, timer = D.records[role], D.sleepTimers[role]
    if clockFresh(r, now) then
      if r.down and r.cause == 2 then
        if not timer or timer.generation ~= r.generation then
          D.sleepTimers[role] = {generation = r.generation, at = now, elapsed = 0, paused = true}
        end
      else D.sleepTimers[role] = nil end
    end
  end
  local pairedClock = clockFresh(a, now) and clockFresh(b, now)
  if not pairedClock then C.Pause("peer_stale") end
  for role = 1, 2 do
    local timer = D.sleepTimers[role]
    if pairedClock and timer then
      -- Credit only an entire bounded interval certified by BOTH fresh endpoint
      -- contexts. The prior status validity must cover its end as well; a new
      -- packet cannot retroactively fill an unobserved/stale gap.
      local delta = timer.intervalAt and now - timer.intervalAt
      if not timer.granted and delta and delta >= 0 and delta <= FRESH and
        timer.freshUntil and now <= timer.freshUntil and
        timer.pairGeneration1 == a.generation and timer.pairGeneration2 == b.generation then
        timer.elapsed = math.min(SLEEP, timer.elapsed + delta)
      end
      timer.intervalAt, timer.freshUntil, timer.paused = now, math.min(a.at, b.at) + FRESH, false
      timer.pairGeneration1, timer.pairGeneration2 = a.generation, b.generation
    end
  end
  return pairedClock
end
C.Reset()
end
