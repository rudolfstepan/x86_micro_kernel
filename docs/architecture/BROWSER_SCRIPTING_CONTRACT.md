# Browser scripting profile 1 — R3.25

## R3.27 frozen extension — parser-blocking external classic scripts

Frozen8 September2026 on3eab01ab. Reference [HTML classic-script fetching](https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-classic-script),
[script processing](https://html.spec.whatwg.org/multipage/scripting.html#the-script-element),
[JavaScript MIME essence](https://mimesniff.spec.whatwg.org/#javascript-mime-type)
and RFC9110/9111 redirect/cache terminology. One resource/execution/publication
boundary, no new event lifetime. Inline and external classic scripts execute
at the real parser callback in the same realm; future nodes remain absent.
External src body text is never interpreted as a fallback script. async,
defer, modules, explicit crossorigin/integrity/referrerpolicy remain inert in
this bounded subset, not run with a false order or security policy.

Append private script profile3 (48-byte header unchanged), reserved word0
means inline source and1 means external reference. Profiles1/2 keep zero.
External payload is first parsed base href (or empty), NUL, raw src bytes;
each reference<8193 bytes, exactly one separator. Snapshot uses DOM profile2;
journal remains its profile2 grammar. Replay binds profile, source kind,
exact source/reference and ordinal. Browser binds the actual document URL,
resolves base/src with the existing canonical resolver, and admits the final
URL and every redirect. No HTTPS downgrade, remote-to-local transition,
credentials, unsupported scheme or invalid URI escapes the existing policy.
Conservatively deny all execution/fetch under enforcing HTTP/meta CSP.

BROWSER directly owns a separate CURL while HTMLWORK waits for the script;
JSWORK receives no VFS/network/IPC authority beyond its existing service.
No changes to CURL or TLS verification. HTTP resources require a supported
JavaScript MIME essence and UTF8/ASCII charset; missing MIME, non-JS content,
unsupported charset and non-success status skip the script, continuing later
scripts without executing error bodies. This deliberately stricter policy is
not full legacy MIME sniffing or charset/CORS compatibility. Local source is
UTF8, read in at most4KiB per UI turn. Missing local files skip; native worker,
protocol, quota or deadline failures reject the candidate and preserve the old
page. No unvalidated partial source or mutation publication.

One noncopyable loader owns its endpoint, exact spawned PID/generation and
borrowed source lifetime. Fresh least-rights SEND-only CURL endpoint per hop;
<=5 redirects, absolute5s fetch budget, no retry. Close before cancellation,
at most one kill, <=1s reap, no wait on live/unowned children or replacement
before reap. Unknown identity/reap failure is terminal, not silent success.
Navigation/close fence JS before invalidating borrowed source. All three owned
workers participate in readiness/cleanup; UI remains nonblocking, total<=16
timeout-zero IPC calls/turn including existing CSS path. Old parse20s,
engine32MiB/16KiB stack, source1MiB/result64KiB and journal caps unchanged.

Loader private storage<=3MiB demand-backed; total scripting adapter<=9MiB.
Individual external source<=1MiB, cache aggregate<=1MiB/32 entries. Cache is
document-private, not persisted or shared. Network reuse requires explicit
positive Cache-Control max-age without no-cache/no-store, Age or Vary;
expiry is monotonic and capped to the document parse budget. Other responses
are not reused. Local sources may be reused within the same navigation.
Journal-only CSS passes/resize never fetch or execute again; a fresh navigation
invalidates the loader cache. No general HTTP cache-compatibility claim.

Frozen gates in R3.27: actual fetch/response/owner OS-mock behavior O0/O2,
real parser callbacks, legacy regressions, both reference images and real
headless browser HTTP/local scripts, >256KiB source, redirect, cache/reflow,
missing/MIME failure continuation, actual in-flight CURL cancellation and
recovery. Existing JS native fault/noncooperative hang and browser input/
crash/restart guests remain mandatory. Both kernels and protected benchmark,
JSWORK and CURL payloads unchanged. No agents, visible VMs/dialogs or push.
Later R3.26/R3.25 sections describe their historical accepted boundaries.

## R3.26 frozen extension — HTML attributes and classes

Frozen8 September2026 on2d6aba16 before implementation. One extension of
the existing candidate-tree publication boundary, not a new event lifetime.
Reference: [WHATWG Element attributes](https://dom.spec.whatwg.org/#interface-element),
[DOMTokenList](https://dom.spec.whatwg.org/#interface-domtokenlist) and
[Web IDL argument conversion](https://webidl.spec.whatwg.org/#es-overloads).
Implement getAttribute/getAttributeNames/hasAttribute/hasAttributes,
setAttribute/removeAttribute/toggleAttribute, writable id/className and live
classList length/item/contains/add/remove/toggle/replace/value/stringification
and iteration. Class wrapper identity persists across snapshots and reflected
updates. Validate all variadic tokens before any class mutation. Preserve
ordered-set semantics, absent versus empty attributes, ASCII case folding for
HTML, null/Symbol conversions and detached wrapper identity. CSS uses the real
mutated attributes (including style strings); no private styling bypass.

Deliberate bounded adapter: HTML elements only for attribute operations,
no namespace APIs/SVGAnimatedString/NamedNodeMap, CSSOM or selector engine.
Attribute writes/removal use the ASCII XML Name subset, at most255 bytes; unsupported
non-ASCII names throw a named NotSupportedError, malformed names an
InvalidCharacterError. Errors are ordinary Error instances with standard error
names, not a claimed DOMException implementation. Class token count<=1024,
mutation and string limits unchanged; capacity failure poisons the journal even
when caught. Indexed exotic properties on classList are outside this profile;
use item() or iteration. No unsupported API reported as successful.

Append private script message/journal version2; version1 remains accepted with
its original text/title grammar and read-only id. Version2 records are ASCII
hex: operation,node,name_length,value_length (8 hex digits each), then name
and value UTF-8 bytes as hex. Operations0=text/title,1=set attribute,2=remove.
No implicit namespace or pointers. Snapshots explicitly select the profile
and include ordered attribute pairs. Replay binds exact profile/source/ordinal;
mixed profiles within a parse fail closed. Whole journals validate operation,
existing node kind/namespace, strict UTF-8/name grammar and cumulative node,
attribute,string,work reserve before applying any record. Retired storage is
never recycled into a stale reference. Existing fixed caps,20s parse,16KiB
engine stack,32MiB engine and adapter<=6MiB remain unchanged.

No kernel/SDK/JSWORK/engine or network authority changes. Existing CSP denial,
source-exact reflow, fresh navigation and direct-worker ownership remain.
External/module scripts, events/timers, dynamic script insertion remain open.
Frozen gates in R3.26: real QuickJS O0/O2 binding, real Hubbub all-before-apply
journals and owner replay tests, all existing browser/JS/benchmark regressions,
both reference builds, protected image payload hashes and real headless guest
with CSS-visible attribute change (including resize replay and recovery),
ordinary exception continuation and fault/hang/old-page/restart preservation.
No performance/WCET claim or visible VM/native crash dialogs.

The following R3.25 sections record the previously accepted profile1 boundary.

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

## Implemented adapter details

The private HTML header remains48bytes and CSS request444bytes. Append-only
HTML profile4 / CSS profile5 use the formerly zero second reserved word for
the separate DOM response endpoint; earlier profiles retain their old checks.
JSWORK and its engine/service protocol are unchanged. The browser owns a
noncopyable C++ adapter and checks its explicit cleanup before destruction.
An unreapable/unknown JS owner is a terminal browser diagnostic, not a silent
infinite retry or a new child with borrowed old authority.

The initial Element.id reflection is read-only (assignment throws); tagName is
read-only. There is no Element constructor API or DOM event lifetime after
parsing. The realm is gracefully shut down after a parse; reflow uses only
the accepted journal. Local documents report their existing native VFS path
as URL/documentURI; network documents report the admitted URL. The UTF-8 tree
cannot contain embedded NUL: that journal is rejected, not truncated. DOM
quota exhaustion poisons journal extraction even if author code catches the
RangeError. Script completion values still share the accepted bounded EVAL
result conversion; this is not a general browser execution compatibility claim.

DOM traversal and title whitespace normalization are iterative, avoiding
recursive helper/callback chains on the unchanged16KiB interpreter stack.
The test uses the real QuickJS interpreter at both O0/O2 and the real Hubbub
callback/tree with malformed-journal admission. Host owner tests use only
OS-boundary mocks around the actual C++ owner, session and wire validators.
The guest selector is a trusted keyboard action on the existing explicit
input probe, never a URL/source pattern; faults reuse accepted JSWORK argv
fixture modes, not a privileged API exposed to page scripts.

On the reference desktop the native frame can retain its original REIST Web
caption. The browser therefore also paints the current scripted document title
in its own chrome, independently of compositor decoration caching. The guest
requires actual title pixels to change on navigation, survive failed candidates
and return on recovery. UTF-8 truncation at the chrome edge has a real renderer
regression; it never triggers another document-buffer upload. This does not
claim a repair of shared native-window decoration refresh.
