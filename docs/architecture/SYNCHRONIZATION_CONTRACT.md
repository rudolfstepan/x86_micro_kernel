# Synchronisations- und Ausführungskontextvertrag

Stand: 13. August 2026. Dieser Vertrag gilt für den aktuellen
Uniprozessor-Kernel. Vor der Inbetriebnahme weiterer CPUs müssen Scheduler-
Zustand, IRQ-Tiefe, Präemptionszähler und Lock-Ownership per CPU geführt werden.

## Ausführungskontexte

| Kontext | Erkennung | Erlaubt | Verboten |
|---|---|---|---|
| Hard-IRQ | `irq_in_context()` | begrenztes PIO/MMIO, Ack/EOI, atomare Zustandsänderung, kurze IRQ-sichere Locks, Queue-Wakeup als letzte Aktion | Heap, VFS, Dateisystem, blockierende Treiber-I/O, `printf`/`klog`, Sleep, Yield oder Kontextwechsel |
| Foreground mit IF=0 | `!irq_enabled()` und nicht Hard-IRQ | kurze `*_locked`-Scheduleroperationen | IRQ-abhängige I/O, Sleep und lange Arbeit |
| Präemption deaktiviert, IF=1 | `scheduler_preempt_is_disabled()` | kurze synchrone VFS-/Dateisystem-/Treibertransaktionen | Sleep, Yield, Block und Kontextwechsel |
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

Unter einem Spinlock darf Code nicht schlafen oder blockieren. Rekursiv verboten
ist der Spinlock-Erwerb; Freigabe eines nicht gehaltenen Locks ist ebenso
ein Kernel-Fehler. Die Laufzeitassertions in `spinlock.h` bilden diesen
UP-Vertrag ab.

Priority Inheritance gilt nur für blockierende IPC-Endpunkte mit genau einem
generationssicher identifizierten Gegenprozess. Sie propagiert transitiv über
höchstens `MAX_TASKS` und endet bei Wakeup, Timeout, Cancel oder Exit.
Spinlocks, IRQ-Locks und Präemptions-Guards erhalten keine Inheritance: Unter
ihnen ist Blockieren bereits ein Vertragsfehler.

## Subsystemverträge

- Scheduler- und Wait-Queue-Funktionen mit Suffix `_locked` verlangen IF=0.
- `wait_queue_block_locked()` ist zusätzlich nur aus Foreground-Kontext erlaubt.
- Heap- und Frame-Allokation sind nicht IRQ-tauglich.
- VFS, Dateisystem und synchrone Block-I/O laufen mit IF=1 unter einem
  nestbaren Präemption-Guard. Hardware-IRQ-Completion bleibt dadurch möglich.
- Prozessende schließt Dateien mit IF=1 unter Präemptionsschutz und veröffentlicht
  Exitstatus sowie Waiter-Wakeup erst danach atomar mit IF=0.
- IRQ-Handler bestätigen Hardware, veröffentlichen nur bounded Zustand und
  verschieben umfangreiche Verarbeitung in Foreground-Polling. Ein generischer
  Bottom-Half-Worker ist noch nicht vorhanden; Netzwerk-Polling erfolgt an den
  bestehenden Shell- und Netzwerk-Fortschrittspunkten.

## Diagnose

Strukturierte Logs verwenden `klog(level, component, format, ...)` mit den
stabilen Stufen `TRACE`, `DEBUG`, `INFO`, `WARN` und `ERROR`. Logging ist im
Hard-IRQ-Kontext verboten. Panics zeigen die ELF-Build-ID, CR2 und bei
CPU-Exceptions den vollständigen Registerframe.
