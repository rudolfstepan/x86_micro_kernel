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
