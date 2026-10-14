#pragma once
#include "carry_queued_abort_contract.hpp"
#include "carry_request_payload.hpp"
namespace mgscoop::carry_queued_abort_gate {
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!carry_request_payload::ImageSpan(image,span))return false;unsigned char buffer[8192]{};
 for(const auto& p:carry_queued_abort_contract::sites){if(p.rva>span||p.size>span-p.rva||p.size>sizeof(buffer)||!r(image+p.rva,buffer,p.size)){mismatch=p.rva;return false;}for(std::size_t j=0;j<p.size;++j)if(buffer[j]!=p.bytes[j]){mismatch=p.rva;return false;}}
 for(const auto& p:carry_queued_abort_contract::slots){std::uintptr_t v=0;if(p.rva>span||sizeof(v)>span-p.rva||p.target>=span||!r(image+p.rva,&v,sizeof(v))||v!=image+p.target){mismatch=p.rva;return false;}}return true;
}
} // namespace mgscoop::carry_queued_abort_gate
