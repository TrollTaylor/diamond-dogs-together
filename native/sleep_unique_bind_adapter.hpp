#pragma once
#include "sleep_unique_bind.hpp"
#include "sleep_native_bank_trace.hpp"
#include "sleep_raw_constructor_trace.hpp"
namespace mgscoop::sleep_unique_bind_native {
using CommonBind=void(*)(std::uintptr_t,std::uint32_t);
using AddMtar=bool(*)(std::uintptr_t,std::uintptr_t);
struct Configuration {
 void* context=nullptr;
 std::uintptr_t image=0;
 CommonBind originalCommon=nullptr;
 AddMtar originalAppend=nullptr;
 // Root-only providers. Reads/hash must be bounded and exception-contained.
 bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept=nullptr;
 bool (*hash)(void*,const void*,std::size_t,char*) noexcept=nullptr;
 bool (*scope)(void*,const sleep_unique_bind::Event&,sleep_unique_bind::Scope&) noexcept=nullptr;
 void (*report)(void*,const sleep_unique_bind::Event&,const sleep_unique_bind::Reply&) noexcept=nullptr;
 // Optional private read-only natural sequence diagnostic. These callbacks
 // cannot authorize append. Unknown outside-frame actor is UINT32_MAX until
 // traceScope proves its actual current record/CS row from the original engine.
 bool (*traceScope)(void*,const sleep_unique_bind::Event&,sleep_unique_bind::Scope&) noexcept=nullptr;
 void (*traceReport)(void*,const sleep_native_bank_trace::Receipt&) noexcept=nullptr;
 // Separate opaque-object admission, ONLY exact natural9C3FC1 and unknown
 // actor. It cannot provide player/creator or append authority.
 bool (*rawConstructorScope)(void*,const sleep_unique_bind::Event&,sleep_raw_constructor_trace::Scope&) noexcept=nullptr;
 bool privateInstallationAdmitted=false;
 bool observeOnly=true;
};
// The stable configuration must outlive all hooks. Call only after pre-hook
// full PE/function gates and exact actual private-run admission. Parent owns
// MinHook installation/removal and must disable hooks before Unconfigure.
bool Configure(const Configuration* configuration) noexcept;
void Unconfigure() noexcept;
void CommonBindHook(std::uintptr_t record,std::uint32_t nativeActor);
bool AddMtarHook(std::uintptr_t engine,std::uintptr_t bank);
#if defined(MGS_BANK_TRACE_DETACHED_TEST)
// Synthetic return-site seam for detached ABI fixtures only, absent in builds.
bool DetachedNaturalAppend(std::uintptr_t,std::uintptr_t,std::uintptr_t);
#endif
} // namespace mgscoop::sleep_unique_bind_native
