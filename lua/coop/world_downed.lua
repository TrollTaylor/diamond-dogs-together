do
local W=e.MgsCoopWorld
local D={records={},sequence=0,commitSequence=0,grantSerial=0,receivedGrant=0,
  progress=0,target=0,active=false}
W.downed = D
local OP,H,floor,fmt=W.OP,W.handlers,math.floor,string.format
local U32,RANGE2,HOLD,FRESH,SLEEP=4294967296,9000000,3000,500,30000
local function valid(n,lo,hi)
  return type(n) == "number" and n == floor(n) and n >= lo and n <= hi
end
local function newer(n,old,modulus)
  local delta=(n - old) % modulus
  return delta > 0 and delta <= floor(modulus / 2)
end
local function log(text) W.log("event=downed;" .. text) end
local function on()
  local coop=Tpp and Tpp.MgsCoop
  return not D.nativePending and not (D.maskOwned and not D.active) and
    W.active == true and W.paired == true and W.eff >= 3 and
    (W.mission == 30010 or W.mission == 30020 or W.mission == 30050) and
    (W.role == 1 or W.role == 2) and vars and vars.missionCode == W.mission and
    type(coop) == "table" and coop.playerPackageSelected == true and
    not (W.recovery and W.recovery.dead)
end
D.hitEnabled=on
-- The local player's own cardboard box (PlayerStatus CBOX). A boxed body cannot
-- take the native prone request; native accepts the box as the downed posture
-- (two-PC test 2026-10-23 item 4). A refused query reads as not boxed.
local function boxed()
  local query,status=W.get(PlayerInfo,"OrCheckStatus"),W.get(PlayerStatus,"CBOX")
  if type(query) ~= "function" or not valid(status, 0, 2147483647) then return false end
  local ok,v=pcall(query,{status})
  return ok and v == true
end
local function native(command,generation)
  local mission=command==3 and D.boundMission or W.mission
  local epoch=command==3 and D.boundEpoch or W.epoch
  if not valid(mission, 0, 99999) or not valid(epoch, 1, 65535) then return nil end
  -- Polls and locks carry the posture field (0 or 1); other commands never do.
  local posture=(command == 0 or command == 1) and (boxed() and " 1" or " 0") or ""
  local ok,r=pcall(W.receipt,fmt("return 'MGS_COOP_DOWNED_V1' -- %d %d %d %.0f%s",
    mission, epoch, command, generation or 0, posture))
  if command == 3 then return ok and type(r) == "table" and r.applied == true and r or nil end
  if not ok or type(r) ~= "table" or r.ready ~= true or type(r.down) ~= "boolean" or
    type(r.held) ~= "boolean" or not valid(r.generation, 0, U32 - 1) or not valid(r.cause, 0, 2) or
    type(r.pos) ~= "table" then return nil end
  if r.down and (r.generation == 0 or r.cause == 0) or not r.down and r.cause ~= 0 then return nil end
  for i = 1, 3 do if not valid(r.pos[i], -1000000000, 1000000000) then return nil end end
  return r
end
local function fresh(r,now)
  return r and r.ready and now >= r.at and now - r.at <= FRESH
end
local function partnerSleepClear()
  if D.partnerSleepClear then D.partnerSleepClear() end
end
local function distance(a,b)
  local sum=0
  for i = 1, 3 do sum = sum + (a.pos[i] - b.pos[i]) ^ 2 end
  return sum <= RANGE2
end
local function mask(enabled,cause)
  local f=W.get(Player,enabled and "SetPadMask" or "ResetPadMask")
  local validPlayer=W.get(Player,"IsInstanceValid")
  if type(f) ~= "function" or type(validPlayer) ~= "function" then return false end
  local ok,alive=pcall(validPlayer)
  if not ok or alive ~= true then return false end
  local options={settingName="MgsCoopDowned"}
  if enabled then
    cause = cause or D.records[W.role] and D.records[W.role].cause
    if cause ~= 1 and cause ~= 2 then return false end
    options.except, options.buttons, options.sticks, options.triggers = false, cause == 2 and 0xFFFFFFFF or 0xFDDFFFFF, 0, 3
  end
  ok = pcall(f, options)
  if ok then D.maskOwned = enabled or nil end
  return ok
