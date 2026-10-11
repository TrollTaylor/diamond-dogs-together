-- Scratch-only private capacity exercise. Original code; no ride attachment.
-- Root must omit this fragment from normal profiles and admit the exact candidate.
do
 local W=e.MgsCoopWorld
 if type(W)=="table" then
  local R={last="idle",nextSelect=0,nextPlace=0,nextRide=0,mounted=false};W.buddyRide=R
  local floor=math.floor
  local function finite(n,b)return type(n)=="number" and n==n and n>=-b and n<=b end
  local function uint(n,lo,hi)return finite(n,hi) and n>=lo and n==floor(n) end
  local function note(s)R.last=s;return s end
  local function packSuffix(r)
   if type(r)~="table" or type(r.pack)~="string" or r.pack=="" then return "" end
   return " pack"..r.pack
  end
  local function guard()
   local B=W.buddy
   if type(B)~="table" or type(B.guard)~="function" then return false,"buddy_unbound" end
   local ok,yes,why=pcall(B.guard,true)
   if not ok or not yes then return false,why or "context" end
   if not uint(W.mission,30010,30050) or not uint(W.epoch,1,65535) then return false,"epoch" end
   if type(W.api)~="table" or type(W.receipt)~="function" then return false,"binding" end
   return true
  end
  local function localPose()
   if type(W.get)~="function" then return end
   local ok,x,y,z,yrot=pcall(function()return W.get(vars,"playerPosX"),W.get(vars,"playerPosY"),W.get(vars,"playerPosZ"),W.get(vars,"playerRotY")end)
   if not ok then return end
   if not finite(x,999996) or not finite(y,999996) or not finite(z,999996) or not finite(yrot,1000000000) then return end
   return x,y,z,((yrot+180)%360-180)*math.pi/180
  end
  local function kind()
   if not guard() then return end
   local a=W.api;if type(a.buddyType)~="function" then return end
   local ok,t=pcall(a.buddyType)
   if not ok then return end
   if t==a.buddyHorse then return 1,"TppHorse2",6656 end
   if t==a.buddyWalker then return 2,"TppWalkerGear2",22528 end
   return 0
  end
  local function receipt(k)
   local ok,why=guard();if not ok then return nil,why end
   local mission,epoch=W.mission,W.epoch
   local called,r=pcall(W.receipt,string.format("return 'MGS_COOP_BUDDY_CAPACITY_V1' -- %d %d %d",mission,epoch,k))
   if not called then return nil,"native_error" end
   if type(r)~="table" or r.ready~=true or r.admitted~=true or r.profile~="buddy_capacity_two_v1" then
    local why=type(r)=="table" and r.reason
    return nil,type(why)=="string" and #why<=40 and why:match("^[a-z0-9_]+$") and "native_"..why or "not_admitted"
   end
   if r.mission~=mission or r.epoch~=epoch or r.kind~=k or not uint(r.generation,1,4294967295) or not uint(r.cycle,1,4294967295) then return nil,"native_scope" end
   if W.mission~=mission or W.epoch~=epoch or not guard() then return nil,"context_changed" end
   if k==0 then return r end
   if r.pack~=nil and (type(r.pack)~="string" or r.pack=="" or #r.pack>16 or not r.pack:match("^[0-9a-z_]+$")) then return nil,"bad_pack" end
   if r.rideMounted~=nil and type(r.rideMounted)~="boolean" then return nil,"bad_mount" end
   if r.remoteRide~=nil and r.remoteRide~=255 and not uint(r.remoteRide,0,5) then return nil,"bad_remote" end
   if type(r.commands)~="boolean" then return nil,"command_admission" end
   if not uint(r.allocated,1,2) or not uint(r.realized,0,r.allocated) or not uint(r.mask,0,3) or
     (r.allocated==1 and r.mask>1) or r.realized~=((r.mask%2)+floor(r.mask/2)) or type(r.actors)~="table" or #r.actors~=r.allocated then return nil,"bad_capacity" end
   local _,_,first=kind();if (k==1 and first~=6656) or (k==2 and first~=22528) then return nil,"selection_changed" end
   for i=1,r.allocated do
    local a=r.actors[i]
    if type(a)~="table" or a.id~=first+i-1 or type(a.occupied)~="boolean" or not uint(a.rider,0,65535) or
      a.realized~=(floor(r.mask/2^(i-1))%2==1) then return nil,"bad_actor" end
    if k==1 then
     if not uint(a.pending,0,65535) or a.state~=nil then return nil,"bad_actor" end
    elseif not uint(a.state,0,255) or a.pending~=nil or a.occupied~=(a.rider~=65535 and a.state==1) then return nil,"bad_actor" end
   end
   return r
  end
  local function stable(before,k)
   local after,why=receipt(k)
   if not after then return nil,why end
   if before.mission~=after.mission or before.epoch~=after.epoch or before.generation~=after.generation or before.cycle~=after.cycle or
     before.allocated~=after.allocated or before.realized~=after.realized or before.mask~=after.mask or before.commands~=after.commands or before.pack~=after.pack then return nil,"owner_changed" end
   for i=1,after.allocated do
    local a,b=before.actors[i],after.actors[i]
    if a.id~=b.id or a.rider~=b.rider or a.pending~=b.pending or a.state~=b.state or a.occupied~=b.occupied or a.realized~=b.realized then return nil,"actor_changed" end
   end
   return after
  end
  local function second(k,class)
   local r,why=receipt(k);if not r then return nil,why end
   if r.allocated~=2 or r.realized~=2 or r.mask~=3 then return nil,"second_unrealized" end
   local same,err=stable(r,k);if not same then return nil,err end
   if type(W.api.buddyId)~="function" then return nil,"binding" end
   local ok,id=pcall(W.api.buddyId,class,1)
   if not ok or id~=same.actors[2].id then return nil,"second_id" end
   if not guard() or kind()~=k then return nil,"context_changed" end
   local final,err=stable(same,k);if not final then return nil,err end
   return final,id
  end
  local function makeVector(a,x,y,z)
   if type(a.buddyVector)~="function" then return end
   local ok,p=pcall(a.buddyVector,x,y,z)
   local read,px,py,pz=pcall(function()return p:GetX(),p:GetY(),p:GetZ()end)
   if ok and read and finite(px,1000000) and finite(py,1000000) and finite(pz,1000000) and
    math.abs(px-x)<0.001 and math.abs(py-y)<0.001 and math.abs(pz-z)<0.001 then return p end
  end
  local function clock()
   if type(W.now)~="function" then return end
   local ok,n=pcall(W.now);if ok and finite(n,1000000000000000) then return n end
  end
  local function busy(a,k)return a.occupied or a.rider~=65535 or (k==1 and a.pending~=65535) or (k==2 and a.state~=0) end
  function R.select(which)
   local allowed={dog="buddyDog",horse="buddyHorse",walker="buddyWalker",none="buddyNone"}
   local expected={none=0,horse=1,dog=2,walker=4}
   local key=allowed[which];if not key then return note("selection_invalid") end
   local admission,why=receipt(0);if not admission then return note(why) end
   if admission.commands~=true then return note("command_admission") end
   local ride=which=="horse" or which=="walker"
   local owned=W.buddy.s and W.buddy.s.owned
   if owned and not ride then return note("dog_borrowed") end
   local now=clock();if not now or now<R.nextSelect then return note("selection_cooldown") end
   local a=W.api;local target=a[key]
   if target~=expected[which] or type(a.buddyVector)~="function" or type(a.buddyCall)~="function" then return note("selection_binding") end
   local x,y,z,yrot=localPose();if not x then return note("local_pose") end
   local p=makeVector(a,x,y,z);if not p then return note("vector_error") end
   local recheck,err=receipt(0);if not recheck then return note(err) end
   if recheck.commands~=true then return note("command_admission") end
   if recheck.generation~=admission.generation or recheck.cycle~=admission.cycle then return note("owner_changed") end
   local yielded=false
   if owned then
    local yield=W.buddy.yieldForRide
    if type(yield)~="function" then return note("dog_borrowed") end
    local yok,yres=pcall(yield)
    if not yok or yres==false or (W.buddy.s and W.buddy.s.owned) then return note("dog_borrowed") end
    yielded=true
   end
   R.nextSelect=now+1000
   local ok,result=pcall(a.buddyCall,target,p,yrot)
   if not ok or result==false then
    if yielded and type(W.buddy.resumeFollow)=="function" then pcall(W.buddy.resumeFollow) end
    return note("selection_refused")
   end
   if yielded and type(W.buddy.keepRideYield)=="function" then pcall(W.buddy.keepRideYield) end
   return note("selection_submitted_"..which)
  end
  function R.census()
   local ok,why=guard();if not ok then return note(why) end
   local k,class=kind();if not k or k==0 then return note("select_ride_first") end
   local r,why=receipt(k);if not r then return note(why) end
   if r.allocated~=2 or r.realized~=2 or r.mask~=3 then return note(string.format("k%d allocated%d realized%d mask%d%s",k,r.allocated,r.realized,r.mask,type(r.pack)=="string" and (" pack"..r.pack) or "")) end
   local verified,id=second(k,class);if not verified then return note(id) end
   -- Native actors remain an exact census. A peer wire heartbeat does not
   -- establish a rider or an attachment on either local body.
   local a1,a2=verified.actors[1],verified.actors[2]
   local r1,o1=a1.rider,a1.occupied and 1 or 0
   local r2,o2=a2.rider,a2.occupied and 1 or 0
   local remote=verified.remoteRide or 255
   if k==2 then
    if type(W.api.send)~="function" then return note("binding") end
    local ok,count=pcall(W.api.send,{type="TppWalkerGear2"},{id="GetMaxInstanceCount"})
    if not ok or count~=verified.allocated then return note("walker_count_changed") end
    local seen,p=pcall(W.api.send,id,{id="GetPosition"})
    local readable,x,y,z=pcall(function()return p:GetX(),p:GetY(),p:GetZ()end)
    if not seen or not readable or not finite(x,1000000) or not finite(y,1000000) or not finite(z,1000000) then return note("walker_position") end
    local riding,b=pcall(W.api.send,id,{id="IsPlayerRiding"})
    if not riding or type(b)~="boolean" or b~=verified.actors[2].occupied then return note("walker_rider_changed") end
    if not stable(verified,k) then return note("owner_changed") end
    return note(string.format("k2 a2 r2 id%d rider%d occupied%d id%d rider%d state%d occupied%d p%.1f,%.1f,%.1f localRide%s%s localMounted%d peerWireRide%d",verified.actors[1].id,r1,o1,id,r2,verified.actors[2].state,o2,x,y,z,verified.rideReady and tostring(verified.rideId) or "unknown",type(verified.pack)=="string" and (" pack"..verified.pack) or "",verified.rideMounted==true and 1 or 0,remote))
   end
   return note(string.format("k1 a2 r2 id%d rider%d occupied%d id%d rider%d pending%d occupied%d localRide%s%s localMounted%d peerWireRide%d",verified.actors[1].id,r1,o1,id,r2,verified.actors[2].pending,o2,verified.rideReady and tostring(verified.rideId) or "unknown",type(verified.pack)=="string" and (" pack"..verified.pack) or "",verified.rideMounted==true and 1 or 0,remote))
  end
  function R.placeSecond()
   local ok,why=guard();if not ok then return note(why) end
   local k,class=kind();if not k or k==0 then return note("select_ride_first") end
   local r,id=second(k,class);if not r then return note(id) end
   local a=r.actors[2]
   if r.commands~=true then return note("command_admission") end
   if busy(a,k) then return note("second_busy") end
   local now=clock();if not now or now<R.nextPlace then return note("place_cooldown") end
   local x,y,z,yrot=localPose();if not x then return note("local_pose") end
   if type(W.api.buddyVector)~="function" or type(W.api.send)~="function" then return note("binding") end
   local p=makeVector(W.api,x+4,y,z);if not p then return note("vector_error") end
   local recheck,why=stable(r,k);if not recheck then return note(why) end
   if recheck.commands~=true then return note("command_admission") end
   if busy(recheck.actors[2],k) then return note("second_busy") end
   R.nextPlace=now+2000
   local cmd=k==1 and {id="SetCallHorse",startPosition=p,goalPosition=p} or {id="SetPosition",pos=p,rotY=yrot}
   local ok,result=pcall(W.api.send,id,cmd)
   if not ok or result==false then return note("place_refused") end
   return note(k==1 and "horse_place_submitted" or "walker_place_submitted")
  end
  function R.assign(hostRide,guestRide,id0,id1)
   local function known(id)
    if id==id0 then return 0 end
    if id==id1 then return 1 end
    if id==nil or id==65535 then return nil end
    return false
   end
   local h,g=known(hostRide),known(guestRide)
   if h==false or g==false then return nil,nil,"ride_id_unknown" end
   if h==nil and g==nil then return 0,1 end
   if h~=nil and g==nil then return h,1-h end
   if g~=nil and h==nil then return 1-g,g end
   if h==g then return nil,nil,"same_actor" end
   return h,g
  end
  function R.ride()
   R.mounted=false
   local ok,why=guard();if not ok then return note(why) end
   local k,class=kind();if not k or k==0 then return note("select_ride_first") end
   local r,why=receipt(k);if not r then return note(why) end
   if r.allocated~=2 or r.realized~=2 or r.mask~=3 then return note("ride_capacity") end
   if type(r.pack)~="string" or r.pack=="" or r.pack=="unknown" then return note("ride_pack") end
   if r.actors[1].rider~=65535 and r.actors[1].rider==r.actors[2].rider then return note("same_actor") end
   local localRide=r.rideReady and r.rideId or 65535
   local peer=65535
   for i=1,2 do
    local actor=r.actors[i]
    if actor.occupied and actor.rider~=65535 and actor.id~=localRide then peer=actor.id end
   end
   local hostRide,guestRide=localRide,peer
   if not W.host then hostRide,guestRide=peer,localRide end
   local h,g,err=R.assign(hostRide,guestRide,r.actors[1].id,r.actors[2].id)
   if h==nil then return note(err or "same_actor") end
   local seat=W.host and h or g
   local actor=r.actors[seat+1]
   if not actor or actor.id==r.actors[2-seat].id then return note("same_actor") end
   if (actor.occupied or actor.rider~=65535) and not (r.rideReady and r.rideId==actor.id) then return note("ride_busy") end
   local otherIndex=seat==0 and 2 or 1
   if k==2 and busy(r.actors[otherIndex],k) then return note("other_ride_busy") end
   local now=clock();if not now or now<R.nextRide then return note("ride_cooldown") end
   local x,y,z,yrot=localPose();if not x then return note("local_pose") end
   if type(W.api.send)~="function" then return note("binding") end
   if k==1 and type(W.api.buddyVector)~="function" then return note("binding") end
   local recheck,why=stable(r,k);if not recheck then return note(why) end
   if recheck.commands~=true then return note("command_admission") end
   if recheck.allocated~=2 or recheck.realized~=2 or recheck.pack~=r.pack then return note("owner_changed") end
   if k==2 and busy(recheck.actors[otherIndex],k) then return note("other_ride_busy") end
   R.nextRide=now+1000
   if k==1 then
    local p=makeVector(W.api,x,y,z);if not p then return note("vector_error") end
    local sent,result=pcall(W.api.send,actor.id,{id="SetCallHorse",startPosition=p,goalPosition=p})
    if not sent or result==false then return note("ride_call_refused") end
   else
    local function at(id,dx)
     local sent,result=pcall(W.api.send,id,{id="SetPosition",pos={x+dx,y,z},rotY=yrot})
     return sent and result~=false
    end
    local other=recheck.actors[otherIndex]
    if not at(other.id,40) then return note("ride_call_refused") end
    -- The first command yields to the engine. Re-read the same qualified
    -- native actor fields before issuing the second placement command.
    local after,why=stable(recheck,k);if not after then return note(why) end
    if busy(after.actors[otherIndex],k) then return note("other_ride_busy") end
    if not at(actor.id,2) then return note("ride_call_refused") end
   end
   return note("ride_ready_"..actor.id)
  end
  function R.status()return tostring(R.last):sub(1,160) end
  W.startHooks[#W.startHooks+1]=function()R.last,R.nextSelect,R.nextPlace,R.nextRide,R.mounted="idle",0,0,0,false end
  W.stopHooks[#W.stopHooks+1]=function()R.last,R.nextSelect,R.nextPlace,R.nextRide,R.mounted="stopped",0,0,0,false end
 end
end
