// nx2android: Core::Memory::Memory for the dual-screen runtime. The game's code runs natively in this process, so a guest address is a host
// address: reads and writes check this process's mappings (/proc/self/maps, duo_host.cpp) and go through process_vm_readv / writev, which fail
// instead of faulting.
#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#include "common/common_types.h"

namespace Common { using ProcessAddress = u64; }

namespace Core::Memory {
constexpr u64 YUZU_PAGEBITS = 12;
constexpr u64 YUZU_PAGESIZE = 1ULL << YUZU_PAGEBITS;
constexpr u64 YUZU_PAGEMASK = YUZU_PAGESIZE - 1;
bool DuoMapped(u64 addr, u64 size, bool write);
bool DuoRead(u64 addr, void* dst, size_t n);
bool DuoWrite(u64 addr, const void* src, size_t n);

class Memory {
public:
    [[nodiscard]] bool IsValidVirtualAddress(u64 a) const { return DuoMapped(a, 1, false); }
    [[nodiscard]] bool IsValidVirtualAddressRange(u64 base, u64 size) const { return DuoMapped(base, size, false); }
    u8* GetPointerSilent(u64 a) { return DuoMapped(a, 1, false) ? reinterpret_cast<u8*>(static_cast<uintptr_t>(a)) : nullptr; }
    u8* GetPointer(u64 a) { return GetPointerSilent(a); }
    template <typename T> T* GetPointer(u64 a) { return reinterpret_cast<T*>(GetPointerSilent(a)); }
    u8 Read8(u64 a) { return Get<u8>(a); }
    u16 Read16(u64 a) { return Get<u16>(a); }
    u32 Read32(u64 a) { return Get<u32>(a); }
    u64 Read64(u64 a) { return Get<u64>(a); }
    void Write8(u64 a, u8 v) { DuoWrite(a, &v, 1); }
    void Write16(u64 a, u16 v) { DuoWrite(a, &v, 2); }
    void Write32(u64 a, u32 v) { DuoWrite(a, &v, 4); }
    void Write64(u64 a, u64 v) { DuoWrite(a, &v, 8); }
    std::string ReadCString(u64 a, std::size_t max_length) {
        std::string s;
        for (std::size_t i = 0; i < max_length; ++i) { char c; if (!DuoRead(a + i, &c, 1) || !c) break; s.push_back(c); }
        return s;
    }
    bool ReadBlock(u64 a, void* dst, std::size_t n) { if (DuoRead(a, dst, n)) return true; std::memset(dst, 0, n); return false; }
    bool ReadBlockUnsafe(u64 a, void* dst, std::size_t n) { return ReadBlock(a, dst, n); }
    bool WriteBlock(u64 a, const void* src, std::size_t n) { return DuoWrite(a, src, n); }
    bool WriteBlockUnsafe(u64 a, const void* src, std::size_t n) { return DuoWrite(a, src, n); }
private:
    template <typename T> T Get(u64 a) { T v{}; if (!DuoRead(a, &v, sizeof v)) v = T{}; return v; }
};
} // namespace Core::Memory
