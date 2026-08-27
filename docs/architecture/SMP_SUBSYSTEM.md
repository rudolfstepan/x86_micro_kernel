# SMP-Subsystem

Stand: 27. August 2026

## Abnahmegrenze der ersten Stufe

REIST erkennt aktivierbare Prozessoren über die ACPI-MADT nach ACPI 6.x und
startet bis zu 16 xAPIC-CPUs über die Intel-MP-konforme INIT/SIPI-Sequenz. Der
RSDP-, RSDT-/XSDT- und MADT-Pfad prüft Signaturen, Längen, Checksummen,
Eintragsgrenzen, doppelte APIC-IDs und die 32-Bit-Adressierbarkeit. Der
EBDA-Zeiger wird vor dem absichtlichen Unmapping von Seite null erfasst.

Das Trampolin belegt die vor dem Frame-Allocator reservierte physische Seite
`0x7000`. Jeder Application Processor erhält einen privaten, vom Scheduler-
Allocator verwalteten 8-KiB-Stack mit ungemappten Guard-Seiten
und übernimmt zunächst IDT und Kernel-CR3. Vor seiner Online-Meldung wechselt
er auf eine private GDT mit eigenem Runtime-TSS und eigenem Double-Fault-TSS
samt Notfallstack. Der BSP startet APs einzeln. IPI-Zustellung und
Online-Handshake besitzen endliche Deadlines; nach einem Timeout wird die
Mailbox nicht wiederverwendet, damit ein verspäteter AP niemals Stack oder
Identität eines anderen Prozessors erbt.

Eine feste APIC-ID-Tabelle ordnet jeder CPU genau einen von 16 Slots zu.
IRQ-Kontexttiefe, Präemptionszähler, Pending-Präemption und aktueller
Seitentabellenzeiger liegen bereits CPU-lokal. Das gilt nun auch für aktuellen
Task, gespeicherten Kernel-Schedulerkontext und dessen Gültigkeitsmarker.
Unbekannte oder doppelte Identitäten werden vor der Online-Publikation
abgewiesen.

Kurze Kernel-Critical-Sections verwenden einen atomaren Test-and-set-Lock mit
logischem CPU-Besitz und einer festen Obergrenze von `2^20` `PAUSE`-Versuchen.
Rekursive Übernahme, Freigabe durch eine fremde CPU und erschöpfter Warteetat
enden diagnostiziert und fail-closed statt in einem unbegrenzten Spin. Während
des AP-Handshakes publizieren BSP und jeder AP je ein Bit unter demselben Lock;
damit prüft der Mehrkern-Smoke reale Cross-CPU-Synchronisation.

Die Synchronisationsdiagnose speichert zusätzlich den aktuellen IRQ-Vektor
CPU-lokal. Bei rekursiver Spinlock-Übernahme enthält der Panic-Kontext neben
der Lockadresse die CPU-Nummer und die auf 24 Bit begrenzte Aufruferadresse.
Damit bleibt auch ein nur sporadisch auftretender Lockfehler auf einem
unstripped Kernelabbild eindeutig symbolisierbar.

Seitentabellen-Schreiboperationen sind mit einem eigenen SMP-Spinlock
serialisiert. Die feste Lockordnung lautet `Seitentabelle -> Frame-Allocator`;
der umgekehrte verschachtelte Erwerb ist unzulässig. Ein eigener fixer IPI-
Vektor invalidiert betroffene Einträge auf allen CPUs, die den gemeinsamen
Kerneladressraum oder den betroffenen Prozessadressraum verwenden. Jede
Anforderung trägt eine monotone Generation und eine feste Zielmaske; jeder AP
publiziert seine beobachtete Generation atomar in der ACK-Maske. Sende- und
ACK-Warten sind begrenzt. Bei Timeout stoppt das System fail-closed, und ein
entfernter physischer Frame wird erst nach vollständiger Quittierung wieder
freigegeben. Ein Prozessor, der bei ursprünglich aktivierten Interrupts auf
den Seitentabellen-Lock wartet, öffnet zwischen begrenzten Erwerbsversuchen ein
minimales IPI-Fenster. Dadurch kann er eine vom Lockbesitzer angeforderte
TLB-Invalidierung quittieren; der Besitzer wartet nicht auf einen AP, der sich
mit deaktivierten Interrupts am selben Lock festgesetzt hat.

