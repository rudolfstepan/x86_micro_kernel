# Third-party source archives

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
