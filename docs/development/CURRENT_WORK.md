# REIST OS – aktueller Arbeitsstand

Stand: 28. August 2026

Branch/Startpunkt: `working_branch` / `0deb7be`

Aktives Thema: R8.1a – isolierter x86_64-Long-Mode-Bootstrap

R8.1a beginnt den Dual-Architekturpfad. Der vorhandene i386-Kernel samt
Userspace, BIOS-Images, VMware-/Hardwarepaketen und Installern bleibt der
unveränderte Standard und Fallback. Ein getrenntes Bootstrap-Artefakt soll
zunächst ausschließlich den geprüften Übergang von Multiboot-32-Bit-Code in
IA-32e Long Mode nachweisen. Ein vollständiger x86_64-Kernel, ELF64-Prozesse
und 64-Bit-Userspace sind ausdrücklich spätere Pakete. R6.2o bleibt als
nächstes Paket geordnet in der Queue.

R7.1a ist abgeschlossen. Das neue
`BENCHMARK.PRG` misst CPU, RAM, sequentielle VFS-/Datentraegerzugriffe und den
vollstaendigen VGA-Framebufferpfad mit festen Arbeitsgrenzen. Es verwendet nur
oeffentliche Ring-3-ABIs, schreibt ausschliesslich eine eigene temporaere Datei
und gibt die Ergebnisse nach Wiederherstellung der Textkonsole als feste
ASCII-Tabelle aus. Der Quell-/Layoutvertrag bestand 5 Tests, das freestanding
i386-Programm linkte als 20-KiB-MYPR und der QEMU-VGA-Paketbuild bestand in 36
Sekunden; der finale inkrementelle Paketnachweis nach exklusivem CREATE bestand
in 10 Sekunden ohne VM-Start. R6.2o ist wieder aktiv.

Diese Datei ist der kompakte Wiedereinstiegspunkt. Maßgeblich bleiben Code,
Tests und der lokale Diff.

## Auftrag und Grenzen

- Änderungen direkt im sichtbaren lokalen Haupt-Worktree umsetzen.
- Keine Subagenten, isolierten Klone oder zusätzlichen Worktrees verwenden.
- Automatisierte Laufzeittests nur mit QEMU und VMware; echte Hardware prüft
  der Benutzer.
- Reguläre Kernel- und Ring-3-Dienste bleiben CPU-0-affin, bis die
  verbleibenden gemeinsamen Treiberzustände für R6.2 auditiert sind.
- Der begrenzte SMP-Bootstrap ist als `ad8884e8` committed. Die aktuelle
  R6.2-Scheibe baut direkt darauf auf.

## Abgeschlossener Memory-Resilience-PoC

R2.2ai ist mit erhaltener Audio-/Surface-Implementierung beendet, aber nicht
als vollständig bestanden markiert: Alle gezielten Gates und beide
Paketbuilds bestanden, der manuelle VMware-Test zeigt keine Audiostörung, doch
der kombinierte QEMU-Gate endet beim ersten retained Control-Gallery-Frame mit
`DESKTOP_AUDIO_FAIL launch status=-110`. Die Queue dokumentiert das Paket
deshalb als `cancelled` statt `done`.

R1.2a ist abgeschlossen. Vier explizite kernel-eigene 4096-Byte-Objekte
besitzen je zwei Copy-on-write-Bänke in zwei aktiven simulierten
Fehlerdomänen, generationsgebundene Critical-Object-Metadaten und CRC32 für
die Nutzdaten. Der Host-Fault-Test besteht Commit-Unterbrechungen,
Einzeldomänen- und Doppelverlust, stale Handles, CRC-/Konfliktfälle und den
festen Rebuild. Der normale QEMU-VGA-Paketbuild linkt das Modul erfolgreich.

R1.2b ist ebenfalls abgeschlossen. Ein spezieller Testbuild führt die feste
Kampagne einmal nach den Speichertests und vor allgemeiner Prozessaufnahme aus.
Der QEMU-Gast validiert geordnet Commit, degradierte committed Daten, ein
unabhängiges Objekt, Rebuild und HEALTHY, erreicht danach Shell, Userspace und
`TEST_OK` und bleibt innerhalb einer festen 180-Sekunden-Grenze. Der
Desktop-Autostart ist inzwischen für alle Targets entfernt: Jeder normale und
jeder Proof-Boot erreicht zuerst die Ring-3-Shell; `DESKTOP` bleibt ein
ausdrücklicher Benutzerbefehl. Auch diese Stufe erteilt keine User-, DMA- oder
Paging-Autorität und behauptet keine physische DIMM-/Rank-/Channel-Isolation.

