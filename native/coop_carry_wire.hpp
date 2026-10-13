#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::coop_carry_wire {
constexpr std::size_t size=20;
constexpr std::uint8_t start=1,drop=2,readiness=3;
struct Frame {std::uint32_t mission=0,localGeneration=0,peerGeneration=0;std::uint16_t epoch=0;std::uint64_t at=0;bool valid=false,ready=false;};
struct Message {std::uint8_t event=0;bool ready=false;std::uint16_t sequence=0,epoch=0;std::uint32_t mission=0,sender=0,victim=0;};
inline bool Scope(const Frame& f,std::uint64_t now) noexcept {return f.valid&&f.epoch&&f.localGeneration&&f.peerGeneration&&
 (f.mission==30010||f.mission==30020||f.mission==30050)&&f.at&&now>=f.at&&now-f.at<=750;}
inline std::uint32_t Word(const std::uint8_t* p) noexcept {return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
inline void Put(std::uint8_t* p,std::uint32_t n) noexcept {for(unsigned k=0;k<4;++k)p[k]=static_cast<std::uint8_t>(n>>(k*8));}
inline bool Parse(const std::uint8_t* p,std::size_t n,Message& m) noexcept {
 m={};if(!p||n!=size||p[0]!=0x4b||p[1]!=2||p[2]<start||p[2]>readiness||p[3]>1||(p[2]!=readiness&&p[3]))return false;
 m.event=p[2];m.ready=p[3]!=0;m.sequence=std::uint16_t(p[4]|(std::uint16_t(p[5])<<8));m.epoch=std::uint16_t(p[6]|(std::uint16_t(p[7])<<8));
 m.mission=Word(p+8);m.sender=Word(p+12);m.victim=Word(p+16);return m.epoch&&m.sender&&m.victim;
}
inline bool Encode(const Frame& f,std::uint64_t now,std::uint8_t event,std::uint16_t sequence,std::uint8_t (&p)[size]) noexcept {
 if(!Scope(f,now)||event<start||event>readiness)return false;
 p[0]=0x4b;p[1]=2;p[2]=event;p[3]=event==readiness&&f.ready?1u:0u;
 p[4]=static_cast<std::uint8_t>(sequence);p[5]=static_cast<std::uint8_t>(sequence>>8);p[6]=static_cast<std::uint8_t>(f.epoch);p[7]=static_cast<std::uint8_t>(f.epoch>>8);
 Put(p+8,f.mission);Put(p+12,f.localGeneration);Put(p+16,f.peerGeneration);return true;
}
struct State {
 Frame frame{};std::uint16_t last=0;bool seen=false,peerReady=false;std::uint64_t readyAt=0;
 void Update(const Frame& f) noexcept {if(frame.mission!=f.mission||frame.epoch!=f.epoch||frame.localGeneration!=f.localGeneration||frame.peerGeneration!=f.peerGeneration){seen=false;peerReady=false;readyAt=0;}frame=f;}
 bool Receive(const Message& m,std::uint64_t now,bool& fresh) noexcept {
  fresh=false;if(!Scope(frame,now)||m.mission!=frame.mission||m.epoch!=frame.epoch||m.sender!=frame.peerGeneration||m.victim!=frame.localGeneration)return false;
  if(seen){const auto delta=std::uint16_t(m.sequence-last);if(!delta)return true;if(delta>=0x8000u)return false;}
  seen=true;last=m.sequence;fresh=true;
  if(m.event==readiness){peerReady=m.ready;readyAt=now;}return true;
 }
 bool PeerReady(std::uint64_t now) const noexcept {return Scope(frame,now)&&peerReady&&readyAt&&now>=readyAt&&now-readyAt<=750;}
};
}