Ein erfolgreich gestarteter AP prüft seine lokale APIC-ID, aktiviert seinen
lokalen APIC zunächst in einem IPI-only-Profil und wartet anschließend in
`HLT`. Timer,
LINT0, LINT1 und APIC-Error bleiben maskiert; nur der feste TLB-Shootdown und
der Spurious-Vektor sind zulässig. Jede CPU kalibriert ihren eigenen LAPIC-
Timer gegen die monotone PIT-Referenz und publiziert das Ergebnis CPU-lokal;
der AP-Timer bleibt bis zur späten Scheduler-Freigabe nach `BOOT_OK` maskiert.
Alle 8259-PIC-
Geräte-IRQs tragen explizit
die Affinitätsmaske CPU 0. Eine unerwartete Zustellung an eine andere CPU
maskiert die Leitung fail-closed.

Nach vollständiger Publikation der Systemdienste legt der BSP genau einen
Kernel-Probetask pro AP an. Jeder Task besitzt eine einzelne CPU-Affinitätsmaske
und kann deshalb weder auf CPU 0 noch auf einem fremden AP laufen. Ein eigener
IPI-Vektor aktiviert erst dann den bereits kalibrierten periodischen LAPIC-
Timer. Der Nachweis ist erst erfolgreich, wenn jeder AP den IPI quittiert, den
affinen Task tatsächlich betreten und nach dessen `task_exit` wieder seinen
CPU-lokalen, guard-page-geschützten Kernelkontext erreicht hat. IPI-, Eintritts-
und Rückkehrmasken werden mit einer festen Zwei-Sekunden-Deadline geprüft.

Nach erfolgreicher Rückkehr werden alle drei beendeten Probetasks
generationsgeprüft und deterministisch reap't. Das feste Kernelstack-Areal
reserviert 48 Slots: 32 öffentliche Taskslots bleiben dadurch auch bei den
maximal 15 privaten AP-Idle-Stacks vollständig verfügbar. Der Gasttest belegt
diese Grenze mit 32 gleichzeitig lebenden, blockierten `CAPWAIT.PRG`-Tasks und
verlangt danach `TEST_OK`; eine bloße Folge kurzlebiger Prozesse genügt nicht
als Kapazitätsnachweis.

## Bewusste Grenze

Tasktabelle und Runqueue sind global, aber durch den CPU-besitzenden
Tasktabellen-Lock serialisiert. Scheduling-Zeitfenster und Auswahlcursor sind
dagegen CPU-lokal getrennt. Auswahl und atomare Übernahme beachten die Task-
Affinitätsmaske. Bestehende Kernel- und Ring-3-Dienste werden weiterhin
standardmäßig ausschließlich CPU 0 zugeordnet. Eine append-only Supervisor-
Konfiguration kann nun eine explizite, ausschließlich aus online CPUs
bestehende Affinitätsmaske an einen überwachten Ring-3-Treiber weiterreichen;
der Wert null bewahrt die bisherige BSP-Affinität. Als erste R6.2-
Arbeitsdomäne nutzt nur die autoritätslose Driver-Fault-Fixture diese Freigabe
und läuft AP-only. Nach dem zweiten R6.2-Audit darf auch der überwachte VMware-
SVGA2D-Prozess auf einem SMP-System AP-only laufen; auf Ein-CPU-Systemen bleibt
sein BSP-Fallback erhalten. Andere reale Video-, Audio-, Storage-, Netzwerk-,
USB- und Eingabetreiber bleiben BSP-affin.

