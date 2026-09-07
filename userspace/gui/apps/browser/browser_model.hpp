#ifndef REIST_BROWSER_MODEL_HPP
#define REIST_BROWSER_MODEL_HPP
#include "browser_model.h"
#include <reist/result.h>

namespace reist::browser {
/** Private ISO C++20 profile-1 adapters, never wire/SDK objects or owners.
 * AddressEdit borrows the caller's mutable buffer and state for ONE serialized
 * edit. Their actual storage/lifetime/nonaliasing remain caller obligations;
 * external mutation, reset, navigation or release invalidates this borrow.
 * No allocation, payload copy, retained navigation authority or destructor.
 * Factories preserve the existing -22 convention before any mutation. */
class AddressEdit final {
    class Key { Key() noexcept = default; friend class AddressEdit; };
    char* text_;
    uint32_t capacity_;
    uint32_t *length_, *cursor_, *replace_;
public:
    AddressEdit(Key, char* text, uint32_t capacity, uint32_t* length,
                uint32_t* cursor, uint32_t* replace) noexcept
        : text_(text), capacity_(capacity), length_(length), cursor_(cursor), replace_(replace) {}
    [[nodiscard]] static Result<AddressEdit,int> open(char* text, uint32_t capacity,
        uint32_t* length, uint32_t* cursor, uint32_t* replace) noexcept {
        if (!text || !length || !cursor || !replace || capacity < 2U ||
            *length >= capacity || *cursor > *length) return Result<AddressEdit,int>::failure(-22);
        return Result<AddressEdit,int>::success(Key{},text,capacity,length,cursor,replace);
    }
    // This one-shot hot adapter must not materialize a view/call frame between
    // the C entrypoint and the existing edit loop (Clang profile 1).
    [[gnu::always_inline]] int edit(uint32_t key) & noexcept;
};

/** Checked byte range within an already admitted document extent. No pointer
 * or payload is copied. Unicode decoding/wrapping remains the existing code. */
class TextRange final {
    class Key { Key() noexcept = default; friend class TextRange; };
    uint32_t offset_, length_;
public:
    TextRange(Key, uint32_t offset, uint32_t length) noexcept : offset_(offset), length_(length) {}
    [[nodiscard]] static Result<TextRange,int> open(uint32_t offset, uint32_t length,
                                                  uint32_t extent) noexcept {
        if (offset > extent || length > extent-offset) return Result<TextRange,int>::failure(-22);
        return Result<TextRange,int>::success(Key{},offset,length);
    }
    uint32_t offset() const noexcept { return offset_; }
    uint32_t length() const noexcept { return length_; }
};

/** Infallible normalization with exactly the existing viewport/document caps.
 * Encapsulates view >=1, maximum <=262143 and position <=maximum together. */
class ScrollExtent final {
    uint32_t view_, maximum_, position_;
    ScrollExtent(uint32_t view, uint32_t maximum, uint32_t position) noexcept
        : view_(view), maximum_(maximum), position_(position) {}
public:
    static ScrollExtent clamp(uint32_t view, uint32_t total, uint32_t position) noexcept {
        if (view > 768U) view = 768U;
        if (view == 0U) view = 1U;
        if (total > 262144U) total = 262144U;
        uint32_t maximum = total > view ? total-view : 0U;
        return ScrollExtent(view,maximum,position > maximum ? maximum : position);
    }
    uint32_t view() const noexcept { return view_; }
    uint32_t maximum() const noexcept { return maximum_; }
    uint32_t position() const noexcept { return position_; }
};
static_assert(sizeof(Result<AddressEdit,int>) <= 64);
static_assert(sizeof(Result<TextRange,int>) <= 64);
static_assert(sizeof(ScrollExtent) <= 64);
static_assert(sizeof(browser_layout_run_t) == 36);
static_assert(sizeof(browser_layout_t) == 73736);
static_assert(sizeof(browser_image_slot_t) == 262164);
}
#endif
