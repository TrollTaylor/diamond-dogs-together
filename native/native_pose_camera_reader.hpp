#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
#include <cstring>
#include "native_pose_camera_reader_contract.hpp"
namespace mgscoop::native_pose_camera_reader {
// Diagnostic witness only. It cannot authorize a native camera method or a
// suppression. An owned re-entry is an explicit private-worker refusal until
// its actual caller has been qualified. Stock calls join the shared domain.
// Exact1156558 CALL r15 sets EDX=the indexed node-loop body. A raw value
// from any other caller is unqualified. This is only a census, never permission
// to suppress a callback, change selected body or call a native method.
struct IncomingBody {bool known=false;std::uint32_t body=UINT32_MAX;};
inline IncomingBody Incoming(std::uintptr_t caller,std::uintptr_t image,std::uint32_t argument)noexcept {
 if(!image||image>UINTPTR_MAX-0xa01d000||caller<image||caller-image>=0xa01d000||
    caller-image!=native_pose_camera_reader_contract::indexed_return)return {};
 return {true,argument};
}
struct Witness {std::uint32_t realLocal=0,selected=0,mode=0,target=0,targetSelected=0;};
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& v)noexcept {
 return p&&p<=std::numeric_limits<std::uintptr_t>::max()-sizeof(v)&&r(p,&v,sizeof(v));
}
template<class R>bool Once(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t argument,Witness& out)noexcept {
 out={};if(!image||image>UINTPTR_MAX-0xa01d000||!player||player>UINTPTR_MAX-0x21c||(player&7u)||!argument||argument<0x20||argument>UINTPTR_MAX-0x388||(argument&7u))return false;
 std::uintptr_t camera=0,vt=0,owner=0;
 return Read(r,player,vt)&&vt==image+0x2295490&&Read(r,player+0xb8,camera)&&camera==argument&&
   Read(r,camera,vt)&&vt==image+0x23b8250&&Read(r,camera-0x20,vt)&&vt==image+0x23b8218&&
   Read(r,camera+0x348,owner)&&owner==player&&Read(r,player+0x214,out.realLocal)&&
   Read(r,player+0x218,out.selected)&&Read(r,camera+0x20c,out.mode)&&
   Read(r,camera+0x380,out.target)&&Read(r,camera+0x384,out.targetSelected);
}
inline bool Same(const Witness& a,const Witness& b)noexcept {
 return a.realLocal==b.realLocal&&a.selected==b.selected&&a.mode==b.mode&&a.target==b.target&&a.targetSelected==b.targetSelected;
}
template<class R>bool Pair(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t argument,Witness& out)noexcept {
 out={};Witness a{},b{};if(!Once(r,image,player,argument,a)||!Once(r,image,player,argument,b)||!Same(a,b))return false;out=b;return true;
}
// The caller's RBP is delivered by the exact owned RX shim before the C++
// prologue can reuse it. It is a sender only at the qualified native return.
inline constexpr std::size_t shimSize=17;
inline void Shim(std::uintptr_t wrapper,unsigned char (&bytes)[shimSize])noexcept {
 const unsigned char prefix[]={0x49,0x89,0xe8,0xff,0x25,0,0,0,0};
 std::memcpy(bytes,prefix,sizeof(prefix));std::memcpy(bytes+9,&wrapper,8);
}
template<class R>bool ShimMatches(R& r,std::uintptr_t at,std::uintptr_t wrapper)noexcept {
 if(!at||at>UINTPTR_MAX-shimSize||!wrapper)return false;
 unsigned char wanted[shimSize]{},actual[shimSize]{};Shim(wrapper,wanted);
 return r(at,actual,sizeof(actual))&&std::memcmp(actual,wanted,sizeof(actual))==0;
}
// Shared by both actual direct E9 and relay-JMP ownership branches.
inline bool OwnedEntry(std::uintptr_t rva,std::uintptr_t wrapper,std::uintptr_t stub,bool exactRxEntry)noexcept {
 return rva!=0x111cba0||(wrapper&&wrapper==stub&&exactRxEntry);
}
struct Slice {bool owned=false,job=false,worker=false,birthCurrent=false,modelReady=false,selectedOwned=false,sameLease=false,fault=false;
 std::uint32_t selected=0,consumer=UINT32_MAX,generation=0,epoch=0,thread=0,ownerThread=0,sequence=0,contextSequence=0;std::uint64_t birthSerial=0;};