R5.2x ist abgeschlossen. Die append-only USB-Diagnose
Version 7 persistiert das exakte Setup-Paket und den terminalen Eventzustand.
Completion Code 13 wird nur bei einem host-to-device Zero-Data-Request ohne
Data-TRB, mit Eventzeiger auf die Status-TRB und Restlänge null angenommen.
Alle anderen Shorts sowie fehlgeschlagenes `SET_CONFIGURATION` oder
`SET_PROTOCOL` bleiben fail-closed. Die USB-Tastaturtests bestehen 9/9, die
Maustests 16/16, das nach der Kernel-Log-/ABI-Erweiterung vollständig neu
erzeugte VMware-VGA-Paket bestand in 153 Sekunden; der abschließende
inkrementelle Paketnachweis nach der Pointer-Überlaufhärtung bestand in 16
Sekunden und der echte
VMware-RFB-Mauspfad in 14 Sekunden. Der Runtime-Nachweis wartet zuerst auf die
Ring-3-Shell, gibt erst dann `DESKTOP` ein und verwirft jeden Lauf, in dem
Desktopmarker bereits vor diesem ausdrücklichen Befehl erscheinen.

Ein abschließender automatisierter VMware-Nachlauf konnte die Paket-VM auf dem
Host nicht starten und erreichte den Gast daher nicht. Der Benutzer startete
das erzeugte VMware-Paket anschließend manuell und bestätigte dessen
fehlerfreien Betrieb. Diese manuelle Abnahme ergänzt den zuvor bestandenen
automatisierten RFB-Lauf; sie wird nicht als automatischer Gate-Erfolg
ausgegeben.

Der erfolgreich physisch getestete Kandidat liegt unter
`build/r5.2x-physical-test/reist-os-r5.2x.img` (64 MiB, SHA-256
`DBA5E5794405180CC11CBCB3FDB2BF81FDA001B2206BFF17700ED29135EDA3C3`). Der
`real_hw/vga`-Build erzeugte das Image vollständig; RSA-PSS-Signatur und
Bootmanifest bestanden. Als begrenzter Hardware-Diagnosekandidat wurde der
Release-SBOM-Schritt für den verschachtelten Testordner ausdrücklich
übersprungen; das Image ist daher kein Releaseartefakt. Der ASUS-Nachtest
bestätigt nun die USB-Tastatur am USB-3/xHCI-Port und den paged `DMESG`-Zugriff.
Die physische Paketabnahme ist damit für diese konkrete Hardwarekombination
erfüllt; eine breite xHCI-Freigabe wird nicht behauptet.

Der ASUS-Nachtest bestätigt die Tastatur am separaten USB-2.0-Controller. Am
USB-3.0-Port wurde sie zunächst nicht nutzbar, funktionierte jedoch parallel,
sobald eine PS/2-Tastatur angeschlossen war. Das grenzt den Restfehler auf eine
Bootzeitabhängigkeit des xHCI-Pfads ein. Der feste 500-ms-Zeitraum zum Sammeln
sichtbar werdender Root-Ports gilt deshalb jetzt für jeden xHCI-Controller und
nicht mehr nur nach Intel-Companion-Routing. Tastatur, Boot-HID-Parser und
allgemeiner Eingabepfad bleiben unverändert. Der nie
freigegebene Desktop-Autostart ist ausnahmslos aus dem Kernelboot aller Images
entfernt, damit die physische Diagnose sichtbar bleibt. Ein nachfolgend
erfolgreicher Mauskandidat darf den vorherigen Tastatur-Control-Fehler nicht
mehr löschen. Die Shell-Startanzeige gibt beim Ausfall nun ohne Eingabe auch
Setup-Paket, Completion, Restlänge, Eventstufe und Flags aus. Nur bei einer
nicht bereiten USB-Tastatur ergänzt sie weiterhin eine kompakte Startdiagnose.
Unabhängig vom 80x25-Scrollback hält ein neuer fester 32-KiB-Kernel-Logring alle
Ring-0-Konsolenausgaben im Speicher. Der append-only Syscall 125 und das
Ring-3-Programm `DMESG` lesen einen begrenzten Snapshot; `DMESG` pausiert
standardmäßig nach 22 Zeilen mit Leertaste, Enter oder Q. Der Host-Ringtest
besteht Wrap, Stale-Cursor und begrenzte Reads. Der erste ASUS-Kandidat wies
den Syscall wegen der veralteten exklusiven Prozessprofilgrenze 125 mit `-13`
ab; sie ist nun append-only auf 126 angehoben, und `DMESG` zeigt künftige
Fehlercodes an. Der inkrementelle `real_hw/vga`-Build umfasst 104 Kernelobjekte,
baute `DMESG.PRG` neu und bestand Signatur- sowie Bootmanifestprüfung.

