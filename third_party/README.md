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

## NetSurf HTML5 dependencies (R3.9)

`libparserutils.tar.gz` is the unmodified LibParserUtils 0.2.5 release archive:
https://download.netsurf-browser.org/libs/releases/libparserutils-0.2.5-src.tar.gz
SHA-256: `317ed5c718f17927b5721974bae5de32c3fd6d055db131ad31b4312a032ed139`.

`libhubbub.tar.gz` is the unmodified Hubbub 0.3.8 release archive:
https://download.netsurf-browser.org/libs/releases/libhubbub-0.3.8-src.tar.gz
SHA-256: `8ac1e6f5f3d48c05141d59391719534290c59cd029efc249eb4fdbac102cd5a5`.
Both use the MIT license; each complete COPYING is retained in its archive and
installed under `usr/share/licenses/{libparserutils,libhubbub}/COPYING` in the SDK.

`scripts/build_html_engine.py` verifies pins and extracts only bounded source,
headers, generators and license files. Host Perl and GNU gperf generate the
upstream charset/entity/element lookup tables using the included generators;
generated sources are not a network build dependency. On Windows the normal
MSYS2 tools (`perl`, `gperf`) are used. No host library is linked into the guest.
The generated source adapter removes debug-only stdio includes and substitutes
stdint for inttypes (only typedefs are required outside debug printing).
NDEBUG is mandatory. The upstream WITHOUT_ICONV_FILTER configuration disables
the optional iconv filter, not its UTF-8 decoder. Parser algorithms and archived
bytes are unchanged. The worker explicitly fixes UTF-8; it does not claim
arbitrary charset conversion, all current HTML Living Standard features, CSS
layout or JavaScript support. Tree ownership is an append-only quota-bound
worker arena; slots are retained until process teardown, never reused in-place.

## NetSurf CSS dependency (R3.10)

`libcss.tar.gz` is the unmodified MIT LibCSS 0.9.2 release archive:
https://download.netsurf-browser.org/libs/releases/libcss-0.9.2-src.tar.gz
SHA-256: `2df215bbec34d51d60c1a04b01b2df4d5d18f510f1f3a7af4b80cddb5671154e`.
The complete COPYING is installed in the SDK under
`usr/share/licenses/libcss/COPYING`. The SDK exposes `libcss.a`, public headers
and `libcss.pc` (LibParserUtils/LibWapcaplet dependencies); the browser chrome
does not link LibCSS. The included C property generator runs only on the host.
The extraction adapter uses the existing NDEBUG/stdint substitutions, includes
the POSIX strings.h declaration missing from mq.c, and supplies NetSurf's
buildsystem/Makefile.clang `_ALIGNED=__attribute__((aligned))` definition.
Archived source bytes and parser/cascade algorithms are unchanged.

The selected Zig installation's compiler_rt.zig supplies standard i386 compiler
builtins in `libclang_rt.builtins-i386.a`, compiled freestanding for i386 without
stack-check dependencies. Its license is installed at
`usr/share/licenses/zig-compiler-rt/LICENSE`. This is toolchain support, not a
new OS ABI or a TLS dependency. REIST's actual CSS normal-flow subset, limits
and blocked resource loading are documented in BROWSER_ENGINE_PORT_PLAN.md.
