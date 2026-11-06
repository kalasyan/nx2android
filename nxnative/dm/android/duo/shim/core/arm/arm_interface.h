// nx2android: the runtime's guest bridge is off (no breakpoints in a native game), so this is only the type it names
#pragma once
#include "core/hle/kernel/svc_types.h"
namespace Core {
class ArmInterface { public: void SetContext(const Kernel::Svc::ThreadContext&) {} void GetContext(Kernel::Svc::ThreadContext&) const {} };
}