Jeder `real_hw`-Build veröffentlicht künftig unabhängig vom internen
Ausgabeordner zusätzlich `build/reist-os-real-hw.img`. Die Samsung- und
Fujitsu-Installer verwenden ausschließlich diesen nach einer zweiten
Manifestprüfung atomar veröffentlichten Pfad. QEMU- und VMware-Builds können
das physische Installerartefakt dadurch nicht überschreiben.
Das aktuell validierte 64-MiB-Artefakt hat SHA-256
`BF774039CF11370093B49E4E0D20094FB1315E15E440E84E6778B23F0E4DBFE9`.

Ein weiterer ASUS-Kaltstart mit demselben Image zeigte eine verbleibende
Zeitabhängigkeit: Ohne gleichzeitig angeschlossene PS/2-Tastatur endete bereits
der erste acht Byte große `GET_DESCRIPTOR(Device)`-Request ohne übernommenes
Transfer-Event (`cc=0`, `residual=8`, `stage=0`, Timeout/Failed). Mit PS/2 war
derselbe USB-Pfad nutzbar. R5.2y ergänzt deshalb nach erfolgreichem xHCI
`Address Device` die feste USB-2.0-SetAddress-Recovery vor dem ersten
EP0-Doorbell; fehlende Events und Descriptor-Shorts bleiben fail-closed.
R6.2o bleibt bis zu diesem Hardware-Nachtest geordnet in der Queue.

Der R5.2y-Kandidat besteht 10 Tastatur- und 16 Maustests sowie den
VMware-VGA-Paketbuild in 15 Sekunden. Der vollständige `real_hw/vga`-Build
veröffentlichte nach zwei erfolgreichen Bootmanifestprüfungen das 64-MiB-Image
`build/reist-os-real-hw.img` mit SHA-256
`93E416F48327CA62E7056B6CD9D791D983E62782A0B3318B2A83B6488691B8A5`.
Die ASUS-Abnahme startete dieses Image ohne angeschlossene PS/2-Tastatur kalt;
die USB-Tastatur funktioniert am betroffenen xHCI-Port. R5.2y ist damit
abgeschlossen. Die Bestätigung gilt für diese Controller-/Gerätekombination
und ist keine breite USB-Hardwarefreigabe.

## Erreichter stabiler Meilenstein

- ACPI/MADT-Erkennung, AP-Trampolin und begrenzter INIT/SIPI-Start für bis zu
  15 APs.
- Private AP-GDTs, Runtime-/Double-Fault-TSS, guard-page-geschützte Stacks und
  CPU-lokaler IRQ-, Präemptions-, Adressraum- und Schedulerzustand.
- CPU-besitzende Spinlocks, sleepfähige deadlinebegrenzte Kernelmutexe,
  atomarer Runqueue-Handoff und generationsgebundener TLB-Shootdown.
- Serialisierte Waitqueue-, IPC-, Prozess-, Socket-, VFS-, FAT32-, ATA-,
  AHCI-, FDD- und Storage-Pool-Pfade.
- Drei AP-affine Scheduler-, Mutex-, Integritäts- und Storage-Probetasks werden
  nach Abschluss generationsgeprüft reap't.
- 48 feste Kernelstack-Slots erhalten die 32 öffentlichen Taskslots auch bei
  maximal 15 AP-Idle-Stacks. `CAPWAIT.PRG` belegt im Gast alle 32 Taskslots
  gleichzeitig und liefert `TASK_CAPACITY_OK`.
- Der Unicode-Desktopprobe führt nach Capability-Aktivierung einen realen,
  begrenzten VMware-SVGA2D-`RECT_COPY` aus.
- IRQ-Kontextdiagnosen speichern den Vektor CPU-lokal. Eine rekursive
  Spinlock-Panic enthält Lockadresse, CPU und symbolisierbare Aufruferadresse.

