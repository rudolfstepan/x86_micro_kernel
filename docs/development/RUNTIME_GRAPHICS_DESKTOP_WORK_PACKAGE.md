# Arbeitspaket: Laufzeitstart des grafischen Desktops aus VGA

Stand: 18. August 2026  
Status: aktiv, direkte Ausführung im sichtbaren Hauptarbeitsbaum genehmigt

## Kennung

`R1.5-runtime-desktop`

## Ziel

Ein normal mit VGA-Text gestartetes REIST OS muss den Befehl
`desktop.prg` aus der Userspace-Shell annehmen können. Das Programm fordert
über eine versionierte, angehängte Display-ABI einen Grafikmodus an. Ein
nativer, fest begrenzter Treiber programmiert auf QEMU den eindeutig erkannten
Standard-VGA-/Bochs-DISPI-Adapter und unter VMware den SVGA-II-Adapter. Für
Legacy-BIOS-Systeme ohne natives Backend darf ausschließlich ein bereits in
Stage 2 ausgewählter und vollständig validierter VBE-Modus über einen festen
Kernel-Thunks aktiviert werden. Erst danach wird der lineare Framebuffer
veröffentlicht und der Desktop im Stil einer klassischen Amiga Workbench
gezeichnet.

Der weitere, unter VMware sichtbar abgearbeitete Ausbau von Window-Manager,
Compositor und späterer GUI-Client-ABI wird in
[`GRAPHICAL_DESKTOP_WINDOW_MANAGER_WORKFLOW.md`](GRAPHICAL_DESKTOP_WINDOW_MANAGER_WORKFLOW.md)
mit verbindlichen Checkboxen geführt.

Der bestehende Bootmodus `VIDEO=framebuffer` bleibt kompatibel. Ein nicht
unterstützter Adapter oder eine fehlgeschlagene Vorprüfung lässt den laufenden
VGA-Textmodus unverändert; die Shell bleibt bedienbar und erhält einen
eindeutigen Fehlerstatus. Escape deaktiviert einen zur Laufzeit aktivierten
Grafikmodus und stellt die vorhandene VGA-Shell wieder sichtbar her.

## Ausgangslage

- Stage 2 kann VBE 1024x768x32 oder 800x600x32 derzeit während eines
  `VIDEO=framebuffer`-Boots auswählen.
- Bei `VIDEO=vga` wird kein Framebuffer an den Kernel übergeben.
- `desktop.prg` benutzt bereits die geclippte Ring-3-Display-ABI, beendet sich
  ohne Framebuffer aber mit „Grafikmodus nicht verfuegbar“.
- Reale Legacy-BIOS-Grafikkarten besitzen keine einheitliche native
  Registerschnittstelle. Der vom Benutzer freigegebene Kompatibilitätspfad
  kapselt deshalb genau VBE `4F02h`, `4F03h` und Mode 03 in Ring 0; beliebige
  Firmwareaufrufe aus Ring 3 bleiben ausgeschlossen.
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
8. Stage 2 veröffentlicht bei einem VGA-Boot einen versionierten VBE-Handoff,
   ohne den Textmodus zu verlassen. Der Kernel akzeptiert nur diesen Modus,
   dessen Geometrie, Masken, LFB-Bereich und PCI-Display-BAR übereinstimmen.
9. Die bestehende Display-Control-ABI erhält die append-only Operation
   `DEACTIVATE`. QEMU, VMware und VBE kehren damit nach Escape in VGA Mode 03
   beziehungsweise den emulierten Legacy-VGA-Pfad zurück; erst danach wird der
   Framebufferzustand verworfen.
10. Dieselbe Display-Control-ABI erhält append-only `FRAME_BEGIN`,
    `FRAME_COMMIT` und `FRAME_CANCEL`. Eine feste Lease, Seriennummer sowie PID
    plus Prozessgeneration binden jeden Frame; Prozessende verwirft oder
    vervollständigt den bereits validierten Übergang idempotent.
