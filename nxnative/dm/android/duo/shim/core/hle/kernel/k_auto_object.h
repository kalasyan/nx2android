// nx2android: a non-owning stand-in for Kernel::KScopedAutoObject (the port has one process, alive as long as the companion)
#pragma once
namespace Kernel {
class KernelCore;
template <typename T>
class KScopedAutoObject {
public:
    KScopedAutoObject() = default;
    explicit KScopedAutoObject(KernelCore&) {}
    KScopedAutoObject(T* o) : obj{o} {}
    T* GetPointerUnsafe() const { return obj; }
    void SetObject(T* o) { obj = o; }
    bool IsNull() const { return obj == nullptr; }
    bool IsNotNull() const { return obj != nullptr; }
    T* operator->() const { return obj; }
private:
    T* obj = nullptr;
};
} // namespace Kernel
