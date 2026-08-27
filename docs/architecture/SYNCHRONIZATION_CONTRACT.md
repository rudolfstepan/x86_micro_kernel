# Synchronisations- und Ausführungskontextvertrag

Stand: 26. August 2026. Dieser Vertrag gilt für den begrenzten SMP-Kernel.
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
SMP-Vertrag ab und begrenzen jeden Erwerbsversuch.

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