11. Der Ring-3-Compositor verarbeitet Eingaben über typisierte Events, trennt
    Pointer- und Keyboard-Fokus und rekonstruiert höchstens acht Dirty Regions
    mit geclippten Primitiven in Z-Reihenfolge. Controls und externe
    GUI-Clients bleiben außerhalb dieses Pakets.

## Verbindliche Invarianten

- Die öffentliche ABI bleibt append-only; vorhandene Syscalls 44 bis 46 und
  ihre Strukturen ändern Bedeutung und Layout nicht.
- Ring 3 erhält weder I/O-Rechte noch ein LFB-Mapping noch eine Firmware-
  Aufrufschnittstelle.
- Der VBE-Laufzeitpfad ist auf den Loader-Handoff und die festen Operationen
  Grafikmodus setzen, Modus rücklesen und VGA Mode 03 begrenzt. Ring 3 kann
  weder Firmwarefunktion noch Modusnummer bestimmen.
- Vor dem Real-Mode-Aufruf werden der periodische Local-APIC-Timer und beide
  remappten PICs maskiert. Ihre vollständigen vorherigen Zustände werden vor
  dem Wiederherstellen der Interrupt-Flags zurückgeschrieben, damit ein
  firmwareinternes `STI` keinen Kernelvektor über die BIOS-IVT ausführt.
- Native Backends erfordern die exakte PCI-/Registeridentität. VBE erfordert
  zusätzlich, dass der vollständige LFB-Bereich innerhalb eines über die
  standardisierte PCI-BAR-Größenabfrage validierten Display-Class-Memory-BARs
  liegt; ein LFB darf dabei wie bei NVIDIA-VBIOS üblich innerhalb des BARs
  beginnen.
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
- Längere Rasteroperationen bleiben präemptibel. Frame-Zustandsübergänge halten
  nur kurze IRQ-sichere Sperren; ein Task-Ende räumt Draw-Reservation,
  Transaktion und einen unterbrochenen Publish-/Restore-Übergang anhand von PID
  und Generation auf.
- Pointer- und Keyboard-Fokus sind getrennt. Jede Pointer-Button-Sequenz bleibt
  bis Button-Up implizit an ihr ursprüngliches Ziel gebunden.
- VGA- und bestehende Framebuffer-Boots bleiben startfähig. Ein fehlender oder
  unbrauchbarer DISPI-Adapter darf den normalen VGA-Boot nicht verhindern.
- Der Grafikpfad behauptet keine Fail-operational- oder
  Zertifizierungseigenschaft.

## Vorgesehene Dateien

Die endgültige `allowed_files`-Liste ist beim Eintragen in die Automation zu
fixieren. Erwartet werden ausschließlich:

- `drivers/video/framebuffer.h`
- `drivers/video/framebuffer.c`
- `drivers/video/frame_transaction.h`
- `drivers/video/frame_transaction.c`
- `drivers/video/display_control.h`
- `drivers/video/display_control.c`
- `Makefile`
- `arch/x86/boot/bios/stage2_bios.asm`
- `arch/x86/boot/vbe_runtime.asm`
- `arch/x86/boot/vbe_runtime.h`
- neue Display-Control-Dateien unter `drivers/video/`
- `drivers/video/display.c`
- `lib/libc/stdlib.h`
- `kernel/proc/process.h`
- `kernel/proc/process.c`
- `kernel/sched/scheduler.c`
- `kernel/syscall/syscall_table.c`
- `kernel/init/kernel.c`
- `userspace/sdk/include/x86os.h`
- `userspace/sdk/x86os.c`
- `userspace/bin/shell.c`
- `userspace/gui/README.md`
- `userspace/gui/compositor/desktop.c`
- `userspace/gui/compositor/desktop_wm.h`
- `userspace/gui/compositor/desktop_wm.c`
- im Zielabbild `/usr/gui/bin/desktop.prg`; `/DESKTOP.PRG` und der bisherige
  Pfad `/usr/bin/desktop.prg` bleiben ausschließlich feste Kompatibilitätsaliase
- `scripts/build_system_programs.py`
- `scripts/build-windows.ps1`
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
8. Einen erzwungenen VBE-Gastnachweis für Aktivierung und Rückkehr nach VGA
   ergänzen.