Das Audit der gemeinsam genutzten Treiberzustände trennt drei Gruppen. VFS,
FAT32, ATA, AHCI und FDD sind durch die unten beschriebenen deadlinebegrenzten
Transaktionsmutexe geschützt und besitzen bereits parallele Read-Evidenz.
Tastatureingabe, UDP/TCP-Sockettabellen, ARP-Bindings und Storage-Pool besitzen
kurze CPU-besitzende Locks. Die globalen Controller-, Ring-, DMA- und
Pollzustände von PCI-Netzwerktreibern, xHCI/OHCI, serieller Ausgabe und den
verbleibenden Video-/Audio-Mediatoren sind dagegen noch nicht vollständig als
SMP-feste Transaktionen auditiert; ihre Produktionsdomänen dürfen deshalb
keine AP-Maske erhalten.

Der VMware-Display-Control-Zustand besitzt nun einen rekursiven, maximal eine
Sekunde wartenden Kernelmutex. Er umfasst Aktivierung, Deaktivierung, Probe,
Busy-/Capability-Abfrage und die Übergabe an den FIFO. Die feste Lockordnung
lautet `Displayzustand -> VMware-FIFO -> Scheduler`; kein FIFO-Spinlock wird
über einen Kontextwechsel gehalten. Der Ring-3-Treiber erhält weiterhin weder
DMA- noch IRQ-, BAR-Mapping- oder rohe Portautorität. Ein vierkerniger QEMU-
Lauf verlangt einen echten AP-Eintritt sowie den realen begrenzten
`RECT_COPY`, während Compositor und Supervisor auf dem BSP fortschreiten.

Der Waitqueue-Kern serialisiert Taskzustand, intrusive Nodes und Timeout-Scans
unter dem Tasktabellen-Lock. IPC sowie die begrenzten UDP- und TCP-Sockettabellen
besitzen eigene CPU-besitzende SMP-Locks. Blockierende Operationen übertragen
atomar vom gehaltenen Subsystemlock in die Scheduler-Warteschlange: Der
Scheduler-Lock wird zuerst erworben, danach der Subsystemlock freigegeben und
erst dann der Kontext gewechselt. Dadurch bleibt die feste Lockordnung
`IPC/Socket -> Scheduler` erhalten, ohne einen Spinlock über `swtch()` zu halten
oder ein Wakeup-Fenster zu öffnen.

Der gemeinsame PS/2-/USB-Tastaturpuffer verwendet denselben Vertrag. Ein Leser
prüft und entnimmt Eingabe unter dem Eingabelock oder überträgt sich atomar in
die Waitqueue; ein IRQ- oder Poll-Produzent kann deshalb zwischen Leerprüfung
und Blockierung keinen Wakeup mehr verlieren.

Die Prozessliste, PID-/Generationsvergabe und Exit-Zustände besitzen ebenfalls
einen eigenen SMP-Lock. Die feste Verschachtelung lautet
`Prozess -> IPC/Socket -> Scheduler`. `terminating` reserviert eine laufende
Prozessidentität während der langsamen, mit IF=1 ausgeführten Ressourcen-
Freigabe. Erst danach veröffentlicht der kurze Commit unter
`Prozess -> Scheduler` Exitstatus, Zombie-Zustand und Waiter-Wakeup. Der
blockierende `wait`-Syscall überträgt atomar vom Prozesslock in die kindeigene
Exit-Waitqueue.

Der feste Storage-Request-Pool einschließlich Bulk-Slots, Generationen,
Integritätskopien und Statistik verwendet einen eigenen SMP-Lock. Er enthält
weder Heap- noch VFS- oder Geräte-I/O und hält den Lock nur über die begrenzte
Slottransaktion. Für lange Foreground-Transaktionen steht nun ein rekursiver,
schlafbarer Kernelmutex mit absoluter monotoner Deadline bereit. Sein kurzer
Zustandslock wird atomar in die intrusive Scheduler-Waitqueue übertragen und
nie über einen Kontextwechsel gehalten. VFS, FAT32, ATA, AHCI und FDD verwenden
ihn in den festen Reihenfolgen `VFS -> FAT32 -> ATA/AHCI -> Scheduler` und
`VFS -> FDD -> Scheduler`; ihre alten globalen Kontexte können dadurch
SMP-sicher serialisiert werden, ohne andere CPUs per Präemptionsguard
festzuhalten. FAT12 wird durch VFS und den FDD-Transaktionsmutex abgedeckt.
Weitere Legacy-Treiber bleiben bis zu ihrer eigenen Migration CPU-0-affin.

