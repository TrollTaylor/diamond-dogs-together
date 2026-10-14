#pragma once
#include <cstdint>
namespace mgscoop::stock_sleep_release_policy {
struct Episode {std::uint32_t mission=0,generation=0;std::uint16_t epoch=0;};
inline bool Same(const Episode& a,const Episode& b) noexcept {return a.mission==b.mission&&a.generation==b.generation&&a.epoch==b.epoch;}
struct Release {
 Episode owner{};bool requested=false,physicalRetired=false;
 bool Pending(const Episode& current,bool down,std::uint8_t cause) const noexcept {return requested&&down&&cause==2&&Same(owner,current);}
 bool Request(const Episode& current,bool down,std::uint8_t cause) noexcept {
  if(!down||cause!=2||!current.mission||!current.generation||!current.epoch)return false;
  if(requested)return Same(owner,current);
  owner=current;requested=true;physicalRetired=false;return true;
 }
 bool ObservePhysicalRetirement(const Episode& current,bool actualRetired) noexcept {
  if(!requested||!Same(owner,current)||!actualRetired)return false;physicalRetired=true;return true;
 }
 bool MaySeed(const Episode& current,bool down,std::uint8_t cause) const noexcept {return !Pending(current,down,cause);}
 void LogicalExit(bool actualComplete) noexcept {if(actualComplete)*this={};}
};
}