end
local function publishHud(h)
  pcall(W.receipt,fmt("return 'MGS_COOP_DOWNED_HUD_V3' -- %d %d %d %d %d %d %d %d %d",
    h and W.mission or 0,h and W.epoch or 0,h and h.down and 1 or 0,
    h and h.teammateDown and 1 or 0,h and h.inRange and 1 or 0,h and h.progress or 0,
    h and h.cause or 0,h and h.teammateCause or 0,h and h.near and 1 or 0))
end
local function resetHold()
  D.holdAt,D.holdGeneration,D.holdActor,D.target,D.progress=nil,nil,nil,0,0
  D.decay=nil
end
-- MGO's down rules drain an interrupted revive instead of restarting it: the
-- same reviver resuming on the same downed generation keeps what is left.
local function releaseHold(now)
  local decay
  if D.holdAt and D.progress > 0 and D.target ~= 0 then
    decay={target=D.target,generation=D.holdGeneration,actor=D.holdActor,progress=D.progress,at=now}
  end
  resetHold()
  D.decay=decay
end
local function resumedProgress(now,targetRole,target,actor)
  local d=D.decay
  if not d or d.target ~= targetRole or d.generation ~= target.generation or d.actor ~= actor.generation or
    type(now) ~= "number" or now ~= now or now < d.at then return 0 end
  return math.max(0, d.progress - (now - d.at))
end
local function clockTime(n)
  return type(n)=="number" and n==n and n>=0 and n<=9007199254740991
end
local function pauseSleepClock(reason)
  if D.sleepClock then D.sleepClock.Pause(reason) end
end
function D.reset()
  partnerSleepClear(); D.partnerSleepBridge = nil
  if D.active then D.nativePending = true end
  D.records,D.sequence,D.commitSequence,D.grantSerial,D.receivedGrant={},0,0,0,0
  D.remoteCommit,D.commitSeen,D.txAt,D.commitAt,D.remoteCommitAt,D.pendingGrant=nil,0,0,0,nil,nil
  D.active,D.failed,D.granted,D.lastNow,D.refusalAt=false,false,{},nil,nil
  D.unlockGeneration,D.unlockAt,D.sleepTimers=nil,nil,{}
  if D.sleepClock then D.sleepClock.Reset() end
  if not D.nativePending then D.boundMission, D.boundEpoch = nil, nil end
  resetHold(); publishHud(); if D.hitReset then D.hitReset() end
end
function D.stop()
  if not D.nativeSleepActive and D.maskOwned then mask(false) end
  D.nativePending=D.nativePending or D.active or nil
  if D.nativePending and native(3, 0) then D.nativePending = nil end
  if D.nativeSleepActive and not D.nativePending then
    if D.maskOwned then mask(false) end
    D.nativeSleepActive=nil
  end
  D.active=false; D.reset()
end
function D.handleDeath(index)
  if not on() then return false end
  local f=W.get(PlayerInfo,"GetLocalPlayerIndex")
  if type(f) ~= "function" then return false end
  local ok,localIndex=pcall(f)
  if not ok or (localIndex ~= 0 and localIndex ~= 1) or index ~= localIndex then return false end
  local r=native(0,0)
  if not r or not r.down then return false end
  local lock=native(1,r.generation)
  local protected=lock ~=nil and lock.applied==true and lock.down and lock.generation==r.generation
  if protected then D.active, D.boundMission, D.boundEpoch = true, W.mission, W.epoch end
  return protected
