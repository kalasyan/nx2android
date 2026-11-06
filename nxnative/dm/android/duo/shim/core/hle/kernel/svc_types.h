// nx2android: the thread context type the runtime's guest bridge saves (the bridge is off here: no guest calls)
#pragma once
#include <array>
#include "common/common_types.h"
namespace Kernel::Svc {
struct ThreadContext { std::array<u64, 29> r{}; u64 fp{}, lr{}, sp{}, pc{}; u32 pstate{}; u32 padding{}; std::array<u128, 32> v{}; u32 fpcr{}, fpsr{}; u64 tpidr{}; };
}
