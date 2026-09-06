#ifndef REIST_CPP_FIXED_STRING_H
#define REIST_CPP_FIXED_STRING_H
#include <reist/span.h>
#include <reist/utility.h>

namespace reist {
/** N payload bytes plus a terminator, no heap. Length counts bytes, not Unicode
 * characters. Span input may contain embedded NUL. Rejection preserves content;
 * overlap with this string is supported. Moves empty the source. */
template<size_t N> class FixedString final {
    static_assert(N < PTRDIFF_MAX);
    char bytes_[N + 1]{};
    size_t size_ = 0;
    static void copy(char* to, const char* from, size_t count) noexcept {
        if (reinterpret_cast<uintptr_t>(to) > reinterpret_cast<uintptr_t>(from)) {
            for (size_t i = count; i; --i) to[i - 1] = from[i - 1];
        } else {
            for (size_t i = 0; i < count; ++i) to[i] = from[i];
        }
    }
public:
    constexpr FixedString() noexcept = default;
    FixedString(const FixedString& other) noexcept { (void)assign(other.view()); }
    FixedString(FixedString&& other) noexcept { (void)assign(other.view()); other.clear(); }
    FixedString& operator=(const FixedString& other) noexcept {
        if (this != &other) (void)assign(other.view());
        return *this;
    }
    FixedString& operator=(FixedString&& other) noexcept {
        if (this != &other) { (void)assign(other.view()); other.clear(); }
        return *this;
    }
    [[nodiscard]] bool assign(Span<const char> input) noexcept {
        if (input.size() > N) return false;
        copy(bytes_, input.data(), input.size()); size_ = input.size(); bytes_[size_] = 0;
        return true;
    }
    [[nodiscard]] bool append(Span<const char> input) noexcept {
        if (input.size() > N - size_) return false;
        copy(bytes_ + size_, input.data(), input.size()); size_ += input.size(); bytes_[size_] = 0;
        return true;
    }
    /** Array convenience requires a trailing NUL and excludes that terminator. */
    template<size_t M> [[nodiscard]] bool assign(const char (&input)[M]) noexcept {
        Span<const char> view(input), payload;
        return input[M - 1] == 0 && view.subspan(0, M - 1, payload) && assign(payload);
    }
    template<size_t M> [[nodiscard]] bool append(const char (&input)[M]) noexcept {
        Span<const char> view(input), payload;
        return input[M - 1] == 0 && view.subspan(0, M - 1, payload) && append(payload);
    }
    void clear() noexcept { size_ = 0; bytes_[0] = 0; }
    constexpr size_t size() const noexcept { return size_; }
    static constexpr size_t capacity() noexcept { return N; }
    constexpr bool empty() const noexcept { return !size_; }
    /** Read-only borrow invalidated semantically by mutation/move/destruction. */
    const char* c_str() const & noexcept { return bytes_; }
    const char* c_str() const && = delete;
    Span<const char> view() const & noexcept {
        Span<const char> all(bytes_), payload;
        (void)all.subspan(0, size_, payload); return payload;
    }
    Span<const char> view() const && = delete;
};
}
#endif
