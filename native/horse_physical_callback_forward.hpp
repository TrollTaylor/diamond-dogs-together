#pragma once
// Scratch hook wrapper. No registration or game calls other than ORIGINAL.
// Root takes _ReturnAddress() in its actual detour, not in this inline helper.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "horse_physical_callback_probe.hpp"
namespace mgscoop::horse_physical_callback_probe {
inline thread_local unsigned nesting=0;
// Installation must reject a null original. Recursive calls still forward
// exactly once but do not re-enter the telemetry sampler. Original LastError
// is retained, and __finally restores TLS even if the original raises SEH.
inline void ForwardOriginal(Original original,const RawWindow& raw,Probe* probe,SampleFn sample,void* context) {
    const DWORD incoming=GetLastError();const unsigned previous=nesting;
    nesting=previous+1;
    __try {
        if(probe)probe->Enter(raw);
        if(!previous&&probe)probe->Observe(raw,sample,context);
        SetLastError(incoming);
        original(reinterpret_cast<void*>(raw.self),reinterpret_cast<void*>(raw.poseContext));
    } __finally {nesting=previous;}
}
}