Der CPU-lokale Kernel-Schedulerkontext ist selbst kein Runnable-Task und darf
deshalb nicht auf einem Kernelmutex schlafen. Jede erfolgreiche Übernahme aus
diesem Kontext hält genau einen verschachtelbaren lokalen Präemptionsguard bis
zur korrespondierenden Freigabe; Konkurrenz liefert unmittelbar `-EAGAIN`.
Damit kann ein Timertick keinen `owner_task=-1`-Besitzer vom einzigen späteren
Freigabepfad verdrängen. Prozess-Exit und fremde Terminierung halten dagegen
keinen Präemptionsguard über VFS- oder Block-I/O: `terminating`, Generation und
atomarer `running_cpu`-Besitz pinnen die Identität während sleepbarer
Ressourcenfreigabe.

ATA-, AHCI- und FDD-Fencezustand wird vor einem möglichen Mutex-Warten atomar
publiziert. Hardware-Quieszenz und Fence-Rücknahme erfolgen anschließend unter
dem jeweiligen Transaktionsmutex. AHCI sperrt je Port genau den zugeordneten
Command-Slot und DMA-Puffer; Fence und Quieszenz nehmen alle veröffentlichten
Ports in stabiler Controller-/Portreihenfolge. Ein Timeout lässt den logischen
Fence aktiv und verhindert dadurch eine verfrühte Reintegration.

ARP-Binding-Cache, Handover-Status und Handover-Replikat sind ebenfalls feste,
heapfreie Critical-Object-Tabellen mit eigenen SMP-Locks. Externe Fence-I/O
bleibt ausdrücklich außerhalb des Handover-Locks.

Administrative Tasktabellen-Transaktionen sind bereits durch einen eigenen
SMP-Lock geschützt: Slotvergabe, generationsgeprüfte Zustandsabfrage,
Ressourcen-Detach/Reaping und Terminierungs-Commit. Die Tasktabelle ist nicht
mehr als globales Symbol außerhalb des Schedulers sichtbar. Kein solcher Lock
wird über `swtch()` gehalten.

Auch der Runqueue-Handoff besitzt nun ein explizites SMP-Modell. Jeder Task
trägt genau einen atomar per Compare-and-swap beanspruchten `running_cpu`-
Besitzer. Ein READY-Task mit fremdem Besitzer ist nicht wählbar. Beim Wechsel
bleibt der alte Task bis zur tatsächlichen Ankunft im Zielkontext besessen und
im transienten Zustand `TASK_HANDOFF`; erst der Zielkontext publiziert READY
und gibt den CPU-Besitz frei. Blockierte und beendete Tasks behalten ihren
fachlichen Zustand, werden aber ebenfalls erst nach vollzogenem `swtch()`
freigegeben. Neue Tasks schließen denselben Handoff im Trampolin ab. Dadurch
kann kein Task gleichzeitig auf zwei CPUs laufen und kein Runqueue-Lock wird
über einen Kontextwechsel getragen. Der isolierte AP-Probelauf und die
überwachte Driver-Fault-Fixture verwenden genau diesen Pfad; allgemeine
Dienste bleiben bis zur Migration ihrer eigenen Synchronisation BSP-exklusiv.
Der Driver-Domain-Lauf verlangt mindestens zwei AP-Eintritte der einzigen AP-
fähigen Fixture und beweist damit sowohl ihren Initialstart als auch einen
generationsgebundenen Restart nach Crash oder Timeout. Reset-Failure-Fixture
und Supervisor-Worker laufen weiter auf CPU 0, sodass der Nachweis echte
parallele Supervisor-/Arbeitsdomänenausführung enthält.

Der Boot-Probelauf führt zusätzlich auf allen APs nach einer gemeinsamen
Startbarriere denselben read-only Root-Sektorzugriff aus. Ein BSP-gelesenes
Referenzabbild muss auf jedem AP bytegleich zurückkehren; erst
`REIST_SMP SUBSYSTEM_READY` bestätigt damit echte konkurrierende ATA- oder
AHCI-Treiberausführung. Verbleibend sind parallele Fault-Injection-Tests und
die Migration weiterer Gerätezustände, bevor reguläre Dienste ihre
CPU-0-Affinität erweitern dürfen.

