# Synchronisations- und Ausführungskontextvertrag

Stand: 28. August 2026. Dieser Vertrag gilt für den begrenzten SMP-Kernel.
Schedulerzustand, IRQ-Tiefe und Präemptionszähler sind CPU-lokal;
gemeinsam genutzter Kernelzustand verwendet CPU-besitzende SMP-Locks.

## Ausführungskontexte

| Kontext | Erkennung | Erlaubt | Verboten |
|---|---|---|---|
| Hard-IRQ | `irq_in_context()` | begrenztes PIO/MMIO, Ack/EOI, atomare Zustandsänderung, kurze IRQ-sichere Locks, Queue-Wakeup als letzte Aktion | Heap, VFS, Dateisystem, blockierende Treiber-I/O, `printf`/`klog`, Sleep, Yield oder Kontextwechsel |
| Foreground mit IF=0 | `!irq_enabled()` und nicht Hard-IRQ | kurze `*_locked`-Scheduleroperationen | IRQ-abhängige I/O, Sleep und lange Arbeit |
| Präemption deaktiviert, IF=1 | `scheduler_preempt_is_disabled()` | kurze verbleibende Legacy-Treibertransaktionen | Sleep, Yield, Block und Kontextwechsel |
| Schlafbar | `scheduler_can_sleep()` | blockierende Scheduler-APIs | Verwendung nach Erwerb eines Spinlocks oder Präemption-Guards |

`irq_save()`/`irq_restore()` bilden streng geschachtelte LIFO-Paare. IF=0 allein
bedeutet nicht Hard-IRQ-Kontext. Der IRQ-Kontext endet nach Gerätecallbacks und
EOI, aber vor dem Scheduler-Tail, weil dieser per `swtch` zu einem anderen Task
wechseln kann.

Öffentliche Kontextprüfungen sind fail-closed:

- `KASSERT_IRQ_DISABLED()` schützt `*_locked`-APIs.
- `KASSERT_NOT_IRQ()` schützt Heap, VFS und andere Foreground-APIs.
- `KASSERT_CAN_SLEEP()` schützt Sleep, Yield und blockierende APIs.

## Lock-Reihenfolge

Die globale Erwerbsordnung lautet: `VFS → DATEISYSTEM → TREIBER → SCHEDULER`.
Freigaben erfolgen in umgekehrter Reihenfolge. Ein Scheduler-Lock bzw. IF=0
darf niemals zurück in VFS, Dateisystem oder Treiber verzweigen. IRQ-Handler
dürfen Scheduler-Wakeups nur als abschließende Operation ausführen.

Die interne Allokatorordnung lautet `HEAP → FRAME`; der Frame-Lock darf niemals
den Heap-Lock erwerben. Der Präemption-Guard ist kein Lock-Rank.
`PREEMPT_GUARD` darf nie Sleep kreuzen. `PREEMPT_GUARD` darf nie Yield kreuzen.
`PREEMPT_GUARD` darf nie Block kreuzen. `PREEMPT_GUARD` darf nie Exit kreuzen.
`PREEMPT_GUARD` darf nie `swtch` kreuzen.

Für SMP-Lifecycle-Transaktionen gilt ergänzend
`PROZESS → IPC/SOCKET/EINGABE → SCHEDULER`. Langsame VFS-, Geräte- und
Ressourcenfreigabe läuft niemals unter dem Prozess- oder Schedulerlock. Der
Zustand `terminating` hält dabei PID und Generation bis zum abschließenden
kurzen Exit-Commit stabil. Condition-to-Waitqueue-Übergaben erwerben den
Schedulerlock, geben anschließend den Condition-Lock frei und halten keinen
Spinlock über den Kontextwechsel.

Unter einem Spinlock darf Code nicht schlafen oder blockieren. Rekursiv verboten
ist der Spinlock-Erwerb; Freigabe eines nicht gehaltenen Locks ist ebenso
ein Kernel-Fehler. Die Laufzeitassertions in `spinlock.h` bilden diesen
SMP-Vertrag ab und begrenzen jeden Erwerbsversuch. Das atomare Lockwort trägt
dabei `CPU-Index + 1` als maßgeblichen Besitzer-Token; Null bedeutet frei.
Rekursion wird ausschließlich aus dem vom Compare-and-swap beobachteten Token
abgeleitet. Das getrennte `owner_cpu`-Feld ist nur ein Diagnoseabbild und darf
wegen möglicher Übergangs- oder Sichtbarkeitszustände niemals allein Besitz
begründen.

Ein normaler kontendierter Erwerb verwendet nach der einmaligen TSC-
Kalibrierung eine feste Frist von 250 ms und zusätzlich eine endliche harte
Iterationsgrenze. Vor der Kalibrierung gilt weiterhin die kleinere endliche
Bootgrenze. Der Wartende liest das Lockwort zuerst und versucht Compare-and-
swap nur bei beobachtetem Nullwert; dadurch bleibt die Cacheline während des
Besitzes geteilt und VMware-vCPUs verhindern die Freigabe nicht durch einen
Strom atomarer Schreibversuche. Rekursion bleibt unabhängig davon sofort
fatal. Ein Timeout meldet Lockadresse sowie Besitzer-CPU und wartenden
Aufrufer. Die Frist macht kurze Host-Präemption tolerierbar, ist aber kein
Freibrief für lange kritische Abschnitte: Nach Ablauf bleibt der Übergang
fail-closed.

