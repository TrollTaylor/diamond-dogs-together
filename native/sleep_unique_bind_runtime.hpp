#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::sleep_unique_bind_runtime {
using Log=void(*)(const char*) noexcept;
struct Authorization {
 bool exactRetailPe=false,offline=false,copiedSaveLab=false;
 // Fresh actual initialized worker/private-thread/lab/save check. Not a
 // caller-supplied gameplay writer, birth, scene or native invocation ticket.
 bool(*privateWorkerCurrent)() noexcept=nullptr;
};
// Initial observer only: code gate is taken before either entry is patched.
// No native getter/append or engine resource method is invoked by this API.
bool Prepare(std::uintptr_t image,std::size_t span,const wchar_t* directory,const Authorization&,Log) noexcept;
bool Prepared() noexcept;
bool SourceStatus(char*,std::size_t) noexcept;
bool BindingStatus(char*,std::size_t) noexcept;
// fresh=true takes another bounded read-only current typed owner snapshot.
// Neither command creates a native constructor lifetime or append authority.
bool BankTraceStatus(char*,std::size_t,bool fresh=false) noexcept;
}