## Verifizierte Evidenz

- `python test/test_smp.py -v` – 24/24
- `python test/test_sync_diagnostics_r13.py -v` – 28/28
- `python test/test_block_transactions_r13.py -v` – 10/10
- `python test/test_vmware_svga2d.py -v` – 16/16
- `python test/test_qemu_smoke.py -v` – 41/41
- Systemprogramm-Toolchaintest – 1/1
- `scripts/build-windows.ps1 -Target qemu -Video vga -SkipReleaseSbom` – PASS
- Vier aufeinanderfolgende vollständige QEMU-SMP4-Läufe nach der
  Lockdiagnose – PASS; der letzte zusätzlich mit VMware-SVGA2D.
- `python test/test_usb_keyboard.py -v` – 5/5
- `python test/test_usb_mouse.py -v` – 14/14
- `python test/test_reist_supervisor.py -v` – 9/9
- `python test/test_vmware_mouse.py -v` – 6/6
- `python test/test_desktop_source.py -v` – 41/41
- VMware-VGA-Paket – PASS in 14 Sekunden; `vmware-mouse`-Runtime – PASS in
  13 Sekunden mit geordneten Markern `USB: xHCI HID ready`,
  `REIST_SMP SCHEDULER_READY cpus=4`, `COMPOSITOR_READY`, `DESKTOP_OK`,
  `DESKTOP_EXPLORER_OK`, `COMPOSITOR_AP_EXEC` und `DESKTOP_MOUSE_OK`.

Referenzlauf:

```powershell
python scripts/run_qemu_smoke.py --image build/reist-os.img --smp 4 --expect-smp --vmware-vga --expect-svga2d --timeout 120 --log build/codex-agent/smp4-final-low-rect.log
```

Er enthält `online=4`, alle SMP-READY-Marker, `REAP_READY`,
`TASK_CAPACITY_OK`, `SVGA2D_RECT_COPY_OK` und `TEST_OK`, ohne Panic,
Assertion oder unbekannten FIFO-Befehl.

## VMware-Maus und Explorer wiederhergestellt

Der virtuelle VMware-xHCI-Boot-Mausendpunkt kodiert Max ESIT Payload nun in
den standardisierten Endpoint-Context-Feldern statt den Average-TRB-Wert in
das High-Payload-Feld zu duplizieren. Der überwachte Compositor darf den exakt
identifizierten Displaydienst verbinden. Seine Pointer-Präsentation hält die
Präemption nicht mehr über dem sleepfähigen Displaymutex; die bestehende
Frame-Reservierung bleibt die Veröffentlichungsgrenze.

Das Default-Deny-Compositorprofil besitzt zusätzlich nur den begrenzten
Submit-/Collect-/Cancel-/Bulk-Transport für serviceeigene VFS-Shadow-Objekte.
Der Syscall weist für diese Domäne Raw-Block-, Format- und Maintenance-
Operationen vor jeder Veröffentlichung ab. Damit öffnet der initiale Explorer
`/` wieder. Ein deadlinebegrenzter VMware-Runner startet ausschließlich bei
leerem Workstation-Laufzustand, speist eine virtuelle Bewegung ein, verlangt
die geordneten Explorer-/Mausmarker und beendet genau seinen einzigen VMX-
Prozess. Der Benutzer bestätigte die Maus zusätzlich sichtbar; der frische
Gast bestätigte den Root-Explorer über den Erfolgsmarker.

## R6.2n abgeschlossen

Der veröffentlichte xHCI-Eventring, die Diagnosesnapshots und die
generationsgebundenen HID-Tastatur-/Mauszustände werden als kurze, feste
IRQ-sichere SMP-Transaktionen serialisiert. Die deadlinegebundene
Controllerkonstruktion bleibt davon getrennt und vollständig BSP-only. Danach
darf genau die gesunde aktuelle Compositorgeneration ihre im geschützten
Kontrollrecord abgelegte Einmal-AP-Maske nach `SERVICE_READY` übernehmen.

