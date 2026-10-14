#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include "native_pose_selected_export_contract.hpp"
namespace mgscoop::native_pose_selected_export {
struct Scope {
 bool codeGate=false,worker=false,writer=false,birthCurrent=false,modelPair=false,selectedOwned=false;
 std::uint32_t generation=0,currentGeneration=0,sceneEpoch=0,currentEpoch=0,thread=0,ownerThread=0,sequence=0,contextSequence=0;
 std::uint64_t birthSerial=0,currentBirth=0;
 std::uintptr_t image=0,caller=0,player=0,block=0,state=0,cs=0,entry=0;
 std::uintptr_t currentPlayer=0,currentBlock=0,currentState=0,currentCS=0,currentEntry=0,argumentState=0;
 std::uint32_t actualBody=0,realLocal=0,selected=0;
};
inline bool Suppress(const Scope& s)noexcept {
 return s.codeGate&&s.worker&&s.writer&&s.birthCurrent&&s.modelPair&&s.selectedOwned&&
  s.generation&&s.generation==s.currentGeneration&&s.sceneEpoch&&s.sceneEpoch==s.currentEpoch&&
  s.thread&&s.thread==s.ownerThread&&s.sequence&&(s.sequence&1u)&&s.sequence==s.contextSequence&&
  s.birthSerial&&s.birthSerial==s.currentBirth&&s.image&&s.image<=UINTPTR_MAX-native_pose_selected_export_contract::callerReturn&&
  s.caller==s.image+native_pose_selected_export_contract::callerReturn&&s.player&&s.block&&s.state&&s.cs&&s.entry&&
  s.player==s.currentPlayer&&s.block==s.currentBlock&&s.state==s.currentState&&s.cs==s.currentCS&&s.entry==s.currentEntry&&
  s.argumentState==s.state&&s.actualBody==1&&s.realLocal==0&&s.selected==1;
}
template<class R>bool CodeGate(R& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept {
 mismatch=0;if(!image||!span||image>std::numeric_limits<std::uintptr_t>::max()-span)return false;unsigned char bytes[8192]{};
 for(const auto& s:native_pose_selected_export_contract::sites){if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!r(image+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}}
 for(const auto& s:native_pose_selected_export_contract::slots){std::uintptr_t got=0;if(s.rva>span||8>span-s.rva||s.target>=span||!r(image+s.rva,&got,8)||got!=image+s.target){mismatch=s.rva;return false;}}return true;
}
}
