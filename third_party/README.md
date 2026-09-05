# Third-party source archives

`libwapcaplet.tar.gz` is the unmodified official LibWapcaplet 0.4.3 source
archive from https://download.netsurf-browser.org/libs/releases/libwapcaplet-0.4.3-src.tar.gz
(project: https://www.netsurf-browser.org/projects/libwapcaplet/).
SHA-256: `9b2aa1dd6d6645f8e992b3697fdbd87f0c0e1da5721fa54ed29b484d13160c5c`.
The complete MIT license is retained as `COPYING` inside the archive and
installed under `usr/share/licenses/libwapcaplet/`. Extraction is restricted
to the pinned C source, public header and license. The generated public header
removes only its unused `#include <sys/types.h>`; no declaration requires that
header, no fake POSIX types are supplied, and upstream source/API is unchanged.
The opt-in Ring-3 C runtime supplies actual malloc/free and byte operations.
These upstream C calls still require valid caller-owned data, finite string
lengths and balanced references; this is not a validated untrusted-input IPC
adapter or a complete browser engine. Runtime probes bound all such inputs.

`stb_image.h` v2.30 is pinned to upstream commit
`013ac3beddff3dbffafd5177e7972067cd2b5083` of
https://github.com/nothings/stb (raw source at that exact revision).
The adjacent SHA-256 pins the original header. REIST uses the MIT license
option included verbatim at the end of the header. Only the Ring-3 browser
adapter enables PNG/JPEG, without stdio, SIMD, HDR or thread-local state.
Its fixed arena and input/output limits are checked before decoding; this is
not a general unbounded image API or a claim of complete format conformance.

`mbedtls-4.1.1.tar.bz2` is the official Mbed TLS 4.1.1 LTS release archive,
licensed under Apache-2.0 or GPL-2.0-or-later. REIST uses the Apache-2.0 option.
The adjacent SHA-256 file pins the exact upstream archive. Build tooling must
verify that digest before extracting any selected source into ignored build
storage.

`cacert-2026-08-13.pem` is curl's dated conversion of Mozilla's CA store,
licensed under MPL-2.0. Its adjacent upstream SHA-256 file pins the exact
121-certificate input used to generate REIST's embedded TLS trust anchors.
curl's PEM conversion does not carry Mozilla name constraints. REIST therefore
does not claim equivalent policy enforcement, certificate revocation checking
or secure-clock resistance.