Die Abnahme verwendet die generierte VMware-Referenz mit vier vCPUs. Legacy-
PIC-xHCI-IRQ bleibt auf CPU 0; der Compositor muss vor der virtuellen
Mauszustellung AP-Ausführung melden. Die virtuelle Bewegung kommt
deterministisch über einen ausschließlich an `127.0.0.1:5909` gebundenen
RFB-3.8-Pointerevent; physisches HID-Passthrough bleibt verboten. Das Paket-
Gate bestand in 14 Sekunden, der Vier-vCPU-Runtime-Nachweis in 13 Sekunden.
Restart-Affinität wurde nicht implizit übernommen und benötigt einen
getrennten Fehlerlauf.

## Restrisiko und nächster zusammenhängender Schritt

Der physische Fehler `VFS_ERR_IO` beim Laden von
`/usr/gui/bin/desktop.prg` trat nur auf dem AMD-Mainboard auf. Dasselbe Image
startet den Desktop auf dem unterstützten ASUS-Board ohne diesen Fehler. Das
AMD-Board wird derzeit nicht weiter benutzt; deshalb bleiben R2.2af, R2.2ag
und R2.2ah ohne Produktionskandidat abgebrochen. Insbesondere wird aus dem
AMD-Befund keine allgemeine Storage-/Startup-Admission-Änderung abgeleitet.

Die danach unter VMware und Zielhardware beobachteten Desktopausfälle beim
Abspielen einer WAV-Datei und beim Start der alten Control Gallery haben
dieselbe Quellursache: Beide Programme waren synchron gestartete
Vollbildclients. Der Compositor blockierte deshalb in `x86os_wait()` und
verpasste seinen 500-ms-Heartbeat; nach zwei Sekunden wurden nacheinander die
drei Restartbudgets verbraucht. R2.2ai migriert beide Programme auf den
delegierten Surface-Vertrag und beweist gleichzeitig die einmalige echte
Wiedergabe des paketierten Startklangs, beide aktiven Clients und fortlaufende
Compositor-Heartbeats.

Zum selben Audio-Lifecycle gehören sechs eigenständig erzeugte, unter CC0
freigegebene Systemklänge. `reist.sounds/1` ordnet Start, Ende, Fehler
und Benachrichtigung sowie erfolgreiches Papierkorb-Drop und endgültiges
Leeren festen WAV-Pfaden oder `none` zu; der bestehende
Konfigurationsdienst ist bereits die spätere Mutationsgrenze für die
Systemsteuerung. Der Desktop hält keine Audio-Capability, sondern startet
höchstens zwei generationengeprüfte `wavplay`-Kinder und wartet nie auf ein
lebendes Kind. Ein sauberer Clientwechsel benötigt das antwortlose
Audio-`RELEASE`, den tatsächlichen Peer-Entzug und einen zusätzlich in Ring 0
geprüften leeren IPC-Endpunkt; ein Crash behält die vollständige
Servicegenerationrotation.

Der VMware-Befund beim Doppelklick auf WAV-Dateien war keine HDA-Störung:
Ordneröffnungen und erfolgreiche Programmstarts lösten fälschlich
`event.notification` aus. Das kurzlebige `wavplay`-Kind belegte dadurch den
einzelnen Audio-Clientendpunkt, während der Sound Player denselben Endpunkt
öffnen wollte. Navigation und erfolgreiche Datei-/Programmaktivierung bleiben
nun still; Benachrichtigungsklang ist echten Informationsdialogen vorbehalten.
Der danach weiterhin beobachtete Fehler war im Seriellog eindeutig
`SOUNDPLAYER_AUDIO_FAIL stage=start status=-110`: Der Sound Player hatte die
öffentliche 500-ms-Audiofrist lokal auf 100 ms verkürzt, obwohl der vermittelte
Service-zu-HDA-Start selbst bis zu 500 ms beanspruchen darf. Er verwendet nun
die öffentliche Standardfrist; schnelle Starts erhalten dadurch keine
zusätzliche Wartezeit, nur der Fail-Closed-Abbruch bleibt ausreichend groß.

Die beobachtete WAV-Startverzögerung hatte eine eigene, reproduzierbare
Ursache: 96 PCM-Bytes im v1-Payload erforderten für 15360 Frames bis zu 640
synchrone Roundtrips, bevor `START` gesendet wurde. Der append-only IPC-v2-Pfad
überträgt nun 2016 PCM-Bytes beziehungsweise 504 Stereo-Frames pro Block in
einem getrennten CRC-geschützten Rendezvous-Slot. V1 bleibt bytegleich; eine
maximale Vorschau benötigt höchstens 31 bestätigte Schreibvorgänge. Der
Preview-Loader öffnet und liest die WAV-Datei nur einmal, und der Sound Player
startet die Wiedergabe vor seiner Surface-Konstruktion.

