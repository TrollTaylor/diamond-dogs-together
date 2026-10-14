#pragma once
#include "coop_carry_original_view.hpp"
#include "mgscoop/camera_mode_setter.hpp"
namespace mgscoop::coop_carry_presentation_setter {
namespace view=coop_carry_original_view;
namespace mode=camera_mode_setter;
// Pure actual-ownership proof under the module's shared receipt lock. The
// module supplies its compiled HookSetMode address and actual original call
// target. No original-view overlay or native setter invocation is involved.
template<class Reader>bool Read(Reader& read,const view::Book& book,std::uintptr_t image,std::uintptr_t slot,
 std::uintptr_t actual,std::uintptr_t expectedWrapper,std::uintptr_t originalCall,mode::Receipt& out) noexcept {
 out={};if(book.image!=image||!view::Range(expectedWrapper,1)||!view::Within(book,image+0x11f6520,4)||
  originalCall!=image+0x11f6520||actual!=expectedWrapper)return false;
 unsigned index=0;
 if(slot==image+0x23c75f8)index=view::mode0;
 else if(slot==image+0x23c7498)index=view::mode1;
 else return false;
 const auto& patch=book.patches[index];std::uintptr_t original=0,captured=0;
 std::memcpy(&original,patch.original,8);std::memcpy(&captured,patch.capturedExpected,8);
 if(patch.kind!=view::Kind::pointer||patch.site!=slot||patch.width!=8||patch.detour!=expectedWrapper||
  original!=originalCall||captured!=originalCall||!view::Live(read,book,patch))return false;
 constexpr unsigned char nativeBytes[4]={0x88,0x51,0x6d,0xc3};unsigned char first[4]{},last[4]{};
 if(!read(originalCall,first,4)||std::memcmp(first,nativeBytes,4)||!view::Live(read,book,patch)||
  !read(originalCall,last,4)||std::memcmp(last,nativeBytes,4)||!view::Live(read,book,patch))return false;
 mode::Receipt result{mode::Kind::ownedCarry,image,slot,originalCall,actual,patch.serial};
 if(!mode::Valid(result,image,slot,actual))return false;out=result;return true;
}
}
