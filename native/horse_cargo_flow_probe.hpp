#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::horse_cargo_flow_probe {
// DEFAULT OFF. Private copied-save passive registration only, no new detour,
// native dispatch, prototype loader, input mutation or writer purpose.
bool Prepare(std::uintptr_t image,std::size_t span) noexcept;
bool Line(char* output,std::size_t capacity) noexcept;
}
