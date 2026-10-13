-- Accepted-peer sleep presentation bridge. Installed passively after downed.
do
local W = e.MgsCoopWorld
local D, floor, fmt = W.downed, math.floor, string.format
local U32, FRESH = 4294967296, 500
local function valid(n, lo, hi)
  return type(n) == "number" and n == floor(n) and n >= lo and n <= hi
end
local function fresh(r, now)
  return r and r.ready and now >= r.at and now - r.at <= FRESH
end
-- Native partner sleep is an optional private-worker consumer. Normal builds
-- explicitly report false; no extra per-tick bridge traffic or state changes.
function D.partnerSleepClear()
  if D.partnerSleepBridge == true then
    pcall(W.receipt, "return 'MGS_COOP_PARTNER_SLEEP_V1' -- 0 0 0 0 0 0 0 0 0")
  end
end
-- Teammate rescue (docs/coop-carry.md): both accepted downed rows, every tick.
-- Native keeps the teammate's body in the stock sleep while they are darted
-- and lets a healthy player pick up a sleeping or wounded teammate.
local function publishTeam(now)
  local l, r = D.records[W.role], D.records[3 - W.role]
  local lf, rf = fresh(l, now), fresh(r, now)
  local ok, carry = pcall(W.receipt, fmt("return 'MGS_COOP_TEAM_V1' -- %d %d %d %d %d %d %d", W.mission, W.epoch,
    lf and l.down and 1 or 0, lf and l.down and l.cause or 0,
    rf and r.down and 1 or 0, rf and r.down and r.cause or 0, rf and 1 or 0))
  -- A carried sleeper is not woken by the 30 s timer (the carrier decides).
  if ok and (carry == 1 or carry == 2) and type(D.sleepTimers) == "table" then
    for _, timer in pairs(D.sleepTimers) do if type(timer) == "table" and not timer.granted then timer.at = now end end
  end
end
function D.partnerSleepPublish(now)
  publishTeam(now)
  if D.partnerSleepBridge == nil then
    local ok, capable = pcall(W.receipt, "return 'MGS_COOP_PARTNER_SLEEP_CAP_V1'")
    D.partnerSleepBridge = ok and capable == true or false
  end
  if not D.partnerSleepBridge then return end
  local peer = D.records[3 - W.role]
  if not D.hitEnabled() or not W.inGame() or not fresh(peer, now) or
    not valid(peer.sequence, 1, 65535) or not valid(peer.generation, 0, U32 - 1) then
    D.partnerSleepClear(); return
  end
  -- Only the already accepted status row is used. Its receive age is passed,
  -- rather than minting freshness from this later presentation poll.
  local age = floor(now - peer.at)
  if not valid(age, 0, FRESH) then D.partnerSleepClear(); return end
  pcall(W.receipt, fmt("return 'MGS_COOP_PARTNER_SLEEP_V1' -- %d %d %d %d %.0f %d %d %d %d",
    W.mission, W.epoch, W.role, peer.sequence, peer.generation,
    peer.down and 1 or 0, peer.cause, peer.ready and 1 or 0, age))
end
end
