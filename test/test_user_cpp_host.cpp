/* Actual runtime + actual C allocator. CRT allocation symbols are renamed in
 * those two objects only, so the host loader keeps its own allocation domain. */
#include <new>
#include <stdint.h>
#include <stdio.h>
#include <reist/libc.h>
#include <reist/cpp.h>
#include <x86os.h>

extern "C" void _Exit(int) __attribute__((noreturn));
extern "C" void x86os_exit(int status) { _Exit(status); }
extern "C" void reist_libc_fail(unsigned) { _Exit(70); }
extern "C" void __cxa_pure_virtual();
extern "C" void __cxa_deleted_virtual();

#define CHECK(x) do { if (!(x)) { printf("CPP_HOST_FAIL line=%d\n", __LINE__); return 1; } } while (0)
alignas(max_align_t) static unsigned char storage[1024 * 1024];
static bool acquired;
static unsigned releases, constructions, destructions;
static void *acquire(void*, size_t size) {
    if (acquired || size > sizeof(storage)) return nullptr;
    acquired = true; return storage;
}
static void release(void*, void *p, size_t) {
    if (!acquired || p != storage) _Exit(69);
    acquired = false; ++releases;
}
struct Object {
    int value;
    Object() noexcept : value(42) { ++constructions; }
    ~Object() noexcept { ++destructions; }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&& other) noexcept : value(other.value) {
        other.value = 0; ++constructions;
    }
};
struct alignas(4096) Aligned : Object {};

int main(int argc, char **argv) {
    /* No hidden heap initialization and no constructors before main. */
    CHECK(constructions == 0 && !acquired);
    CHECK(::operator new(4, std::nothrow) == nullptr);
    reist_libc_backing_t backing{REIST_LIBC_BACKING_VERSION, sizeof(backing),
        sizeof(storage), sizeof(storage), nullptr, acquire, release};
    CHECK(reist_libc_init_backing(&backing) == 0);
    if (argc == 2) {
        /* Normal process exit, not a host fault / Windows crash dialog. */
        if (argv[1][0] == 'p') __cxa_pure_virtual();
        if (argv[1][0] == 'd') __cxa_deleted_virtual();
        void *p = ::operator new(SIZE_MAX);
        (void)p; return 2;
    }
    {
        Object automatic;
        Object moved(static_cast<Object&&>(automatic));
        CHECK(moved.value == 42 && automatic.value == 0);
    }
    alignas(Object) unsigned char placement[sizeof(Object)];
    Object *placed = new(placement) Object;
    CHECK(placed->value == 42 && !acquired);
    placed->~Object();
    Object *single = new(std::nothrow) Object;
    Object *array = new(std::nothrow) Object[7];
    CHECK(single && array && array[6].value == 42);
    CHECK(::operator new(SIZE_MAX, std::nothrow) == nullptr);
    CHECK(::operator new(SIZE_MAX, std::align_val_t(4096), std::nothrow) == nullptr);
    volatile size_t invalid_alignment = 3;
    CHECK(::operator new(16, std::align_val_t(invalid_alignment), std::nothrow) == nullptr);
    invalid_alignment = 0;
    CHECK(::operator new(16, std::align_val_t(invalid_alignment), std::nothrow) == nullptr);
    volatile size_t huge = SIZE_MAX / sizeof(Object) + 1;
    CHECK(new(std::nothrow) Object[huge] == nullptr);
    CHECK(single->value == 42 && array[6].value == 42);
    CHECK(reist_libc_reset() != 0);
    delete single; delete[] array;
    CHECK(!acquired && releases == 1);
    Aligned *aligned = new Aligned[2];
    CHECK(reinterpret_cast<uintptr_t>(aligned) % 4096 == 0);
    CHECK(aligned[1].value == 42);
    delete[] aligned;
    void *zero = ::operator new(0, std::nothrow);
    CHECK(zero != nullptr);
    ::operator delete(zero, size_t(0));
    void *aligned_zero = ::operator new[](0, std::align_val_t(4096), std::nothrow);
    CHECK(aligned_zero && reinterpret_cast<uintptr_t>(aligned_zero) % 4096 == 0);
    ::operator delete[](aligned_zero, size_t(0), std::align_val_t(4096));
    ::operator delete(nullptr);
    ::operator delete[](nullptr);
    ::operator delete(nullptr, std::align_val_t(4096));
    CHECK(!acquired && constructions == destructions);
    reist_libc_stats_t stats{REIST_LIBC_VERSION, sizeof(stats), 0, 0, 0, 0};
    CHECK(reist_libc_stats(&stats) == 0 && stats.capacity == 0 && stats.live_objects == 0);
    CHECK(reist_libc_reset() == 0);
    puts("REIST_CPP_HOST_OK");
    return 0;
}
