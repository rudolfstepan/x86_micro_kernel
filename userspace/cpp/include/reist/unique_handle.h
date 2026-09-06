#ifndef REIST_CPP_UNIQUE_HANDLE_H
#define REIST_CPP_UNIQUE_HANDLE_H
#include <reist/utility.h>

namespace reist {
template<class T> struct HandleTraits; // No default authority or raw-handle policy.

/** Explicit unique owner of an already acquired C handle. T is a trivial handle
 * value, not a resource object. Traits supply invalid/is_valid/equal/close with
 * noexcept bounded behavior; close consumes cleanup responsibility, including
 * its explicitly defined error/fencing path. No destructor retry/recovery loop.
 * Traits must preserve existing generations and cannot create new authority.
 * No callbacks may reenter this owner; duplicate adoption remains caller error. */
template<class T, class Traits = HandleTraits<T>> class UniqueHandle final {
    static_assert(detail::storable<T> && __is_trivially_copyable(T) &&
                  detail::copyable<T> && __is_nothrow_assignable(T&, const T&));
    static_assert(noexcept(Traits::invalid()) && noexcept(Traits::is_valid(Traits::invalid())) &&
                  noexcept(Traits::equal(Traits::invalid(), Traits::invalid())) &&
                  noexcept(Traits::close(Traits::invalid())));
    static_assert(__is_same(decltype(Traits::invalid()), T) &&
                  __is_same(decltype(Traits::is_valid(Traits::invalid())), bool) &&
                  __is_same(decltype(Traits::equal(Traits::invalid(), Traits::invalid())), bool) &&
                  __is_same(decltype(Traits::close(Traits::invalid())), void));
    T handle_ = Traits::invalid();
public:
    UniqueHandle() noexcept = default;
    /** Adopt existing sole ownership, not a borrowed/duplicate handle. */
    explicit UniqueHandle(T owned) noexcept : handle_(owned) {}
    ~UniqueHandle() noexcept { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    explicit operator bool() const noexcept { return Traits::is_valid(handle_); }
    /** Borrowed raw value, no ownership/lifetime extension. */
    T get() const & noexcept { return handle_; }
    T get() const && = delete;
    /** Transfer cleanup responsibility to the caller; owner becomes empty. */
    [[nodiscard]] T release() noexcept {
        T old = handle_; handle_ = Traits::invalid(); return old;
    }
    /** Replace owned handle; equal full identity is a no-op. Release old state
     * before callback; the callback consumes responsibility even on failure. */
    void reset(T owned = Traits::invalid()) noexcept {
        if (Traits::equal(handle_, owned)) return;
        T old = release();
        if (Traits::is_valid(old)) Traits::close(old);
        handle_ = owned;
    }
};
}
#endif
