# REIST desktop icons

These ten 32x32 RGBA icons are generated source artwork reduced to a single
Microsoft ICO image each. The packaged payload is deliberately an uncompressed
40-byte BITMAPINFOHEADER followed by 32-bit BGRA pixels and a 1-bit AND mask;
PNG-compressed ICO entries are not used.

The desktop reads every optional asset once at startup, validates it with the
bounded REIST image decoder and precomposes fixed XRGB8888 cache variants for
the client, selected and desktop backgrounds. Rendering performs no file I/O,
decoding, allocation or scaling. Missing or invalid assets use deterministic
vector fallbacks.

Source artwork was generated with OpenAI Imagegen for this project and then
cropped and downsampled offline. Files contain no third-party icon resources.
The empty/full trash variants are original vector-style shapes rasterized with
ImageMagick into the same uncompressed 32-bit ICO profile. They deliberately
share one silhouette while visible paper distinguishes the full state.