Die im REIST Editor beobachtete unbrauchbare Verzögerung von Scrollbar-Drag und
Menü-Hover lag nicht in den Controls. Der erste Reparaturversuch behielt die
lokale Paint-Differenz, Damage-Akkumulation, Inputweiterleitung unter Paintlast
und den gecachten Notepad-Viewport bei, begrenzte den Broker aber auf vier
Queue-Scheiben. Auf dem SMP-Desktop verschlechterte das vollständige Frames:
Notepad läuft auf CPU 0, der Compositor auf einem AP, und jeder nach vier
Nachrichten blockierte Produzent wartete mangels CPU-übergreifendem Wakeup bis
zum nächsten 10-ms-Schedulertick. Der Broker verarbeitet deshalb wieder bis zu
64 faire Queue-Scheiben für einen kompletten, fest begrenzten Paintframe. Neu
fordert jeder Foreground-Wakeup erst nach Freigabe des Scheduler-Locks einen
spin-begrenzten Reschedule-IPI für die entfernte Affinitäts-CPU an. Ein
IPI-Fehler verändert den READY-Zustand nicht; der periodische Scheduler bleibt
Fallback. Die vorhandenen Paint- und Viewport-Optimierungen bleiben erhalten.

Der erste VMware-Nachtest mit Reschedule-IPI war wesentlich schneller, blieb
aber messbar hinter derselben VM mit nur einer aktiven CPU zurück. Ursache ist
der verbleibende Transportaufwand: Ein maximaler Paintframe benötigt bis zu 48
Queue-Refills zwischen dem BSP-Notepad und dem AP-Compositor und damit ebenso
viele lokale-APIC-/Schedulerübergaben. Das Produktionsprofil hält den
Compositor deshalb wieder auf CPU 0 bei seinen gewöhnlichen Surface-Clients.
Storage, Netzwerk, HDA, Audio-Service und geprüfte Gerätetreiber behalten ihre
AP-Nutzung. Die geschützte post-ready Compositor-AP-Maske bleibt im Supervisor
implementiert, ist aber standardmäßig leer, bis Paint-Batching oder eine
gemeinsame GUI-Affinitätsdomäne separat nachgewiesen ist.

Der im anschließenden Rescue-Shell-Bild sichtbare USB-Fehler ist eine getrennte
xHCI-Enumerationsgrenze: `config=59` bezeichnet die Länge des gelesenen
Konfigurationsdeskriptors; `cc=13` ist ein unerwarteter Short-Packet-Abschluss
bei einem nachfolgenden HID-Control-Request. R5.2x persistiert nun Typ, Request,
Wert, Index, Länge, Completion, Restlänge, Eventstufe und Flags vor dem
Doorbell-Zugriff. Die begrenzte Korrektur akzeptiert ausschließlich einen
terminalen, restlosen Status-Short für einen Zero-Data-Request und führt keinen
Retry auf dem alten EP0-Zustand aus. R6.2o bleibt bis zum physischen Nachweis
queued.

Ein früherer Lauf meldete einmal eine rekursive Übernahme von
`task_table_lock`. Der Fehler trat in vier vollständigen Folgeläufen nicht
erneut auf; die neue Panic-Diagnose liefert bei Wiederholung sofort CPU und
Aufrufer. Das ist starke Regressionsevidenz, aber kein Beweis, dass ein
nichtdeterministisches Rennen ausgeschlossen ist.

Die erste R6.2-Scheibe inventarisiert gemeinsam genutzte Treiberzustände und
gibt ausschließlich die autoritätslose, überwachte Driver-Fault-Fixture für
Online-APs frei. Null bleibt im append-only Supervisorprofil BSP-only; reale
Treiber und allgemeine Ring-3-Dienste behalten diese Voreinstellung. Der
Driver-Domain-Gastlauf muss Initialstart und Restart auf einem AP sowie Crash,
Hang, stale Generation, Reset-Fence und Budgeterschöpfung gemeinsam bestehen.
Weitere Produktionsdomänen folgen erst nach ihrem eigenen Zustandsaudit.

## Erste R6.2-Scheibe abgeschlossen

