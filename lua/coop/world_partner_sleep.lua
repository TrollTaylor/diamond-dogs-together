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
-- REVIVE_GRANT is host-to-guest reliable in world_wire; its mission/epoch
-- envelope is admitted before this handler. Role1 is presentation permission
-- for the host body on a guest, never permission to unlock the guest owner.
local function current(r,now)
  return fresh(r,now) and r.clockMission==W.mission and r.clockEpoch==W.epoch
end
local function live()
  return D.hitEnabled() and W.inGame() and valid(W.mission,1,99999) and valid(W.epoch,1,65535)
end
local function newer(n,old)
  local delta=(n-old)%U32
  return delta>0 and delta<=floor(U32/2)
end
local reset=D.reset
function D.reset()
  D.peerWakeGrant,D.peerWakeSerial=nil,0
  return reset()
end
for i,f in ipairs(W.startHooks or {}) do if f==reset then W.startHooks[i]=D.reset end end
local originalGrant=W.handlers[W.OP.REVIVE_GRANT]
W.handlers[W.OP.REVIVE_GRANT]=function(v,...)
  if type(v)~="table" or v[1]~=1 then return originalGrant(v,...) end
  if W.host or W.role~=2 or not live() or #v~=3 or not valid(v[2],1,U32-1) or
    not valid(v[3],1,U32-1) or not newer(v[3],D.peerWakeSerial or 0) then return end
  local now=W.now()
  local peer,me=D.records[1],D.records[2]
  if not current(peer,now) or not current(me,now) or not peer.down or
    (peer.cause~=1 and peer.cause~=2) or peer.generation~=v[2] then return end
  D.peerWakeGrant={mission=W.mission,epoch=W.epoch,generation=v[2],serial=v[3]}
  D.peerWakeSerial=v[3]
  D.granted[1]=v[2]
end
-- Seal every host grant to the current accepted target. For a local-host wake,
-- reliably mirror this SAME serial before requesting native recovery; retry it
-- while that recovery is pending, without allocating a fresh grant each tick.
function D.grantReady(p)
  if not W.host or W.role~=1 or not live() or type(p)~="table" or
    not valid(p.role,1,2) or not valid(p.generation,1,U32-1) or not valid(p.serial,1,U32-1) then return false end
  local now=W.now()
  local target=D.records[p.role]
  if not current(target,now) or not target.down or target.generation~=p.generation then return false end
  if p.mission==nil then p.mission,p.epoch=W.mission,W.epoch end
  if p.mission~=W.mission or p.epoch~=W.epoch then return false end
  if p.role~=W.role then return true end
  if now>=(p.mirrorRetryAt or 0) then
    p.mirrorRetryAt=now+500
    if W.tx(W.OP.REVIVE_GRANT,p.role,p.generation,p.serial) then p.mirrorSent=true end
  end
  return p.mirrorSent==true
end
local function peerGranted(peer,now)
  if not live() or not current(peer,now) or not peer.down or not valid(peer.generation,1,U32-1) or
    D.granted[3-W.role]~=peer.generation then return 0 end
  local p=W.host and D.pendingGrant or D.peerWakeGrant
  if not p or p.mission~=W.mission or p.epoch~=W.epoch or p.generation~=peer.generation or
    not valid(p.serial,1,U32-1) or (W.host and p.role~=3-W.role) then return 0 end
  return peer.generation
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
  lf,rf=lf and current(l,now),rf and current(r,now)
  local gen=rf and r.generation or 0
  local granted=peerGranted(r,now)
  local ok,role=pcall(W.receipt,fmt("return 'MGS_COOP_TEAM_V2' -- %d %d %d %d %d %d %d %.0f %.0f",W.mission,W.epoch,
    lf and l.down and 1 or 0,lf and l.down and l.cause or 0,
    rf and r.down and 1 or 0,rf and r.down and r.cause or 0,rf and 1 or 0,gen,granted))
  -- Older native readers may consume V1 state, which has no wake grant.
  if not ok or not valid(role,0,2) then
    pcall(W.receipt,fmt("return 'MGS_COOP_TEAM_V1' -- %d %d %d %d %d %d %d",W.mission,W.epoch,
      lf and l.down and 1 or 0,lf and l.down and l.cause or 0,
      rf and r.down and 1 or 0,rf and r.down and r.cause or 0,rf and 1 or 0))
  end
  -- Carrying does not restart or pause certified sleep gameplay time. Auto-wake
  -- uses the same authenticated grant and native detach/recovery as Action wake.
  -- Native sleep retirement remains required before controls can be released.
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
