#ifndef REIST_BROWSER_RESPONSE_HPP
#define REIST_BROWSER_RESPONSE_HPP
#include "browser_response.h"
#include <reist/result.h>

namespace reist::browser {
/** Diagnostic snapshot only: preserves the legacy C error output, including
 * HTTP status. Offsets/redirect/encoding on failure grant no admission. */
struct ResponseError final {
    int code;
    browser_response_t diagnostic;
    ResponseError(int error,const browser_response_t& partial) noexcept
        : code(error), diagnostic(partial) {}
};

/** Fully admitted metadata, never an owner of input, files or transport.
 * Borrowed bytes/URL must remain valid and immutable during open; neither
 * pointer is retained. Offsets refer only to that caller-owned response and
 * must not be used after its buffer/navigation is invalidated. The existing
 * URL resolver and fixed private parser scratch require serialized calls, as
 * already required by the C resolver. Returned metadata is an independent
 * value, never a view into scratch. Scratch has constant initialization and
 * process lifetime, no heap budget or destructor/exit registration.
 * No allocation, destructor cleanup, new wire layout or protocol policy. */
class ValidatedResponse final {
    class Key {
        Key() noexcept = default;
        friend class ValidatedResponse;
    };
    browser_response_t metadata_;
    int decision_;
public:
    // Public only for Result's in-place construction; Key cannot be forged
    // using ordinary construction or aggregate initialization by callers.
    ValidatedResponse(Key,const browser_response_t& metadata,int decision) noexcept
        : metadata_(metadata), decision_(decision) {}
    [[nodiscard]] static Result<ValidatedResponse,ResponseError> open(
        const uint8_t* bytes,size_t length,const char* url,uint32_t kind,
        bool document=false) noexcept;
    const browser_response_t& metadata() const & noexcept { return metadata_; }
    const browser_response_t& metadata() const && = delete;
    int decision() const noexcept { return decision_; } // 0 content, 1 redirect
};
}
#endif
