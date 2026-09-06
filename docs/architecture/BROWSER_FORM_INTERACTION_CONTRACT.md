# Static browser form interaction (R3.13)

Status: accepted on 2026-09-06; all frozen host, reference-build and real-input
QEMU gates passed. Evidence is recorded in `../development/CURRENT_WORK.md`.
This is a bounded Ring-3 adapter, not DOM or full HTML compatibility.

## References and admission

Use [WHATWG HTML forms](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html)
for ownership, successful controls, reset and explicit submission, and the
[WHATWG URL serializer](https://url.spec.whatwg.org/#application/x-www-form-urlencoded)
for UTF-8 GET queries. Preserve tree order, repeated names, checked controls,
selected enabled options and only the activated submitter. Normalize line
breaks to CRLF at serialization, spaces to `+`; replace the action query,
preserving its fragment. Hidden `_charset_` submits `UTF-8`.

Hubbub supplies the retained tree and parser form association. Explicit `form`
uses the first matching element ID, which must identify a form. A disabled
fieldset exempts its first legend subtree. Values are Unicode scalar strings;
text/search removes line breaks, textarea retains LF. Reset restores defaults.

This bounded subset supports text/search, hidden, checkbox/radio, textarea,
select/options, submit/reset and labels. Unsupported input types, methods,
encoding/target overrides, validation constraints, base elements affecting form
resolution and credential/file-bearing submissions fail closed with a visible
message before transport. No POST-to-GET fallback. Only HTTP(S) actions without
credentials are admitted. Existing URL capacity is 256 bytes including NUL;
overflow rejects the entire candidate, never a truncated request.

## Ownership, memory and interaction

At most 16 forms, 256 controls (including labels), 512 options, 128 KiB immutable
strings and 128 KiB mutable values, allocated in private Ring 3. The private
worker envelope carries indices and offsets, no pointers. Exact worker identity
and generation, envelope sizes, counts, flags, UTF-8, relationships and geometry
must validate before publication. A failed candidate retains the old document.

Form state belongs to one accepted navigation generation. Reflow can reuse it
only with an identical immutable model; replacement, including identical-content
reload, resets it. Capture never survives reflow or navigation. Editing uses no
worker, parser, network or allocation. Capacity failures preserve previous data.

The existing native value controller accepts only ASCII and 64 bytes; reusing
that storage would violate this package's Unicode and memory contract. A private
browser adapter therefore implements scalar cursor movement and bounded value
storage, using the browser's existing native Surface painting, bevel and font
primitives. No shared UI or public Surface ABI changes. Clipboard, grapheme/IME
editing, DOM events, script, cookies and uploads are not implied.

The browser links the existing opt-in ISO C byte/string runtime and headers;
there is no second browser-private memcpy/memset/memcmp implementation. This
does not select a libc heap: browser allocations still use the existing
private process allocator and STB still uses its fixed decoder arena.

## Acceptance

Real-code host tests exercise projection, malformed envelopes, ownership,
encoding, defaults, exhaustion and stale/reflow state. QEMU must receive actual
pointer/keyboard input and a controlled HTTP fixture must observe the exact
accepted GET and no requests for rejected submissions. Existing input, browser
recovery and resource probes remain frozen. No runtime claim from source tests.

## R3.15 candidate: native maxlength

The 2026-09-06 Google screenshot and retained response identify the normal
`maxlength=2048` search attribute as the local rejection cause. Implement it
for text/search and textarea rather than treating the form as unsupported.
Reference: [WHATWG maxlength](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#limiting-user-input-length)
and [non-negative integer parsing](https://html.spec.whatwg.org/multipage/common-microsyntaxes.html#rules-for-parsing-non-negative-integers).
Measure UTF-16 code units, not UTF-8 bytes; textarea counts normalized LF.
Missing/invalid limits are absent. Zero is valid. Numerical limits at or above
the existing 128-KiB value capacity saturate there without overflow; that private
byte bound remains stronger. Author defaults are not truncated. User insertion
cannot exceed the limit; deletion remains possible. A dirty, still-overlong
editable value blocks submission; reset restores the default and clears dirty
state, and same-generation reflow preserves it. Readonly/disabled controls
retain their constraint-validation exemptions.

Private form version 2 appends one `max_length_plus_one` word per live control
after the existing strings in the compact wire: zero means absent, otherwise
subtract one for the unit limit. Version 1 remains explicitly decodable with
no limit extension. Old control fields and scene envelope versions do not change.
Validate version, exact length, applicable control kind and limit bounds before
publication. Private editing state caches unit counts and dirty flags without
network, allocation or parser work per key. No Google-specific field rewriting,
JavaScript, POST, cookie or credential authority is added. Other unsupported
constraints remain rejected. Original transport/worker deadlines stay frozen.

Acceptance requires real projection and legacy/new wire tests, Unicode and
boundary edit/reset/reflow cases, then all five browser guest gates. The native
forms gate additionally proves a maximum-length field, refused extra typing
without value loss and the exact successful GET. This is not a claim that
Google's live results pages or every form work.
