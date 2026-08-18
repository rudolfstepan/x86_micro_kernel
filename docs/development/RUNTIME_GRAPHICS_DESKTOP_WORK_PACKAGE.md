# Arbeitspaket: Laufzeitstart des grafischen Desktops aus VGA

Stand: 18. August 2026  
Status: vorgeschlagen, noch nicht in `automation/reist-s03b.toml` aktiviert

## Kennung

`R1.5-runtime-desktop`

## Ziel

Ein normal mit VGA-Text gestartetes REIST OS muss den Befehl
`desktop.prg` aus der Userspace-Shell annehmen können. Das Programm fordert
über eine versionierte, angehängte Display-ABI einen Grafikmodus an. Ein
nativer, fest begrenzter Treiber programmiert auf QEMU ausschließlich den
eindeutig erkannten Standard-VGA-/Bochs-DISPI-Adapter. Erst nach vollständiger
Registerrücklesung und Validierung wird der lineare Framebuffer veröffentlicht
und der Desktop im Stil einer klassischen Amiga Workbench gezeichnet.

Der bestehende Bootmodus `VIDEO=framebuffer` bleibt kompatibel. Ein nicht
unterstützter Adapter oder eine fehlgeschlagene Vorprüfung lässt den laufenden
VGA-Textmodus unverändert; die Shell bleibt bedienbar und erhält einen
eindeutigen Fehlerstatus. Firmwareaufrufe nach dem Kernelstart sind verboten.

## Ausgangslage

- Stage 2 kann VBE 1024x768x32 oder 800x600x32 derzeit während eines
  `VIDEO=framebuffer`-Boots auswählen.
- Bei `VIDEO=vga` wird kein Framebuffer an den Kernel übergeben.
- `desktop.prg` benutzt bereits die geclippte Ring-3-Display-ABI, beendet sich
  ohne Framebuffer aber mit „Grafikmodus nicht verfuegbar“.
- Ein BIOS-Aufruf im laufenden Kernel ist nicht zuverlässig durch eine
  monotone Deadline abbrechbar und daher für dieses Paket unzulässig.
- QEMU Standard VGA stellt eine native, feste Bochs-DISPI-Registerschnittstelle
  und einen PCI-LFB-BAR bereit; andere Adapter sind zunächst nicht unterstützt.

## Umfang

1. Der Kernel erkennt ausschließlich die freigegebene QEMU-Standard-VGA-
   PCI-Identität, einen Memory-BAR mit ausreichender, überlauffrei geprüfter
   Größe und eine unterstützte Bochs-DISPI-ID.
2. Ein begrenzter nativer Grafikdienst programmiert eine feste Priorität
   `1024x768x32`, danach optional `800x600x32`. Jede Registerfolge hat eine
   konstante Anzahl von I/O-Zugriffen und wird vollständig zurückgelesen.
3. Ein neuer, an die bestehende Syscall-Tabelle angehängter Display-Control-
   Syscall erlaubt ausschließlich den Wechsel in einen vom Loader validierten
   Modus. Beliebige I/O-Ports, Firmwarefunktionen oder frei gewählte Modi
   werden Ring 3 nicht zugänglich gemacht.
4. Erst nach erfolgreichem Moduswechsel und erneuter Prüfung von Adresse,
   Pitch, Geometrie, BPP und RGB-Masken initialisiert der Kernel den
   Supervisor-only-Framebuffer. Veröffentlichung erfolgt als letzter Schritt.
5. `desktop.prg` versucht bei `ENODEV` genau einmal die Grafikaktivierung,
   fragt danach die Display-Information erneut ab und zeichnet den Desktop.
6. Der Desktop erhält eine Workbench-artige Oberfläche mit Menüleiste,
   Desktop-Hintergrund, Laufwerks-/Programmicons, Auswahlrahmen und unterer
   Statuszeile. Alle vorhandenen Programme bleiben über Tastatur erreichbar.
7. Vor der ersten DISPI-Mode-Enable-Schreiboperation müssen alle Prüfungen
   abgeschlossen sein. Nicht unterstützte Hardware und Vorprüfungsfehler
   verändern den VGA-Textmodus nicht. Ein fehlgeschlagenes Register-Readback
   deaktiviert DISPI mit genau einem festen Schreibzugriff und wird seriell
   diagnostiziert.

## Verbindliche Invarianten

- Die öffentliche ABI bleibt append-only; vorhandene Syscalls 44 bis 46 und
  ihre Strukturen ändern Bedeutung und Layout nicht.
- Ring 3 erhält weder I/O-Rechte noch ein LFB-Mapping noch eine Firmware-
  Aufrufschnittstelle.
- Die Laufzeitaktivierung führt keinen BIOS-Aufruf und keinen Real-Mode-
  Übergang aus.
