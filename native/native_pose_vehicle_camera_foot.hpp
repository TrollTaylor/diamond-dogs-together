#pragma once
#include "native_pose_camera_reader.hpp"
namespace mgscoop::native_pose_vehicle_camera_foot {
namespace cam=native_pose_camera_reader;
struct Scope {
 cam::Slice camera{};bool enabled=false,exactNative=false,held=false,pending=false,retained=false,stopping=false;
};
inline bool Ready(const Scope& s)noexcept {
 return s.enabled&&s.exactNative&&!s.held&&!s.pending&&!s.retained&&!s.stopping&&cam::SliceReady(s.camera);
}
inline bool SameScope(const Scope& a,const Scope& b)noexcept {
 return Ready(a)&&Ready(b)&&a.camera.generation==b.camera.generation&&a.camera.epoch==b.camera.epoch&&
  a.camera.thread==b.camera.thread&&a.camera.ownerThread==b.camera.ownerThread&&
  a.camera.sequence==b.camera.sequence&&a.camera.contextSequence==b.camera.contextSequence&&
  a.camera.birthSerial==b.camera.birthSerial;
}
struct Foot {
 std::uintptr_t block=0,primary=0,node=0,storage=0,record=0,bitmap=0,map=0;
 std::uint32_t bitmapWord=0;std::uintptr_t callback=0;
};
template<class R>bool FootOnce(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,Foot& out,const char** reason=nullptr)noexcept {
 out={};out.block=block;
 const auto stage=[&](const char* text)noexcept{if(reason)*reason=text;};stage("vehicle_foot_block_or_controller");
 if(!image||image>UINTPTR_MAX-0xa01d000||!player||player>UINTPTR_MAX-0x218||!block||
    block>UINTPTR_MAX-0x140||(player&7u)||(block&7u))return false;
 std::uintptr_t vt=0,owner=0,header=0,size=0,used=0,stride=0,count=0,bound=0;
 std::uint32_t one=0,base=0,row=0,tag=0;std::uint16_t alignment=0,flags=0,vehicle=0,candidate=0;
 std::uint8_t phase=0,next=0;std::int32_t adjustment=0;
 if(!cam::Read(r,block,vt)||vt!=image+0x2295210||!cam::Read(r,block+8,one)||one!=1||
    !cam::Read(r,block+0xc,base)||base!=1||!cam::Read(r,block+0x138,owner)||owner!=player||
    !cam::Read(r,block+0x128,out.primary)||!out.primary||out.primary>UINTPTR_MAX-0x88||(out.primary&7u)||
    !cam::Read(r,out.primary,vt)||vt!=image+0x23cbea8||
    !cam::Read(r,out.primary+0x10,vt)||vt!=image+0x23cbee0||
    !cam::Read(r,out.primary+0x30,vt)||vt!=image+0x23cbf10||
    !cam::Read(r,out.primary+0x48,vt)||vt!=image+0x23cbf50||
    !cam::Read(r,out.primary+8,owner)||owner!=block)return false;
 stage("vehicle_foot_node");
 if(!cam::Read(r,out.primary+0x38,out.node)||!out.node||out.node>UINTPTR_MAX-0x40||(out.node&7u)||
    !cam::Read(r,out.node,vt)||vt!=image+0x2306628||!cam::Read(r,out.node+0x20,one)||one!=1||
    !cam::Read(r,out.node+0x24,base)||base!=1||!cam::Read(r,out.node+0x2c,row)||row!=0x23||
    !cam::Read(r,out.node+0x38,owner)||owner!=out.primary+0x30)return false;
 // Exact vehicle-controller allocator closure from1244CB0/12447E0. Padding is not read.
 stage("vehicle_foot_allocator");
 if(!cam::Read(r,out.primary+0x50,out.storage)||!out.storage||out.storage>UINTPTR_MAX-0x270||(out.storage&15u)||
    !cam::Read(r,out.primary+0x58,header)||header!=out.storage||
    !cam::Read(r,out.primary+0x60,size)||size!=0x270||!cam::Read(r,out.primary+0x68,used)||used!=size||
    !cam::Read(r,out.primary+0x70,alignment)||alignment!=16||
    !cam::Read(r,out.primary+0x72,flags)||!(flags&1u)||!cam::Read(r,out.primary+0x74,tag)||tag!=0x1c0048||
    !cam::Read(r,out.primary+0x78,out.record)||out.record!=out.storage+0x30||
    !cam::Read(r,header,vt)||vt!=image+0x645940||!cam::Read(r,header+0x10,owner)||owner!=out.record||
    !cam::Read(r,header+0x18,stride)||stride!=0x240||!cam::Read(r,header+0x20,count)||count!=1)return false;
 stage("vehicle_foot_phase");
 if(!cam::Read(r,out.node+8,out.bitmap)||!out.bitmap||(out.bitmap&3u)||!cam::Read(r,out.bitmap,out.bitmapWord)||
    !cam::Read(r,out.record+0x40,out.callback)||(out.callback&&out.callback!=image+0x645940)||
    !cam::Read(r,out.record+0x48,adjustment)||adjustment||
    !cam::Read(r,out.record+0x54,phase)||phase||!cam::Read(r,out.record+0x55,next)||next||
    !cam::Read(r,out.record+0x84,vehicle)||vehicle!=0xffff||
    !cam::Read(r,out.record+0x86,candidate)||candidate!=0xffff)return false;
 // No physical vehicle attachment may be skipped, even if core purpose is cold.
 stage("vehicle_foot_attachment");
 return cam::Read(r,player+0x120,out.map)&&out.map&&out.map<=UINTPTR_MAX-16&&!(out.map&7u)&&
    cam::Read(r,out.map+8,bound)&&!bound;
}
inline bool SameFoot(const Foot& a,const Foot& b)noexcept {
 return a.block==b.block&&a.primary==b.primary&&a.node==b.node&&a.storage==b.storage&&a.record==b.record&&
  a.bitmap==b.bitmap&&a.bitmapWord==b.bitmapWord&&a.map==b.map&&a.callback==b.callback;
}
struct Receipt {cam::SenderWitness sender{};Foot foot{};};
template<class R,class S>bool Filter(R& r,S& sample,std::uintptr_t image,std::uintptr_t caller,std::uint32_t incoming,
 std::uintptr_t player,std::uintptr_t block,std::uintptr_t camera,std::uintptr_t sender,Receipt& out,const char** reason=nullptr)noexcept {
 out={};if(reason)*reason="camera_vehicle_foot_scope";
 const auto first=sample();if(!Ready(first))return false;
 const auto body=cam::Incoming(caller,image,incoming);if(!body.known||body.body!=1)return false;
 Receipt a{},b{};
 if(!cam::SenderOnceExactMode(r,image,player,block,camera,sender,4,a.sender,reason)||
    !FootOnce(r,image,player,block,a.foot,reason))return false;
 const auto middle=sample();if(!SameScope(first,middle))return false;
 if(!cam::SenderOnceExactMode(r,image,player,block,camera,sender,4,b.sender,reason)||
    !FootOnce(r,image,player,block,b.foot,reason)||!SameScope(first,sample())||
    !cam::SameSender(a.sender,b.sender)||!SameFoot(a.foot,b.foot))return false;
 out=b;if(reason)*reason="camera_vehicle_foot_filtered";return true;
}
}
