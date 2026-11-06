// nx2android: the game's process: its memory is this process's (core/memory.h)
#pragma once
#include "core/hle/kernel/k_auto_object.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"
inline u64 GetInteger(u64 v) { return v; }
namespace Kernel {
/// the guest heap (svc_heap_range in hle_svc.c, through duo_host.cpp)
class KPageTable {
public:
    u64 GetHeapRegionStart() const;
    u64 GetHeapRegionSize() const;
    u64 GetNormalMemorySize() const;
};
class KProcess {
public:
    KPageTable& GetPageTable() { return page_table; }
    Core::Memory::Memory& GetMemory() { return memory; }
    u64 GetProcessId() const { return 0x51; }
    u64 GetProgramId() const;
private:
    Core::Memory::Memory memory;
    KPageTable page_table;
};
class KThread {
public:
    Svc::ThreadContext& GetContext() { return context; }
    KProcess* GetOwnerProcess() const { return nullptr; }
    u64 GetThreadId() const { return 0; }
private:
    Svc::ThreadContext context;
};
class KernelCore {
public:
    KScopedAutoObject<KProcess> RetainApplicationProcess(u64* epoch = nullptr) { if (epoch) *epoch = 1; return KScopedAutoObject<KProcess>{&process}; }
    bool IsApplicationProcess(const KProcess* p, u64) const { return p == &process; }
    KProcess* ApplicationProcess() { return &process; }
private:
    KProcess process;
};
} // namespace Kernel
