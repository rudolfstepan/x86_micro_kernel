#ifndef REIST_CPP_SPAN_H
#define REIST_CPP_SPAN_H
#include <stddef.h>
#include <stdint.h>

namespace reist {
/** Borrowed contiguous range, profile 1. Caller proves live array extent and
 * lifetime; arithmetic/alignment checks cannot validate arbitrary pointers.
 * Element mutation requires caller authority. A view never keeps storage alive. */
template<class T> class Span final {
    static_assert(__is_object(T) && !__is_array(T));
    T* data_ = nullptr;
    size_t size_ = 0;
public:
    constexpr Span() noexcept = default;
    template<class U, size_t N> requires __is_convertible(U(*)[], T(*)[])
    constexpr Span(U (&array)[N]) noexcept : data_(array), size_(N) {
        static_assert(N <= PTRDIFF_MAX / sizeof(T));
    }
    template<class U> requires __is_convertible(U(*)[], T(*)[])
    constexpr Span(Span<U> other) noexcept : data_(other.data()), size_(other.size()) {}
    /** Reject null/nonempty, misalignment, size/address overflow before changing
     * output. A non-null pointer must already designate a valid live array. */
    [[nodiscard]] static bool try_from(T* data, size_t size, Span& output) noexcept {
        uintptr_t address = reinterpret_cast<uintptr_t>(data);
        if ((!data && size) || address % alignof(T) || size > PTRDIFF_MAX / sizeof(T) ||
            address > UINTPTR_MAX - size * sizeof(T)) return false;
        output.data_ = data; output.size_ = size;
        return true;
    }
    constexpr T* data() const noexcept { return data_; }
    constexpr size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return !size_; }
    /** Null on out-of-range, never unchecked operator[]. */
    constexpr T* at(size_t index) const noexcept { return index < size_ ? data_ + index : nullptr; }
    /** Rejection preserves output; output may alias this view. */
    [[nodiscard]] constexpr bool subspan(size_t offset, size_t count, Span& output) const noexcept {
        if (offset > size_ || count > size_ - offset) return false;
        T* begin = offset ? data_ + offset : data_;
        output.data_ = begin; output.size_ = count;
        return true;
    }
};
}
#endif