9. Erst nach bestandenen äußeren Gates Dokumentation und Queue auf den
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
- Escape stellt nach einer Laufzeitaktivierung die sichtbare VGA-Shell samt
  neuem Prompt wieder her.
- Der erzwungene VBE-Gastnachweis durchläuft VGA, VBE-Desktop und VGA-Rückkehr.
- Der vorhandene `-Video framebuffer`-Boot startet weiterhin den Desktop.
- Der normale VGA-Pakettest sowie alle bestehenden Display-, Desktop-, Loader-
  und Syscall-Tests bleiben erfolgreich.

## Vorgesehene Gates

### Targeted

```powershell
python test/test_display_abi_minimal.py -q
python test/test_desktop_source.py -q
python test/test_runtime_graphics_switch.py -q
python test/test_bios_vbe_source.py -q
```

### Package

```powershell
.\scripts\test-reist-package.ps1 -Target qemu -Video vga
```

### Runtime

```powershell
.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop
.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-vbe
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
- Ein Moduswechsel würde eine frei wählbare Firmwarefunktion, einen
  callergewählten Modus oder allgemeine I/O-Rechte für Ring 3 benötigen.
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
- Drag-and-drop und vollständiges GUI-Clientprotokoll
- frei ladbare Grafiktreiber oder UEFI GOP
- Auflösungsdialog und beliebige dynamische Modi
- direktes Framebuffer-Mapping in Userspace
- gleichzeitige grafische Sitzungen mehrerer Benutzer

Diese Funktionen benötigen nach dem stabilen Laufzeit-Moduswechsel eigene,
begrenzte Arbeitspakete.

Die vom Benutzer freigegebene Erweiterung implementiert zuerst eine
USB-HID-Boot-Maus über den vorhandenen xHCI-Root-Port. Bewegungen, drei Tasten
und optionales Mausrad gelangen über eine feste, generationsgebundene Queue
und den append-only Syscall 110 nach Ring 3. Direkte Hardware- oder
Framebufferrechte werden dabei nicht an den Desktop übertragen.

## Physischer Zwischenstand: ASUS/NVIDIA

Der reale Legacy-BIOS-Test auf dem ASUS-Mainboard mit NVIDIA `10DE:1280`
bestätigt den VBE-Laufzeitpfad für Modus `0x118` bei `1024x768x32`, Pitch
`4096` und LFB `0xF1000000` innerhalb des vermessenen BAR 3 ab
`0xF0000000`. Der Desktop wird aus dem VGA-Textmodus gezeichnet; Escape
beendet ihn und stellt die bedienbare VGA-Shell wieder her.

Dieser Stand ist bewusst noch nicht als Paketabschluss markiert. Auf der
physischen Zielhardware wird der Desktop flackerig und zäh aufgebaut, und die
USB-Maus liefert dort noch keine sichtbare Bewegung. QEMU- und VMware-Nachweise
ersetzen diese beiden offenen Hardwarebefunde nicht. Die nächste Arbeit setzt
deshalb bei begrenzter Präsentation/Dirty-Region-Ausgabe und bei der
xHCI-HID-Mausdiagnose auf dem ASUS-System an.

Die erste Renderoptimierung hält nun bis zu `1024x768x32` in einem festen,
heapfreien Kernel-Backbuffer. Flächen, Glyphen und Cursor werden im normalen
RAM aufgebaut und als geclippte Dirty Rectangles mit 32-Bit-Schreibzugriffen
in den sichtbaren LFB übertragen. Der Laufzeitpfad präsentiert den schwarzen
Initialpuffer nicht mehr unnötig, und `desktop.prg` zeichnet den ersten Frame
nur noch einmal. Größere oder ungewöhnliche Boot-Framebuffer fallen weiterhin
auf den direkten, geclippten Pfad zurück. Der Leistungsgewinn auf dem
ASUS/NVIDIA-System bleibt durch einen erneuten Hardwarelauf zu bestätigen;
PAT/MTRR-Write-Combining ist noch nicht Bestandteil dieses Schritts.

Der anschließende physische Lauf bestätigte den Backbuffer-Gewinn, zeigte aber
weiterhin begrenzte LFB-Übertragungsrate. Der Framebuffer versucht deshalb nun
einen separaten Write-Combining-Pagingpfad. Dieser schaltet PAT-Eintrag 1 nur
nach CPUID-Nachweis für MSR, PAT und SSE auf WC, ordnet die Schreibzugriffe mit
`SFENCE` und fällt auf das unveränderte uncached MMIO-Mapping zurück, wenn die
CPU den Vertrag nicht erfüllt. Der allgemeine MMIO-Pfad bleibt unverändert.

Für das reale ASUS-System akzeptiert der weiter fest begrenzte xHCI-Pfad nun
bis zu 32 Root-Ports, protokolliert die verbundene Portmaske, bewahrt einen
bereits aktiven Controller vor späterem Überschreiben und bevorzugt bei einem
Composite-Boot-HID die Maus-Schnittstelle. Boot-HID-Endpunkte dürfen bis zu 64
Byte Paketgröße ankündigen; ausgewertet werden weiterhin höchstens acht Byte.
Statt nur das erste belegte Root-Port-Gerät zu verwenden, prüft die
Initialisierung höchstens acht verbundene Ports und wählt zuerst eine
Boot-Maus; nur wenn keine gefunden wird, wird die erste Boot-Tastatur erneut
konfiguriert. Jeder Versuch bleibt durch die bestehenden monotonen
Control-Transfer-Deadlines begrenzt.

Da die frühen USB-Meldungen auf realer Hardware vor der Eingabeaufforderung
wegscrollen, hält xHCI zusätzlich einen festen Diagnose-Snapshot ohne Heap und
ohne formatierte IRQ-Ausgabe. Das als `/sbin/usbinfo.prg` in beiden Imagepfaden
paketierte Ring-3-Kommando `USBINFO` liest über einen append-only, rein lesenden
Diagnose-Syscall Controllerarten, letzten xHCI-Zustand, verbundene
Root-Port-Maske, Geräteauswahl, IRQ sowie Transfer-, akzeptierte und verworfene
Mausreports. Damit sind insbesondere ein reines EHCI-System, eine fehlende
Root-Port-Maus und ein konfiguriertes Gerät ohne Interruptreports eindeutig
unterscheidbar. Der Eintrag in der Kernel-Rettungsshell bleibt nur als
Fallback; neue Kommandos gelten erst mit nachgewiesener Erreichbarkeit aus der
normal gestarteten Userspace-Shell als integriert.

Auf dem ASUS/Intel-xHCI-Controller mit 17 Root-Ports deckte `USBINFO` eine
frühe `capabilities-rejected`-Ablehnung auf. Ursache war die vertauschte
Dekodierung der beiden nicht zusammenhängenden Scratchpad-Bitfelder in
`HCSPARAMS2`: Ein kleiner realer Wert wurde dadurch als mindestens 32
Scratchpads interpretiert. Die High-Bits 25:21 und Low-Bits 31:27 werden nun
in Spezifikationsreihenfolge zusammengesetzt; die reservierten Niederbits von
`DBOFF` und `RTSOFF` werden vor der Bereichsprüfung maskiert.

Da derselbe Controller anschließend weiterhin an der Capability-Grenze
abgewiesen wurde, unterstützt der feste Scratchpad-Pool nun bis zu 32 statt
acht Seiten. Das bleibt statisch und kapazitätsbegrenzt, deckt aber frühe
Intel-xHCI-Implementierungen mit mehr als acht Scratchpads ab. Diagnose-ABI
Version 2 hängt `CAPLENGTH`, `MaxSlots`, die dekodierte Scratchpad-Anzahl,
`DBOFF`, `RTSOFF` und eine bitgenaue Ablehnungsmaske an. Version-1-Aufrufer
erhalten weiterhin exakt den bisherigen 96-Byte-Snapshot.

Der folgende Hardwarelauf erreichte den Controllerstart (`reject=0`), meldete
aber bei zwei vorhandenen EHCI-Controllern alle 17 xHCI-Root-Ports als
getrennt. Für Intel-Controller mit erkanntem Intel-EHCI-Companion übernimmt
der xHCI-Pfad deshalb vor dem Controllerstart die umschaltbaren Ports über die
PCI-Konfigurationsregister `USB3PRM`/`USB3_PSSEN` und
`USB2PRM`/`XUSB2PR`. SuperSpeed-Terminierungen werden zuerst aktiviert, erst
danach folgen die USB-2-Datenleitungen. Alle Masken werden vor dem Schreiben
gelesen, nach dem Schreiben rückgelesen und bei fehlender Bestätigung wird der
Start mit eigenem Diagnosezustand geschlossen abgebrochen. Die bekannte
Sony-VAIO-Ausnahme `104D:90A8` bleibt unangetastet. Ein monoton und zusätzlich
durch Pollanzahl begrenztes 500-ms-Fenster erlaubt anschließend die durch das
Umschalten ausgelöste Neuverbindung. Diagnose-ABI Version 3 hängt PCI-ID,
Routingflags sowie Soll- und Istmasken an; Versionen 1 und 2 behalten ihre
bisherigen Größen von 96 und 120 Byte.

Der erste Lauf mit aktivierter Intel-Portübergabe identifizierte den realen
Controller als `8086:8C31`, blieb aber unmittelbar vor dem ersten xHCI-Reset
stehen. Der Ownership-Handoff adressierte nun zwar korrekt `USBLEGSUP`, ließ
jedoch die SMI-Enable-Bits im direkt folgenden `USBLEGCTLSTS` aktiv. Vor der
Portübergabe werden deshalb USB-, Hostfehler-, Ownership-, PCI-Command- und
BAR-SMIs abgeschaltet, die drei RW1C-Ereignisse quittiert und das Ergebnis
rückgelesen. Reservierte und schreibgeschützte Felder bleiben gemäß xHCI-
Legacy-Vertrag erhalten. Gleichzeitig folgt die Suche nach erweiterten
Capabilities nun dem relativ zur aktuellen Capability kodierten Next-Zeiger
mit MMIO- und Besuchsgrenze. Der erste Controllerreset protokolliert
`halt-request`, `host-reset-request`, `controller-ready-wait` und `complete`,
sodass ein verbleibender physischer Stillstand ohne weiteren Verdachtslauf
einer exakten Registerphase zugeordnet werden kann.

Nach erfolgreicher Portübergabe fiel die USB-Tastatur aus, weil der damalige
xHCI-Vertrag nur ein einziges Boot-HID veröffentlichte und die Maus bevorzugte.
Außerdem setzte die Portsuche den gesamten Controller zwischen Kandidaten
zurück und verwarf damit ein bereits adressiertes Gerät. Der Treiber verwaltet
nun zwei feste, heapfreie HID-Ressourcensätze mit getrennten Slots, Device-
Contexts, EP0-/Interrupt-Ringen und Reports: höchstens eine Boot-Tastatur und
eine Boot-Maus an getrennten Root-Ports. Der Controller wird für die gesamte
Suche nur einmal gestartet; nicht passende Kandidaten werden mit `Disable
Slot` nach bestätigtem Command-Completion freigegeben. Das 500-ms-Fenster nach
Intel-Routing sammelt alle sichtbar werdenden Root-Ports, statt beim ersten
Gerät vorzeitig zurückzukehren.

Diagnose-ABI Version 4 hängt für Tastatur und Maus jeweils Root-Port, Slot und
Endpoint sowie Zähler für akzeptierte und verworfene Tastaturreports an und
besitzt den Zustand `keyboard-mouse-ready`. Versionen 1 bis 3 behalten ihre
bisherigen Größen. Kernel-Rettungsshell und das paketierte
`/sbin/usbinfo.prg` zeigen dieselben neuen Felder. Der reale ASUS-Nachweis für
gleichzeitige Tastatur- und Mausreports steht noch aus.

Der erste Lauf dieses Dual-HID-Stands zeigte weiterhin keine Eingabe in der
normalen Userspace-Shell. Die Enumeration war nicht der einzige Pfadfehler:
Der blockierende gemeinsame `getchar()`-Leser wartete zwar mit einer
10-ms-Deadline auf PS/2-Eingabe, rief den vorhandenen xHCI-Poll-Fallback aber
nicht auf. USB-Tastaturreports hingen damit auf realer Hardware vollständig
von einem funktionierenden Legacy-PCI-IRQ ab. Der Leser leert nun vor jeder
atomaren Queue-Prüfung auch den begrenzten xHCI-Eventring; das geschieht vor
dem Queue-Lock, da die Reportauswertung über `kb_submit_key_event()` in genau
diese Queue publiziert. IRQ-Betrieb bleibt erhalten, Polling ist nur der
bereits zeitlich begrenzte Ausweichpfad.

Damit ein weiterer realer Lauf auch bei ausbleibender Eingabe verwertbar ist,
zeigt die normale Userspace-Shell vor dem ersten Prompt selbsttätig eine
kompakte USB-Tastaturdiagnose. Bei erfolgreicher Enumeration enthält sie Port,
Slot, Endpoint sowie akzeptierte und verworfene Reports; andernfalls Zustand,
verbundene Portmaske und Anzahl der Enumerationsversuche. Dafür ist kein
Tastendruck und keine Rettungsshell erforderlich.

Der darauf folgende reale Lauf blieb ebenfalls ohne USB-Tastatureingabe. Eine
Prüfung der HID-Interrupt-Endpunktkonfiguration gegen den xHCI-Vertrag zeigte
vier zusammenhängende Fehler, die ein emulierter Controller tolerieren kann:
Der Slot-Kontext wurde aus dem alten Input- statt aus dem vom Controller
aktualisierten Device-Kontext kopiert, `Context Entries` blieb trotz neuem
HID-Endpunkt auf EP0 begrenzt, `Max ESIT Payload` war null und `bInterval`
wurde ohne geschwindigkeitsabhängige Umrechnung als xHCI-Exponent verwendet.
Die Konfiguration kopiert nun den autoritativen Output-Slot-Kontext, setzt den
höchsten gültigen Device Context Index, trägt die begrenzte Paketgröße als
periodische ESIT-Nutzlast ein und kodiert High-/Super-Speed sowie
Full-/Low-Speed-Intervalle nach ihren jeweiligen Regeln.

Diagnose-ABI Version 5 hängt die genaue Enumerationsfehlerstufe, den zuletzt
untersuchten Root-Port samt Geschwindigkeit, die Geräteklasse und die Länge
des Konfigurationsdeskriptors an. Version 4 behält dabei unverändert ihre
Größe von 180 Bytes. Die normale Userspace-Shell zeigt diese Werte bereits vor
dem ersten Prompt; `usbinfo.prg` und der gleichnamige Befehl der
Kernel-Rettungsshell liefern dieselbe Diagnose. Damit unterscheidet der nächste
reale Lauf ohne Tastatureingabe unmittelbar zwischen Portreset, Address Device,
Deskriptorabruf, HID-Erkennung, Endpoint-Konfiguration und Set Configuration.

Eine zwischenzeitlich ergänzte physische VMware-HID-Durchreichung wurde wieder
vollständig entfernt: Sie konnte Host-Tastatur und -Maus exklusiv an die VM
binden und damit das Beenden einer fehlerhaften Gastinstanz verhindern. Die
generierte VMX erzwingt `usb.generic.allowHID=FALSE`, enthält weder Autoconnect-
noch HID-Quirk-Regeln und verwendet ausschließlich die virtuelle PS/2-Tastatur
sowie VMwares virtuelle xHCI-Maus. Intels `8086:8c31`-Register, Timing und
physische HID-Geräte bleiben prinzipbedingt Nachweise auf dem ASUS-Board.

Das am Entwicklungsrechner verifizierte reale Testkeyboard meldet sich als
BY-Tech/AULA-Gaming-Keyboard `258A:010C` mit 12 Mbit/s. Es ist ein
Composite-Gerät: Interface 0 ist HID Boot Keyboard (`03/01/01`), Interface 1
ist ein zusätzliches HID für LED-Steuerung, Lautstärkedrehregler und weitere
Herstellerfunktionen. Interface 0 verwendet Interrupt-IN `0x81` mit acht
Bytes, Interface 1 Interrupt-IN `0x82` mit 16 Bytes; der Konfigurationsdeskriptor
ist 59 Bytes lang. Der Basistreiber wählt ausschließlich Interface 0 und
ignoriert das Zusatzinterface zunächst. Das Gerät meldet bereits
`bMaxPacketSize0=8`; `Evaluate Context` ist für dieses konkrete Keyboard daher
nicht erforderlich. Weicht die gemeldete Größe bei einem anderen Full-Speed-
Gerät ab, bleibt die zuvor ergänzte, begrenzte EP0-Aktualisierung aktiv.

Der auf dem ASUS-System beobachtete Completion Code 12 kann außerdem entstehen,
wenn `Address Device` beginnt, während der physische USB2-Port noch im Reset
steht. Der alte Pfad wartete nur auf `PED`; dieses Bit konnte bereits vor dem
Reset gesetzt sein. Zusätzlich schrieb er den vollständigen `PORTSC`-Snapshot
zurück, obwohl `PED` ein RW1CS- und die Änderungsfelder RW1C-Bits sind. Der
Portreset verwendet nun einen neutralisierten Kontrollwert, löscht nur wirklich
gesetzte Änderungsbits, wartet begrenzt auf `PR=0`, `PRC=1`, `PED=1` und `CCS=1`
und hält danach die vorgeschriebene 10-ms-Recovery-Zeit ein. Erst dann darf die
Slot-Adressierung starten.

Der darauffolgende ASUS-Lauf erreichte beide verbundenen Geräte und grenzte den
Fehler auf `GET_DESCRIPTOR(Device, 8)` ein: Port 4 meldete Full-Speed, zunächst
erschien Completion Code 13 (`Short Packet`), nach einer unvollständigen
Einzelkorrektur nur noch ein Timeout (`cc=0`). Der vollständige Vergleich mit
den Linux- und U-Boot-xHCI-Pfaden zeigte daraufhin vier zusammenhängende
Abweichungen im Control-TD-Vertrag: Setup und Data trugen fälschlich `CHAIN`,
dem IN-Data-TRB fehlte `ISP`, ein Short-Packet-Ereignis am Data Stage wurde
verworfen statt bis zum Status Stage weiterverfolgt, und Setup wurde nicht als
letztes atomar an den Controller übergeben. Der Pfad bildet nun die feste
Setup/Data/Status-Folge ohne `CHAIN`, mit `ISP` für IN, getrennt ausgewerteten
Data- und Status-Ereignissen sowie einem Speicherbarriere-geschützten Cycle-
Handoff ab. Doorbell-Schreibzugriffe werden rückgelesen. Rest- und Istlängen
bleiben begrenzt validiert, und der IN-Puffer wird vor jeder Übertragung
gelöscht.

Nach dieser Korrektur meldete der ASUS-Lauf zwar
`ready port=3 slot=1 endpoint=5`, aber auch nach Tastendrücken weiterhin
`reports=0`. Endpoint 5 ist der xHCI-DCI für USB-Endpoint `0x82`; genau diesen
verwendet die Boot-Keyboard-Nebenschnittstelle der Composite-Gaming-Maus
`258A:0027`, während deren primäre Schnittstelle 0 eine Boot-Maus ist. Der
bisherige Parser bevorzugte auf einem Gerät bedingungslos jedes gefundene
Keyboard-Protokoll und belegte deshalb die einzige Tastaturressource mit den
Zusatzknöpfen der Maus. Das eigentliche Keyboard am zweiten Root-Port wurde
anschließend übersprungen. Sind Maus und Tastatur auf demselben Composite-
Gerät noch beide zulässig, wird nun die primäre, niedriger nummerierte Boot-
Schnittstelle gewählt. Damit belegt Port 3 die Mausressource und die Suche
kann das echte Keyboard am nächsten Root-Port veröffentlichen. Einzelne
Keyboard- oder Mausgeräte bleiben unverändert. Interrupt-IN-TRBs tragen
außerdem wie Control-IN-TRBs `ISP`, damit auch kurze HID-Reports sicher ein
Transferereignis erzeugen. Der anschließende QEMU-Dual-HID-Nachweis mit rein
virtueller USB-Tastatur und -Maus erreichte 17 Transfers, 16 akzeptierte
Tastaturreports und einen akzeptierten Mausreport ohne Verwerfung. Es wurde
kein Host-HID-Gerät durchgereicht.

Ein zweites, einfaches USB-Bootkeyboard funktionierte auf demselben ASUS-
Stand, während das AULA/BY-Tech-Composite-Keyboard `258A:010C` weiterhin
keine Tastendrücke lieferte. Damit sind Controller, zweiter HID-Ressourcensatz
und Keyboard-Reportparser praktisch bestätigt; die Abweichung liegt in der
geräteabhängigen HID-Initialisierung. Interface 0 des AULA meldet den
standardmäßigen acht Byte langen Bootkeyboard-Report. Nach
`SET_CONFIGURATION` und `SET_PROTOCOL(Boot)` sendet der Treiber für
Bootkeyboards nun zusätzlich das standardisierte `SET_IDLE` mit einem
begrenzten 40-ms-Intervall. Dieser von Referenz-Bootkeyboard-Treibern
verwendete Startablauf deckt Firmware ab, die vor Interrupt-IN-Reports eine
explizite Idle-Programmierung verlangt; Mäuse und bereits funktionierende
einfache Keyboards behalten ihren bisherigen Datenpfad.

### Offener Hardwarebug: AULA/BY-Tech `258A:010C`

Der anschließende physische ASUS-Retest blieb auch mit `SET_IDLE` ohne
verwendbare Eingabe des AULA-Keyboards. Der Fehler ist damit ausdrücklich
offen; dieser Stand beansprucht keine Unterstützung für dieses Gerät. Die
USB-Maus und ein anderes einfaches USB-Bootkeyboard funktionieren am selben
Mainboard, ebenso Tastatur und Maus im QEMU-Dual-HID-Test. Der bestätigte
allgemeine xHCI-Pfad bleibt deshalb erhalten.

Eine spätere Bearbeitung beginnt nicht mit weiteren Initialisierungsversuchen,
sondern mit einer festen, begrenzten Diagnose je HID-Ressource: erster
Interrupt-Completion-Code, Restlänge und höchstens die ersten acht Reportbytes
für Slot und DCI des AULA-Interface 0. Erst dieser Nachweis entscheidet, ob der
Controller keinen Transfer abschließt, das Gerät den Bootmodus ignoriert oder
ein abweichendes Reportformat liefert. Diagnoseausgabe erfolgt weiterhin nur
aus Task-Kontext; Host-HID-Durchreichung in VMware bleibt verboten.

## Erwartetes Restrisiko

Der native Treiber und der feste VBE-Thunks vergrößern die privilegierte
Trusted Computing Base. Für
QEMU wird Bochs-DISPI, für VMware Workstation das PCI-Gerät VMware SVGA II
unterstützt. Beide Backends bleiben bewusst auf einen festen 32-Bit-Modus
begrenzt. Der VBE-Aufruf selbst kann von fehlerhafter Ziel-Firmware nicht durch
eine Kernel-Deadline abgebrochen werden und ist deshalb ein expliziter
Legacy-Kompatibilitätspfad, keine Hochsicherheitsgarantie. UEFI GOP benötigt
weiterhin einen eigenen Pfad.

Der VMware-Nachweis erfolgt mit dem vorhandenen Legacy-BIOS-Paket:

```powershell
.\scripts\build-windows.ps1 -Target vmware -Video vga
.\build\vmware\reist-os\START-VMWARE.cmd
```

Nach dem Boot wird `desktop.prg` an der Shell gestartet. Das serielle
Protokoll `build\vmware\reist-os\vmware-serial.log` bleibt die Diagnosequelle;
die VM muss für einen Neubau ausgeschaltet sein.