Die PIT-Sequenzlese verwendet denselben kalibrierten 250-ms-Vertrag mit einer
endlichen Bootgrenze. Scheduling-Pfade lesen die monotone Zeit vor Erwerb des
Task-Table-Locks und übergeben den Snapshot in den festen Policy-Commit. Damit
kann eine vom Host angehaltene PIT-Writer-vCPU den globalen Scheduler-Lock
nicht indirekt festhalten.

Blockierende Syscalls duerfen grosse, ueber den Wait hinweg lebende
Arbeitsfelder nicht auf dem 8-KiB-Task-Kernelstack halten. Der
`SYS_READDIR_BATCH`-Pfad verwendet deshalb genau einen festen Arbeitsbereich
pro Task-Slot. Dessen Besitzer ist die nie-null Task-Generation; dieselbe
Generation kann ihn nicht rekursiv erwerben, und ein belegter Rest darf erst
nach nachgewiesener Slot-Wiederverwendung durch eine andere Generation
zurueckgesetzt werden. So bleibt der Bereich waehrend eines Kontextwechsels
privat, ohne Heap, per-CPU-Alias oder vergroesserten Kernelstack.

Lange Foreground-Transaktionen verwenden `kernel_mutex_t`. Dieser interne,
rekursive Mutex besitzt einen festen maximalen Rekursionstiefenwert und nimmt
für jeden konkurrierenden Erwerb eine absolute monotone Deadline entgegen.
Der Übergang vom kurzen Zustands-Spinlock zur Waitqueue erfolgt atomar; kein
Spinlock bleibt über `swtch()` gehalten. Kernel-/Idle-Kontexte ohne Tasknode
dürfen ihn nur konkurrenzfrei erwerben. Timeout und ein konkurrierender
Erwerb aus einem solchen Kontext werden vor fachlichen Seiteneffekten
abgewiesen.

Priority Inheritance gilt nur für blockierende IPC-Endpunkte mit genau einem
generationssicher identifizierten Gegenprozess. Sie propagiert transitiv über
höchstens `MAX_TASKS` und endet bei Wakeup, Timeout, Cancel oder Exit.
Spinlocks, IRQ-Locks und Präemptions-Guards erhalten keine Inheritance: Unter
ihnen ist Blockieren bereits ein Vertragsfehler.

## Subsystemverträge

- Scheduler- und Wait-Queue-Funktionen mit Suffix `_locked` verlangen IF=0.
- Blockierende Waitqueue-APIs sind zusätzlich nur aus Foreground-Kontext
  erlaubt; `wait_queue_block_until_spinlocked()` überträgt atomar vom
  Condition-Lock in den Scheduler.
- Ein Foreground-Wakeup darf nach Freigabe des Scheduler-Locks einen
  spin-begrenzten Reschedule-IPI an freigegebene entfernte CPUs aus der
  Affinitätsmaske senden. Der Ziel-ISR bestätigt den LAPIC und verlässt
  `irq_in_context()` vor dem Scheduler-Tail. IPI-Fehler ändern den READY-
  Commit nicht; der periodische Scheduler bleibt Fallback.
- Hard-IRQ-Wakeups senden keinen Reschedule-IPI, weil der unterbrochene Kontext
  selbst den ICR-Lock halten könnte. Sie bleiben auf atomare READY-Publikation
  als letzte Aktion und den periodischen Scheduler begrenzt.
- Heap- und Frame-Allokation sind nicht IRQ-tauglich.
- VFS, FAT32 sowie synchrone ATA-PIO-, AHCI- und FDD-Transaktionen laufen mit
  IF=1 unter rekursiven Timed-Mutexen. Die zulässigen verschachtelten Pfade
  sind `VFS -> FAT32 -> ATA/AHCI -> SCHEDULER` und
  `VFS -> FDD -> SCHEDULER`. AHCI besitzt einen festen Mutex je veröffentlichtem
  Port; VFS-, ATA-, AHCI- und FDD-Einstiege melden einen erschöpften Erwerb als
  `BUSY` beziehungsweise fehlgeschlagene Operation.
  FAT32 bewahrt für die alten Direkt-APIs den unveränderten Guard-ABI und
  stoppt bei einem unerwartet erschöpften internen Lock fail-closed.
- FAT12 besitzt keinen zusätzlichen globalen Guard: VFS serialisiert seinen
  Namensraumzustand, der FDD-Mutex schützt FIFO, ISA-DMA und IRQ6-Abschluss.
- Treiber ohne eigenen abgenommenen SMP-Vertrag bleiben weiterhin CPU-0-affin.
- Prozessende schließt Dateien mit IF=1 unter Präemptionsschutz und veröffentlicht
  Exitstatus sowie Waiter-Wakeup erst danach atomar mit IF=0.
- IRQ-Handler bestätigen Hardware, veröffentlichen nur begrenzten Zustand und
  verschieben umfangreiche Verarbeitung in Foreground-Kontext. Der dedizierte
  Supervisor-Worker treibt überwachte Dienste, Deadlines und Recovery voran;
  treiberspezifische Paketarbeit bleibt begrenzt an den vorgesehenen
  Foreground-/Polling-Fortschrittspunkten.

## Diagnose

Strukturierte Logs verwenden `klog(level, component, format, ...)` mit den
stabilen Stufen `TRACE`, `DEBUG`, `INFO`, `WARN` und `ERROR`. Logging ist im
Hard-IRQ-Kontext verboten. Panics zeigen die ELF-Build-ID, CR2 und bei
CPU-Exceptions den vollständigen Registerframe.