Die Driver-Fault-Domäne lief im vierkernigen QEMU-Nachweis bei Initialstart
und drei Restartgenerationen auf CPU 1 beziehungsweise CPU 3. Crash, Hang,
stale Generation, Reset-Fence und Budgeterschöpfung blieben begrenzt; die
Ring-3-Shell lief parallel auf dem BSP weiter. Das Runtime-Gate bestand in
15 Sekunden. Als Nächstes ist genau eine reale Treiberdomäne samt Controller-,
DMA-, IRQ- und Fencezustand zu auditieren; bis dahin bleibt sie BSP-affin.

## Zweite R6.2-Scheibe abgeschlossen

Der VMware-SVGA2D-Normalpfad startet weiterhin innerhalb seines bisherigen
BSP-Startup-Watchdogs. Erst nach `SCHEDULER_READY` wird die aktuelle gesunde
Generation generationsgeprüft AP-only umgebunden; Restarts fallen bewusst auf
den BSP-Default zurück. Der gemeinsame Displayzustand ist durch einen
deadlinebegrenzten Kernelmutex vor dem FIFO-Spinlock serialisiert. Der
vierkernige QEMU-Lauf führte den Treiber auf CPU 2 aus und bestätigte einen
realen `RECT_COPY`, geordnete Deaktivierung, `TASK_CAPACITY_OK` und `TEST_OK`.
Der anschließende R6.2c-Nachweis schützt die gewünschte AP-Maske im
ECC-gesicherten Driver-Control-Record. Jede Generation konstruiert und testet
sich auf dem BSP und wird erst nach gesunder Veröffentlichung AP-only. Eine
nur im Testbuild vorhandene Fault Injection unterdrückte den ersten
AP-Heartbeat: Epoch 1 wurde nach zwei Sekunden isoliert, vor dem Fence auf den
BSP zurückgeführt und innerhalb des einsekündigen Recovery-Fensters beendet.
Epoch 2 startete erneut auf dem BSP, wurde gesund, lief auf CPU 2 und schloss
den realen `SVGA2D_RECT_COPY_OK`-Pfad sowie `TEST_OK` ab. Die Treiberdomäne
erhielt dabei weiterhin keine DMA-, IRQ-, BAR- oder Raw-Port-Autorität.

## Dritte produktive R6.2-Scheibe abgeschlossen

Der HDA-Treiber konstruiert jede Generation einschließlich vermitteltem DMA,
IRQ-Bindung, Codec-Erkennung und Self-Test auf dem BSP. Nach
`SCHEDULER_READY` wird nur die gesunde Treibergeneration AP-only; Audio-Service,
Supervisor und Legacy-PIC-Hard-IRQ bleiben BSP-affin. Der SMP4-QEMU-Lauf führte
autorisierte HDA-Operationen auf CPU 3 aus, bestand fünf Playback-Zyklen und
erzeugte 278332 Stereo-S16-Frames bei 440,4 Hz mit `max-gap=1`. Für begrenzte
SMP-/PIC-Schedulinglatenz gilt ein festes Fünf-Sekunden-Heartbeatfenster;
einsekündiger Fence und Restartbudget drei bleiben erhalten.

## HDA-AP-Restart nachgewiesen

Eine compile-time-begrenzte Fault Injection unterdrückte den Heartbeat der
ersten HDA-AP-Epoch. Nach fünf Sekunden kehrte die Generation auf den BSP
zurück; der Fence entzog IRQ und Bus-Mastering, nullte den DMA-Pool und führte
das profildefinierte, auf 100 Polls und die einsekündige Recovery-Deadline
begrenzte GCTL-Resetrezept aus. Die neue Treibergeneration wurde auf dem BSP
gesund, der stale Service-Endpoint löste die normale Service-Rotation aus und
die Ersatzgeneration lief wieder auf einem AP. Fünf Playback-Zyklen ergaben
271216 Stereo-S16-Frames bei 440,4 Hz und `max-gap=1`.

## Audio-Servicegenerationen auf APs

Der geräteautoritätslose Audio-Service verbindet, testet und publiziert jede
Generation weiterhin auf CPU 0. Erst nach `SERVICE_READY` wird die geschützte
Online-AP-Maske angewandt. Vor jeder normalen Sessionrotation kehrt die alte
Generation sleepfähig auf den BSP zurück, bevor der präemptionsgeschützte
Fence-/Reap-Commit beginnt. Der SMP4-Lauf bestätigte AP-Ausführung über fünf
Rotationen und fünf PCM-Zyklen mit 274297 Frames bei 440,4 Hz und `max-gap=1`.

