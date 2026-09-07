# Browser scripting profile 1 — R3.25

Frozen 7 September 2026 on4b2b3302, before implementation. User priority:
JavaScript in the actual browser as quickly as possible, not another unused
runtime foundation. One document-publication/DOM-mutation vertical slice.

## Standard and deliberate subset

References: [WHATWG script processing](https://html.spec.whatwg.org/multipage/scripting.html#the-script-element),
[DOM textContent](https://dom.spec.whatwg.org/#dom-node-textcontent),
[HTML document.title](https://html.spec.whatwg.org/multipage/dom.html#document.title).
Use the pinned Hubbub script callback at the parser's actual script boundary,
not a regex scanner or execution of all scripts after parsing. Classic inline
scripts share a realm and run in parser order. Script type, src presence,
template/foreign content and scripting-enabled noscript semantics are explicit.

Initial exposed subset: global window/self, document.URL/documentURI,
document.readyState=loading while parser callbacks run, document.title,
document.getElementById(), document.body and Element.textContent/id/tagName.
Element wrappers retain identity while the parser adds later nodes. Get-by-id
uses connected tree order; textContent replaces children, not HTML parsing.
Detached node references remain distinct. UTF-16 lone surrogates cross the
UTF-8 tree adapter as U+FFFD; document this deviation, do not claim full DOM.
No external/module scripts, events/timers, document.write, fetch/XHR, cookies,
storage, Date or broad modern-site compatibility in this slice. Unsupported
APIs are absent, not successful no-ops. Enforcing HTTP/meta CSP disables inline
execution conservatively; no CSP compatibility claim or CSP bypass.

## Ownership and publication

BROWSER directly owns HTMLWORK and the accepted JSWORK via JsSession. Never
embed either parser or JS interpreter into chrome and never make JSWORK an
unfenced grandchild of a killable parser. HTMLWORK owns the real tree. At a
script boundary it sends a bounded tree snapshot and exact script source to
the parent, then waits on a separate least-rights delegated response endpoint.
The parent asynchronously initializes the JS binding, synchronizes the tree,
evaluates the script and obtains a bounded mutation journal. JSWORK receives
no network, filesystem, Surface or device authority.

Private versioned messages bind parent/parser PID and generation, parser request
and script ordinal. No raw pointers. No partial message or result publication.
The parser validates the whole returned journal (node kinds/IDs, UTF-8, lengths,
counts and cumulative allocation/work) before mutating the candidate tree.
Only the existing validated scene publication can affect the displayed page.
Author exceptions preserve earlier valid script mutations and allow later
classic scripts; worker failure/capacity/deadline rejects the candidate, fences
both workers and keeps the previous page usable. Navigation never applies old
results to a new page. No automatic replay into a replacement JS realm.

Keep separate candidate/active journals. CSS-resource passes and reflows
reapply the accepted per-script mutations against the same immutable source,
with exact source/ordinal matching, without executing scripts again. Failed
navigation leaves the old journal and page intact. New navigation/reload gets
a fresh realm. Scriptless pages never start JSWORK or allocate large JS buffers.

## Fixed budgets and acceptance

Existing JS core/service limits unchanged:32MiB engine,16KiB stack,64MiB process,
1MiB source,65535-byte result,5000ms command,1000ms reap,one kill. Additional
browser adapter private storage<=6MiB, allocated on demand;32 inline scripts,
aggregate1MiB script source,1MiB snapshot per script,256KiB aggregate journal,
128 mutations per script,8192 existing tree nodes/depth128. The original36MiB
browser workspace and parser32MiB heap stay bounded. Scripted candidate parse
has an explicit20000ms overall deadline including startup, all callbacks and
rendering; original inert profiles keep5000ms. At most16 nonblocking IPC calls
per UI turn across bridge/service, no UI-side synchronous script wait. No
kernel, scheduler, memory/FPU, benchmark, SDK or QuickJS engine changes.

Host behavior must cover actual QuickJS binding, parser-order/inert scripts,
tree identities/text/title/escaping, malformed/stale/oversized journals before
side effects, source-exact replay and cancellation. Existing HTML/CSS, browser
navigation/runtime/source, service and benchmark regressions remain unchanged
unless a demonstrated integration adjustment preserves their assertions.
Both reference images and a real headless QEMU browser proof: inline script
changes visible title/text, later script sees earlier globals but not future
nodes, ordinary exception continues, reflow does not reexecute, navigation
gets fresh state, timeout/crash stays contained and input/close/restart work.
Protected kernel, GTEST, BENCHMARK, MATHTEST, TEXTTEST, JSTEST, JSWORK bytes
remain identical to4b2b3302. No new performance/WCET claim.

Compiler bounds90s/four workers (existing HTML/CSS compilation120s unchanged),
native execution30s, guest180s, headless only and suppress host crash dialogs.
Frozen gate commands and file scope in the queue. Retain all failures/images;
only repeat affected gates after a demonstrated in-scope fix. No agents/push.
