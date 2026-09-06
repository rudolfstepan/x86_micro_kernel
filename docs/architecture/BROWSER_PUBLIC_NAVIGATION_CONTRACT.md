# Public document admission (R3.14)

Accepted 2026-09-06: all 15 targeted command groups, both reference builds,
the byte-verified loader benchmark and all five frozen browser guest gates
pass. Commands, final logs and timings are recorded in CURRENT_WORK.md.

Resumed transport repair: the browser uses the private `--reist-ipc HANDLE`
CURL adapter instead of a transient file. It buffers at most 1 MiB plus the
16-KiB HTTP-header quota in child-private memory and sends only a fully framed
successful response. Version-1 bulk packets contain magic, exact endpoint,
offset, total and at most 2032 data bytes. Parent admission checks every field,
complete length and exact child identity/exit before publication. Cancellation
fences the endpoint before terminating/reaping the child. A fresh endpoint is
required for each hop; no old bytes or authority survive redirects/retries.
Delegation races and IPC waits are bounded; no parser gains network authority.
The UI transfers at most eight packets per turn. A successful packet permits
one scheduler yield to its now-runnable peer, avoiding an idle timer tick for
each single-slot handoff. EAGAIN never causes a yield/retry spin; idle UI turns
still sleep and the original absolute deadlines remain authoritative.
The main loop counts accepted packets as work even without keyboard/mouse
input: their existing yield replaces, rather than precedes, an idle timer wait.
No packet progress means the ordinary idle sleep remains mandatory.
This REIST-private adapter is not a claim of upstream curl option compatibility.
Existing CLI file/stdout modes retain their contracts. Network/socket capacities
are unchanged; the measured transient-file overhead is removed in Ring 3.

User-approved extension, 2026-09-06: long HTTP(S) CSS/image resource URLs must
actually load, not be skipped. Support up to 8192 URL bytes using versioned
private metadata and a bounded shared resolver adapter, without enlarging the
legacy semantic-document fields. Preserve legacy private wire decoding. CURL
must stream a complete request across bounded transport writes. The existing
spawn mechanism follows [the argument contract](PROCESS_ARGUMENT_CONTRACT.md);
this and the subsequently user-approved bounded PIO read-quota repair are the
authorized Ring-0 changes. The latter follows
[the ATA read contract](ATA_PIO_TRANSFER_CONTRACT.md), retains the existing
write/journal/AHCI limits and introduces no kernel cache or parser. HTTP URI sizes are guided by
[RFC 9110 section 4.1](https://www.rfc-editor.org/rfc/rfc9110.html#section-4.1).

The implementation uses resource-record version 3 with explicit version-2 decoding,
and scene version 4 for long image sources; version-3 scenes remain accepted.
Top-level address-bar and legacy link/form fields remain their existing size;
this extension specifically transports CSS and image resource URLs. Kernel
argument storage is a bounded temporary snapshot, not a kernel web cache.

The boundary is one generation-owned Ring-3 navigation, not full modern web
compatibility. HTTP follows RFC 9110/9112, encoding follows the supported labels
of the [WHATWG Encoding Standard](https://encoding.spec.whatwg.org/), HTML parsing
uses existing Hubbub, and process error categories follow
[curl's error codes](https://curl.se/libcurl/c/libcurl-errors.html).

The new private document profile admits 1 MiB of source in demand-owned private
memory. Legacy public semantic structures, old 64-KiB worker requests and
Surface messages do not change. Decoded trees, strings, attributes, controls
and scenes remain fixed-capacity; admission failure retains the prior document.
The existing 32-MiB worker heap, private browser heap and generation reaper own
all new storage. There is no global cache or kernel allocation policy change.
Both worker profiles now use the existing demand-backed process provider:
the legacy profile retains its 4-MiB budget, without adding an unused 4-MiB
static arena to every extended process image. Legacy input/node/attribute
limits remain unchanged; provider exhaustion is still a contained parse error.
Resource reset touches only the fixed header. Records are fully zeroed when
admitted, including legacy-to-wide decoding, so unused storage is never sent.
Reflow copies only live records, CSS bytes and decoded image pixels after
range preflight, not the entire maximum-capacity pools. No cache lifetime or
generation is extended by this work reduction.

Support UTF-8 and HTML's windows-1252 aliases, including ISO-8859-1. An explicit
transport encoding is authoritative except for a Unicode BOM; a meta declaration
may cause at most one restart before publication. Reuse upstream decoders, not
a second HTML parser. Unrecognized encodings remain visible errors. Scripts,
cookies, credentials and POST gain no authority.

Ordinary headings through 64 CSS pixels are supported by the bounded native
raster adapter. Larger pathological values still reject, rather than allowing
unbounded drawing. No claim of complete CSS layout or font compatibility.

The existing end-to-end transport chain stays 30 seconds and each HTML/CSS
worker stays five seconds. The parent must not prematurely kill a transport
whose own admitted operation is still within the chain. A transient failed GET
may be repeated only once, after exact reap and cleanup, inside that same
deadline; cancellation, TLS rejection and quota exhaustion do not retry.
DNS, connect, TLS, timeout, byte-limit, parse and output failures have distinct
visible diagnostics. Network failures cannot become silent HTTP downgrades.

File downloads coalesce decoded network fragments in a fixed private 128-KiB
CURL buffer. Full buffers use the existing bounded file-write path; the final
partial buffer is flushed only after successful HTTP framing/length validation.
Short writes are completed, write/flush errors fail the transfer, and the old
close/rename/unlink publication protocol remains authoritative. Failed responses
discard their unflushed tail; the next response starts empty. Stdout remains
streaming. No kernel cache, extra authority or longer timeout is introduced.

Acceptance includes real-code host projection/raster tests, controlled HTTP
guest navigation above 64 KiB with legacy encoding and 36/64-pixel text, and
the unchanged browser/input/resource/forms recovery gates. Captured Intracom
and Google responses remain separate timestamped diagnostic evidence; a local
fixture is never described as successful live Internet browsing.
