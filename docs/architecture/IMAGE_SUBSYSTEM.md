# REIST Image-Library und Bildbetrachter

## Schichten und Verantwortung

`libreistimage.a` ist die wiederverwendbare, darstellungsunabhängige
Rasterbibliothek. Sie entspricht der üblichen Trennung etablierter Systeme:
Codec und Pixelmodell liegen unterhalb von Anwendung, Windowing und
Dateizuordnung. Der grafische Bildbetrachter enthält deshalb keinen eigenen
BMP- oder GIF-Parser.

```text
Desktop-Dateizuordnung
  -> /usr/gui/bin/imageviewer.prg
     -> libreistimage.a
        -> BMP-Decoder
        -> GIF87a/GIF89a-Decoder
```

Die öffentliche C11-API liegt in `reist/image.h`; pkg-config veröffentlicht
`reist-image.pc`. Eingaben sind unveränderliche caller-owned Bytes. Ausgabe ist
ein caller-owned XRGB8888-Puffer. Ein expliziter Workspace hält die begrenzten
GIF-LZW-Tabellen und Subblöcke. Damit bleiben Decoderinstanzen reentrant und es
gibt weder versteckte Heapallokation noch globalen Decoderzustand.

## Unterstützte Formate

| Format | Version/Variante | Status |
|---|---|---|
| BMP | Windows DIB, BI_RGB, 24 und 32 Bit | unterstützt |
| BMP | top-down und bottom-up | unterstützt |
| GIF | GIF87a und GIF89a | unterstützt |
| GIF | globale/lokale Palette, LZW, Interlace, Transparenz | unterstützt |
| GIF | Animation | erster Frame; Frameanzahl wird gemeldet |
| BMP | RLE, Bitfelder, indizierte Varianten | noch nicht unterstützt |

Breite und Höhe sind jeweils begrenzt; Version 1 akzeptiert höchstens
1024 × 768 Pixel. Strukturgrößen und Versionsfelder sind Bestandteil der
stabilen API. Beschädigte Header, überlaufende Maße, ungültige Paletten und
fehlerhafte LZW-Ketten werden vor Veröffentlichung des Ergebnisobjekts
abgewiesen.

## Desktop-Integration

`/etc/reist/filetypes.conf` ordnet `.bmp` und `.gif` dem Bildbetrachter zu.
Mitgelieferte Beispiele liegen unter:

- `/usr/share/images/demo-desktop.bmp`
- `/usr/share/images/demo-colors.gif`

Der Viewer skaliert große Bilder seitenverhältnisgetreu in den sichtbaren
Bereich. Die append-only Display-Control-Operation `DRAW_PIXELS` überträgt ein
vollständiges XRGB8888-Rechteck über die SDK-Funktion `x86os_draw_pixels()`.
Der Kernel validiert Quelle, Maße, Stride und jeden Farbwert vor dem ersten
Schreibzugriff, kopiert anschließend in kapazitätsbegrenzten Blöcken und
veröffentlicht genau ein Damage-Rechteck. Dadurch benötigt der Viewer für die
Bildfläche einen statt potenziell hunderttausender Zeichen-Syscalls.
Bei der üblichen nativen XRGB8888-Anordnung von VMware SVGA, VBE und linearen
PC-Framebuffern werden bereits validierte Pixel zeilenweise direkt in den
Shadow-Framebuffer kopiert. Die generische Kanalumrechnung bleibt als
Fallback für abweichende Pixelformate erhalten.

Große Bilddateien werden im Syscallpfad in maximal 16-KiB-Abschnitten gelesen.
Der Zielbereich wird vollständig als schreibbarer Ring-3-Bereich validiert;
die begrenzten Direkttransfers vermeiden anschließend sektorweise
Bounce-Buffer-Kopien und wiederholte FAT-Kettenläufe. Falls REIST später
mehrere Threads pro Adressraum unterstützt, muss dieser Vertrag durch echtes
Page-Pinning ersetzt werden, bevor parallele Mapping-Änderungen zugelassen
werden.

Während einer zur Laufzeit aktivierten Grafiksitzung bleiben normale Kernel-
und Dienstausgaben im seriellen Diagnosekanal. Sie werden nicht als
Terminaltext in den Framebuffer geschrieben und können damit weder Desktop
noch Anwendungen überzeichnen. Nach dem Deaktivieren der Grafiksitzung wird
die normale Textausgabe wiederhergestellt.