## Audio-Service-AP-Restart nachgewiesen

Eine nur im Fault-Build vorhandene Injection unterdrückte Fortschrittsberichte
der ersten AP-affinen Audio-Service-Epoch. Der bestehende Zwei-Sekunden-
Heartbeat isolierte die Generation; der begrenzte BSP-Handoff lief vor
Endpoint-Entzug und Reap. Die Ersatzgeneration verband sich auf CPU 0 erneut
mit der aktuellen HDA-Generation, bestand Self-Test und Ready-Publikation und
kehrte anschließend auf einen AP zurück. Fünf PCM-Zyklen erzeugten 269153
Stereo-S16-Frames bei 440,4 Hz und `max-gap=1`, ohne neue Geräteautorität.

## Storage-Service auf AP nachgewiesen

Der Ring-3-Storage-Service inventarisiert, bindet, testet und publiziert Ready
weiterhin auf CPU 0. Nach der SMP-Scheduler-Freigabe wird nur die aktuelle
gesunde Generation über die ECC-geschützte Zielmaske auf einen AP verschoben.
Der SMP4-Gast bestätigte eine autorisierte Storage-Operation auf CPU 2, den
realen Storage-Self-Test, VFS-/FAT-Zugriffe, `TASK_CAPACITY_OK` und `TEST_OK`.
ATA, AHCI, FDD, Administration und Legacy-PIC-IRQ blieben BSP-affin.

## Storage-Service-AP-Restart nachgewiesen

Die bestehende Test-Injection beendete Generation 1 erst nach einem realen
vermittelten Block-Read. Der Supervisor entzog die generationsgebundene
Request-Autorität und startete Generation 2 innerhalb des vorhandenen Budgets.
Sie band und publizierte Ready auf CPU 0, erhielt erst danach erneut die
geschützte AP-Maske und führte auf CPU 1 aus. Der SMP4-Gast bestand
`STORAGE_RESTART_OK`, `STORAGE_SERVICE_OK`, `TASK_CAPACITY_OK` und `TEST_OK`.

## Gesunder Netzwerkdienst auf AP nachgewiesen

Die drei begrenzten Crash-, Hang- und Invalid-Reply-Bootstrapgenerationen
blieben auf CPU 0 und erreichten `RECOVERY_SEQUENCE_OK`. Die gesunde vierte
Generation publizierte `SERVICE_READY` vor der SMP-Scheduler-Freigabe und
führte danach einen autorisierten Heartbeat auf CPU 2 aus. Der Gast bestand
`NETWORK_PARSER_OK`, `TASK_CAPACITY_OK` und `TEST_OK`; NIC-Treiber,
`netdev_poll`, Supervisor und Legacy-PIC-IRQ blieben BSP-affin. Vor einem
späteren Fence kehrt eine noch lebende Generation begrenzt auf CPU 0 zurück.

## Netzwerkdienst-AP-Restart nachgewiesen

Im RTL8139-Socket-Hub-Lauf arbeitete Generation 4 auf CPU 3. Der bestehende
Queue-Druck-/Crashpfad führte sie vor dem Entzug von ARP-, Socket-, Lease-,
Frame- und Endpoint-Autorität auf CPU 0 zurück. Ersatzgeneration 5 self-testete
und publizierte Ready auf dem BSP, lief anschließend wieder auf CPU 3 und
bestand `NETWORK_RECOVERY_OK`, `NETWORK_PARSER_OK`, `TASK_CAPACITY_OK` und
`TEST_OK`.

## Session-Compositor unter begrenzter Aufsicht

Der Desktop startet nicht mehr als unbeschraenkter Kompatibilitaetsprozess,
sondern in einer eigenen Default-Deny-Domaene. Jede Generation aktiviert und
initialisiert Display und Surface-Broker auf CPU 0, meldet danach geordnet
Self-Test, Fortschritt und Ready und erneuert ihren Heartbeat alle 500 ms.
Crash, ungueltiger Report oder Zwei-Sekunden-Timeout deaktivieren die
Displaypublikation vor der Prozessbeendigung; der Supervisor startet hoechstens
drei Ersatzgenerationen mit einem einsekundigen Recovery-Fenster. Erst normales
Sitzungsende oder Budgeterschoepfung gibt den Userspace-Shell-Fallback frei.
Diese Scheibe erteilt noch keine AP-Affinitaet.
