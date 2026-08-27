# REIST OS – aktueller Arbeitsstand

Stand: 27. August 2026

Branch/Startpunkt: `working_branch` / `c184674`

Aktives Thema: R2.2ag – initialen Compositor-Ladefehler begrenzt wiederherstellen

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

Der aktuelle physische Lauf auf dem AMD-Mainboard mit NVIDIA zeigt vor jeder
Grafikinitialisierung beim Öffnen von `/usr/gui/bin/desktop.prg` den Fehler
`Program load open failed (-4)`. `-4` ist `VFS_ERR_IO`; unmittelbar danach
scheitert auch der Shell-
Fallback. Trotzdem meldet der Supervisor später `COMPOSITOR_RESTARTED epoch=2`
und die Ersatzgeneration erreicht den optionalen Beschleunigungsdienst. Damit
ist die frühere Erstframe-Timinghypothese widerlegt und R2.2af abgebrochen.

R2.2ag korrigiert die nachgewiesene geteilte Sitzungsautorität: Sobald der
geschützte Compositor-Kontrollrecord aktiv ist, bleibt auch ein fehlgeschlagener
Initialspawn im vorhandenen begrenzten Isolate-/Fence-/Restart-Zyklus. Der
Kernel wartet, solange diese Sitzung administrativ aktiv ist, und startet die
Shell erst nach Budgeterschöpfung oder wenn die Sitzung gar nicht sicher
etabliert werden konnte. VFS-Fehler, Deadlines und Restartbudget werden nicht
umgedeutet oder erweitert. R6.2o bleibt danach als getrenntes
Fault-Injection-Paket queued.

Ein früherer Lauf meldete einmal eine rekursive Übernahme von
`task_table_lock`. Der Fehler trat in vier vollständigen Folgeläufen nicht
erneut auf; die neue Panic-Diagnose liefert bei Wiederholung sofort CPU und
Aufrufer. Das ist starke Regressionsevidenz, aber kein Beweis, dass ein
nichtdeterministisches Rennen ausgeschlossen ist.

Das zurückgestellte R6.2o-Paket ist der getrennte Compositor-Restart-Nachweis: Eine nur
im Fault-Build betroffene erste AP-Generation muss ihren Heartbeat verlieren,
vor Display-Fence und Reap begrenzt auf den BSP zurückkehren und die
Ersatzgeneration darf ihre geschützte AP-Maske erst nach erneutem Self-Test,
Healthy und `SERVICE_READY` auf CPU 0 anwenden. Der Vier-vCPU-VMware-Lauf muss
danach erneute AP-Ausführung und reale xHCI-Mauszustellung über den lokalen
RFB-Kanal belegen.

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