- Nur die exakte unterstützte PCI-/DISPI-Identität mit validiertem LFB-BAR ist
  aktivierbar.
- Pointer, Strukturversion, Strukturgröße, Flags und reservierte Felder werden
  vor jedem Seiteneffekt geprüft.
- Es gibt höchstens einen Aktivierungsversuch pro explizitem Userspace-Aufruf;
  keine unbeschränkten Schleifen, Retries oder Busy-Waits.
- Die native Transition läuft exklusiv und besitzt eine feste Obergrenze für
  sämtliche Registerzugriffe.
- Ein partieller oder unbestätigter Wechsel veröffentlicht keinen
  Framebuffer. Der Fehlerpfad deaktiviert DISPI genau einmal und bleibt
  seriell diagnostizierbar.
- Framebuffer-Adresse und -Größe müssen vollständig im unterstützten
  physischen Adressraum liegen; sämtliche Multiplikationen und Additionen
  werden vor dem Mapping auf Überlauf geprüft.
- Der Framebuffer bleibt Supervisor-only. Zeichenoperationen bleiben
  geclippt, vorzeitig unterbrechbar und größenbegrenzt.
- VGA- und bestehende Framebuffer-Boots bleiben startfähig. Ein fehlender oder
  unbrauchbarer DISPI-Adapter darf den normalen VGA-Boot nicht verhindern.
- Der Grafikpfad behauptet keine Fail-operational- oder
  Zertifizierungseigenschaft.

## Vorgesehene Dateien

Die endgültige `allowed_files`-Liste ist beim Eintragen in die Automation zu
fixieren. Erwartet werden ausschließlich:

- `drivers/video/framebuffer.h`
- `drivers/video/framebuffer.c`
- neue Display-Control-Dateien unter `drivers/video/`
- `kernel/proc/process.h`
- `kernel/proc/process.c`
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
2. Native Adaptererkennung sowie feste DISPI-Registerfolge test-first
   spezifizieren; fremde PCI-Identitäten müssen vor dem ersten Seiteneffekt
   scheitern.
3. Begrenzten Display-Control-Treiber mit BAR-Prüfung, DISPI-ID-Prüfung,
   Registerrücklesung und einmaliger Deaktivierung bei Fehler implementieren.
4. Append-only Display-Control-ABI und SDK-Wrapper implementieren.
5. Framebuffer erst nach erfolgreicher Transition initialisieren und den
   VGA-Rollback vervollständigen.
6. `desktop.prg` um einmalige Aktivierung und Workbench-artige Darstellung
   erweitern.
7. QEMU-Gastnachweis für den kompletten Ablauf und den erzwungenen
   DISPI-Fehlerpfad ergänzen.
8. Erst nach bestandenen äußeren Gates Dokumentation und Queue auf den
   nächsten Zustand setzen.

## Akzeptanzkriterien

- Ein Image wird mit `-Video vga` gebaut und erreicht sichtbar `C:\>`.
- `desktop.prg` wechselt dasselbe laufende System ohne Neustart in einen
  validierten Grafikmodus.
- Der serielle Ablauf enthält in dieser Reihenfolge eindeutige Marker für
  VGA-Shell, Modusanforderung, erfolgreichen Framebuffer und `DESKTOP_OK`.
- Die Workbench-artige Oberfläche wird in einem QEMU-Screenshot nachgewiesen.
- Ein absichtlich fehlgeschlagener DISPI-Wechsel liefert einen dokumentierten
  Fehler und hinterlässt eine bedienbare VGA-Shell.
- Ein zweiter Prozess kann während einer laufenden Transition keinen weiteren
  Moduswechsel beginnen.
- Der vorhandene `-Video framebuffer`-Boot startet weiterhin den Desktop.
- Der normale VGA-Pakettest sowie alle bestehenden Display-, Desktop-, Loader-
  und Syscall-Tests bleiben erfolgreich.

## Vorgesehene Gates

### Targeted

```powershell
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

- Adapteridentität und LFB-BAR können vor der Programmierung nicht eindeutig
  und begrenzt validiert werden.
- Ein Moduswechsel wäre nur durch BIOS-Firmware, Real Mode oder allgemeine
  I/O-Rechte für Ring 3 möglich.
- Ein partieller Wechsel könnte unvalidierte Framebuffer-Metadaten sichtbar
  machen.
- Nicht unterstützte Hardware oder ein Vorprüfungsfehler könnte die laufende
  VGA-Console verändern.
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

Der native DISPI-Treiber vergrößert die privilegierte Trusted Computing Base
und unterstützt zunächst nur die exakt geprüfte QEMU-Standard-VGA-Variante.
Der QEMU-Nachweis belegt keine VMware- oder universelle Hardwarekompatibilität.
VMware SVGA, reale Adapter und UEFI GOP benötigen eigene begrenzte Treiber und
separate Arbeitspakete.
