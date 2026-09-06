#ifndef REIST_CPP_OPTIONAL_H
#define REIST_CPP_OPTIONAL_H
#include <reist/utility.h>

namespace reist {
/** In-place optional value, profile 1. No allocation, throwing access or implicit
 * conversion. Mutations are not reentrant. Moves retain the source discriminator
 * and leave its T moved-from. Assignment reconstructs T; self-assignment is inert. */
template<class T> class Optional final {
    static_assert(detail::storable<T>, "Optional requires an unqualified non-array noexcept-destructible object");
    detail::Slot<T> slot_;
    bool engaged_ = false;
public:
    constexpr Optional() noexcept = default;
    ~Optional() noexcept { reset(); }
    Optional(const Optional& other) noexcept requires detail::copyable<T> {
        if (other.engaged_) (void)try_emplace(*other.get());
    }
    Optional(Optional&& other) noexcept requires detail::movable<T> {
        if (other.engaged_) (void)try_emplace(reist::move(*other.get()));
    }
    Optional& operator=(const Optional& other) noexcept requires detail::copyable<T> {
        if (this != __builtin_addressof(other)) {
            reset(); if (other.engaged_) (void)try_emplace(*other.get());
        }
        return *this;
    }
    Optional& operator=(Optional&& other) noexcept requires detail::movable<T> {
        if (this != __builtin_addressof(other)) {
            reset(); if (other.engaged_) (void)try_emplace(reist::move(*other.get()));
        }
        return *this;
    }
    /** Construct only when empty. Failure returns null before consuming args. */
    template<class... Args> requires __is_nothrow_constructible(T, Args&&...)
    [[nodiscard]] T* try_emplace(Args&&... args) & noexcept {
        if (engaged_) return nullptr;
        T* value = ::new(static_cast<void*>(slot_.address())) T(reist::forward<Args>(args)...);
        engaged_ = true;
        return value;
    }
    /** Idempotent destruction; invalidates all borrowed pointers to this value. */
    void reset() noexcept {
        if (engaged_) { engaged_ = false; slot_.address()->~T(); }
    }
    constexpr bool has_value() const noexcept { return engaged_; }
    explicit constexpr operator bool() const noexcept { return engaged_; }
    /** Checked borrow, null when empty; never extends owner lifetime. */
    T* get() & noexcept { return engaged_ ? slot_.address() : nullptr; }
    const T* get() const & noexcept { return engaged_ ? slot_.address() : nullptr; }
    T* get() && = delete;
    const T* get() const && = delete;
};
}
#endif