end
local function accept(v,role,now)
  if type(v) ~= "table" or #v ~= 10 or not valid(v[1], 1, 65535) or not valid(v[2], 0, U32 - 1) or
    not valid(v[3], 0, 1) or not valid(v[4], 0, 2) or not valid(v[8], 0, 1) or
    not valid(v[9], 0, U32 - 1) or not valid(v[10], 0, 1) then return false end
  for i = 5, 7 do if not valid(v[i], -1000000000, 1000000000) then return false end end
  local down=v[3]==1
  if down and (v[2] == 0 or v[4] == 0 or v[10] == 0) or not down and v[4] ~= 0 then return false end
  local old=D.records[role]
  if old then
    if not newer(v[1], old.sequence, 65535) then return false end
    if v[2] ~= old.generation and not newer(v[2], old.generation, U32) then return false end
    if down and not old.down and v[2] == old.generation then return false end
    if down and old.down and v[2] == old.generation and v[4] ~= old.cause then return false end
    -- Up again under a newer generation: the owner's native ended that cycle
    -- without a revive (its controls timeout retires the generation, item 4).
    -- It is never a revive of the old generation, so it needs no grant.
    local abandoned=old.down and not down and v[2] ~= old.generation
    if old.down and not down and not abandoned and D.granted[role] ~= old.generation then return false end
  end
  D.records[role]={sequence=v[1],generation=v[2],down=down,cause=v[4],
    pos={v[5],v[6],v[7]},held=v[8]==1,targetGeneration=v[9],ready=v[10]==1,at=now,
    clockMission=W.mission,clockEpoch=W.epoch}
  return true
end
D.acceptStatus=accept
H[OP.DOWNED_STATUS] = function(v, now)
  if on() then accept(v, 3 - W.role, now or W.now()) end
end
H[OP.DOWNED_COMMIT] = function(v, now)
  if W.host or not on() or type(v) ~= "table" or #v ~= 9 or not valid(v[1], 1, 65535) or
    not newer(v[1], D.commitSeen, 65535) or not valid(v[2], 0, U32 - 1) or not valid(v[3], 0, 1) or
    not valid(v[4], 0, 2) or not valid(v[5], 0, U32 - 1) or not valid(v[6], 0, 1) or
    not valid(v[7], 0, 2) or not valid(v[8], 0, HOLD) or not valid(v[9], 0, 2) then return end
  if v[3] == 1 and (v[2] == 0 or v[4] == 0) or v[3] == 0 and v[4] ~= 0 or
    v[6] == 1 and (v[5] == 0 or v[7] == 0) or v[6] == 0 and v[7] ~= 0 or
    v[9] == 0 and v[8] ~= 0 then return end
  local r=D.records[1]
  if W.role == 2 and r and r.down and v[3] == 0 and v[2] == r.generation then D.granted[1] = r.generation end
  D.commitSeen,D.remoteCommit,D.remoteCommitAt=v[1],v,now or W.now()
end
local function reviveLocal(generation)
  if D.nativeSleepActive then
    local wake=native(5,generation)
    if not wake or wake.applied ~= true or wake.nativeSleepReleaseReady ~= true or
      wake.generation ~= generation then return false end
  end
  if D.unlockGeneration ~= generation then
    if not mask(false) then return false end
    D.unlockGeneration,D.unlockAt=generation,W.now()
  end
  local r=native(2,generation)
  if not r or r.applied ~= true or r.down or r.generation ~= generation then
    if W.now() - D.unlockAt > 2000 then
      D.unlockGeneration,D.unlockAt=nil,nil
      mask(true)
    end
    return false
  end
  D.unlockGeneration,D.unlockAt=nil,nil
  D.granted[W.role]=generation
  if PlayerStance then pcall(Player.RequestToSetTargetStance,PlayerStance.STAND)end
  return true
end
H[OP.REVIVE_GRANT] = function(v)
  if W.host or not on() or type(v) ~= "table" or #v ~= 3 or v[1] ~= W.role or
    not valid(v[2], 1, U32 - 1) or not valid(v[3], 1, U32 - 1) or
    not newer(v[3], D.receivedGrant, U32) then return end
  local r=D.records[W.role]
  if not r or not r.down or r.generation ~= v[2] then return end
  D.pendingGrant={role=v[1],generation=v[2],serial=v[3]}
  if reviveLocal(v[2]) then D.receivedGrant, D.pendingGrant = v[3], nil end
