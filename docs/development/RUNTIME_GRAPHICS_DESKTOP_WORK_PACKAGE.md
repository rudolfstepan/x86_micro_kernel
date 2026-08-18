# Arbeitspaket: Laufzeitstart des grafischen Desktops aus VGA

Stand: 18. August 2026  
Status: vorgeschlagen, noch nicht in `automation/reist-s03b.toml` aktiviert

## Kennung

`R1.5-runtime-desktop`

## Ziel

Ein normal mit VGA-Text gestartetes REIST OS muss den Befehl
`desktop.prg` aus der Userspace-Shell annehmen können. Das Programm fordert
über eine versionierte, angehängte Display-ABI einen Grafikmodus an. Erst nach
einem vollständig validierten Moduswechsel wird der lineare Framebuffer
veröffentlicht und der Desktop im Stil einer klassischen Amiga Workbench
gezeichnet.

Der bestehende Bootmodus `VIDEO=framebuffer` bleibt kompatibel. Scheitert die
Laufzeitaktivierung, bleibt beziehungsweise wird VGA-Modus 03h wieder aktiv;
die Shell bleibt bedienbar und erhält einen eindeutigen Fehlerstatus.

## Ausgangslage

- Stage 2 kann VBE 1024x768x32 oder 800x600x32 derzeit nur während eines
  `VIDEO=framebuffer`-Boots auswählen.
- Bei `VIDEO=vga` wird kein Framebuffer an den Kernel übergeben.
- `desktop.prg` benutzt bereits die geclippte Ring-3-Display-ABI, beendet sich
  ohne Framebuffer aber mit „Grafikmodus nicht verfuegbar“.
- Ein BIOS-Aufruf aus Ring 3 und ein direktes Framebuffer-Mapping in Ring 3
  sind nicht zulässig.

## Umfang

1. Stage 2 ermittelt bei jedem BIOS-Boot eine feste, priorisierte Liste
   geeigneter VBE-LFB-Modi, ohne beim VGA-Boot den Textmodus zu verlassen.
   Übergeben werden nur vollständig geprüfte Modusmetadaten.
2. Ein begrenzter x86-BIOS-Grafikdienst führt `INT 10h, AX=4F02h` aus einer
   kontrollierten Kernel-Transition aus. Die Transition besitzt feste
   Speicher- und Zeitbudgets und stellt CPU-, GDT-, IDT-, PIC- und
   Interruptzustand auf jedem Ausgang wieder her.
3. Ein neuer, an die bestehende Syscall-Tabelle angehängter Display-Control-
   Syscall erlaubt ausschließlich den Wechsel in einen vom Loader validierten
   Modus und die Rückkehr nach VGA-Modus 03h. Beliebige BIOS-Funktionen oder
   frei gewählte Modusnummern werden Ring 3 nicht zugänglich gemacht.
4. Erst nach erfolgreichem Moduswechsel und erneuter Prüfung von Adresse,
   Pitch, Geometrie, BPP und RGB-Masken initialisiert der Kernel den
   Supervisor-only-Framebuffer. Veröffentlichung erfolgt als letzter Schritt.
5. `desktop.prg` versucht bei `ENODEV` genau einmal die Grafikaktivierung,
   fragt danach die Display-Information erneut ab und zeichnet den Desktop.
6. Der Desktop erhält eine Workbench-artige Oberfläche mit Menüleiste,
   Desktop-Hintergrund, Laufwerks-/Programmicons, Auswahlrahmen und unterer
   Statuszeile. Alle vorhandenen Programme bleiben über Tastatur erreichbar.
7. Beim Verlassen oder bei fehlgeschlagener Initialisierung wird kontrolliert
   auf VGA-Text zurückgeschaltet, die Console neu initialisiert und die Shell
   wieder benutzbar gemacht.

## Verbindliche Invarianten

- Die öffentliche ABI bleibt append-only; vorhandene Syscalls 44 bis 46 und
  ihre Strukturen ändern Bedeutung und Layout nicht.
