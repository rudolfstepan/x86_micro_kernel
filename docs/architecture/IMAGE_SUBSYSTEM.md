# REIST Image-Library und Bildbetrachter

Stand: 25. August 2026.

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

Der Viewer ist im Desktop ein eigener Ring-3-Surface-Client. Er skaliert Bilder
seitenverhältnisgetreu in seine jeweils bestätigte Clientgröße und publiziert
einen unveränderlichen XRGB8888-Puffer über `attach`, `damage` und `commit`.
Der Kernel validiert Quelle, Maße, Stride und jeden Farbwert, bevor die
generationengebundene Ressource sichtbar wird. Nur der Desktopprozess, der den
Viewer gestartet hat, darf daraus geclippte Pixel in die Szene übernehmen;
Anwendung und Codec kennen weder globale Koordinaten noch den Framebuffer.
Beim Resize wird zuerst der neue Puffer atomar übernommen und erst danach der
alte Puffer zur Freigabe gemeldet. Prozessende widerruft beide Seiten
idempotent.

Beim direkten Start aus der Shell bleibt der kompatible Vollbildpfad erhalten.
Dort überträgt die append-only Display-Control-Operation `DRAW_PIXELS` ein
vollständiges XRGB8888-Rechteck über `x86os_draw_pixels()`.
Bei der üblichen nativen XRGB8888-Anordnung von VMware SVGA, VBE und linearen
PC-Framebuffern werden bereits validierte Pixel zeilenweise direkt in den
Shadow-Framebuffer kopiert. Die generische Kanalumrechnung bleibt als
Fallback für abweichende Pixelformate erhalten.

Bilddateien werden nicht mehr über den Legacy-Kernel-VFS-Pfad geladen. Der
Viewer öffnet einmalig ein owner- und generationgebundenes Ring-3-Objekt,
validiert Typ und Größe mit `fstat` und liest anschließend höchstens 128 KiB
pro Bulk-Request in seinen festen 1-MiB-Eingabepuffer. Eine maximale Datei
benötigt damit acht statt 64 serialisierte 16-KiB-Abschnitte. Kontrollframe,
CRC, Deadline, Cancel und Offsetfortschritt bleiben Teil des vorhandenen
Storage-Service-Vertrags; ein Fehler veröffentlicht keine Teilabbildung.

Der FAT-Parser hält pro Operation genau einen validierten 512-Byte-FAT-Sektor
im Stackcache. Verzeichnisläufe bleiben auf 128 Cluster begrenzt; nur der
Dateilesepfad besitzt eine getrennte feste Grenze von 2176 besuchten Clustern.
Damit benötigt ein 128-KiB-Transfer bei der kleinsten Clustergröße höchstens
256 Datensektoren und bleibt einschließlich FAT-Weg innerhalb des bestehenden
320-Sektor-Budgets. Der Cache überlebt weder Volumevalidierung noch Request.
Die zwei kernel-eigenen Bulk-Slots belegen zusammen fest 256 KiB; dynamische
Cacheallokation und ungebundene Vorablesezugriffe werden nicht eingeführt.

Während einer zur Laufzeit aktivierten Grafiksitzung bleiben normale Kernel-
und Dienstausgaben im seriellen Diagnosekanal. Sie werden nicht als
Terminaltext in den Framebuffer geschrieben und können damit weder Desktop
noch Anwendungen überzeichnen. Nach dem Deaktivieren der Grafiksitzung wird
die normale Textausgabe wiederhergestellt.