inline bool SliceReady(const Slice& s)noexcept {
 return s.owned&&s.job&&s.worker&&s.birthCurrent&&s.modelReady&&s.selectedOwned&&s.sameLease&&!s.fault&&s.selected==1&&s.consumer==2&&
  s.generation&&s.epoch&&s.thread&&s.thread==s.ownerThread&&(s.sequence&1u)&&s.sequence==s.contextSequence&&s.birthSerial;
}
struct SenderWitness {Witness camera{};std::uintptr_t driver=0,primary=0,body=0,phaseFunction=0;std::int32_t adjustment=0;};
template<class R>bool SenderOnce(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,std::uintptr_t camera,
 std::uintptr_t sender,SenderWitness& out,const char** reason=nullptr)noexcept {
 out={};const auto stage=[&](const char* text)noexcept{if(reason)*reason=text;};stage("camera_local_view");
 if(!Pair(r,image,player,camera,out.camera)||out.camera.realLocal!=0||out.camera.selected!=1||
    out.camera.target!=0||out.camera.targetSelected!=0||out.camera.mode!=0)return false;
 std::uintptr_t p=0,vt=0,ids=0,pools=0,slots=0,blocks=0;std::uint32_t n=0,flags=0;std::uint16_t id=0;std::uint8_t pool=0;
 stage("camera_sender_block");
 if(player>UINTPTR_MAX-0x380||!block||block>UINTPTR_MAX-0x140||(block&7u)||!Read(r,player+0x204,flags)||!(flags&1u)||(flags&0x100u)||
  !Read(r,player+0xa8,p)||p||!Read(r,player+0x208,n)||n!=2||!Read(r,player+0x20c,n)||n!=2||
  !Read(r,player+0x70,ids)||!ids||ids>UINTPTR_MAX-4||!Read(r,ids,id)||id!=0||!Read(r,ids+2,id)||id!=1||
  !Read(r,player+0x78,pools)||!pools||pools>UINTPTR_MAX-2||!Read(r,pools+1,pool)||pool>=16||
  !Read(r,player+0x378,blocks)||!blocks||blocks>UINTPTR_MAX-16*0x6a8||blocks+std::uintptr_t(pool)*0x6a8!=block||
  !Read(r,player+0x50,slots)||!slots||slots>UINTPTR_MAX-128||!Read(r,slots+pool*8,p)||p!=block||
  !Read(r,block,vt)||vt!=image+0x2295210||!Read(r,block+8,n)||n!=1||!Read(r,block+0xc,n)||n!=1||
  !Read(r,block+0x138,p)||p!=player)return false;
 stage("camera_sender_pointer");
 if(!Read(r,block+0x70,out.driver)||out.driver<0x38||out.driver>UINTPTR_MAX-0x170||(out.driver&7u))return false;
 out.primary=out.driver-0x38;out.body=block;
 if(sender!=out.primary)return false;
 stage("camera_sender_owner");
 if(!Read(r,out.primary,vt)||vt!=image+0x23bc140||!Read(r,out.primary+0x20,vt)||vt!=image+0x23bc170||
  !Read(r,out.driver,vt)||vt!=image+0x23bc1b0||!Read(r,out.primary+0x1a0,p)||p!=block||
  !Read(r,image+0x23bc1b0,p)||p!=image+0x1156280||!Read(r,image+0x23bc148,p)||p!=image+0x11565d0||
  !Read(r,image+0x23b84c0,p)||p!=image+0x111cba0)return false;
 stage("camera_phase2_table");
 if(!Read(r,image+0x2c56430,out.phaseFunction)||out.phaseFunction!=image+0x11559d0||
  !Read(r,image+0x2c56438,out.adjustment)||out.adjustment!=0)return false;
 // The producer defines only function8+signed adjustment4. Padding12 is
 // deliberately not sampled or interpreted as another adjustment.
 stage("camera_selected_replica");return true;
}
inline bool SameSender(const SenderWitness& a,const SenderWitness& b)noexcept {
 return Same(a.camera,b.camera)&&a.driver==b.driver&&a.primary==b.primary&&a.body==b.body&&a.phaseFunction==b.phaseFunction&&a.adjustment==b.adjustment;
}
template<class R>bool SelectedReplica(R& r,const Slice& scope,std::uintptr_t image,std::uintptr_t caller,std::uint32_t incomingBody,
 std::uintptr_t player,std::uintptr_t block,std::uintptr_t camera,std::uintptr_t sender,SenderWitness& out,const char** reason=nullptr)noexcept {
 out={};if(reason)*reason="camera_selected_scope";const auto incoming=Incoming(caller,image,incomingBody);
 if(!SliceReady(scope)||!incoming.known||incoming.body!=1)return false;
 SenderWitness a{},b{};
 if(!SenderOnce(r,image,player,block,camera,sender,a,reason)||!SenderOnce(r,image,player,block,camera,sender,b,reason))return false;
 if(!SameSender(a,b)){if(reason)*reason="camera_sender_changed";return false;}out=b;return true;
}
template<class R>bool CodeGate(R& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept {
 mismatch=0;if(!image||!span||image>UINTPTR_MAX-span)return false;unsigned char bytes[4096]{};
 for(const auto& s:native_pose_camera_reader_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!r(image+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}
 }
 for(const auto& s:native_pose_camera_reader_contract::slots){std::uintptr_t got=0;
  if(s.rva>span||8>span-s.rva||s.target>=span||!Read(r,image+s.rva,got)||got!=image+s.target){mismatch=s.rva;return false;}
 }return true;
}
}