- Ring 3 erhält weder I/O-Rechte noch ein LFB-Mapping noch eine allgemeine
  BIOS-Aufrufschnittstelle.
- Nur vom Loader ermittelte und im Kernel erneut validierte 32-Bit-
  Direct-Color-LFB-Modi sind aktivierbar.
- Pointer, Strukturversion, Strukturgröße, Flags und reservierte Felder werden
  vor jedem Seiteneffekt geprüft.
- Es gibt höchstens einen Aktivierungsversuch pro explizitem Userspace-Aufruf;
  keine unbeschränkten Schleifen, Retries oder Busy-Waits.
- Die BIOS-Transition läuft exklusiv, maskiert konkurrierende Grafikwechsel
  und besitzt eine monotone Deadline.
- Ein partieller oder zeitlich unklarer Wechsel veröffentlicht keinen
  Framebuffer. Der Fehlerpfad versucht einmal Modus 03h und fällt danach in
  einen diagnostizierten, seriell beobachtbaren sicheren Anzeigezustand.
- Framebuffer-Adresse und -Größe müssen vollständig im unterstützten
  physischen Adressraum liegen; sämtliche Multiplikationen und Additionen
  werden vor dem Mapping auf Überlauf geprüft.
- Der Framebuffer bleibt Supervisor-only. Zeichenoperationen bleiben
  geclippt, vorzeitig unterbrechbar und größenbegrenzt.
- VGA- und bestehende Framebuffer-Boots bleiben startfähig. Ein fehlendes oder
  unbrauchbares VBE-BIOS darf den normalen VGA-Boot nicht verhindern.
- Der Grafikpfad behauptet keine Fail-operational- oder
  Zertifizierungseigenschaft.

## Vorgesehene Dateien

Die endgültige `allowed_files`-Liste ist beim Eintragen in die Automation zu
fixieren. Erwartet werden ausschließlich:

- `arch/x86/boot/bios/stage2_bios.asm`
- `arch/x86/boot/multiboot_parser.h`
- `arch/x86/boot/multiboot_parser.c`
- neue, eng begrenzte Dateien unter `arch/x86/` für die BIOS-Transition
- `drivers/video/framebuffer.h`
- `drivers/video/framebuffer.c`
- neue Display-Control-Dateien unter `drivers/video/`
- `kernel/syscall/syscall_table.c`
- `kernel/init/kernel.c`
- `userspace/sdk/include/x86os.h`
- `userspace/sdk/x86os.c`
- `userspace/programs/desktop.c`
- `scripts/build_system_programs.py`
- `scripts/test-reist-runtime.ps1`
- ein neuer QEMU-Runtime-Runner unter `scripts/`
- neue paketbezogene Tests unter `test/`
- `docs/features/FRAMEBUFFER.md`
- `docs/development/BUILD_MODES.md`
- `docs/development/OS_GAP_ANALYSIS_AND_ROADMAP.md`
- `automation/reist-s03b.toml`

Benötigt die Implementierung weitere Produktionsdateien, ist das Paket vor
der Änderung zu stoppen und die Architekturursache zu dokumentieren.

## Umsetzungsschritte

1. Regressionstests für den heutigen Fehlerfall ergänzen: VGA-Boot,
   Shell-Prompt, Start von `desktop.prg`, erwartetes `ENODEV`.
2. Versioniertes Loader-Handoff für eine feste Zahl geprüfter VBE-Kandidaten
   definieren und Parsertests ergänzen.
3. BIOS-Transition isoliert implementieren und Host-/Quelltests für Register-,
   Deskriptor-, Interrupt- und Fehlerpfadwiederherstellung hinzufügen.
4. Append-only Display-Control-ABI und SDK-Wrapper implementieren.
5. Framebuffer erst nach erfolgreicher Transition initialisieren und den
   VGA-Rollback vervollständigen.
6. `desktop.prg` um einmalige Aktivierung und Workbench-artige Darstellung
   erweitern.
