#ifndef REIST_BROWSER_RESOURCES_HPP
#define REIST_BROWSER_RESOURCES_HPP
#include "browser_resources.h"
#include <reist/result.h>
#include <reist/span.h>

namespace reist::browser {
/** Validated, immutable BORROW of an existing caller-owned resource snapshot.
 * ISO C++20 profile-1 value/view adapter, not a resource owner or wire object.
 * The entire bundle must remain live and unchanged while any derived borrow
 * is used. Mutation, init/reset, navigation and workspace release invalidate
 * every view; open must validate again afterwards. A generation check cannot
 * detect a dangling pointer or grant authority. No reentry/thread guarantee:
 * the existing URL canonicalizer uses serialized process-private scratch.
 * Input document URL is borrowed during open only, never retained.
 * No heap, payload copy, lifetime extension or destructor/close operation. */
class ValidatedResources final {
    class Key { Key() noexcept = default; friend class ValidatedResources; };
    const browser_resources_t* bundle_;
public:
    // Result can construct in-place, but ordinary callers cannot create Key.
    ValidatedResources(Key,const browser_resources_t* bundle) noexcept : bundle_(bundle) {}
    [[nodiscard]] static Result<ValidatedResources,int> open(
        const browser_resources_t*,const char* document,uint32_t generation) noexcept;
    const browser_resources_t& snapshot() const & noexcept { return *bundle_; }
    const browser_resources_t& snapshot() const && = delete;
    uint32_t generation() const noexcept { return bundle_->generation; }
    const browser_resource_t* entry(uint32_t index) const & noexcept {
        return index<bundle_->count ? &bundle_->entries[index] : nullptr;
    }
    const browser_resource_t* entry(uint32_t) const && = delete;
    [[nodiscard]] Result<Span<const uint8_t>,int> bytes(uint32_t index) const & noexcept {
        using Slice=Result<Span<const uint8_t>,int>;
        const auto* resource=entry(index);
        if(!resource || !resource->ready) return Slice::failure(-22);
        Span<const uint8_t> pool(bundle_->bytes), result;
        if(!pool.subspan(resource->offset,resource->length,result)) return Slice::failure(-22);
        return Slice::success(result);
    }
    Result<Span<const uint8_t>,int> bytes(uint32_t) const && = delete;
};
static_assert(sizeof(Result<ValidatedResources,int>)<=64);
}
#endif