end
local function poll(now)
  local r=native(0,0)
  if not r then
    local old=D.records[W.role]
    if old then old.ready = false end
    resetHold()
    D.refusalAt=D.refusalAt or now
    if D.maskOwned and now - D.refusalAt > 2000 then
      D.stop(); log("step=refused;reason=controls_timeout")
    end
    return false
  end
  if D.revivePoll then r.held = D.revivePoll(r, now) end
  D.refusalAt=nil
  D.nativeSleepActive=r.nativeSleepActive==true or nil
  D.active,D.boundMission,D.boundEpoch=true,W.mission,W.epoch
  if D.unlockGeneration and D.unlockGeneration ~= r.generation then D.unlockGeneration, D.unlockAt = nil, nil end
  if not r.down and D.maskOwned and not D.nativeSleepActive then
    -- A healthy owner never keeps the downed mask: the native owner ended the
    -- cycle without a revive (its controls timeout, item 4, before or after
    -- the lock). accept() below takes the newer generation as abandoned.
    if not mask(false) then resetHold(); return false end
    local own=D.records[W.role]
    log(fmt("step=abandoned;role=%d;generation=%.0f;next=%.0f", W.role, own and own.generation or 0, r.generation))
  end
  if r.down and D.unlockGeneration ~= r.generation then
    if not mask(true, r.cause) then resetHold(); return false end
    local lock=native(1,r.generation)
    if not lock or lock.applied ~= true or not lock.down or lock.generation ~= r.generation then
      local old=D.records[W.role]
      if old then old.ready = false end
      resetHold()
      return false
    end
  end
  local remote=D.records[3 - W.role]
  local targetGeneration=fresh(remote,now) and remote.down and remote.generation or 0
  D.sequence=D.sequence % 65535 + 1
  local v={D.sequence,r.generation,r.down and 1 or 0,r.cause,r.pos[1],r.pos[2],r.pos[3],
    r.held and 1 or 0, targetGeneration, 1}
  local wasDown=D.records[W.role] and D.records[W.role].down
  if not accept(v, W.role, now) then resetHold(); return false end
  if r.down and not wasDown then
    log(fmt("step=entered;role=%d;generation=%.0f;cause=%d", W.role, r.generation, r.cause))
  end
  if now >= D.txAt and W.tx(OP.DOWNED_STATUS, unpack(v)) then
    D.txAt=now + 100; if D.hitPublished then D.hitPublished(v,now) end
  end
  if D.hitTick then D.hitTick(r, now) end
  return true
end
local function grant(role,generation)
  local p=D.pendingGrant
  if not p then
    D.grantSerial=D.grantSerial % (U32 - 1) + 1
    p = {role = role, generation = generation, serial = D.grantSerial}
    D.pendingGrant=p
  end
  if D.grantReady and not D.grantReady(p) then return false end
  if role == W.role then
    if reviveLocal(generation) then D.pendingGrant = nil; return true end
    return false
  end
  if not W.tx(OP.REVIVE_GRANT, p.role, p.generation, p.serial) then return false end
  D.granted[role]=generation
  log(fmt("step=grant;role=%d;generation=%.0f;serial=%.0f", role, generation, p.serial))
  return true
end
local function bothDown()
  local R=W.recovery
  if not R or type(R.failBothDowned) ~= "function" then
    if not D.failed then log("step=refused;reason=shared_recovery"); D.failed = true end
    return
  end
  if R.failBothDowned() == true then D.failed = true; log("step=both_downed;shared_failure=1") end
end
local function hostTick(now)
  local a,b=D.records[1],D.records[2]
  local pairedClock=D.sleepClock and D.sleepClock.Tick(now) or false
  if not fresh(a, now) or not fresh(b, now) then resetHold(); return end
  if D.pendingGrant then
    local p=D.pendingGrant
    local target=D.records[p.role]
    if not target or not target.down or target.generation ~= p.generation then D.pendingGrant = nil
    else
      if now >= (p.retryAt or 0) and grant(p.role, p.generation) then p.retryAt = now + 500 end
      resetHold()
      return
    end
  end
  for role = 1, 2 do
    local timer=D.sleepTimers[role]
    if pairedClock and timer and not timer.granted and timer.elapsed >= SLEEP then
      if grant(role, timer.generation) then
        timer.granted = true
        log(fmt("step=sleep_auto_wake;role=%d;generation=%.0f;duration_ms=%d", role, timer.generation, SLEEP))
      end
      resetHold(); return
    end
  end
  if a.down and b.down then
    resetHold()
    if a.cause == 1 and b.cause == 1 and not D.failed then bothDown() end
    return
  end
  local targetRole=a.down and 1 or b.down and 2 or 0
  if targetRole == 0 then resetHold(); return end
  local target,actor=D.records[targetRole],D.records[3 - targetRole]
  if actor.down or not actor.held or actor.targetGeneration ~= target.generation or not distance(actor, target) then
    releaseHold(now); return
  end
  if D.target ~= targetRole or D.holdGeneration ~= target.generation or D.holdActor ~= actor.generation then
    local resumed=resumedProgress(now,targetRole,target,actor)
    D.decay=nil
    D.holdAt,D.holdGeneration,D.holdActor=now - resumed,target.generation,actor.generation
  end
  D.target,D.progress=targetRole,math.min(HOLD,math.max(0,floor(now - D.holdAt)))
  if D.progress >= HOLD and grant(targetRole, target.generation) then resetHold() end
