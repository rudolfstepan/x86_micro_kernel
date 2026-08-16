# Anzeige: VGA, Framebuffer und Desktop-MVP

Stand: 16. August 2026.

VGA-Text bleibt der robuste Standardweg. Ein `VIDEO=framebuffer`-Build richtet
über den eigenen BIOS-Loader einen linearen RGB-Framebuffer ein und startet
darauf bevorzugt den grafischen Ring-3-Desktop.

## Buildauswahl

```bash
make kernel TARGET=qemu VIDEO=vga
make kernel TARGET=qemu VIDEO=framebuffer
make run-fb
```

Unter Windows baut beispielsweise

```powershell
.\scripts\build-windows.ps1 -Target qemu -Video framebuffer -RunTests
```

den Kernel und das native BIOS-Image; Stage 2 erhält dabei den
Framebuffer-Schalter. Ohne `-Video framebuffer` bleibt `VIDEO=vga` aktiv.

## Nativer VBE-Handoff

Stage 2 sucht im Framebuffer-Build einen linearen 32-Bit-Direct-Color-Modus.
Bevorzugt wird 1024x768x32, als Rückfall 800x600x32. Erst nachdem der Modus
erfolgreich gesetzt wurde, veröffentlicht der Loader Adresse, Pitch, Auflösung,
Farbtiefe und RGB-Masken in der Multiboot-1-kompatiblen Übergabestruktur.

Scheitert VBE oder ist kein passender Modus vorhanden, stellt Stage 2 BIOS-Modus
03h wieder her und setzt kein Framebuffer-Flag. Der Kernel verwendet dann
VGA-Text und startet die Userspace-Shell statt des Desktops.

## Kernelanzeige

`drivers/video/display.c` stellt die gemeinsame Console-Ausgabe bereit. Der
Framebuffertreiber akzeptiert nur konsistente RGB-Metadaten, prüft Adresse,
Geometrie, Pitch, Kanalmasken und Überläufe und mappt den Speicher ausschließlich
als Supervisor-MMIO. Er unterstützt Console-Text sowie geclippte Rechtecke und
Pixelschrift. Console-Ausgaben werden auch im Framebuffer-Modus einmal nach
COM1 gespiegelt; dadurch bleiben Bootdiagnose und Bereitschaftsmarker headless
sichtbar.

## Versionierte Ring-3-Display-ABI

Das SDK kapselt drei angehängte Syscalls. Farben sind unabhängig vom nativen
Pixelformat als `0x00RRGGBB` angegeben.

| Syscall | SDK-Funktion | Vertrag |
|---:|---|---|
| 44 | `x86os_display_info()` | versionierte Geometrie-, Pitch-, RGB- und Schriftmetrik-Ausgabe |
| 45 | `x86os_fill_rect()` | an den sichtbaren Bereich geclipptes Rechteck |
| 46 | `x86os_draw_text_pixels()` | geclippte Pixelschrift, höchstens 256 Zeichen pro Aufruf |

Alle Übergabestrukturen tragen `version` und `struct_size`; Pointer und Text
werden mit den geprüften User-Copy-Hilfen übertragen. Ring-3-Programme erhalten
bewusst kein direktes Mapping des linearen Framebuffers.

## Desktop-MVP

Bei einem tatsächlich initialisierten Framebuffer startet der Kernel
`DESKTOP.PRG` vor `SHELL.PRG`. Der Desktop zeigt vier App-Karten:

- Shell (`SHELL.PRG`)
- Dateien (`LS.PRG`)
- Editor (`EDIT.PRG`)
- Systeminformationen (`SYSINFO.PRG`)

`Tab` und die Pfeiltasten ändern die Auswahl, `Enter` startet die gewählte App
und `Esc` die Shell. Eine App läuft als Vollbild-Kindprozess; der Desktop wartet
auf ihr Ende, leert verbliebene Eingabe und zeichnet sich neu. Der serielle
Marker `DESKTOP_OK` bestätigt den ersten erfolgreichen Renderdurchlauf.

## Grenzen

- keine Hardwarebeschleunigung oder dynamische Modusumschaltung
- kein direktes LFB-Mapping für Ring 3
- keine Maus
- kein Compositor, Windowmanager oder Fenster-/Fokusmodell
- genau eine Vordergrund-App; vorhandene Console-Programme laufen im Vollbild

Für frühe Boot- und Hardwarefehlersuche bleibt `VIDEO=vga` der einfachste
Referenzpfad.
