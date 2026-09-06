#ifndef REIST_CPP_RESULT_H
#define REIST_CPP_RESULT_H
#include <reist/optional.h>

namespace reist {
/** Exactly one value or error, created by a named noexcept factory. Profile 1
 * expected-like adapter, not std::expected. No invalid/default state or heap.
 * Copies require nothrow copy construction; moves preserve source discriminator.
 * Assignment reconstructs the alternative. Mutation/callback reentry is forbidden. */
template<class T, class E> class Result final {
    static_assert(detail::storable<T> && detail::storable<E>);
    union Storage {
        unsigned char empty;
        T value;
        E error;
        constexpr Storage() noexcept : empty(0) {}
        ~Storage() noexcept {}
    } storage_;
    bool ok_;
    struct Success {};
    struct Failure {};
    template<class... Args> explicit Result(Success, Args&&... args) noexcept : ok_(true) {
        ::new(static_cast<void*>(__builtin_addressof(storage_.value))) T(reist::forward<Args>(args)...);
    }
    template<class... Args> explicit Result(Failure, Args&&... args) noexcept : ok_(false) {
        ::new(static_cast<void*>(__builtin_addressof(storage_.error))) E(reist::forward<Args>(args)...);
    }
    void destroy() noexcept {
        if (ok_) storage_.value.~T(); else storage_.error.~E();
    }
    void copy_from(const Result& other) noexcept {
        if (ok_) ::new(static_cast<void*>(__builtin_addressof(storage_.value))) T(other.storage_.value);
        else ::new(static_cast<void*>(__builtin_addressof(storage_.error))) E(other.storage_.error);
    }
    void move_from(Result& other) noexcept {
        if (ok_) ::new(static_cast<void*>(__builtin_addressof(storage_.value))) T(reist::move(other.storage_.value));
        else ::new(static_cast<void*>(__builtin_addressof(storage_.error))) E(reist::move(other.storage_.error));
    }
public:
    template<class... Args> requires __is_nothrow_constructible(T, Args&&...)
    [[nodiscard]] static Result success(Args&&... args) noexcept {
        return Result(Success{}, reist::forward<Args>(args)...);
    }
    template<class... Args> requires __is_nothrow_constructible(E, Args&&...)
    [[nodiscard]] static Result failure(Args&&... args) noexcept {
        return Result(Failure{}, reist::forward<Args>(args)...);
    }
    ~Result() noexcept { destroy(); }
    Result(const Result& other) noexcept requires (detail::copyable<T> && detail::copyable<E>) : ok_(other.ok_) {
        copy_from(other);
    }
    Result(Result&& other) noexcept requires (detail::movable<T> && detail::movable<E>) : ok_(other.ok_) {
        move_from(other);
    }
    Result& operator=(const Result& other) noexcept requires (detail::copyable<T> && detail::copyable<E>) {
        if (this != &other) { destroy(); ok_ = other.ok_; copy_from(other); }
        return *this;
    }
    Result& operator=(Result&& other) noexcept requires (detail::movable<T> && detail::movable<E>) {
        if (this != &other) { destroy(); ok_ = other.ok_; move_from(other); }
        return *this;
    }
    constexpr bool has_value() const noexcept { return ok_; }
    explicit constexpr operator bool() const noexcept { return ok_; }
    /** Checked borrowed access. Null for the inactive alternative; invalidated
     * by assignment/destruction. Borrowing from a temporary owner is forbidden. */
    T* value_if() & noexcept { return ok_ ? __builtin_addressof(storage_.value) : nullptr; }
    const T* value_if() const & noexcept { return ok_ ? __builtin_addressof(storage_.value) : nullptr; }
    E* error_if() & noexcept { return ok_ ? nullptr : __builtin_addressof(storage_.error); }
    const E* error_if() const & noexcept { return ok_ ? nullptr : __builtin_addressof(storage_.error); }
    T* value_if() && = delete;
    const T* value_if() const && = delete;
    E* error_if() && = delete;
    const E* error_if() const && = delete;
};

/** A successful operation without a payload; only the error has object lifetime. */
template<class E> class Result<void, E> final {
    Optional<E> error_;
    struct Success {};
    struct Failure {};
    explicit Result(Success) noexcept {}
    template<class... Args> explicit Result(Failure, Args&&... args) noexcept {
        (void)error_.try_emplace(reist::forward<Args>(args)...);
    }
public:
    [[nodiscard]] static Result success() noexcept { return Result(Success{}); }
    template<class... Args> requires __is_nothrow_constructible(E, Args&&...)
    [[nodiscard]] static Result failure(Args&&... args) noexcept {
        return Result(Failure{}, reist::forward<Args>(args)...);
    }
    Result(const Result&) noexcept = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;
    bool has_value() const noexcept { return !error_; }
    explicit operator bool() const noexcept { return !error_; }
    E* error_if() & noexcept { return error_.get(); }
    const E* error_if() const & noexcept { return error_.get(); }
    E* error_if() && = delete;
    const E* error_if() const && = delete;
};
}
#endif
