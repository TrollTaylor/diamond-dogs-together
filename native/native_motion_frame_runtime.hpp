#pragma once
#include <cstddef>
#include <cstdint>
#include "native_motion_frame_observer.hpp"
#include "mgscoop/native_pose_window.hpp"
namespace mgscoop::native_motion_frame_runtime {
using Log=void(*)(const char*) noexcept;
struct Authorization {
 bool exactRetailPe=false,offline=false,copiedSaveLab=false;
 bool(*privateWorkerCurrent)() noexcept=nullptr;
 bool(*profileCurrent)(const wchar_t*) noexcept=nullptr;
 bool(*readOriginal)(std::uintptr_t,void*,std::size_t) noexcept=nullptr;
 // Current genuine constructor/model/lifecycle census from owned core hooks.
 // Optional diagnostics only. Its `thread` is this census reader's current
 // thread, not the constructor's creator or a native job authority.
 bool(*readBirth)(partner_input_hook::NativePoseBirthCensus&) noexcept=nullptr;
};
struct Current {
 std::uint64_t serial=0;native_motion_frame_observer::Frame frame{};
 partner_input_hook::NativePoseBirthCensus birth{},afterBirth{};bool birthMatches=false,afterBirthMatches=false,birthStable=false;
 std::int32_t engineRefsBefore=0,engineRefsAfter=0;std::uint32_t rowFlagsBefore=0,rowFlagsAfter=0;bool nativeRefsBefore=false,nativeRefsAfter=false;
 // This API cannot mint either of these grants.
 bool taskRetentionProven=false,sourceSelectionPermitted=false;
};
bool Prepare(std::uintptr_t,std::size_t,const wchar_t*,const Authorization&,Log) noexcept;
bool Prepared() noexcept;
bool Status(char*,std::size_t) noexcept;
bool ReadCurrent(Current&) noexcept; // observer only, current TLS callback
bool OverlayOriginal(std::uintptr_t,void*,std::size_t) noexcept;
}
