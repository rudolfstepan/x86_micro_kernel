# Anzeige: VGA, Framebuffer und Desktop-MVP

Stand: 19. August 2026.

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

Für Modi bis 1024x768x32 zeichnet der Treiber zunächst in einen festen
Shadowbuffer. Ein Frame-Commit überträgt nur die begrenzten Damage-Rechtecke
zeilenweise mit gebündelten Dword-Kopien in den LFB und führt bei aktivem PAT
genau eine Write-Combining-Barriere pro Publikation aus. Der Softwarezeiger ist
ein transparenter klassischer Pfeil mit Kontur, Füllung und Schatten; beim
Bewegen werden nur seine alte und neue kleine Fläche veröffentlicht.

Die append-only Frame-Operation `FRAME_STAGE_BLIT` unterstützt flüssiges
Software-Compositing beim Verschieben: Ein geprüftes Quellrechteck wird in
einen festen Kernel-Puffer kopiert, während Ring 3 den freigelegten Hintergrund
im Shadowbuffer rekonstruiert. Der Commit trägt den gecachten vollständigen
Fensterinhalt an der Zielposition ein und veröffentlicht Quell- und Ziel-Damage
gemeinsam. Der Cache gehört genau einer PID/Generation/Frame-Serial, nimmt nur
einen Blit pro Frame an und wird bei Cancel, Lease-Ablauf oder Prozessende
verworfen. Ring 3 erhält dabei weder eine LFB- noch Shadowbuffer-Abbildung.

## Versionierte Ring-3-Display-ABI

Das SDK kapselt die append-only erweiterten Display-Syscalls. Farben sind
unabhängig vom nativen Pixelformat als `0x00RRGGBB` angegeben.

| Syscall | SDK-Funktion | Vertrag |
|---:|---|---|
| 44 | `x86os_display_info()` | versionierte Geometrie-, Pitch-, RGB- und Schriftmetrik-Ausgabe |
| 45 | `x86os_fill_rect()` | an den sichtbaren Bereich geclipptes Rechteck |
| 46 | `x86os_draw_text_pixels()` | gegen den Bildschirm geclippte Pixelschrift, höchstens 256 Zeichen pro Aufruf |
| 115 | `x86os_draw_text_pixels_clipped()` | Pixelschrift mit zusätzlichem pixelgenauem Damage-Clip |

Syscall 115 lässt auch Hintergrundpixel angeschnittener Zeichen niemals über
das übergebene Clip-Rechteck hinausschreiben. Der Compositor kann dadurch eine
Dirty Region vollständig back-to-front rekonstruieren, ohne außerhalb dieser
Region Texte aus tieferen Z-Ebenen über bereits korrekte Fenster zu zeichnen.
Syscall 46 bleibt für bestehende Programme binär unverändert.

Alle Übergabestrukturen tragen `version` und `struct_size`; Pointer und Text
werden mit den geprüften User-Copy-Hilfen übertragen. Ring-3-Programme erhalten
bewusst kein direktes Mapping des linearen Framebuffers.

## Desktop- und Window-Manager-MVP

Bei einem tatsächlich initialisierten Framebuffer startet der Kernel
`DESKTOP.PRG` vor `SHELL.PRG`. Nach einem VGA-Textboot kann derselbe Desktop
aus der Ring-3-Shell gestartet werden; er fordert dann einmalig den validierten
VMware-SVGA-II-, QEMU-DISPI- oder vorbereiteten VBE-Grafikpfad an.

Der Desktop besitzt ein festes Ring-3-Fenstermodell für vier Anwendungen:

- Shell (`SHELL.PRG`)
- Dateien (`LS.PRG`)
- Editor (`EDIT.PRG`)
- Systeminformationen (`SYSINFO.PRG`)

Zwei überlappende Fenster sind beim Start sichtbar. Der Manager besitzt eine
explizite Z-Order, genau einen Fokus und ein implizites Pointer-Capture von
Button-Down bis Button-Up. Ein Fenster kann fokussiert, nach vorne geholt, an
der Titelleiste innerhalb des Arbeitsbereichs verschoben und am linken Gadget
geschlossen werden; das zugehörige Desktopicon öffnet es wieder. Zustand und
Darstellung liegen in getrennten Modulen und verwenden weder Heap noch eine
unbegrenzte Eventqueue.

`Tab` und die Pfeiltasten ändern die Auswahl, `Enter` startet die gewählte App
und `Esc` stellt nach einer Laufzeitaktivierung die VGA-Shell wieder her. Die
vorhandenen Console-Programme sind noch keine GUI-Clients: Sie laufen bewusst
als Vollbild-Kindprozess. Der Desktop wartet auf ihr reguläres Ende, erzwingt
bei einem Wait-Fehler Kill/Reap, leert verbliebene Eingabe und stellt danach
dieselbe Fensterszene wieder her. Der serielle Marker `DESKTOP_OK` bestätigt
den ersten erfolgreichen Renderdurchlauf.

Die weitere Architektur folgt dem Surface-/Commit- und Eingabemodell von
Wayland/xdg-shell, ohne schon Protokoll- oder Binärkompatibilität zu behaupten.
Die verbindliche Zuordnung und die schrittweise Umsetzung stehen im
[Window-Manager-Workflow](../development/GRAPHICAL_DESKTOP_WINDOW_MANAGER_WORKFLOW.md).

## Grenzen

- noch keine allgemeine Hardwarebeschleunigung
- kein direktes LFB-Mapping für Ring 3
- noch kein versioniertes Surface-/GUI-Client-Protokoll
- Resize komponiert die betroffenen Dirty-Clips vollständig; reine
  Fensterbewegungen verwenden einen atomaren gecachten Vollinhalts-Blit
- vorhandene Console-Programme laufen weiterhin einzeln im Vollbild

Für frühe Boot- und Hardwarefehlersuche bleibt `VIDEO=vga` der einfachste
Referenzpfad.
