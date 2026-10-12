#pragma once
#include <cstdint>
namespace mgscoop::native_pose_birth {
struct Identity {
 std::uintptr_t player=0,block=0,state=0,cs=0,entry=0;
};
inline bool Same(const Identity& a,const Identity& b) noexcept {
 return a.player&&a.block&&a.state&&a.cs&&a.entry&&a.player==b.player&&a.block==b.block&&a.state==b.state&&a.cs==b.cs&&a.entry==b.entry;
}
struct Completion {
 Identity identity{};
 bool installedAtEntry=false,installedAtExit=false,workerAtEntry=false,workerAtExit=false;
 bool exactCaller=false,normalReturn=false,typedPair=false;
 std::uint32_t entryThread=0,exitThread=0;std::uint64_t lifecycleEpoch=0;
};
struct Receipt {Identity identity{};std::uint32_t thread=0;std::uint64_t serial=0;bool ready=false;};
inline Receipt Complete(const Completion& c,std::uint64_t serial,std::uint64_t currentEpoch) noexcept {
 if(!serial||!c.lifecycleEpoch||c.lifecycleEpoch!=currentEpoch||!c.installedAtEntry||!c.installedAtExit||!c.workerAtEntry||!c.workerAtExit||
    !c.exactCaller||!c.normalReturn||!c.typedPair||!c.entryThread||c.entryThread!=c.exitThread||
    !Same(c.identity,c.identity))return {};
 return {c.identity,c.exitThread,serial,true};
}
inline bool Current(const Receipt& r,const Identity& i) noexcept {return r.ready&&r.serial&&r.thread&&Same(r.identity,i);}
inline bool InvalidatedBy(const Receipt& r,std::uintptr_t object,unsigned kind) noexcept {
 if(!r.ready||!object)return false;
 return kind==0?object==r.identity.block:kind==1?object==r.identity.cs:kind==2?object==r.identity.entry:false;
}
}