end
local function commit(now)
  if not W.host or now < D.commitAt then return end
  local a,b=D.records[1],D.records[2]
  if not a or not b then return end
  local seq=D.commitSequence % 65535 + 1
  if W.tx(OP.DOWNED_COMMIT, seq, a.generation, a.down and 1 or 0, a.cause,
    b.generation, b.down and 1 or 0, b.cause, D.progress, D.target) then
    D.commitSequence,D.commitAt=seq,now + 200
  end
end
function D.hud(now)
  local l,r=D.records[W.role],D.records[3 - W.role]
  local lf,rf=fresh(l,now),fresh(r,now)
  local inRange=lf and rf and not l.down and r.down and distance(l,r) or false
  local progress=0
  if inRange and l.held and l.targetGeneration == r.generation then
    if W.host and D.target == 3 - W.role then progress = D.progress
    elseif not W.host and D.remoteCommit and D.remoteCommitAt and now >= D.remoteCommitAt and
      now - D.remoteCommitAt <= FRESH and D.remoteCommit[9] == 3 - W.role and
      D.remoteCommit[W.role==1 and 5 or 2]==r.generation and
      D.remoteCommit[W.role==1 and 2 or 5]==l.generation then progress=D.remoteCommit[8] end
  end
  return {down = lf and l.down == true or false, teammateDown = rf and r.down == true or false,
  inRange=inRange,near=inRange,progress=progress,holdMs=HOLD,
    cause = lf and l.down and l.cause or 0, teammateCause = rf and r.down and r.cause or 0}
end
function D.tick(now)
  if D.nativePending or D.maskOwned and not D.active then
    D.stop(); if D.nativePending or D.maskOwned then return end
  end
  if not on() then partnerSleepClear(); if D.active then D.stop() else D.sleepTimers = {} end; return end
  if D.sleepClock then D.sleepClock.Bind() end
  if not clockTime(now) then pauseSleepClock("clock_invalid"); resetHold(); publishHud(); return end
  if D.lastNow and (now < D.lastNow or now - D.lastNow > FRESH) then
    pauseSleepClock(now < D.lastNow and "clock_reverse" or "loop_gap"); resetHold()
  end
  D.lastNow=now
  if not W.inGame() then partnerSleepClear(); pauseSleepClock("game_paused"); resetHold(); publishHud(); return end
  if not poll(now) then partnerSleepClear(); pauseSleepClock("native_unreadable"); publishHud(); return end
  if not W.host and D.pendingGrant then
    local p,r=D.pendingGrant,D.records[W.role]
    if not r.down or r.generation ~= p.generation then D.pendingGrant = nil
    elseif reviveLocal(p.generation) then D.receivedGrant, D.pendingGrant = p.serial, nil end
  end
  if W.host then hostTick(now); commit(now) end
  if D.partnerSleepPublish then D.partnerSleepPublish(now) end
  publishHud(on() and D.hud(now) or nil)
end
D.reset()
W.startHooks[#W.startHooks + 1] = D.reset
W.unpairedHooks[#W.unpairedHooks + 1] = D.stop
W.stopHooks[#W.stopHooks + 1] = function(final) if final then D.stop() end end
W.pairedHooks[#W.pairedHooks + 1] = function(changed) if changed then D.stop() end end
end
