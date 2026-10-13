#pragma once
#include "native_pose_birth.hpp"
#include "native_pose_carry_participants.hpp"
namespace mgscoop::native_pose_birth_census {
// Pure identity closure, with full-width actual native birth receipts.
inline bool Current(const native_pose_carry_participants::Pair& pair,
 const native_pose_birth::Receipt& local,const native_pose_birth::Receipt& peer) noexcept {
 if(!pair.player||!pair.data||!pair.scriptState||!local.serial||local.serial==peer.serial||
    pair.body[0].id!=0||pair.body[1].id!=1||pair.body[0].block==pair.body[1].block||
    pair.body[0].state==pair.body[1].state||pair.body[0].cs==pair.body[1].cs||pair.body[0].entry==pair.body[1].entry)return false;
 const native_pose_birth::Identity a{pair.player,pair.body[0].block,pair.body[0].state,pair.body[0].cs,pair.body[0].entry};
 const native_pose_birth::Identity b{pair.player,pair.body[1].block,pair.body[1].state,pair.body[1].cs,pair.body[1].entry};
 return native_pose_birth::Current(local,a)&&native_pose_birth::Current(peer,b);
}
}
