#include <new>
#include <stdint.h>
#include <stdlib.h>
#include <reist/cpp.h>
#include <x86os.h>

namespace std { constinit const nothrow_t nothrow{}; }

namespace {
[[noreturn]] void allocation_failure() noexcept {
    x86os_exit(REIST_CPP_ALLOCATION_FAILURE);
}

void *allocate(size_t size) noexcept {
    static_assert(alignof(max_align_t) >= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
    return malloc(size ? size : 1);
}

void *allocate_aligned(size_t size, size_t alignment) noexcept {
    /* Prefix ownership belongs to this allocation only; no shared registry. */
    if (!alignment || (alignment & (alignment - 1))) return nullptr;
    if (alignment < alignof(void*)) alignment = alignof(void*);
    if (!size) size = 1;
    const size_t extra = alignment - 1 + sizeof(void*);
    if (extra < alignment || size > SIZE_MAX - extra) return nullptr;
    void *raw = malloc(size + extra);
    if (!raw) return nullptr;
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(raw) + sizeof(void*) +
                         alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    void *result = reinterpret_cast<void*>(aligned);
    static_cast<void**>(result)[-1] = raw;
    return result;
}

void release_aligned(void *p) noexcept {
    if (p) free(static_cast<void**>(p)[-1]);
}
}

void *operator new(size_t size) {
    void *p = allocate(size);
    if (!p) allocation_failure();
    return p;
}
void *operator new[](size_t size) { return ::operator new(size); }
void *operator new(size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void *operator new[](size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }
void operator delete(void *p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void *p, const std::nothrow_t&) noexcept { free(p); }

void *operator new(size_t size, std::align_val_t alignment) {
    void *p = allocate_aligned(size, static_cast<size_t>(alignment));
    if (!p) allocation_failure();
    return p;
}
void *operator new[](size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void *operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate_aligned(size, static_cast<size_t>(alignment));
}
void *operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate_aligned(size, static_cast<size_t>(alignment));
}
void operator delete(void *p, std::align_val_t) noexcept { release_aligned(p); }
void operator delete[](void *p, std::align_val_t) noexcept { release_aligned(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { release_aligned(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { release_aligned(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t&) noexcept { release_aligned(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t&) noexcept { release_aligned(p); }

extern "C" [[noreturn]] void __cxa_pure_virtual() {
    x86os_exit(REIST_CPP_INVALID_VIRTUAL_CALL);
}
extern "C" [[noreturn]] void __cxa_deleted_virtual() {
    x86os_exit(REIST_CPP_INVALID_VIRTUAL_CALL);
}
