#pragma once
#include "horse_physical_callback_probe.hpp"
#include "horse_physical_natural_read.hpp"
namespace mgscoop::horse_physical_probe_glue {
// Coordinator supplies a pure current-body sampler. No owned cargo backend
// or matrix writer is linked to this diagnostic-only registration API.
bool Install(const void* image,std::size_t span,bool privateAdmission,
    horse_physical_callback_probe::SampleFn sampler,void* context) noexcept;
bool Line(char* out,std::size_t capacity) noexcept;
bool RawLine(char* out,std::size_t capacity) noexcept;
void RefreshCandidates(const horse_player_physical_read::Observation& current,std::uint32_t ownerThread,bool ready) noexcept;
// Disables entry first. Return false while an existing original call is active;
// keep this module and its sampler context resident until a later true result.
void PublishNatural(const horse_physical_natural_read::Published&) noexcept;
void ClearNatural() noexcept;
bool NaturalLine(unsigned body,bool life,char*,std::size_t) noexcept;
bool RemoveIfQuiescent() noexcept;
}
