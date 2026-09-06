#ifndef REIST_CPP_FIXED_VECTOR_H
#define REIST_CPP_FIXED_VECTOR_H
#include <reist/utility.h>

namespace reist {
/** Inline bounded indexed sequence; inactive slots contain no T. Profile 1
 * exposes checked at(), not a contiguous-array/data()/iterator promise across
 * individually constructed union slots. Element operations must be bounded,
 * noexcept and non-reentrant. A successful move empties the source. */
template<class T, size_t N> class FixedVector final {
    static_assert(detail::storable<T>);
    static_assert(N <= PTRDIFF_MAX / sizeof(detail::Slot<T>));
    detail::Slot<T> slots_[N ? N : 1];
    size_t size_ = 0;
public:
    constexpr FixedVector() noexcept = default;
    ~FixedVector() noexcept { clear(); }
    FixedVector(const FixedVector& other) noexcept requires detail::copyable<T> {
        for (size_t i = 0; i < other.size_; ++i) (void)try_emplace_back(*other.at(i));
    }
    FixedVector(FixedVector&& other) noexcept requires detail::movable<T> {
        for (size_t i = 0; i < other.size_; ++i) (void)try_emplace_back(reist::move(*other.at(i)));
        other.clear();
    }
    FixedVector& operator=(const FixedVector& other) noexcept requires detail::copyable<T> {
        if (this != &other) {
            clear(); for (size_t i = 0; i < other.size_; ++i) (void)try_emplace_back(*other.at(i));
        }
        return *this;
    }
    FixedVector& operator=(FixedVector&& other) noexcept requires detail::movable<T> {
        if (this != &other) {
            clear(); for (size_t i = 0; i < other.size_; ++i) (void)try_emplace_back(reist::move(*other.at(i)));
            other.clear();
        }
        return *this;
    }
    /** Full capacity returns null before constructing or moving from args. */
    template<class... Args> requires __is_nothrow_constructible(T, Args&&...)
    [[nodiscard]] T* try_emplace_back(Args&&... args) & noexcept {
        if (size_ == N) return nullptr;
        T* value = ::new(static_cast<void*>(slots_[size_].address())) T(reist::forward<Args>(args)...);
        ++size_; return value;
    }
    [[nodiscard]] bool pop_back() noexcept {
        if (!size_) return false;
        slots_[--size_].address()->~T(); return true;
    }
    /** Reverse-order destruction, bounded by live size. */
    void clear() noexcept { while (size_) (void)pop_back(); }
    constexpr size_t size() const noexcept { return size_; }
    static constexpr size_t capacity() noexcept { return N; }
    constexpr bool empty() const noexcept { return !size_; }
    /** Borrow is invalidated by removal/clear, assignment, move or destruction. */
    T* at(size_t i) & noexcept { return i < size_ ? slots_[i].address() : nullptr; }
    const T* at(size_t i) const & noexcept { return i < size_ ? slots_[i].address() : nullptr; }
    T* at(size_t) && = delete;
    const T* at(size_t) const && = delete;
};
}
#endif
