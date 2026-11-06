// nx2android: Common::DynamicLibrary over dlopen
#pragma once
#include <string>
#include <dlfcn.h>
namespace Common {
class DynamicLibrary {
public:
    DynamicLibrary() = default;
    explicit DynamicLibrary(const char* filename) { Open(filename); }
    explicit DynamicLibrary(void* h) : handle{h} {}
    ~DynamicLibrary() { Close(); }
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& o) noexcept : handle{o.handle} { o.handle = nullptr; }
    DynamicLibrary& operator=(DynamicLibrary&& o) noexcept { if (this != &o) { Close(); handle = o.handle; o.handle = nullptr; } return *this; }
    static std::string GetUnprefixedFilename(const char* f) { return std::string(f) + ".so"; }
    static std::string GetVersionedFilename(const char* f, int = -1, int = -1) { return std::string("lib") + f + ".so"; }
    bool IsOpen() const { return handle != nullptr; }
    bool Open(const char* filename) { handle = dlopen(filename, RTLD_NOW | RTLD_LOCAL); return handle != nullptr; }
    void Close() { if (handle) dlclose(handle); handle = nullptr; }
    void* GetSymbolAddress(const char* name) const { return handle ? dlsym(handle, name) : nullptr; }
    template <typename T> bool GetSymbol(const char* name, T* ptr) const { *ptr = reinterpret_cast<T>(GetSymbolAddress(name)); return *ptr != nullptr; }
private:
    void* handle = nullptr;
};
}
