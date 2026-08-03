# Anzeige: VGA und Framebuffer

Der verifizierte Standardweg verwendet VGA-Textmodus. Ein optionaler linearer
RGB-Framebuffer ist vorhanden, bleibt aber experimentell.

## Buildauswahl

```bash
make kernel TARGET=qemu VIDEO=vga
make kernel TARGET=qemu VIDEO=framebuffer
make run-fb
```

`scripts/build-windows.ps1` setzt derzeit fest `VIDEO=vga`, damit das native
VMware-Paket den robusten Textpfad verwendet.

## Abstraktionsschicht

`drivers/video/display.c` stellt die gemeinsame Textausgabe bereit. Bei einem
Framebuffer-Build wird der Framebuffer verwendet, wenn der Bootloader gültige
Metadaten liefert; andernfalls fällt die Anzeige auf VGA-Text zurück.

Der Framebuffertreiber prüft unter anderem:

- vorhandene physische Adresse
- RGB-Typ
- Breite, Höhe und Pitch
- 8, 16, 24 oder 32 Bit pro Pixel
- gültige Position und Größe der RGB-Kanäle
- Überlauf und Adressbereich unterhalb von 4 GiB

Er unterstützt Löschen, Zeichenausgabe, Scrollen, Cursorposition und
Vorder-/Hintergrundfarbe.

## Bootloaderbezug

Beim Legacy-GRUB-Weg kann das Multiboot-Framebufferfeld von GRUB kommen. Der
eigene native BIOS-Loader bootet standardmäßig in VGA-Textmodus und fordert
derzeit keinen VBE-Grafikmodus an. Deshalb ist `VIDEO=framebuffer` im nativen
VMware-Paket nicht der Referenzweg.

## Grenzen

- keine Hardwarebeschleunigung
- keine Benutzer-Grafik-API
- keine dynamische Modusumschaltung
- keine garantierte VBE-Einrichtung durch den nativen Loader
- Schrift und Shelllayout sind primär für Textausgabe ausgelegt

Für Kernel-, Shell- und VMware-Fehlersuche sollte VGA verwendet werden.