Ein IOAPIC bleibt für verteilte Geräte-IRQs und moderne Plattformen sinnvoll,
ist aber keine Sicherheitsvoraussetzung für den nun explizit BSP-affinen
Legacy-PIC-Pfad.

Nach dem Probe-Nachweis bleiben APs in ihrem lokalen Scheduler-Idlekontext und
bedienen ausschließlich AP-affine Kernelarbeit sowie TLB-IPIs. Fehlt ACPI oder
LAPIC, bootet REIST diagnostiziert im vorhandenen Ein-CPU-/PIT-Fallback weiter.

## Automatisierte Evidenz

`test/test_smp.py` prüft den begrenzten MADT-Parser, Trampolinvertrag und die
QEMU-Optionen. Der QEMU-Smoke unterstützt `--smp 4 --expect-smp` und verlangt
für jeden AP einen geordneten `REIST_SMP AP_ONLINE`-Marker sowie abschließend
`REIST_SMP READY online=4 parked=3 failed=0` sowie
`REIST_SMP PERCPU_READY cpus=4`, `REIST_SMP LOCK_READY cpus=4 mask=0000000F`,
`REIST_SMP TLB_READY cpus=4` und
`REIST_SMP IRQ_AFFINITY_READY mode=pic-bsp` sowie
`REIST_SMP TIMER_READY cpus=4 mode=masked` und nach der Bootbarriere
`REIST_SMP SCHEDULER_READY cpus=4 probe_mask=0000000E` sowie das vollständige
Reaping mit `REIST_SMP REAP_READY workers=3 reaped=3`. Dieselben drei
AP-affinen Tasks durchlaufen unter erzwungener kurzer Konkurrenz den neuen
schlafbaren Mutex; nur `REIST_SMP MUTEX_READY workers=3 mask=0000000E`
schließt diesen Nachweis ab. Danach lesen sie barriere-synchron denselben
Root-Sektor; `REIST_SMP SUBSYSTEM_READY workers=3 mask=0000000E` verlangt drei
erfolgreiche, bytegleiche Ergebnisse. Die automatisierte Matrix führt diesen
Nachweis sowohl über ATA-PIO als auch über ICH9-AHCI aus. Der abschließende
SMP4-Gastlauf verlangt außerdem `TASK_CAPACITY_OK` und `TEST_OK`; der aktuelle
Referenzlauf besteht zusätzlich die überwachte SVGA2D-Aktivierung und einen
realen, begrenzten `RECT_COPY`-Befehl ohne Software-Fallback.
Der R6.2c-Fehlerlauf unterdrückt compile-time-begrenzt ausschließlich den
Heartbeat der ersten AP-affinen SVGA2D-Epoch. Die gewünschte Online-AP-Maske
liegt im ECC-geschützten Supervisor-Control-Record und wird nach jedem Neustart
erst nach BSP-Konstruktion, Self-Test und gesunder Veröffentlichung angewandt.
Vor dem Fence kehrt die alte Generation deadlinebegrenzt auf den BSP zurück,
damit Prozess-Exit und Reaping nicht parallel auf verschiedenen CPUs laufen.
Der Vier-CPU-Nachweis verlangt Timeout und `DRIVER_RESTARTED`, AP-Ausführung in
beiden Epochs, `SVGA2D_RECT_COPY_OK` der zweiten Generation und `TEST_OK`.
Als nächste Produktionsdomäne startet HDA weiterhin vollständig auf dem BSP
und wird erst nach DMA-/IRQ-Bindung, Codec-Self-Test, gesunder Veröffentlichung
und `SCHEDULER_READY` AP-only. Der Audio-Service und der Legacy-PIC-Hard-IRQ
bleiben auf CPU 0. Der SMP4-Audionachweis beobachtet die erste autorisierte
Geräteoperation auf CPU 3 und fünf echte, lückenfreie PCM-Zyklen.
Zusätzlich bleiben Ein-CPU- und
`--no-apic`-Boots bis zur Ring-3-Shell erhalten.
