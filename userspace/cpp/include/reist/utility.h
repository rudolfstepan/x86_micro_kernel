#ifndef REIST_CPP_UTILITY_H
#define REIST_CPP_UTILITY_H
#include <stddef.h>
#include <stdint.h>
#include <new>

namespace reist {
namespace detail {
template<class T> struct RemoveReference { using type = T; };
template<class T> struct RemoveReference<T&> { using type = T; };
template<class T> struct RemoveReference<T&&> { using type = T; };
template<class T> inline constexpr bool storable = __is_object(T) &&
    !__is_array(T) && !__is_const(T) && !__is_volatile(T) && __is_nothrow_destructible(T);
template<class T> inline constexpr bool copyable = __is_nothrow_constructible(T, const T&);
template<class T> inline constexpr bool movable = __is_nothrow_constructible(T, T&&);

/* One individually aligned object slot; no T exists until placement construction. */
template<class T> union Slot {
    unsigned char empty;
    T value;
    constexpr Slot() noexcept : empty(0) {}
    ~Slot() noexcept {}
    T* address() noexcept { return __builtin_addressof(value); }
    const T* address() const noexcept { return __builtin_addressof(value); }
};
}

/** Explicit value-category transfer; does not itself transfer any authority. */
template<class T> constexpr typename detail::RemoveReference<T>::type&& move(T&& value) noexcept {
    return static_cast<typename detail::RemoveReference<T>::type&&>(value);
}
template<class T> constexpr T&& forward(typename detail::RemoveReference<T>::type& value) noexcept {
    return static_cast<T&&>(value);
}
template<class T> constexpr T&& forward(typename detail::RemoveReference<T>::type&& value) noexcept {
    static_assert(!__is_lvalue_reference(T));
    return static_cast<T&&>(value);
}
}
#endif
