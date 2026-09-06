# Public document admission (R3.14)

Candidate, not accepted until the frozen gates pass.

User-approved extension, 2026-09-06: long HTTP(S) CSS/image resource URLs must
actually load, not be skipped. Support up to 8192 URL bytes using versioned
private metadata and a bounded shared resolver adapter, without enlarging the
legacy semantic-document fields. Preserve legacy private wire decoding. CURL
must stream a complete request across bounded transport writes. The existing
spawn mechanism follows [the argument contract](PROCESS_ARGUMENT_CONTRACT.md);
this is the only authorized Ring-0 change. HTTP URI sizes are guided by
[RFC 9110 section 4.1](https://www.rfc-editor.org/rfc/rfc9110.html#section-4.1).

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

Acceptance includes real-code host projection/raster tests, controlled HTTP
guest navigation above 64 KiB with legacy encoding and 36/64-pixel text, and
the unchanged browser/input/resource/forms recovery gates. Captured Intracom
and Google responses remain separate timestamped diagnostic evidence; a local
fixture is never described as successful live Internet browsing.