7. QEMU-Gastnachweis für den kompletten Ablauf und den erzwungenen
   VBE-Fehlerpfad ergänzen.
8. Erst nach bestandenen äußeren Gates Dokumentation und Queue auf den
   nächsten Zustand setzen.

## Akzeptanzkriterien

- Ein Image wird mit `-Video vga` gebaut und erreicht sichtbar `C:\>`.
- `desktop.prg` wechselt dasselbe laufende System ohne Neustart in einen
  validierten Grafikmodus.
- Der serielle Ablauf enthält in dieser Reihenfolge eindeutige Marker für
  VGA-Shell, Modusanforderung, erfolgreichen Framebuffer und `DESKTOP_OK`.
- Die Workbench-artige Oberfläche wird in einem QEMU-Screenshot nachgewiesen.
- Ein absichtlich fehlgeschlagener VBE-Wechsel liefert einen dokumentierten
  Fehler und hinterlässt eine bedienbare VGA-Shell.
- Ein zweiter Prozess kann während einer laufenden Transition keinen weiteren
  Moduswechsel beginnen.
- Der vorhandene `-Video framebuffer`-Boot startet weiterhin den Desktop.
- Der normale VGA-Pakettest sowie alle bestehenden Display-, Desktop-, Loader-
  und Syscall-Tests bleiben erfolgreich.

## Vorgesehene Gates

### Targeted

```powershell
python test/test_bios_vbe_source.py -q
python test/test_display_abi_minimal.py -q
python test/test_desktop_source.py -q
python test/test_runtime_graphics_switch.py -q
```

### Package

```powershell
.\scripts\test-reist-package.ps1 -Target qemu -Video vga
```

### Runtime

```powershell
.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop
.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-vbe-failure
```

### Kompatibilität

```powershell
.\scripts\test-reist-package.ps1 -Target qemu -Video framebuffer
```

Die äußere Automation führt die eingefrorenen Gates jeweils genau einmal aus.
Ein Quellmustertest ersetzt weder den realen QEMU-Gastnachweis noch spätere
Tests auf unterstützter Zielhardware.

## Stop-Bedingungen

- Der BIOS-Aufruf kann CPU-, Deskriptor-, Interrupt- oder PIC-Zustand nicht
  deterministisch und begrenzt wiederherstellen.
- Ein Moduswechsel wäre nur durch allgemeine BIOS- oder I/O-Rechte für Ring 3
  möglich.
- Ein partieller Wechsel könnte unvalidierte Framebuffer-Metadaten sichtbar
  machen.
- Die Rückkehr zu einer bedienbaren VGA-Console kann nach einem normalen
  VBE-Fehler nicht nachgewiesen werden.
- Eine notwendige Produktionsdatei liegt außerhalb der eingefrorenen
  `allowed_files`.
- Ein Gate scheitert nach einer fokussierten Reparatur weiterhin.

## Nicht Bestandteil dieses Pakets

- überlappende oder frei verschiebbare Fenster
- Compositor und Hardwarebeschleunigung
- Mauszeiger, Drag-and-drop und USB-Maus
- frei ladbare Grafiktreiber oder UEFI GOP
- Auflösungsdialog und beliebige dynamische Modi
- direktes Framebuffer-Mapping in Userspace
- gleichzeitige grafische Sitzungen mehrerer Benutzer

Diese Funktionen benötigen nach dem stabilen Laufzeit-Moduswechsel eigene,
begrenzte Arbeitspakete.

## Erwartetes Restrisiko

Der BIOS-Thunk vergrößert die privilegierte Trusted Computing Base und hängt
von Firmwareverhalten ab, das nach dem Boot nicht auf jeder realen Plattform
gleich gut unterstützt wird. QEMU- und VMware-Nachweise belegen deshalb keine
universelle Hardwarekompatibilität. Für moderne Systeme ist später ein
separater nativer Grafiktreiber- beziehungsweise UEFI-GOP-Pfad vorzusehen.
