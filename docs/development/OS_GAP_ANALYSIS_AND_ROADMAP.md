# Fehlstellenanalyse und Implementierungsfahrplan

Stand: 27. August 2026

Dieses Dokument beschreibt den anhand des aktuellen Quellstands geprüften
Ist-Zustand, die wichtigsten noch fehlenden Betriebssystemfunktionen und eine
Reihenfolge, in der sie ohne unnötige Umbauten ergänzt werden können. Es ist
als Arbeits-Backlog gedacht: Jede Aufgabe besitzt eine feste ID, Abhängigkeiten
und überprüfbare Abnahmekriterien.

## 1. Zielbild und Abgrenzung

Das neue Projektziel ist ein **High-Assurance Research Operating System for
Fault-Tolerant and Fail-Operational Computing**, bei dem
Stabilität, Fehlerbegrenzung, nachweisbares Laufzeitverhalten und langfristige
Wartbarkeit vor Funktionsumfang und Geschwindigkeit stehen. Neue Funktionen
dürfen nur aufgenommen werden, wenn Fehlergrenzen, Diagnose, Rückfallpfad,
Ressourcenobergrenzen, Verifikation und Lebenszyklus geklärt sind. Redundanz ist
der bevorzugte Weg zu Verfügbarkeit, muss aber unabhängig sein und gemeinsame
Fehlerursachen berücksichtigen.

REIST besitzt einen generischen High-Assurance-Kern. Medizin, Raumfahrt,
Industrieautomatisierung und FPGA-Forschung sind getrennte Referenzprofile,
nicht die Identität des Betriebssystems. Der Forschungsprototyp ist weder
zertifiziert noch für einen sicherheitskritischen Produktionseinsatz
freigegeben. Der verbindliche Kernvertrag steht in
[`HIGH_ASSURANCE_CORE_CONTRACT.md`](../architecture/HIGH_ASSURANCE_CORE_CONTRACT.md);
der bisherige medizinische Vertrag ist ein optionales Referenzprofil.

## R8 · Duale x86-Architektur

Die x86_64-Migration erfolgt über getrennte, einzeln abgenommene Artefakte.
`i386` bleibt bis zur vollständigen Kernel-, Prozess-, Syscall-, Userspace- und
Hardwareabnahme unveränderter Standard und Fallback. R8.1a weist zunächst nur
den begrenzten Multiboot-zu-IA-32e-Übergang in einem eigenen Bootstrap nach.
Darauf folgen getrennt Kernel-Ausnahme-/Paginggrundlage, physische
Adressierung, ELF64-/Prozess-ABI, Userspace und erst zuletzt vollständige
Systemimages. Kein Zwischenstand wird als vollständiges 64-Bit-REIST-OS
bezeichnet.

**R8.1a ist umgesetzt:** Der getrennte Multiboot-Prototyp prueft CPUID und
Long-Mode-Unterstuetzung, nullt drei statische Seitentabellen, bildet genau die
ersten 2 MiB ab und aktiviert PAE, EFER.LME und Paging in festgelegter
Reihenfolge. Erst der 64-Bit-Zielcode prueft CR0.PG, CR4.PAE und EFER.LMA und
sendet danach den Erfolgsmarker. Sechs Quellvertragstests, der isolierte
Windows-Build und ein Ein-vCPU-/32-MiB-QEMU-Lauf bestanden. Das ist kein
vollstaendiger x86_64-Kernel; Exception-, Paging-, Prozess-, Syscall- und
Userspace-ABIs bleiben offen.

**R8.1b ist umgesetzt:** Das weiterhin getrennte Bootstrap-Artefakt besitzt genau
die 32 Architektur-Exceptiongates, normalisierte Fehlerframes, eine statische
64-Bit-TSS mit eigenem Double-Fault-IST und einen exakt validierten
`UD2`-Resume-Nachweis. Maskierbare Hardwareinterrupts, erweitertes Paging,
Prozesse, Syscalls und Userspace bleiben außerhalb dieses Pakets. Neun
Quellvertragstests, der warnungsfreie 16.844-Byte-Build und ein begrenzter
Ein-vCPU-/32-MiB-QEMU-Lauf bestanden. Der Lauf veröffentlichte geordnet
`LONG_MODE_BOOT_OK`, `EXCEPTION_IDT_READY`, `EXCEPTION_UD_OK` und
`EXCEPTION_RECOVERY_OK`.

**R8.1c ist umgesetzt:** Das isolierte Bootstrap-Artefakt besitzt feste
4-KiB-Seitentabellen für einen kanonischen Higher-Half-Kernelalias. Die
Abschnitte werden als Text-RX, RoData-R/NX und Data/BSS-RW/NX abgebildet;
`CR0.WP` und `EFER.NXE` werden verpflichtend. Nach dem Wechsel auf einen
Higher-Half-Stack wird die niedrige Übergangsabbildung entfernt. Ein exakt
validierter Instruction-Fetch-Page-Fault aus einer NX-Datenseite bildet den
Laufzeitnachweis. Multiboot-Speicherkarte, physischer Allocator und Direct Map
bleiben R8.1d vorbehalten.

Elf Quellvertragstests und der warnungsfreie 26.180-Byte-Build bestanden. Der
begrenzte Ein-vCPU-/32-MiB-QEMU-Lauf veröffentlichte geordnet Long Mode,
Higher-Half-Paging nach Low-Map-Widerruf, IDT-Bereitschaft, UD2-Resume, den
exakten NX-Page-Fault und den Abschlussmarker. Die PTE-Selbstprüfung ignoriert
ausschließlich die von der CPU gepflegten Accessed-/Dirty-Bits; Adresse,
Schreibrecht und NX bleiben exakt geprüft.

**R8.1d ist umgesetzt:** Die isolierte Scheibe validiert die
Multiboot-v1-Speicherkarte fail-closed und verwaltet nur vollständige
4-KiB-Frames unter einer festen 64-MiB-Grenze. Bootstrap, verwendete
Handoffdaten und Module bleiben reserviert; nicht nutzbare Einträge überstimmen
Überlappungen. Eine feste RW/NX-Direct-Map bildet ausschließlich verwaltbare
Frames ab. Drei reale Allokationen, Schreibzugriff, Free/Reuse und negative
Free-Fälle bilden den begrenzten Laufzeitnachweis. Dynamisches Paging,
Speicher oberhalb 64 MiB und produktive Kernelintegration bleiben offen.

Vierzehn Quellvertragstests und der warnungsfreie 29.788-Byte-Build bestanden.
Der begrenzte Ein-vCPU-/32-MiB-QEMU-Lauf verarbeitete den realen
Multiboot-v1-Handoff, allozierte drei eindeutige Frames, pruefte deren
beschreibbare NX-Direct-Map, Free/Reuse, unaligned und doppeltes Free und
stellte den Freizaehler wieder her. `PHYSICAL_MEMORY_OK` erschien geordnet
zwischen NX-Nachweis und Abschlussmarker.

**R8.1e ist umgesetzt:** Ein separat mit dem normalen x86_64-Assembler und
ELF64-Linker erzeugtes `ET_EXEC`-Probeabbild bildet den ersten Loadervertrag.
Der Gast akzeptiert hoechstens vier Program Header, zwei nicht ueberlappende
`PT_LOAD`-Segmente und acht Userseiten. Alle 64-Bit-Grenzen, Alignment,
Entry-Point und W^X werden vor Allokation validiert. Daten und BSS werden in
R8.1d-Frames gestaged, vollstaendig nachgeprueft und vor Erfolg wieder
freigegeben. Ausfuehrung, Ring 3, User-Seitentabellen und Syscalls folgen erst
in R8.1f. HPASA ist ein eigenstaendiges Projekt ausserhalb dieses
REIST-Repositories und kein Bestandteil der R8-Roadmap.

Alle 17 Quellvertragstests und der warnungsfreie Build des 45.156-Byte-
Bootstrap mit dem unabhaengigen 9.008-Byte-Probeabbild bestanden. Der begrenzte
Ein-vCPU-/32-MiB-QEMU-Lauf meldete `ELF64_LOAD_OK` geordnet nach dem
Speichernachweis und vor dem Abschlussmarker, nachdem alle staged Frames und
der urspruengliche Freizaehler wiederhergestellt waren.

**R8.1f ist umgesetzt:** Eine private feste Vier-Ebenen-Hierarchie bildet das
validierte ELF64-Abbild W^X-konform und genau eine getrennte User-Stackseite NX
ab. Der erste kontrollierte `IRETQ`-Eintritt erreicht den bestehenden
REIST-v1-`EXIT`-Syscall 9 ueber den AMD64-`SYSCALL`-Mechanismus erreichen; der
Entry wechselt vor der Validierung auf den TSS-gebundenen Kernelstack und
verwendet kein `SYSRET`. Ein zweiter Eintritt enthaelt einen CPL3-`UD2` lokal.
Erst nach Wiederherstellung des urspruenglichen CR3, Widerruf der
temporaren Syscall-MSRs, Loeschung aller User-PTEs und vollstaendiger
Framefreigabe darf der Erfolgsmarker erscheinen. Das ist noch kein Scheduler,
allgemeiner Syscall-Dispatcher oder produktiver 64-Bit-Userspace.

Alle 21 Quellvertragstests und der warnungsfreie 50.980-Byte-Build mit dem
unabhaengigen 9.048-Byte-Probeabbild bestanden. Der begrenzte
Ein-vCPU-/32-MiB-QEMU-Lauf beruehrte die User-Stackseite, akzeptierte exakt
Exit 9 mit Status 100, enthielt den erwarteten User-Vektor 6 und meldete
`USER_EXECUTION_OK` geordnet vor dem Abschlussmarker. Nur CPU-eigene
Accessed-/Dirty-Bits sowie das fuer den Fault gesetzte Resume-Flag werden bei
der Nachpruefung architekturgemaess behandelt; Mappingrechte und Adressen
bleiben exakt.

**R8.1g ist umgesetzt:** Der isolierte Nachweis besitzt genau zwei feste,
nichtnull generation-gebundene Prozessslots. Beide erhalten private CR3,
private writable ELF-Seiten und private NX-Stacks; nur validierter RX-Code darf
geteilt werden. Ein begrenzter kooperativer Scheduler verarbeitet nur
`YIELD` 40 und `EXIT` 9. Nach einem exakt gebundenen CPL3-`UD2` von Task B muss
nur dessen Generation terminal werden, waehrend Task A seine private
Datenseite erneut prueft und sauber beendet. Timerpreemption, SMP, allgemeine
Syscalls und produktive x86_64-Integration bleiben offen.

Alle 26 Quellvertragstests und der warnungsfreie 62.612-Byte-Build mit dem
9.264-Byte-ELF64-Probeabbild bestanden. Der begrenzte Ein-vCPU-/32-MiB-QEMU-
Lauf fuehrte drei exakte `YIELD`-Handoffs aus, reapte Task B nach dessen
generation-gebundenem CPL3-`UD2`, setzte Task A fort und akzeptierte dessen
`EXIT` 9 mit Status 101. `PROCESS_SCHEDULER_OK` erschien geordnet vor dem
Abschlussmarker, nachdem die vierzehn Lebenszyklusereignisse validiert und
CR3, TSS, Syscall-MSRs, Tasktabellen sowie alle Frames restauriert waren.

**R8.1h ist umgesetzt:** Die beschleunigte Scheibe buendelt die erste
maskierbare Interrupt- und Clockgrundlage. Genau IDT-Vektor 32 nimmt nach
Standard-PIC-Remap und festem 100-Hz-PIT-Setup drei validierte Kernel-IRQ0-
Ereignisse an. Ein fester TSC-Abbruch verhindert ein unbegrenztes Warten.
CPL3-Praeemption, LAPIC, IOAPIC und produktive Clockintegration folgen danach.

Alle 27 Quellvertragstests und der warnungsfreie 68.888-Byte-Build bestanden.
Der Ein-vCPU-/32-MiB-QEMU-Lauf nahm exakt drei generation- und
framevalidierte IRQ0-Ereignisse an, sendete drei Master-EOIs und meldete
`TIMER_IRQ_OK` vor dem Abschlussmarker. Die feste TSC-Grenze wurde nicht
ausgeschoepft; IF, IRQ0, PIC-Masken und temporaere Generation waren vor Erfolg
restauriert.

**R8.1i ist umgesetzt:** Der R8.1h-PIT ist mit dem privaten Zwei-Slot-
Scheduler verbunden. Nach genau einem `YIELD` von Task A preemptiert und reapt
genau ein generation- und framevalidierter IRQ0 die CPU-gebundene Task B. A
setzt danach mit intakter privater Datenseite fort und liefert `EXIT` 9/Status
102. Bs CPU-Loop besitzt eine feste TSC-Grenze.

Alle 28 Quellvertragstests und der warnungsfreie 70.964-Byte-Build mit dem
9.400-Byte-ELF64-Probeabbild bestanden. Der Ein-vCPU-/32-MiB-QEMU-Lauf
validierte den zehnteiligen Lebenszyklus, meldete `TIMER_PREEMPTION_OK` geordnet
vor dem Abschlussmarker und stellte PIC-Masken, IF, CR3, TSS, Syscall-MSRs,
Tabellen, Taskrecords, Frames und den urspruenglichen Freizaehler wieder her.

**R8.1j ist umgesetzt:** Der einmalige Preemption-Pfad ist zu einem endlichen
Round-Robin-Nachweis mit genau vier PIT-Quanten erweitert. Zwei CPU-gebundene
CPL3-Tasks setzen in der festen Folge A-B-A-B-A ihre vollstaendigen
unterbrochenen Kontexte fort und zeigen unabhaengigen privaten Fortschritt.
Danach wird nur B reaptiert und A beendet mit `EXIT` 9/Status 103.

Alle 29 Quellvertragstests und der warnungsfreie 73.820-Byte-Build mit dem
9.688-Byte-ELF64-Probeabbild bestanden. Der Ein-vCPU-/32-MiB-QEMU-Lauf nahm
exakt vier validierte IRQs und vier Master-EOIs an und meldete
`QUANTUM_SWITCH_OK` vor dem Abschlussmarker. Die vollstaendige Bereinigung
stellte Timer, PIC-Masken, IF, CR3, TSS, Syscall-MSRs, Tabellen, Taskrecords,
Frames und Freizaehler wieder her. Allgemeine Fairness und produktive
Schedulerintegration bleiben offen.

**R8.1k ist abgeschlossen:** Vier feste private Prozessgenerationen werden
ueber eine generationengebundene FIFO-Runqueue statt ueber vorbestimmte direkte
Spruenge gewaehlt. Die endliche Folge 0-1-2-3-0-2 kombiniert Yield, regulare
Exits und einen isolierten CPL3-Fehler. Doppelte und stale Eintraege muessen
ohne Queueaenderung abgewiesen und alle vier Generationen vollstaendig reaptiert
werden. 30 Quelltextpruefungen, der 81.524-Byte-Build mit 9.936-Byte-Probe und
der kurze Ein-vCPU-/32-MiB-Lauf sind gruen. `RUNQUEUE_LIFECYCLE_OK` beweist die
exakte Folge; der temporaere CPL3-Breakpoint-Gate, Queuezustand, Tasks und alle
Speicher- und Syscall-Ressourcen sind vor dem Abschlussmarker restauriert.

**R8.1m ist abgeschlossen:** Der zweisprachige Webauftritt beschreibt den
abgenommenen Stand bis R8.1k und trennt produktive i386-Basis, Ring-3-Dienste
und isolierten x86_64-Bootstrap. Acht echte i386-Laufzeitaufnahmen wurden gegen
ihre aktualisierten Bildtexte geprueft. Zwei neue echte 1024x768-Aufnahmen vom
QEMU-VMware-VGA-Pfad zeigen die aktuellen Desktop-Icons sowie Editor,
Scrollbars und About-Dialog. Drei lokale Vertragstests bestanden.

**R8.1l ist abgeschlossen:** Eine feste generationengebundene
Vier-Eintrag-Deadline-Queue verbindet `SLEEP_MS` 41 und `MONOTONIC_MS` 42 mit
der Vier-Slot-FIFO und dem isolierten 100-Hz-PIT. Drei Tasks blockieren relativ
fuer 30, 10 und 20 ms; der vierte liest monotone Zeit. Der feste Acht-Tick-
Horizont beruecksichtigt einen beim ersten Ring-3-Eintritt bereits anstehenden
PIT-Tick. Der Lauf weckt exakt 1-2-0, akzeptiert Status 120 bis 123 und verlangt
vor `DEADLINE_SLEEP_OK` leere genullte Queues, genullte Tasks sowie restaurierte
Timer-, PIC-, Speicher-, TSS- und Syscall-Autoritaet. 31 Quellvertragstests,
der 89.188-Byte-Build mit 10.088-Byte-Probe und der kurze Ein-vCPU-/32-MiB-
QEMU-Lauf bestanden. Die x86_64-Queue ist wieder leer; dynamische Tasks,
Prioritaeten, SMP und produktive Integration bleiben offen.

**R8.1n ist abgeschlossen:** Der Parent startet allein und erzeugt Kind-Slot 1
ueber `SPAWN` 23 erst nach begrenzter privater Pfadpruefung. `GETPID` 22 und
`WAIT` 24 behalten ihre REIST-v1-Konventionen. Wait bindet PID, Generation,
Elternidentitaet und privaten Statusausgang, blockiert ohne Polling, konsumiert
Status 77 genau einmal und erlaubt Slot-Wiederverwendung erst als Generation
32. Nullpfad, doppelter Spawn, fremde PID, Null-Ausgang und stales Wait werden
vor Seiteneffekten abgewiesen. 32 Quelltests, der 92.372-Byte-Build mit
10.264-Byte-Probe und der kurze QEMU-Lauf bestanden.

**R8.2a ist abgeschlossen:** Der isolierte ELF32-Multiboot-Container bettet ein
separat gelinktes ELF64-C-Payload ein und uebergibt nach allen R8.1-Markern einen gepackten,
versionierten 128-Byte-Vertrag an einen nach SysV AMD64 kompilierten
freestanding-C-Kern. Der getrennte Stack, die vollstaendige Vorvalidierung,
Data-/BSS-/Arithmetik-/Kopier- und Callback-Nachweise sowie die anschliessende
Loeschung von Handoff und C-Zustand werden vor `C_CORE_HANDOFF_OK` geprueft.
Buildregeln lehnen Hosted Runtime, Red Zone, Stackprotektor, Unwind,
Konstruktoren, undefinierte Symbole, Restrelokationen und W+X ab. Der produktive
i386-Pfad bleibt unveraendert; die x86_64-Queue ist wieder leer.
Die Abnahme bestand 37 Quelltests, den isolierten Build eines 106.808-Byte-
Bootstraps mit 13.328-Byte-ELF64-C-Payload und den begrenzten Ein-vCPU-QEMU-
Lauf bis `C_CORE_HANDOFF_OK`.

**R8.2b ist abgeschlossen:** Der gebuendelte Schnitt baut und startet eine
separate freestanding-ELF64-Shell in Ring 3. Die bestehenden REIST-v1-Indizes
`READ` 15, `WRITE` 20, `YIELD` 40 und `EXIT` 9 werden ueber einen vollstaendig
validierten, nicht wartenden seriellen Vermittlungspfad angebunden. Der kurze
QEMU-Gate fuehrt einen echten `INFO`-/`EXIT`-Dialog und verlangt danach die
vollstaendige Ruecknahme aller temporaeren User- und Syscall-Ressourcen. 41
Quellvertragstests, der 117.260-Byte-Bootstrap mit kompakter
1.256-Byte-RX-Shell und der begrenzte Ein-vCPU-/32-MiB-QEMU-Lauf bis
`RING3_SHELL_OK` bestanden; die x86_64-Queue ist leer.

**R8.2c ist in Arbeit:** Die interaktive Shell wird als genau eine
generationengebundene READY-Task in die bestehende feste x86_64-Runqueue
aufgenommen. Der Boot-Sonderaufruf entfaellt; nichtterminale READ-/WRITE-/
YIELD-Ergebnisse laufen ueber denselben validierten Scheduler- und IRETQ-
Rueckweg. EXIT muss die Generation exakt reapen, Queue, Seitentabellen,
Loaderauswahl, TSS, Syscall-MSRs und Frames bereinigen und den Freizaehler
wiederherstellen. VFS, allgemeine Terminaltreiber, Prioritaeten, SMP und
produktive i386-Integration bleiben getrennte Schritte.

Der grafische Ring-3-Launcher bleibt vorhanden, ist aber ausdrücklich
nicht-sicherheitskritisch. Desktop, Netzwerk, Dateisysteme und Diagnose dürfen
keine für das gewählte Profil wesentliche Funktion blockieren oder deren Zeitbudget
verbrauchen.

**REIST OS** steht für **Resilient Execution, Isolation and Stability
Technology**. Die aktuelle Architektur ist trotz des neuen Namens noch ein
modularer monolithischer Kernel: Scheduler, Speicherverwaltung und erhebliche
VFS-/Dateisystempolicy werden weiterhin in `kernel.bin` gelinkt. Ring-3-
Programme sind isoliert; Netzwerk, Storage, HDA und VMware-SVGA-II besitzen
bereits überwachte Userspace-Dienste beziehungsweise Treiber, nutzen aber noch
kleine oder migrationsbedingt spezialisierte Kernelmediatoren. Für das
High-Assurance-Ziel werden diese Restpfade schrittweise auf wenige generische
IPC-, Capability- und Resource-Primitiven zurückgeführt.

## 2. Zusammenfassung

Das OS ist kein Minimalgerüst mehr. Es bootet nativ über BIOS/MBR, besitzt
Paging, Ring-3-Prozesse mit eigenen Seitentabellen, validierte User-Pointer,
präemptives Round-Robin-Scheduling, ein VFS, schreibbares FAT12/FAT32, lesbares
EXT2, mehrere Gerätetreiber, einen kleinen IPv4-Stack und einen gebauten
Userspace. Der geprüfte Windows-Referenzbuild ist erfolgreich. Phase 0 sowie
R1.1, R1.2, R1.3 und der vor R2.1 eingeschobene Desktop-MVP R1.4 sind
abgeschlossen. Die aktuelle Hosttest-Suite und
automatisierte Ring-3-Tests prüfen neben dem normalen, LAPIC-gesteuerten Betrieb
einen eigenen PIT-Scheduler-Fallback ohne LAPIC sowie Speicherkonfigurationen
mit 32, 64, 256, 512 und 1024 MiB.

Die beim ersten Audit belegten Korrektheitslücken in Prozess-Wait,
Exception-Frames und PRG-v1-Vertrag sind in Phase 0 behoben. R1.1 hat darauf
eine allgemeine **Blockier-/Ereignisgrundlage** aufgebaut: intrusive
Wait-Queues, atomaren Prozess-Wait, blockierendes Sleep und Console-Input,
`yield`, 64-Bit-Zeit sowie einen kalibrierten Scheduler-Timer. R1.2 trennt nun
erkannten von verwaltetem Speicher, erweitert den Kernel-Heap dynamisch,
schützt statische und dynamische Kernelstacks mit Guardpages und räumt beendete
Tasks außerhalb langer IRQ-Sperrabschnitte auf. R1.3 definiert nun die
IRQ-, Präemptions-, Schlaf- und Lockverträge, serialisiert VFS und
ATA-/FDD-Zugriffe und verlagert Netzwerk- sowie HPET-Arbeit aus dem harten
IRQ-Kontext. Strukturierte Logs und vollständige Panic-Diagnosen schließen
den Meilenstein ab. R1.4 ergänzte den nativen VBE-Handoff und die erste
Ring-3-Display-ABI. Danach wurden Laufzeitgrafik, Window Manager, Explorer und
eine generationsgebundene Surface-/Event-Grenze aufgebaut; Notepad und Image
Viewer sind echte externe Fensterclients. Die abgeschlossenen Pakete R1.6 und
R1.7 ergänzen kernelvermittelte Ring-3-Gerätedomänen und HDA-Audio mit
Userspacebibliothek. Vor jedem weiteren regulären Funktionspaket steht das
Sicherheits-Gate S0. Die generische `REIST-research`-Baseline ist mit S0.6c
für die feste automatisierte QEMU-/VMware-Matrix abgeschlossen. Der
VMware-xHCI-Mauspfad und der VFS-Shadow-Zugriff des überwachten Compositors
sind zusätzlich end-to-end abgenommen. R6.2n serialisiert den gemeinsamen
xHCI-/HID-Zustand und weist den gesunden Compositor nach `SERVICE_READY` auf
einem AP mit loopbackgebundener RFB-Mauszustellung nach. Ein initialer
`VFS_ERR_IO` beim Laden des Desktops trat nur auf dem inzwischen
zurückgestellten AMD-Mainboard auf; dasselbe Image startet auf dem
unterstützten ASUS-Board. Die daraus abgeleiteten Pakete R2.2af bis R2.2ah
bleiben ohne Produktionskandidaten abgebrochen. Ein danach reproduzierter
WAV- oder Control-Gallery-Start blockiert den Compositor noch im synchronen
Legacy-Child-Wait und verbraucht dadurch sein Heartbeat-Restartbudget; R2.2ai
migriert beide Clients auf die Surface-Grenze und ergänzt konfigurierbare,
originale CC0-Systemklänge. Der getrennte physische xHCI-Control-Fehler
`cc=13` folgt als R5.2x. Das abgeschlossene R7.1a stellt fuer die weitere
Leistungsdiagnose ein begrenztes Ring-3-`BENCHMARK.PRG` fuer CPU, RAM,
Datentraeger und VGA bereit. R6.2o hat auf der VMware-/ASUS-Basis den
begrenzten BSP-Fence und die erneute post-READY-AP-Affinität nach einem
Heartbeat-Restart abgeschlossen. Physische,
zielhardwarespezifische und produktbezogene Nachweise bleiben sichtbar und
werden nicht durch die Emulatorabnahme ersetzt.

Bootpolicy seit 28. August 2026: Kein Target startet den grafischen Desktop
automatisch. QEMU, VMware und physische Images erreichen zuerst die Ring-3-
Shell; `DESKTOP` startet die grafische Sitzung ausschließlich nach einem
ausdrücklichen Benutzerkommando. Automatisierte Grafiktests müssen denselben
expliziten Eingabepfad verwenden.

### 2.1 Fortschrittsübersicht

Diese Liste ist der schnelle Einstieg in den Arbeitsstand. `[x]` bedeutet
umgesetzt und mit den im Paket genannten Tests abgenommen. `[ ]` bedeutet
offen. Ein Zusatz **in Arbeit** ist nur zulässig, wenn `active_id` in
`automation/reist-s03b.toml` auf genau dieses Paket zeigt. Nach Abschluss der
pfadgebundenen Read-Sessions ist auch
`R2.1-storage-claim-client-identity` als Voraussetzung stabiler
serviceeigener Objekt-Handles umgesetzt. Auch der stabile read-only Objekt-
Layer für FAT und EXT2 ist abgenommen. Auch explizite Rechteabschwächung und
generationgebundene Übergabe dieser Objekte sind abgenommen. Auch der
getrennte R2.1-Schnitt für echte, feste Standard-FDs 0/1/2 ist umgesetzt. Auch
die gemeinsame, generierte Syscall-/Fehlerquelle ist umgesetzt; `dup` und
kontrollierte Spawn-Vererbung bleiben anschließende R3.1-Schnitte.
Detailbeschreibung, Restrisiken und Abnahmekriterien bleiben in Abschnitt 7
und 10 verbindlich.

#### Abgeschlossene Grundlagen

- [x] R0.1 Atomarer Wait/Wakeup-Pfad
- [x] R0.2 Einheitliche Exception-Stubs einschließlich `#AC`
- [x] R0.3 Festgeschriebener und validierter PRG-v1-Vertrag
- [x] R0.4 Automatisierter Ring-3-QEMU-Smoke
- [x] R1.1 Wait-Queues, blockierendes Sleep/Yield und monotone 64-Bit-Zeit
- [x] R1.2 Speicherverwaltung, dynamischer Heap, Guardpages und Reaping
- [x] R1.3 Synchronisations-, IRQ-, Logging- und Panic-Vertrag
- [x] R1.4 Nativer VBE-Handoff und grafischer Ring-3-Desktop-MVP

#### High-Assurance-Gate S0

- [x] S0.1 Einsatzprofile, Gefahrenregister und vollständiger Assurance Case
  - [x] Generischer High-Assurance-Kernvertrag und getrennte Referenzprofile
  - [x] Maschinenprüfbares Gefahrenregister-v1-Schema mit eindeutigen IDs,
    FTTI, Safe-State, Restrisiko sowie existierenden Code-/Testreferenzen
  - [x] Gefahrenregister für alle Komponenten der ausgewählten generischen
    Forschungsbaseline; nicht ausgewählte Referenzprofile bleiben explizite
    Ausschlüsse und benötigen für ein konkretes Produkt eigene Gefahren
  - [x] Automatische, SHA-256-gebundene Traceability von Gefahr über konkrete
    Verifikation bis zum Testergebnis; JSON-Baseline unter
    `build/codex-agent/hazard-traceability.json`
- [x] S0.2 Vollständiges Stack-, Exception- und Panic-Containment für die
  automatisierte QEMU/VMware-Forschungsbaseline
  - [x] Guardpages, Double-Fault-Notfallpfad, Crashrecord und QEMU-Watchdog
  - [x] Compilerbasierte Callgraph-Gesamtbudgets und Rekursionsgate
  - [x] Laufzeit-Stack-Watermarks im bestehenden Scheduler-Stats-ABI
  - [x] S0.2a Maschinenprüfbarer Vertrag und physische Abnahmekriterien für
    externen Watchdog, Zielreset, Interlock und elektrisches Readback
  - [x] S0.2b Begrenzte QEMU-/VMware-Laufzeitabnahme; Zielhardware-Backend und
    physische Fault-Injection bleiben ausdrücklich manuelle Nutzerevidenz
- [x] S0.3a Begrenzte IPC-/Capability-Basis
- [x] S0.3b Überwachte, neu startbare Least-Privilege-Probedomäne
- [ ] S0.3c Reale Dienstmigration und Redundanz
  - [x] S0.3c-1 Begrenzter Ring-3-Diagnosedienst
  - [x] S0.3c-2 Freigabe delegierter Client-Capabilities ohne Quota-Leck
  - [x] S0.3c-3a bis 3r Geschützter, epochengebundener Netzwerk-Handoff
  - [x] S0.3c-4a Eng vermittelte ARP-Zustandsänderung aus Ring 3
  - [x] S0.3c-4b Geschützter ARP-Cache mit Ablaufzeit, Quellepoche,
    Redundanz und Fail-Closed-Lookup
  - [x] S0.3c-4c Dienstneustart widerruft Bindungen der alten Generation;
    begrenzter Scrub und Integritätseskalation sind nachgewiesen
  - [x] S0.3c-5 Netzwerkdatenpfad vollständig aus Ring 0 lösen
    und den Dienst unter Fehler-, Druck- und Restart-Injektion abnehmen
    - [x] S0.3c-5a Passive Gateway-Vertrauensentscheidung aus dem
      Ring-0-ARP-/IPv4-Pfad entfernt
    - [x] S0.3c-5b Lokale ARP-Auflösung und Antwortentscheidung über den
      überwachten Dienst vermitteln
      - [x] S0.3c-5b1 Lokale ARP-Antwortentscheidung mit geschützter,
        generationgebundener Einmalautorität vermittelt
      - [x] S0.3c-5b2a Deterministisch injizierten echten RX-Request samt
        vermittelter Antwort im RTL8139-Gast nachweisen
      - [x] S0.3c-5b2b Ausgehende lokale ARP-Auflösung in den Dienst migrieren
    - [x] S0.3c-5c ICMP-Echo-Antwort mit geschützter 250-ms-
      Einmalautorität, festem 32-Byte-Payloadlimit und echtem RTL8139-
      Request/Reply-Nachweis über Ring 3 vermitteln
    - [x] S0.3c-5d1 DHCP-Lease-Konfiguration über einen geschützten,
      generationgebundenen Ring-3-Entscheid und Syscall 73 publizieren
    - [x] S0.3c-5d2 UDP-Datenpfad und DHCP-Renew/Rebind schrittweise in den
      überwachten Dienst verlagern
      - [x] S0.3c-5d2a Begrenztes UDP-Echo auf Port 9000 mit 32-Byte-Limit,
        Pflichtprüfsumme und echtem RTL8139-Request/Reply vermitteln
      - [x] S0.3c-5d2b1 DHCP-Leasezeit aus dem ACK übernehmen, redundant
        schützen und die Netzkonfiguration bei Ablauf fail-closed entziehen
      - [x] S0.3c-5d2b2 Allgemeine UDP-Bindings sowie DHCP-Renew/Rebind
        generationgebunden in den Dienst verlagern
        - [x] S0.3c-5d2b2a Vier statische, generationsgebundene
          Dienst-Bindings mit 32-Byte-Datagrammen, Ablauf und Fence-Revoke
        - [x] S0.3c-5d2b2b DHCP-Renew/Rebind als begrenzten, nichtblockierenden
          Ring-3-Zustandsautomaten mit drei Versuchen je Phase, geschützter
          Einmaltransaktion und realem RTL8139-Renewal-Nachweis umsetzen
    - [x] S0.3c-5e Verbleibende IPv4-/UDP-/DHCP-Protokollzustände und den
      allgemeinen Socket-Demultiplexer aus Ring 0 in die Dienstgrenze verlagern
      - [x] S0.3c-5e1 Separate statische 8-Slot-RX-Queue und append-only
        Syscall 79 für einen vollständigen, generation-frischen 1518-Byte-
        Frame-Handoff mit realem RTL8139-Ring-3-Nachweis
      - [x] S0.3c-5e2 IPv4-/ICMP-/UDP-/DHCP-Parser und Protokollzustand über den
        neuen Handoff übernehmen und den parallelen Ring-0-Demux entfernen
        - [x] S0.3c-5e2a Begrenzten heapfreien IPv4-v1-Shadow-Parser mit
          Headerprüfsumme, Fragmentablehnung und realem RTL8139-Nachweis
          nach Ring 3 verlagern
        - [x] S0.3c-5e2b UDP-/DHCP-Demux und Protokollzustand auf dem
          Ring-3-Parser aufbauen und erst danach den Parallelpfad entfernen
          - [x] S0.3c-5e2b1 Heapfreien UDP-v1-Shadow-Parser mit Pflicht-
            prüfsumme, exakter Längenkonsistenz und RTL8139-Nachweis ergänzen
          - [x] S0.3c-5e2b2 UDP-Bindings und DHCP-Eingang aus dem validierten
            Ring-3-Ergebnis speisen und den Ring-0-UDP-Demux entfernen
            - [x] S0.3c-5e2b2a Dienstgebundene UDP-Ports über einen
              CRC-/generation-/deadlinegeschützten Ring-3-Entscheid speisen
              und deren parallele Ring-0-Zustellung unterbinden
            - [x] S0.3c-5e2b2b DHCP-Eingang und verbleibenden UDP-Demux aus
              Ring 0 lösen; Druck-, Restart- und Fehlpfade abnehmen
              - [x] S0.3c-5e2b2b1 Heapfreien DHCP-v1-Shadow-Parser mit
                begrenzten Optionen, BOOTP-/Cookie-Prüfung, optionaler
                IPv4-UDP-Prüfsumme und realem RTL8139-Nachweis ergänzen
              - [x] S0.3c-5e2b2b2 OFFER/ACK/NAK-Autorität aus dem validierten
                Ring-3-Ergebnis speisen und den Parallelpfad entfernen
                - [x] S0.3c-5e2b2b2a Renewal/Rebind-ACK/NAK über append-only
                  Syscall 81, Frame-CRC, Dienstgeneration und bestehende
                  Transaktionsautorität übernehmen; Ring-0-Queue und -Poller
                  während der Transaktion unterdrücken
                - [x] S0.3c-5e2b2b2b Boot-DISCOVER/OFFER/REQUEST/ACK als
                  begrenzten Ring-3-Zustandsautomaten übernehmen und danach
                  die dedizierte Ring-0-DHCP-Queue entfernen
                  - [x] S0.3c-5e2b2b2b1 Geschützte Boot-Transaktion mit
                    append-only Start-Syscall 82, drei endlichen
                    Dienstversuchen und realem RTL8139-Nachweis übernehmen
                  - [x] S0.3c-5e2b2b2b2 Tote synchrone Ring-0-DHCP-Routinen,
                    Queue und Poller entfernen sowie Restart-/Druckpfade ohne
                    Parallelzustellung abnehmen
        - [x] S0.3c-5e2c Heapfreien ICMP-Echo-v1-Shadow-Parser mit vollständiger
          Prüfsumme, fester 28-Byte-Ausgabe und realem RTL8139-Nachweis ergänzen
        - [x] S0.3c-5e2d ICMP-Eingangsautorität aus einem geschützten,
          CRC-/generations-/deadlinegebundenen Ring-3-Ergebnis speisen und den
          Ring-0-ICMP-Parser entfernen
        - [x] S0.3c-5e2e Verbleibenden Ring-0-IPv4-Demux und seine implizite
          ARP-Lernmutation durch validierte Ring-3-Entscheidungen ersetzen
    - [x] S0.3c-5f Verbleibenden Ring-0-ARP-Fallback und ungeschützten lokalen
      Legacy-Cache entfernen; ausschließlich überwachte ARP-Entscheide zulassen
  - [ ] S0.3c-6 Storage-/Dateisystemdienst und medienübergreifende Recovery
    - [x] S0.3c-6a Geschützte, nicht überlappende und absolut begrenzte
      Storage-/Dateisystem-Transaktionen mit Fail-Closed-Fence
    - [x] S0.3c-6b Versioniertes Block-/VFS-IPC und statische Request-Pools
    - [x] S0.3c-6c Ring-3-Storage-Service mit Capability-Profil und Restart
    - [x] S0.3c-6d Reale Power-Loss-/I/O-/Restart-Injektion in QEMU
      - [x] S0.3c-6d1 Dienstcrash bei beanspruchtem Request, generationssicherer
        Widerruf, begrenzter Restart und erfolgreicher Wiederholungsrequest
      - [x] S0.3c-6d2 Vermittelte ATA-I/O-Fehler mit definiertem Fehlerstatus,
        Quarantäne und geprüftem Weiterbetrieb
      - [x] S0.3c-6d3 Stromverlust während einer persistenten Mutation mit
        Neustart, Journal-Recovery und anschließendem Ring-3-Dienst-Selbsttest
    - [x] S0.3c-6e Automatische ATA-/FDD-Quarantäne und Requalifizierung über
      Geräteidentität, geschützten Fingerprint und zwei frische Reads; echter
      FDD-Disconnect/Reconnect mit Controllerreset und erneutem FAT12-Read;
      unklare Schreibabschlüsse nur read-only reintegrieren
    - [ ] S0.3c-6f Medienunabhängiges Undo/COW/Journal mit Flush-/Barrier- und
      Power-Loss-Nachweis für jeden beschreibbaren Datenträger; stärkere
      Wechselmedien-Identität und kontrolliertes Cache-Invalidieren/Remount
      - [x] S0.3c-6f1 Explizit markiertes FAT12-Undo-Journal mit redundanten,
        CRC-geschützten Headern, fester Kapazität, Recovery vor dem Lesen
        veränderlicher FAT-/Verzeichnismetadaten und fail-closed Verhalten bei
        beschädigtem oder erschöpftem Journal
      - [x] S0.3c-6f2 FAT12-Defektsektorverwaltung: fehlerhafte Datencluster mit
        dem standardisierten FAT12-Wert `0xFF7` quarantänisieren, Metadaten- und
        reservierte Sektoren über eine persistente, gespiegelte Remap-Tabelle
        auf vorab reservierte Ersatzsektoren abbilden und nicht rekonstruierbare
        Daten eindeutig melden statt stillschweigend zu ersetzen
      - [x] S0.3c-6f3 Copy-on-Write beziehungsweise replizierte Daten für
        kritische FAT12-Dateien, damit ein Lesefehler oder Defektsektor nicht
        nur erkannt, sondern aus einer validierten Kopie rekonstruiert werden
        kann; Auswahl über CRC, Sequenz und Dateisysteminvarianten
      - [x] S0.3c-6f4 Geordnete FAT12-Schreibtransaktionen für Dateiinhalt,
        FAT-Kopien und Verzeichniseinträge mit Readback-Verifikation; nach
        unklarem Abschluss ausschließlich Recovery oder `ONLINE_RO`, niemals
        blindes Wiederholen eines Writes
      - [x] S0.3c-6f5 Deterministische Fault-Injection nach jeder Persistenz-
        barriere: Teilwrite, beschädigte Journal-Kopie, beide beschädigten
        Kopien, defekter Daten-/FAT-/Root-Sektor, Medienauswurf und Stromverlust;
        - [x] 29 stabile Barrieren und unveränderte Referenzabbilder werden
          durch die Host-Matrix geprüft
        - [x] QEMU-FDD-Reconnect-Abnahme als Laufzeitevidenz
        - [ ] Reale VMware-Reconnect-Abnahme
      - [ ] S0.3c-6f6 Capability-gebundene Userspace-Wartungswerkzeuge ohne
        direkten DMA-/Controllerzugriff
        - [x] `FDISK.PRG` für validierte Partitionstabellen auf partitionierten
          Medien; Disketten bleiben standardmäßig partitionslose Superfloppies
        - [x] `FORMAT.PRG` für FAT12-Erzeugung sowie explizites FAT32-Quick- und
          Fullformat; beide FAT-Kopien werden verifiziert initialisiert, der
          Full-Modus markiert isolierte Defekte als `0x0FFFFFF7`
        - [x] `CHKDSK.PRG` für begrenzte BPB-/FAT12-Spiegelanalyse und
          explizit bestätigte, journalisierte Reparatur genau einer eindeutig
          strukturell beschädigten FAT-Kopie
        - [x] Begrenzte Root-/Unterverzeichnis- und Clusterkettenanalyse für
          ungültige Links, Loops, Crosslinks, kurze/überlange Ketten, Orphans
          und erschöpfte Scan-Kapazität; ausschließlich eindeutig überlange
          reguläre Dateiketten dürfen bestätigt journalisiert gekürzt werden
        - [x] Eindeutig kurze, normal EOC-terminierte reguläre Dateien durch
          bestätigte, journalisierte Begrenzung ihrer Directory-Dateigröße auf
          die tatsächlich lesbare Kettenkapazität reparieren
        - [x] Unerreichbare, nicht als bad markierte Clusterallokationen bei
          reiner Orphan-Diagnose bestätigt und journalisiert freigeben; keine
          automatische Eigentumszuordnung oder Datenrettung behaupten
        - [x] Reine Schleifen regulärer Dateiketten bestätigt am aus der
          Dateigröße bestimmten Präfix beenden und ausschließlich den danach
          unerreichbaren, unmarkierten Schleifensuffix freigeben
        - [x] Loopende Unterverzeichnisse über jeden eindeutigen Cluster
          vollständig diagnostizieren und bei reiner Directory-Loop-Diagnose
          ausschließlich den letzten Rücksprung durch EOC ersetzen
        - [x] Gleichzeitig kurze und zyklische reguläre Dateien durch EOC am
          letzten eindeutigen Cluster plus journalisierte Größenbegrenzung auf
          deren lesbare Kapazität atomar reparieren
        - [x] Crosslinks ausschließlich aus überlangen regulären Dateitails
          über feste Referenz-/Pflichtreferenzzähler erkennen und ohne Änderung
          der einzigen Sollkette journalisiert trennen
        - [x] Unterverzeichnisse mit gültigem Startcluster trotz unzulässiger
          Größe vollständig scannen und bei reiner Diagnose ausschließlich ihr
          32-Bit-Größenfeld journalisiert auf null setzen
        - [x] Ansonsten gültige Volume-Label-Einträge mit reserviertem
          Startcluster oder Größenfeld ungleich null bei reiner Diagnose
          journalisiert normalisieren, ohne Labelname oder Attribute zu ändern
        - [x] Reguläre Nullgrößendateien mit eindeutig besessener, normal
          terminierter Restkette bestätigt atomar auf Startcluster null setzen
          und ausschließlich diese nicht benötigte Allokation freigeben
        - [x] Positive Dateigrößen regulärer Einträge ohne Startcluster bei
          reiner Short-Diagnose bestätigt und journalisiert auf null begrenzen,
          ohne fehlende Daten oder Cluster zu erfinden
        - [x] Exakte `.`-/`..`-Einträge gegen aktuellen und Parent-Cluster
          validieren und bei korrekter Beziehung ausschließlich unzulässige
          Größenfelder bestätigt journalisiert auf null setzen
        - [x] Falsche niedrige Clusterfelder exakter Dot-Einträge bei Größe
          null bestätigt auf den deterministischen Self-/Parent-Cluster setzen;
          Root- und kombinierte Feldfehler bleiben gesperrt
        - [x] Reine mehrfach benötigte Crosslinks regulärer Dateien durch
          vollständiges verifiziertes Klonen späterer Sollketten in höchstens
          48 freie Cluster auflösen; Daten, beide FATs und Directory-Publikation
          gemeinsam undo-journalisieren
        - [x] Reine Same-Parent-Crosslinks strikt leerer, einclusteriger
          Unterverzeichnisse durch verifizierte Kopie, korrigierten `.`-Self
          und atomare Parent-Umbindung trennen
        - [x] **S0.3c-6f6q gebündelt:** `CHKDSK.PRG` für alle eindeutig
          attribuierbaren nichtleeren, mehrclusterigen, Same-Parent- und
          parentübergreifenden Directory-Crosslinks sowie zusammengehörige
          Verzeichnis-Topologiefelder; mehrdeutige Topologien bleiben
          fail-closed
        - [x] **S0.3c-6f6r gebündelt:** Orphan-Datenrettung in einen begrenzten,
          standardnahen `FOUND.000`-Namensraum mit Herkunftsnachweis statt
          ausschließlichem Verwerfen
        - [x] **S0.3c-6f6s gebündelt:** persistenter Journal-/Remap-/
          Defektkarten-Abschluss einschließlich QEMU-Maintenance-/Remount- und
          automatisiertem VMware-Reconnect-Nachweis; reale Hardware bleibt
          ausdrücklich manueller Nutzernachweis
        - [x] Exklusives Maintenance-Lease: vor Mutation unmounten, offene
          Handles ablehnen, Medienidentität erneut prüfen und nach Erfolg
          kontrolliert remounten; Abbruch lässt das Medium konsistent oder
          eindeutig read-only zurück
    - [x] S0.3c-6f7 FAT32-Schreibzulassung: fremde Volumes kompatibel lesbar,
      aber ohne gültiges REIST-Journal read-only mounten; vor jeder Mutation
      die exakte ATA-Journalbindung an Gerät und Volumegrenzen prüfen und eine
      verdrängte globale Bindung transaktionserhaltend neu aufbauen
    - [x] S0.3c-6f8 FAT12-Schreibzulassung: fremde Volumes kompatibel lesbar,
      aber ohne vollständig validiertes REIST12-Journal-/Remap-/Replikatlayout
      read-only halten und direkte FAT-/Sektormutationen vor Zustandsänderung
      abweisen
  - [ ] **S0.3c-7 teilweise:** Unabhängiger Standby-/Supervisor-Kanal und
    realer Handover
    - [x] S0.3c-7a Statischer Lease-/Epoch-/Fence-Protokollkern mit
      Split-Brain-, Stale-Epoch- und Integritäts-Fault-Tests
    - [ ] S0.3c-7b Plattformbackend für einen elektrisch und zeitlich
      unabhängigen Supervisor-Kanal samt rücklesbarem Fence
      - [x] S0.3c-7b1 Fest gebundener, statischer Request/Readback-Vertrag;
        Backendaufrufe außerhalb des IRQ-Locks und anschließende Revalidierung
      - [x] S0.3c-7b2a Prozessgetrennter QEMU-Host-Supervisor über einen
        dedizierten COM2-Kanal mit CRC-Frame, exaktem Epoch-Readback und
        realem Takeover-Lauf
      - [ ] S0.3c-7b2b Reales externes Transport-/Interlock-Backend auf
        Zielhardware mit eigener Stromversorgung und Zeitbasis
    - [x] S0.3c-7c Zwei reale Ausführungskanäle mit Zustandsreplikation,
      Selbsttest, Übernahme und kontrollierter Reintegration
      - [x] S0.3c-7c1 Zwei getrennte QEMU-Prozesse mit CRC-geschützter
        Epoch-Replikation, explizitem Standby-Ready, nachgewiesen beendetem
        Active vor Fence-Ack und vollständigem Ring-3-Smoke nach Übernahme
      - [x] S0.3c-7c2 Kontinuierliche Replikation des sicherheitsrelevanten
        Dienstzustands und kontrollierte Reintegration des reparierten Kanals
        - [x] S0.3c-7c2a Geschützter Referenz-Dienstzustand mit strikt
          monotonen Frames, drei Updates vor Failover, Epoch-Promotion und
          gefenceter Reintegration eines dritten QEMU-Kanals
        - [x] S0.3c-7c2b Produktionsdienstzustand mit begrenztem Catch-up,
          Selbsttest und kontrollierter Wiederaufnahme realer Ausgänge
    - [ ] S0.3c-7d Common-Cause-Analyse und wiederholte Zielhardware-Failover-Gates
- [ ] S0.4 Deterministische Planung und garantierte Ressourcen
  - [x] S0.4a Heapfreier, gewichteter Klassenzyklus mit statischen
    Safety-/Service-/Ambient-Klassen und begrenzter Auswahl über `MAX_TASKS`
  - [x] S0.4b Absolute CPU-Zeitfenster, Überlast-Erkennung und definierter
    degradierter Zustand
  - [ ] S0.4c Priority Inheritance für blockierende Ressourcen und
    nachgewiesene WCET-/Speicher-/Queue-Budgets
    - [x] S0.4c-1 Generationssichere, transitive Priority Inheritance für
      blockierendes IPC mit automatischer Rücknahme bei Wakeup, Timeout und Exit
    - [ ] S0.4c-2 Statische WCET-, Stack-, Speicher- und Queue-Budgetnachweise
      - [x] S0.4c-2a Maschinenprüfbares Kapazitätsregister für Scheduler,
        Kernelstacks, IPC und Storage einschließlich Quellcode-Driftprüfung
      - [ ] S0.4c-2b Laufzeitnachweise für High-Water-Marken,
        Kapazitätserschöpfung und vollständige Rückgewinnung
        - [x] S0.4c-2b1 Saturierende IPC-/Storage-Pool-Diagnostik mit
          Erschöpfungs- und vollständigem Rückgewinnungsnachweis
        - [ ] S0.4c-2b2 Task-, Heap-, Frame- und weitere statische
          Queue-High-Water-Nachweise
          - [x] S0.4c-2b2a Memory-ABI v2 mit Frame-/Heap-Peaks,
            saturierenden Fehlerzählern, v1-Kompatibilität und Gastnachweis
          - [x] S0.4c-2b2b Taskslot-High-Water sowie deterministische
            Heap-/Frame-ENOMEM-Fault-Injection
            - [x] S0.4c-2b2b1 Versionierte Taskslot-Diagnostik mit aktiver
              Belegung, High-Water, Reserve und saturierenden Ablehnungen;
              Gastnachweis für Erschöpfung und Rückgewinnung
            - [x] S0.4c-2b2b2 Deterministische Heap-/Frame-ENOMEM-Injection
              mit vollständigem Rollbacknachweis
          - [x] S0.4c-2b2c Versionierte, saturierende Laufzeitdiagnostik für
            die vier kernel-eigenen mediated-DMA-Pools mit Hostnachweis für
            Erschöpfung, Cleanup und Wiederverwendung sowie QEMU-HDA-Marker
      - [ ] S0.4c-2c Zielhardwarebezogene WCET- und Stack-Callgraph-Nachweise
        - [x] S0.4c-2c1 Unabhängiger GCC-Analysecompile mit vollständiger
          Stack-Usage-/Callgraph-Evidenz, lokalen 4096-Byte-Gates und
          fail-closed Rekursionsprüfung
        - [ ] S0.4c-2c2 Entry-/IRQ-Gesamtpfadbudgets und WCET-Messungen auf
          jeder ausgewählten Zielplattform
          - [x] S0.4c-2c2a Kumulative Legacy-/Scheduler-IRQ- und
            CPU-Exception-Stackbudgets mit vollständigem Handlerinventar und
            fail-closed Indirektaufrufen
          - [ ] S0.4c-2c2b Syscall-Gesamtpfade sowie bounded
            WCET-Baselines auf QEMU, VMware und ausgewählter Referenzhardware
            - [x] S0.4c-2c2b1 Kumulatives INT-80-/Syscall-Stackbudget mit
              Assemblyreserve sowie vollständigem VFS-/EXT2-Callbackinventar
            - [ ] S0.4c-2c2b2 Bounded WCET-Baselines auf QEMU, VMware und
              ausgewählter Referenzhardware
              - [x] S0.4c-2c2b2a Feste, saturierende Scheduler-/INT-80-
                Laufzeitdiagnostik mit maschinenlesbaren 10-ms-Grenzen und
                frischer QEMU-/VMware-Evidenz
              - [ ] S0.4c-2c2b2b Manuelle WCET-Messung auf eindeutig
                ausgewählter Referenzhardware
    - [x] S0.4c-3 Getestete fail-closed Degradation bei Scheduler-
      Zeitregression sowie per Device-Domain begrenzte IRQ-Stürme mit
      vollständigem Fence und separatem QEMU-Testprofil
- [ ] S0.5 Signierter Boot, redundanter Zustand und atomare A/B-Updates
  - [x] S0.5a1 Versioniertes Bootmanifest v2 mit exaktem Kernel-SHA-256 und
    unabhängigem, fail-closed HDD-/Floppy-Imagegate
  - [x] S0.5a2 Begrenzte SHA-256-Verifikation durch den Bootloader
  - [x] S0.5a3 Signierte Artefakte und gebundener Vertrauensanker
    - [x] S0.5a3a Hostseitige RSA-2048-PSS/SHA-256-Kernelsignatur mit
      gepinntem Research-Public-Key und fail-closed Buildgate
    - [x] S0.5a3b Signatur und Vertrauensanker in Stage 2 binden
  - [ ] S0.5b Redundante Bootkandidaten und atomare Updates
    - [x] S0.5b1 Feste signierte HDD-Slots A/B mit einmaligem, vollständig
      validiertem Stage-2-Fallback; persistenter Updatezustand folgt separat
    - [x] S0.5b2 Redundanter Boot-Control-Record, offline vorbereiteter
      Pending-B-Commit und zwei persistente Testboots mit Rollback auf A
    - [x] S0.5b3 CRC-geschützter Loader-Handoff, nach `BOOT_OK`
      generationgebundenes Ring-3-Erfolgs-Acknowledge, persistentes B und
      verifizierter bestätigter-B-Rollback auf A
    - [x] S0.5b4 Append-only Boot-Control v2 und atomarer Update-/
      Bestätigungspfad für den jeweils inaktiven Slot A oder B bei erhaltener
      v1-Lesesemantik
    - [x] S0.5b5 Fest begrenztes Offline-Update-Bundle mit unabhängigem
      Strukturparser, policy-gepinnter RSA-PSS-Prüfung und Nutzung des
      bestehenden atomaren A/B-Pfads in beiden Richtungen
- [ ] S0.6 Langzeit-, Fault-Injection- und Assurance-Nachweise
  - [x] S0.6a Deterministische, auf 16 bis 128 Fälle begrenzte
    Boot-Update-Bundle-Kampagne mit strukturierten Fehlerklassen,
    seed-gebundenen Einbitmutationen und nachgewiesenem Fail-before-output
  - [x] S0.6b Nach jedem nativen Image-Build erzeugtes und unabhängig
    validiertes SPDX-2.3-JSON-SBOM für Kernel, Signatur, BIOS-Image und alle
    paketierten Ring-3-Programme
  - [x] S0.6c Maschinenlesbarer Abschluss des automatisierten
    QEMU-/VMware-S0-Forschungsgates mit fester Host-, Paket-, Runtime- und
    Containment-Matrix; physische und produktbezogene Nachweise bleiben offen

#### Funktionsroadmap nach dem S0-Gate

- [ ] R2.1 Gemeinsame ABI v1 und vollständige Dateideskriptoren
  - [x] generationsgebundener Ring-3-Shadowpfad für `stat` über
    den bestehenden festen Storage-Request-Pool; Legacy-VFS bleibt bis zum
    Äquivalenznachweis autoritativ
  - [x] heapfreier, fest begrenzter FAT32-/ASCII-VFAT-`stat`-Parser im
    Ring-3-Storage-Service; Veröffentlichung nur bei bytegenauer
    Legacy-Äquivalenz
  - [x] kontrollierter Cutover des kurzlebigen `STAT.PRG`, inzwischen auf die
    autoritative FAT-Parseroperation 4, mit fester Pfadnormalisierung, monotoner
    Deadline, vollständiger Antwortvalidierung und ohne Legacy-Fallback;
    weitere langlebige Clients warten auf ihre getrennte Umstellung
  - [x] append-only Syscall 118 für generation- und handlegebundenes
    Request-Cancel; geclaimte Requests bleiben bis zur Dienstquittierung
    `cancel-pending`, Ergebnisautorität wird widerrufen, physischer I/O-Abbruch
    oder Rollback wird nicht behauptet
  - [x] `HTTPD.PRG` als erster lang laufender FAT32-Metadatenclient ohne
    Legacy-`stat`-Fallback; zwölf echte QEMU-HTTP/TCP-Anfragen belegen
    wiederholten Betrieb, Ring-3-Erfolgsmarker und Rückkehr zur Shell
  - [x] append-only Operation 3 und begrenzter FAT12-Parser mit
    fester Rootdirectory, 12-Bit-Clusterketten und echtem QEMU-FDD-Stat-Nachweis
  - [x] append-only Operation 4 als autoritativer, ausschließlich
    parserbasierter FAT12-/FAT32-`stat`-Pfad ohne `SYS_STAT`-Ergebnisabhängigkeit
  - [x] begrenzter read-only EXT2-Parser und append-only
    Operation 5 als autoritativer generischer FAT12/FAT32/EXT2-`stat`-Pfad
  - [x] append-only Operationen 6/7 für begrenztes `read-at`
    und indexiertes `readdir-at`; `CAT.PRG` und `LS.PRG` werden ohne
    Kernel-VFS-Fallback umgestellt
  - [x] vier feste, generationsgeschützte und kanonisch
    pfadgebundene Read-only-Sessions mit `read`, `seek`, `fstat`, `close` sowie
    vollständiger HTTPD-Umstellung; stabile Inode-Handles und Vererbung folgen
  - [x] append-only Claim-v2-Mediation der exakten Client- und
    Servicegeneration als fehlende Besitzergrenze für stabile Objekt-Handles;
    Syscall 119 liefert einen getrennten 40-Byte-v2-Deskriptor, während
    Syscall 68 und Claim v1 bytegenau 28 Byte groß bleiben
  - [x] fester ownergebundener Service-Handlepool mit
    FAT-Directory-Entry- beziehungsweise EXT2-Inode-Locatoren, Medienidentität
    und pfadfreien read-only Folgeoperationen 9 bis 11 nach einmaligem Open
  - [x] explizite READ-/SEEK-/STAT-/DELEGATE-Rechte und
    abschwächende Übergabe an eine exakt generationgebundene Zielidentität;
    keine ambiente Spawn-Vererbung; das Storage-Rescue-Image mit vollständigen
    Unicode-15-Tabellen bleibt durch 224 KiB Einzellimit und den statischen
    448-KiB-Gesamtpool fest begrenzt
  - [x] echte prozesslokale Standarddeskriptoren 0/1/2 mit
    READ-only `stdin`, WRITE-only `stdout`/`stderr`, nichtblockierendem
    Tastaturpoll und unveränderten acht dynamischen Slots 3 bis 10
  - [x] append-only Syscall 120 mit `RDONLY`/`WRONLY`/`RDWR`, exakten
    Descriptorrechten, `CREAT` und per Schreibaufruf neu bestimmtem `APPEND`;
    `TRUNC` setzt journalmarkierte REIST-FAT12-/FAT32-Dateien inzwischen
    transaktional auf Länge null; andere Adapter bleiben fail-closed
- [ ] R2.2 VFS-/FAT-Zuverlässigkeit und vollständige Sync-Semantik
- [x] R2.3 Blockgeräte, Partitionen und moderne Storage-Abstraktion
- [ ] R3.1 Pipes, Signale, Prozessgruppen und TTY
- [ ] R3.2 Userspace-Shell und Init-/Service-Management
- [ ] R4.1 Gehärtetes IPv4/UDP und Socket-ABI
- [x] R4.2 DNS
- [x] R4.3 TCP
- [ ] R5.1 ACPI-, DMA- und Plattformbasis
- [ ] R5.2 xHCI/USB in überprüfbaren Stufen
- [ ] R6 Optionale Modernisierung: UEFI, SMP, 64 Bit und Highmem
  - [x] R6.1 begrenzter i386-SMP-Bootstrap mit vier CPUs, affinen
    Kernelprobetasks, Reaping und vollständigem 32-Slot-Kapazitätsnachweis
  - [ ] R6.2 allgemeine Mehrkernverteilung regulärer Kernel- und
    Ring-3-Dienste nach Migration der verbleibenden Treiberzustände; erste
    autoritätslose Driver-Fault-Domäne und VMware-SVGA2D-Normal- sowie
    Timeout-/Restartpfad sowie HDA-Normal- und profilgebundener Restartpfad
    AP-fähig; auch normale und nach Heartbeat-Timeout neu gestartete
    Audio-Servicegenerationen sowie normaler und neu gestarteter Storage-Service
    AP-fähig; auch normale und neu gestartete Netzwerkdienstgenerationen
    AP-fähig; der Session-Compositor besitzt nun als Voraussetzung einen
    generationsgebundenen Supervisor-Lebenszyklus; R6.2n serialisiert xHCI/HID
    und hat seinen normalen post-READY-AP-Pfad abgenommen; die AMD-spezifischen
    Startup-Hypothesen R2.2af bis R2.2ah sind abgebrochen; R2.2ai beseitigt
    zuerst die synchronen Sound-Player-/Control-Gallery-Child-Waits und bindet
    konfigurierbare Systemklänge an begrenzte Ring-3-Kinder; R5.2x klärt danach den
    physischen xHCI-Control-Fehler; R6.2o hat anschließend den getrennten
    Compositor-AP-Restartnachweis mit erneuter AP-Ausführung und xHCI-Maus-
    Zustellung abgeschlossen; weitere
    Produktionsdomänen
    bleiben offen

#### Verbindliche Priorität nach dem S0-Gate

1. VFS- und FAT-Policy vertikal aus Ring 0 migrieren: erst read-only Shadow-
   Äquivalenz, danach parser-eigene Ring-3-Autorität und erst zuletzt Mutation.
2. Die bereits produktiven HDA- und SVGA2-Treiber weiter unter dem gemeinsamen
   Driver-Host-/Resource-Mediator-Modell vereinheitlichen.
3. Nach stabiler Dienstmigration spezialisierte Übergangs-Syscalls auf wenige
   generische IPC-, Capability- und Resource-Primitiven konsolidieren.
4. Desktop-/Compositor-Crash, Generationstausch und Client-Reintegration als
   demonstrierbaren Recovery-Fall umsetzen.
5. TCB-Inventar und Kernel-Restbestand maschinenlesbar machen und gegen jeden
   neuen Bestandteil prüfen.
6. Erst danach größere Featureblöcke wie zusätzliche Controls, IPv6, TLS oder
   UEFI priorisieren.

## 3. Verifizierter Ist-Zustand

| Bereich | Vorhanden | Reifegrad |
|---|---|---|
| Boot | BIOS/MBR, zweistufiger Loader, E820, A20, ELF32-Prüfung, Kernel-CRC32, FAT12-Floppy, optionaler nativer VBE-LFB-Handoff | stabiler Referenzpfad mit VGA-Rückfall |
| CPU | GDT/IDT/TSS, Ring 0/3, Exceptions, PIC, gegen PIT kalibrierte lokale APIC-Timer, PIT-Scheduler-Fallback, `INT 0x80`, begrenzter ACPI-/xAPIC-SMP-Bootstrap mit vier CPUs | isolierte AP-Kernelproben abgenommen; reguläre Dienste bleiben CPU-0-affin |
| Speicher | fail-closed normalisierte E820-Karte, 1-GiB-Directmap, Frame-Accounting, dynamischer Kernel-Heap, Kernel-Stack-Guardpages, getrennte Prozessadressräume, sichere User-Kopien | R1.2 plus erster S0.2-Schutz; Speicher oberhalb 1 GiB nur erkannt |
| Prozesse | Spawn mit `argc/argv`, Exit-Status, atomarer Wait, Wait-Queues, Sleep/Yield, eigenes CWD, IPC-v1, generationsgebundene Capabilities, endliche Deadlines, Restartreserve, überwachte Ring-3-Domänen und CPU-affiner SMP-Kernelprobelauf | 32 feste Taskslots; allgemeine Mehrkern-Dienstausführung und dynamisch erweiterbare Taskkapazität fehlen |
| Dateien | VFS, REIST-FAT12/FAT32 read/write, fremde FAT12/FAT32 read-only, ASCII-VFAT-LFN, Undo-Journale, FAT12-Remap/Replikate/Fehlermatrix, `fsync`, Same-Directory-Rename/Replace, EXT2 read-only | Unicode-Normalisierung, allgemeiner transaktionaler Reparaturpfad und medienunabhängige Persistenz fehlen |
| Geräte | PCI, ATA/IDE, AHCI/SATA, FDD, PS/2, experimentelles xHCI-HID, VGA/VBE/QEMU-DISPI, überwachtes Ring-3-VMware-SVGA-II-2D und kernelvermitteltes HDA | mehrere QEMU-/VMware- und einzelne reale Nachweise; breite Hardware-, IOMMU- und Hotplugmatrix fehlt |
| Netzwerk | E1000, RTL8139, RTL8168/8111G, NE2000, Ethernet, ARP, IPv4, ICMP, DHCP, UDP-/TCP-FD-Sockets, DNS und HTTP/1.0 | Host- und QEMU-Nachweise vorhanden; kein IPv6, TLS oder vollständiges POSIX-Socketmodell |
| USB | xHCI-Initialisierung, Root-Port-/Descriptorpfad und HID-Boot-Tastatur/-Maus einschließlich automatisiertem VMware-Desktop-Mausnachweis | einfache reale Geräte und VMware-Maus abgenommen; Composite-AULA, allgemeiner Hotplug und Mass Storage offen |
| Userspace | SDK mit getrennten GUI-/Audio-/Image-Bibliotheken, Ring-3-Shell, Systemprogramme, Surface-Compositor, Explorer, Notepad und Image Viewer als Fensterclients | Sound Player und Control Gallery werden in R2.2ai migriert; Terminal und Systemwerkzeuge bleiben offen |
| Qualität | Host-/Quelltests, Imagevalidatoren, QEMU-Runtimeprofile für Ring 3, Netzwerk, Storage, Handover, Grafik/Surface und PCI-Audio sowie manuelle VMware-/Hardwareevidenz | breite Hardware-, Langzeit-, EMV- und reale Power-Loss-Matrix fehlt |

Maßgebliche Quellen sind der ausführbare Code und die Tests. Der aktuelle
Architekturüberblick in `docs/architecture/ARCHITECTURE_DEEP_DIVE.md` beschreibt
die vorhandene Ring-3-Isolation, den mit R1.1 eingeführten Blockier- und
Zeitvertrag und den R1.2-Vertrag für Speicher, Kernelstacks und Reaping. Der
verbindliche R1.3-Kontext- und Lockvertrag steht ergänzend in
`docs/architecture/SYNCHRONIZATION_CONTRACT.md`.

## 4. Belegte Korrektheits- und Basislücken

### C-01: Verlorenes Aufwecken beim Prozess-Wait — P0

**Status: in R0.1 behoben und in R1.1 verallgemeinert.** `syscall_wait()`
prüft den Kindstatus und blockiert auf der kindeigenen `exit_waiters`-Queue
unter derselben IRQ-geschützten Synchronisationsgrenze. Exit und Kill
veröffentlichen zuerst den Status und wecken danach alle registrierten
Waiter. Der frühere PID-Sonderpfad ist durch den generischen Wait-Queue-Vertrag
ersetzt.

### C-02: Falscher Exception-Frame für `#AC` — P0

**Status: in R0.2 behoben.** Der Prozessor legt für Alignment Check einen
Fehlercode auf den Stack. Der frühere Stub legte zusätzlich einen künstlichen Nullwert ab.
Dadurch interpretiert der gemeinsame Rückkehrpfad den restlichen CPU-Frame
falsch. Vector 17 muss wie die anderen Exceptions mit CPU-Fehlercode behandelt
und durch einen gezielten Regressionstest abgesichert werden.

### C-03: Unklarer bzw. nicht nutzbarer PRG-Relokationspfad — P0

Der beim Audit geprüfte Loader las das Image bei `0x02100000`, rief aber vor
dem Anlegen und Befüllen des Prozessadressraums `apply_relocation()` auf.
Diese Funktion erwartete die Relokationstabelle innerhalb des noch
ungemappten Zielbereichs ab `0x40000000`. Nichtleere Tabellen konnten so nicht
funktionieren; R0.3 hat diesen toten Pfad entfernt.

Die aktuelle Toolchain lehnt Laufzeitrelokationen ab und erzeugt Images für die
feste Adresse `USER_BASE`. Der Loader weist nichtleere Relokationstabellen
deshalb ausdrücklich und getestet vor dem Mapping zurück. Eine spätere
Formatversion kann Segmente und Relokationen sauber beschreiben.

### C-04: Busy-Wait und falsche Semantik bei langen Delays — P1

**Status: in R1.1 für Ring 3 behoben.** `SYS_DELAY` behält ABI-Nummer 2,
blockiert User-Tasks aber nun auf derselben geordneten Deadline-Queue wie
`sleep_ms`; der feste 10-Sekunden-Abbruch entfällt. `pit_delay()` bleibt als
bewusst aktiver Kernel-/Hardware-Helfer für Kontexte erhalten, in denen kein
Task blockiert werden darf. Der Syscall unterscheidet beide Pfade anhand des
Aufrufer-Privilegs.

### C-05: Erkannter und tatsächlich verwalteter Speicher weichen ab — P1

**Status: in R1.2 behoben.** Der gemeinsam eingeblendete, Supervisor-only
Kernelbereich und der Frame-Allocator decken konsistent die ersten 1 GiB bis
`USER_BASE` ab. Vollständige nutzbare Frames in diesem Fenster werden
verwaltet; von E820 gemeldeter Speicher oberhalb der Grenze wird nur als
`detected_usable_bytes` gezählt und nicht vergeben. `SYS_MEMORY_KB` (13)
liefert kompatibel die verwaltete Kapazität, nicht mehr den gesamten erkannten
Wert. Der versionierte `SYS_MEMORY_STATS` (43) trennt erkannt, verwaltet,
reserviert, belegt und frei sowie Heap-Kapazität und -Fragmentierung. Ein
späteres Highmem-/`kmap`-Fenster soll Speicher oberhalb 1 GiB nutzbar machen.

### C-06: Programmseiten sind pauschal schreibbar — P1

Der Loader mappt in `kernel/proc/process.c:269-301` Programm und Stack mit
`PAGE_USER | PAGE_RW`. Es gibt keine getrennten Text-/RODATA-/Datenrechte und
keine W^X-Politik. Eine neue PRG-Formatversion sollte Segmentgrenzen und Rechte
transportieren. Auf i386 ohne PAE/NX ist vollständiges W^X nicht erreichbar,
aber schreibgeschützte Code- und RODATA-Seiten sind trotzdem sinnvoll.

### C-07: Laufzeitverhalten wurde in CI nicht automatisch geprüft — P1

**Status: Kernlücke in R0.4 behoben.** CI bootet nun das erzeugte native Image,
führt den Ring-3-Test aus und sichert bei Fehlschlägen das serielle Protokoll.
Die breitere Laufzeitabdeckung für DMA, Netzwerk und weitere Hardware sowie die
separate Bereinigung der optionalen Legacy-Image-Tests bleiben Folgearbeiten.

## 5. Fehlende Funktionen nach Subsystem

Dieser Abschnitt ist eine thematische Bestandsaufnahme und keine zweite
Statusliste. Deshalb verwendet er normale Aufzählungspunkte. Verbindliche
Erledigt-/Offen-Checkboxen stehen ausschließlich in der Fortschrittsübersicht,
in der Arbeitsreihenfolge, im unmittelbar nächsten Arbeitspaket und in echten
Abnahmechecklisten.

### Prozess, Scheduler und IPC

- **S0.3a umgesetzt:** 16 statische Endpoints, acht Capabilities je Prozess,
  vier Nachrichten je Queue, 128 Byte Nutzlast, blockierendes Send/Receive,
  explizite abschwächende Delegation ohne `CONTROL` und vollständiger
  Exit-Widerruf
- **umgesetzt:** ein Taskslot, ein Prozessslot und 32 Frames bleiben für
  explizite Supervisor-Spawns reserviert
- **umgesetzt:** vollständiges append-only Syscall-Inventar bis Nummer 115;
  Default-Deny-Probeprofil und generation-sicheres Child-Kill vor jedem
  Seiteneffekt
- generische Wait-Queues auf weitere Geräte- und Protokollereignisse anwenden
- Pipes, Prozessgruppen und ein kleines Signalmodell
- `waitpid(-1, ...)`, optionales nichtblockierendes Warten und saubere
  Reparenting-/Reaper-Semantik
- dynamische oder zumindest deutlich größere Tasktabelle über die aktuelle
  feste Grenze von `MAX_TASKS 32` hinaus
- Prioritäten erst nach korrekter Blockierung; Threads und allgemeine
  SMP-Dienstverteilung deutlich später, der begrenzte R6.1-Bootstrap ist
  abgenommen
- Guardpage-Abdeckung auf weitere benutzergesteuerte Abbildungen ausweiten;
  User- und Kernel-Stacks besitzen bereits feste Guardpages
- aussagekräftigere Prozessstatistiken

### Syscall- und Userspace-ABI

R1.1 erweitert die bestehende ABI ohne Umnummerierung: `SYS_YIELD` (40),
`SYS_SLEEP_MS` (41) und `SYS_MONOTONIC_MS` (42) sind im SDK verfügbar.
`SYS_DELAY` (2) bleibt kompatibel und blockiert Ring-3-Tasks über dieselbe
Sleep-Infrastruktur; `SYS_UPTIME` (12) liefert weiterhin das niedrige
32-Bit-Wort. Die vollständige monotone 64-Bit-Zeit schreibt Syscall 42 nach
Pointerprüfung in den Userspace. R1.2 ergänzt `SYS_MEMORY_STATS` (43). Dessen
v1-Struktur beginnt mit `uint32_t version` und `uint32_t struct_size`, ist
88 Byte groß und enthält zehn `uint64_t`-Zähler. Der Aufrufer übergibt Größe
und Version separat; der Kernel lehnt unbekannte Versionen und zu kleine
Puffer vor dem geprüften Copyout ab. Der ältere `SYS_MEMORY_KB` (13) bleibt
erhalten und meldet die vom Frame-Allocator verwalteten KiB. R1.4 hängt
`SYS_DISPLAY_INFO` (44), `SYS_FILL_RECT` (45) und `SYS_DRAW_TEXT` (46) an.
Die versionierten Requests werden geprüft kopiert und geclippt; Ring 3 erhält
kein direktes Framebuffer-Mapping. `SYS_RENAME` (47) ergänzt atomisches Rename
innerhalb desselben FAT32-Verzeichnisses; volumen- oder
verzeichnisübergreifendes Verschieben wird fail-closed abgelehnt.
`SYS_FSYNC` (48) führt writable Deskriptoren über Prozess-FD und VFS bis zu
einem begrenzten ATA-`FLUSH CACHE`; Timeout oder Storage-Fence werden als
Fehler an Ring 3 zurückgegeben. S0.3a hängt `SYS_IPC_CREATE` (49),
`SYS_IPC_SEND` (50), `SYS_IPC_RECEIVE` (51) und `SYS_IPC_CLOSE` (52) an. Die
versionierte 128-Byte-Nachricht wird vollständig über validierte User-Kopien
übertragen; rohe Kernelpointer verlassen die Prozessgrenze nicht.
`SYS_IPC_DELEGATE` (55) bindet eine nichtleere Teilmenge von `SEND|RECEIVE`
an die unter Präemptionsschutz aufgelöste aktuelle Generation einer Ziel-PID.
`CONTROL` und Ambient-Spawn-Vererbung sind ausgeschlossen.

Die Dateiinformationsstruktur führt inzwischen zusätzlich Erstellungs-,
Änderungs- und Zugriffszeit als Unix-Sekunden. FAT12/FAT32 werden über
`fs/vfs/vfs_time.h` validiert konvertiert; Syscall 108 (`x86os_touch`) setzt
mtime/atime. Erzeugung, Inhalts- und Größenänderung publizieren die zugehörigen
FAT-Felder in derselben Metadatentransaktion. Reads, `stat`, `fstat` und
`readdir` schreiben kein implizites atime. Die Grenzen der FAT-Auflösung
(2 Sekunden für mtime, Tagesdatum für atime) sowie die lokale, nicht näher
spezifizierte Zeitzoneninterpretation sind dokumentiert.

- eine einzige gemeinsame, versionierte Quelle für Syscallnummern und
  Fehlercodes
- stabile `errno`-ähnliche Fehlersemantik; aktuell werden VFS-Fehler oft auf
  allgemeine Werte wie `-2`, `-5` oder `-9` reduziert
- `open`-Flags (`RDONLY`, `WRONLY`, `RDWR`, `CREATE`, `TRUNC`, `APPEND`)
- `lseek`, `fstat`, `truncate`, später `dup`/`dup2`; FAT32/ATA-`fsync` und
  Rename sind als erste persistente Spezialfälle vorhanden
- echte Deskriptoren 0/1/2 für Standard-Ein-/Ausgabe
- ABI-Fähigkeitsabfrage, damit ältere Programme kontrolliert weiterlaufen

### VFS, Dateisysteme und Blockgeräte

- Rename auf FAT12 sowie verzeichnis- und volumenübergreifendes Verschieben;
  FAT32 Same-Directory-Replace ist journalgestützt atomar
- konsistente Open-Handle-, Delete- und Unmount-Semantik unter Nebenläufigkeit
- eine generische `block_device`-Schnittstelle mit `read`, `write`, `flush`,
  Sektorgröße und Kapazität statt direkter ATA/FDD-Kopplung
- persistentes FAT12-Undo-Journal, COW/Replica-Schutz für kritische Daten und
  eine gespiegelte Defektsektor-/Remap-Tabelle; `0xFF7` allein schützt nur
  Datencluster und kann verlorene Inhalte nicht rekonstruieren
- capability-gebundene Ring-3-Werkzeuge `FDISK.PRG`, `FORMAT.PRG` und
  `CHKDSK.PRG`; kein Userspace erhält direkten FDC-, DMA- oder Portzugriff
- Partitionen als eigene Blockgeräte; derzeit wird pro physischem Laufwerk nur
  ein gefundenes Dateisystem automatisch gemountet
- vollständige MBR-Prüfung und später GPT; Partitionsgrenzen bei jeder I/O
  erzwingen
- ATA LBA48 und Multi-Sektor-I/O; aktuell ist der PIO-Treiber auf LBA28
  begrenzt (`drivers/block/ata.h:25`)
- FAT-Schreibreihenfolge, Fehlerpropagation und Power-Loss-Tests
- vollständige Unicode-VFAT-Kodierung und Normalisierung; ASCII-LFN bis 255
  Zeichen samt checksum-validiertem 8.3-Fallback ist implementiert
- [x] FAT-Zeitstempel über VFS; FAT12/FAT32 initialisieren und aktualisieren
  Zeitfelder transaktional, EXT2 liefert vorhandene Inode-Zeiten, bleibt aber
  ohne schreibendes `touch`
- mehrere FAT12-Volumes; der Adapter besitzt aktuell nur einen globalen
  `mounted_fat12_fs`
- EXT2 entweder klar dauerhaft read-only halten oder erst nach den
  Zuverlässigkeitsarbeiten vollständig schreibbar machen

### Terminal, Shell und Desktop

Die heutige Console-Eingabe vermeidet bereits Busy-Waiting für reguläre
Ring-3-Tasks: `getchar` prüft den Puffer und reiht den Task atomar auf der
PS/2-eigenen Input-Wait-Queue ein. COM1 ist bewusst output-only und kann weder
Tastencodes veröffentlichen noch PS/2-Leser wecken.
Eine vollständige TTY-Schicht bleibt der nächste darüberliegende Ausbau.
Der Desktop umgeht die feste Terminalgeometrie über Pixelrechtecke,
Pixelschrift und eine versionierte Frame-ABI. Window Manager, Explorer,
Maus-/Tastaturfokus, Drag/Resize, Dirty Regions und eine
generationengebundene Surface-/Event-IPC sind umgesetzt. Notepad, Image
Viewer und die Systemsteuerung laufen als getrennte Ring-3-Fensterclients.
Der überwachte Compositor verbindet den generationsgebundenen Displaydienst,
präsentiert den Softwarezeiger ohne Präemptionssperre über sleepfähigen Locks
und darf ausschließlich VFS-Shadow-Requests über den begrenzten Storage-
Transport senden. Der VMware-Lauf belegt Root-Explorer und Mausbewegung in
dieser Domäne mit geordneten End-to-End-Markern.
Eine feste klassische Taskbar bietet ein aufwärts öffnendes Startmenü,
kapazitätsbegrenzte Fensterbuttons und eine validierte Datums-/Minutenuhr;
Fensteraktivierung bleibt ein typisiertes WM-Ereignis und die Uhr invalidiert
nur bei geändertem Anzeigetext ihre eigene Fläche.
Der compositorinterne Explorer verwendet einmalig geladene 32x32-ICO-Assets
mit festen Cachevarianten und unterscheidet `.PRG`, Text/Konfiguration, WAVE,
BMP/GIF/ICO, unbekannte Dateien sowie nachweislich leere und nichtleere
Ordner. `.` und `..` werden aus dem begrenzten Snapshot entfernt; Rendering
führt keine VFS-Zugriffe oder Dekodierung aus. Der Desktop besitzt nun eine
feste, erweiterbare DragSource/Object/DropTarget/Operation-Schicht und einen
wiederherstellbaren Single-User-Papierkorb. Explorer-Objekte werden an ihren
Snapshot gebunden und erst nach erneuter Identitätsprüfung per atomarem
Same-Directory-Rename auf einen reservierten, ausgeblendeten 8.3-Namen
verschoben. Ein zentraler Katalogmarker und `.trashinfo`-Metadaten mit Original-
und Storage-Pfad machen den Eintrag im Papierkorb sichtbar, ohne die bewusst
unsupported Cross-Directory-Rename-Grenze von FAT32 zu umgehen;
Cross-Volume-Copy/Delete und geschützte Systempfade scheitern geschlossen.
Das Papierkorbfenster bietet nun eine sichtbare Wiederherstellen-Aktion sowie
Doppelklick/Enter; Zielkollision oder manipulierte Metadaten scheitern vor dem
Rename. Ein Rechtsklick auf das Desktop-Icon öffnet ein begrenztes Kontextmenü
mit Öffnen und Papierkorb leeren. Endgültiges Leeren benötigt eine
applikationsmodale Ja/Nein-Bestätigung und verwendet feste Traversierungsbudgets.
Sound Player und Control Gallery werden in R2.2ai als unabhängige
Surface-Clients migriert. Das Terminal verwendet noch die begrenzte
Vollbildbrücke; eine vollständige TTY- und Terminal-Clientarchitektur bleibt
offen. Die Systemsteuerung zeigt vier begrenzte Einstellungs-Applets; derselbe
separate Ring-3-Konfigurationsdienst akzeptiert zusätzlich die fünfte,
versionierte Systemklangtabelle für eine spätere grafische Auswahl.
Derselbe R2.2ai-Schnitt ergänzt ohne Änderung des v1-Formats einen einzelnen
2048-Byte-IPC-v2-Bulkslot: 504 Frames pro Schreibvorgang begrenzen eine volle
Vorschau auf 31 Roundtrips. WAV-Dateien werden in einem Vorwärtsdurchlauf
gelesen und die Wiedergabe beginnt vor der Sound-Player-Surface.
Dynamisches Neuladen durch bestehende Treiber ist noch offen und wird nicht
behauptet.

- TTY-Abstraktion mit kanonischem/raw Modus, Echo und per-Prozess
  Vordergrundgruppe
- `Ctrl+C` als Signal an die Vordergrundgruppe statt Sonderbehandlung direkt
  im `getchar`-Syscall
- dynamische Terminalgröße; mehrere Syscalls prüfen heute fest gegen 80x25,
  obwohl der Framebuffer andere Größen besitzen kann
- Quotes/Escapes, Umgebungsvariablen, persistenter Verlauf und Exitcodes in der
  Userspace-Shell; ein flüchtiger, fester 32-Einträge-Ring mit Cursor-Up/Down
  und Entwurfswiederherstellung ist umgesetzt
- Pipes, Ein-/Ausgabeumleitung und Hintergrundjobs nach Fertigstellung von
  Deskriptoren, Wait-Queues und Signalen
- Editor: das sichere `TEMP -> fsync -> close -> rename`, vollständig
  validiertes RFC-3629-UTF-8 mit Skalarcursor, bytegenaue Persistenz sowie
  horizontale und vertikale Scrollleisten mit geklemmtem Viewport sind
  umgesetzt; dynamischer Puffer, Suche, Auswahl/Clipboard, Graphemnavigation,
  Bidi/Shaping/IME und die Aufhebung des Limits von 200 Zeilen fehlen
- ein kleines Ring-3-`init` als PID 1 statt direktem Shellstart durch den Kernel
- Mausereignisse, Fokusmodell, Compositor und Windowmanager als getrenntes
  späteres Paket statt Erweiterung der schmalen Display-ABI

### Netzwerk

- ARP-Erneuerung sowie DHCP-Renew/Rebind
- robuste IPv4-Fehlerpfade und definierter Umgang mit Fragmenten; aktuell
  werden Fragmente verworfen
- UDP-Socketobjekte sind als Prozessdeskriptoren mit `bind`, `sendto`,
  `recvfrom`, begrenzter ARP-/Empfangsdeadline und Cleanup umgesetzt; der
  QEMU-Datenpfad ist über DNS im kombinierten Gasttest belegt.
- DNS-A/CNAME, begrenzte Kompressionszeiger und ein kleiner TTL-Cache sind
  umgesetzt und gegen einen lokalen deterministischen QEMU-Testpeer belegt.
- Der TCP-Pfad besitzt feste CCBs, Sequenz-/ACK-Prüfung, Retransmission, RTO,
  Fenster, aktiven/passiven Close sowie begrenztes `listen`/`accept` mit kleinem
  Backlog. Aktiver und passiver TCP-Handshake, Nutzdaten und Close sind im
  QEMU-End-to-End-Test belegt.
- `nc.prg` ist als TCP-Client vorhanden. `httpd.prg` bedient begrenzte
  HTTP/1.0-`GET`-/`HEAD`-Anfragen aus `/htdocs` einschließlich Directory-Listing;
  TLS/HTTPS bleibt ein eigenes späteres Paket.
- IPv6 erst nach einer belastbaren IPv4-/Socket-Schicht
- reale H81M-K-Gegenprobe des nun vorhandenen RTL8111G-/RTL8168-PCIe-Treibers;
  die QEMU-Referenz emuliert diesen Controller nicht
- VMXNET3 nur implementieren, wenn E1000 nicht mehr als VMware-Referenz reicht;
  der vorhandene Treiber deaktiviert das Gerät absichtlich

### USB und moderne Hardware

- DMA-API für physische Adressen, Alignment, 32-Bit-Grenzen und kohärente
  Puffer
- vollständiger xHCI-Reset und Controllerstart, Command-/Event-/Transfer-Ringe,
  Doorbells und Interrupts
- Root-Port-Status, Geräteadressierung, Deskriptoren und Konfiguration
- Hub-Unterstützung, danach HID-Tastatur und USB-Massenspeicher
- der heutige Code endet nach PCI/BAR/IRQ-Ausgabe und lässt xHCI deaktiviert
  (`drivers/usb/xhci.c:4-25`); `hub.c` und `hid_kb.c` sind Platzhalter
- zentrale, validierte ACPI-Schicht statt der isolierten experimentellen
  HPET-Suche; danach HPET als optionalen Clocksource, IOAPIC und
  Poweroff/Reboot integrieren
- AHCI/NVMe erst nach Block- und DMA-Abstraktion

### Sicherheit, Diagnose und Produktreife

- dokumentiertes Bedrohungsmodell: vorerst vertrauenswürdiger Single-User oder
  später Benutzer/Rechte/Capabilities
- Prozess-Kill ist generation-sicher auf eigene Kinder bzw. explizite
  Supervisor-Autorität begrenzt
- Dateirechte und ACLs; FAT besitzt derzeit keine Berechtigungsmetadaten
- keine kryptografische Boot-Authentizität: CRC32 erkennt Beschädigung, aber
  keinen absichtlichen Austausch
- Zufallsquelle/CSPRNG, ASLR und sichere Netzwerk-Defaults erst bei einem
  Sicherheitsziel
- optionale Panic-Symbolauflösung zusätzlich zum vorhandenen Register-/CR2-
  Kontext und der SHA1-Build-ID
- Debug-Buildprofil, statische Analyse und hostseitiges Sanitizer-/Fuzzing für
  Parser
- reproduzierbare Gasttests und eine kleine Hardwarematrix

## 6. Abhängigkeiten

```text
P0-Korrektheit
  -> Wait-Queues + monotone Zeit [R1.1 erledigt]
       -> Pipes + TTY + Signale -> Shell-Jobs
       -> Socket-Deskriptoren -> UDP -> DNS -> TCP -> Anwendungen

R1.3-Kontext-/Lockvertrag [erledigt] -> VFS/Blockgeräte + ACPI/DMA
Native VBE + Display-ABI [R1.4 erledigt] -> Desktop-Härtung -> später Fenstersystem/Maus
ABI/FD-Ausbau -> VFS rename/truncate/fsync -> sicherer Editor und Dateitools
Blockgeräte -> Partitionen + DMA -> AHCI/NVMe und USB-Massenspeicher
ACPI + DMA -> xHCI -> USB-Enumeration -> HID/Storage
R1.2-Directmap bis 1 GiB -> Highmem/kmap oberhalb 1 GiB -> später x86-64 und
allgemeine SMP-Dienstverteilung

Generic High-Assurance Gate S0
  -> Einsatzprofil + Gefahren + Essential Functions + FTTI
  -> Fehlerdomänen + minimaler Safety-Kern + unabhängiger Supervisor
  -> Stack-/Exception-Containment + Watchdog + Knoten-Failover
  -> deterministische Ressourcen + persistente Integrität + sichere Updates
  -> Safety Case + Traceability + Fault-Injection + Langzeitnachweis
  -> erst danach weitere Funktionspakete
```

TCP oder USB vor diesen Grundlagen zu bauen würde dieselben Warte-, Timeout-,
Deskriptor- und Pufferprobleme mehrfach lösen lassen.

## 7. Schrittweiser Implementierungsplan

Die Größen `S`, `M`, `L` und `XL` sind relative Aufwände, keine Zeitangaben.
Es soll immer nur das erste noch offene Paket begonnen werden, sofern dessen
Abhängigkeiten erfüllt sind. Die folgenden Listen beschreiben Umfang und
Reihenfolge innerhalb eines Pakets; sie sind keine zweite Statusanzeige und
verwenden deshalb normale Aufzählungspunkte. Der verbindliche Erledigt-/Offen-
Status steht in den Abschnitten 2.1, 8 und 10.

### Phase 0 — Korrektheit und verlässliche Nachweise

#### R0.1 Atomarer Wait/Wakeup-Pfad — S

**Status (13. August 2026):** Kernfix umgesetzt und hostseitig validiert. Der
Statuscheck und die Registrierung als `TASK_WAITING` sind auf dem aktuellen
Single-Core-Kernel durch einen gemeinsamen IRQ-geschützten Abschnitt atomar.
Der neue Regressionstest `test/test_wait_source.py` schützt diese Invariante;
die aktuelle Gesamtabnahme umfasst 134 Hosttests, den
Windows-QEMU-Referenzbuild und 64
erfolgreiche Spawn/Wait-Zyklen im automatisierten Gasttest.

- Kindstatusprüfung und Registrierung des aktuellen Tasks als `TASK_WAITING`
   auf dem Single-Core-System in einem gemeinsamen IRQ-geschützten Abschnitt
   ausführen.
- Kind-Exit, Kill und normales Exit über denselben Wakeup-Pfad führen.
- Status genau einmal konsumieren; verwaiste Zombies kontrolliert aufräumen.
- Einen deterministischen Test-Hook für einen Kind-Exit im bisherigen
   Race-Fenster sowie einen Gast-Stresstest mit vielen Spawn/Wait-Zyklen bauen.

**Fertig, wenn:** Kein Test hängt, jeder Exitstatus wird genau einmal geliefert
und der Elternprozess verbraucht während des Wartens keine CPU.

#### R0.2 Exception-Stubs vereinheitlichen — S

**Status (13. August 2026):** Umgesetzt. Alle Vektoren 0–31 verwenden die
Makros `ISR_NO_ERROR_CODE` oder `ISR_CPU_ERROR_CODE`; Vector 17 (`#AC`) nutzt
nun korrekt den CPU-Fehlercode. Compile-Time-Assertions sichern Offsets und
Größe des gemeinsamen `Registers`-Frames. Die vollständige Fehlercode-Matrix
ist hostseitig getestet.

- Vektoren mit und ohne CPU-Fehlercode tabellarisch definieren und Stubs aus
   zwei Makros erzeugen.
- Vector 17 korrigieren; 8, 10–14, 17, 21, 29 und 30 explizit prüfen.
- Layout von Assembly-Frame und `Registers` mit statischen Offsets absichern.
- Ring-3-Tests für Divide-by-zero, Invalid Opcode und Page Fault ergänzen;
   kontrollierte Testpfade für Fehlercode-Exceptions hinzufügen.

**Fertig, wenn:** User-Exceptions beenden nur den Verursacher, Kernel-Exceptions
liefern einen korrekten Diagnoseframe und kein `iret` verwendet verschobene
Stackdaten.

#### R0.3 PRG-v1-Vertrag festziehen — S

**Status (13. August 2026):** Umgesetzt. Validator, Loader, Builder und
Dokumentation akzeptieren ausschließlich die feste Basis `0x40000000`, keine
Relokationen und keine Nachlaufbytes. Historische, unbenutzte ELF- und
Relokationspfade im Kernel wurden entfernt; negative Hosttests decken falsche
Basen, Relokationen, Überläufe, Überlappungen und ungültige Einstiegspunkte ab.

- Für Version 1 nur `base_address == USER_BASE` und
   `relocation_size == 0` akzeptieren.
- Validator, Loader, Python-Builder und Dokumentation auf dieselben Regeln
   bringen; tote alternative ELF-Lader aus dem Laufzeitpfad entfernen.
- Negative Tests für Relokationen, Überläufe, überlappende Bereiche und
   falsche Entry-Points ergänzen.
- Anforderungen an ein späteres segmentbasiertes PRG v2 separat notieren.

**Fertig, wenn:** Jedes vom Builder erzeugte Image lädt und jede nicht
unterstützte Variante vor dem Mapping eindeutig abgelehnt wird.

#### R0.4 Automatisierter Gast-Smoke-Test — M

**Status (13. August 2026):** Umgesetzt. `scripts/run_qemu_smoke.py` bootet das
native Image headless und unveränderlich in QEMU, startet `GTEST.PRG` über
emulierte PS/2-Tasten und erzwingt die über COM1 beobachtete Markerfolge
`BOOT_OK` → `TEST_OK` mit hartem Timeout. Der Ring-3-Gasttest prüft 64 Spawn/Wait-Zyklen, genau einmal
konsumierbare Exitstatus, Datei-I/O über mehrere 512-Byte-Syscall-Blöcke sowie
kontrollierte `#DE`-, `#UD`- und `#PF`-Prozessabbrüche. Makefile und CI führen
den Test aus und bewahren das serielle Protokoll.

- QEMU mit serieller Konsole, festem Timeout und eindeutigem
   `BOOT_OK`/`TEST_OK`-Protokoll starten.
- Einen Ring-3-Teststarter ins Image legen, der Userspace-Schutz,
   Spawn/Wait, Datei-I/O und Exceptions prüft; nach dem Erfolg beendet der
   Host QEMU kontrolliert.
- Den Smoke-Test direkt an das erzeugte `build/reist-os.img` binden;
   Legacy-Fixture-Tests getrennt halten.
- Den Smoke-Test in CI ausführen und Logs als Artefakt sichern.

**Fertig, wenn:** Ein CI-Lauf nicht nur kompiliert, sondern bis Ring 3 bootet,
Tests ausführt und bei Panic, Triple Fault oder Timeout fehlschlägt.

### Phase 1 — Scheduler, Zeit und Speicher als gemeinsame Grundlage

#### R1.1 Wait-Queues, Sleep und Yield — L

**Status (13. August 2026): Abgeschlossen.** Tasks besitzen genau einen
intrusiven Wait-Knoten; FIFO-Wake-one, Wake-all, Entfernen und stabile
Deadline-Sortierung sind als generische Operationen implementiert. Der
Prozess-Wait prüft Zustand und Queue-Registrierung atomar und verwendet die
kindeigene Exit-Queue. Kill, Exit und Task-Slot-Wiederverwendung entfernen
veraltete Queue-Mitgliedschaften.

Die PIT-IRQ liefert eine atomar gelesene, 64-Bit-monotone Millisekundenzeit.
Eine Bruchteilakkumulation berücksichtigt den tatsächlich programmierten
PIT-Divisor, statt jeden IRQ ungenau als exakt eine Millisekunde zu behandeln.
Sleep verwendet eine geordnete 64-Bit-Deadline-Queue, setzt den Taskstatus auf
`TASK_SLEEPING` und gibt den Prozessor ab; `yield` wechselt ohne Pollschleife
zu einem anderen bereiten Task. Die Console-Eingabe blockiert nach atomarer
Leerprüfung auf der PS/2-Wait-Queue. COM1 ist davon getrennt und dient nur der
Diagnoseausgabe.

Der lokale APIC-Timer wird gegen die bereits laufende PIT-Zeit kalibriert und
treibt danach die Scheduler-Quanten. Fehlt der LAPIC, schedult IRQ0 nach dem
PIC-EOI über den PIT-Fallback. HPET gehört bewusst nicht zu R1.1; seine
Integration bleibt bis zur validierten ACPI-/Plattformbasis in R5.1
zurückgestellt.

- Generische Wait-Queues mit Wake-one/Wake-all einführen.
- 64-Bit-monotone Zeit und geordnete Deadline-Liste implementieren.
- `sleep_ms` und `yield` als Syscalls anbieten; `SYS_DELAY` kompatibel darauf
   abbilden.
- Keyboard- und später Netzwerk-I/O auf blockierende Events vorbereiten;
   COM1-Diagnose bleibt output-only.
- APIC-Timer gegen PIT kalibrieren und bei fehlendem LAPIC einen
   Scheduler-Fallback bereitstellen.

**Abnahme erfüllt:** 134 Hosttests prüfen unter anderem Queue-Invarianten,
stabile Deadline-Reihenfolge, atomare 64-Bit-Lesezugriffe, ABI-Nummern und die
PIC-EOI-Reihenfolge des Fallbacks. `GTEST.PRG` beobachtet einen Kindprozess als
`SLEEPING`, führt währenddessen einen anderen Task aus, prüft Deadline-Wakeup,
`yield`, den kompatiblen `SYS_DELAY`, ungültige 64-Bit-Ausgabezeiger sowie
Kill und Task-Slot-Reuse. Der Gasttest ist sowohl für den kalibrierten
LAPIC-Pfad als auch separat mit `-cpu qemu32,-apic` für den PIT-Fallback in
Makefile und CI verdrahtet. Die 64-Bit-Deadline vermeidet den früheren
32-Bit-Tick-Wrap als praktische Laufzeitgrenze.

#### R1.2 Speicherverwaltung und Schutz — L

**Status (13. August 2026): Abgeschlossen und abgenommen.** Die E820-Einträge
werden sortiert und überlappungsfrei normalisiert. Nicht nutzbare Bereiche,
Multiboot-Strukturen und Module übersteuern nutzbare Einträge; beschädigte,
abgeschnittene oder wegen der festen Regionstabelle nicht vollständig
darstellbare Bootkarten werden fail-closed verworfen.

Der Supervisor-only Kernelanteil bildet die ersten 1 GiB physisch direkt ab
und endet exakt an `USER_BASE`. Der Frame-Allocator verwaltet nur vollständige
E820-Frames innerhalb dieses Fensters und sucht ab einem umlaufenden
Next-Fit-Hinweis. Speicher oberhalb 1 GiB bleibt in
`detected_usable_bytes` sichtbar, wird ohne späteres Highmem-/`kmap`-Fenster
aber nicht vergeben. Für die Framezähler gilt:

```text
managed_bytes = reserved_bytes + allocated_frame_bytes + free_frame_bytes
```

Der Kernel-Heap startet mit etwa 1 MiB und wächst bei Bedarf in mindestens
256-KiB-Schritten aus zusammenhängenden, im Directmap dauerhaft reservierten
Frames. Die adresssortierte Blockliste teilt Blöcke und vereinigt nur physisch
angrenzende Nachbarn; getrennte Arenen werden nie über Lücken hinweg
zusammengelegt. Der Heap schrumpft derzeit nicht und `k_malloc`/`k_free` sind
wegen IRQ-Sperre, Metadatensuche und möglichem Frame-Scan keine APIs für harten
IRQ-Kontext.

R1.2 führte zunächst 64-Byte-Canaries ein. S0.2 hat sie inzwischen durch echte
nicht-präsente Guardpages ersetzt: eine volle Seite unter dem Bootstack sowie
je eine Seite unter und über jedem dynamischen 8-KiB-Kernelstack. Scheduler-
Grenzen prüfen Slot, Mapping und ESP-Bereich. Beendete Tasks wechseln beim
atomaren, owner- und
generationsvalidierten Detach in `TASK_REAPING`. Seitentabellen und Kernelstack
werden anschließend mit aktivierten Hardware-Interrupts, aber unterdrückter
Taskpräemption freigegeben; erst danach darf der Slot wiederverwendet werden.

- Erkannte, verwaltete, reservierte und freie Frames separat zählen.
- Framezugriff oberhalb 256 MiB durch ein konsistentes Directmap bis 1 GiB
   ermöglichen und die Grenze explizit ausweisen.
- Kernel-Heap erweiterbar machen und belegte/freie Bytes exportieren.
- Statische und dynamische Kernelstacks mit 64-Byte-Canaries und
   ESP-Bereichsprüfungen schützen.
- Allokationsfehler, Fragmentierung und wiederholtes Prozess-Reaping testen.

**Abnahme erfüllt:** `MEMINFO` nutzt die versionierte 88-Byte-v1-Struktur von
`SYS_MEMORY_STATS` (43); `SYS_MEMORY_KB` (13) meldet weiterhin kompatibel die
verwalteten KiB. Boottests prüfen Allokation, Reallokation, Freigabe,
Fragment-Reuse, Heapwachstum und einen schreibbaren Frame ab 256 MiB. Der
Ring-3-Test prüft die Zählerinvariante, ungültige User-Pointer, User-
Allokation/Freigabe und 64 Spawn/Exit/Wait/Reap-Zyklen ohne Frame- oder
Heapdrift. Der PRG-Loader hält Images nicht mehr in einem festen physischen
8-MiB-Stagingbereich, sondern lädt sie in einen passend großen temporären
Kernelheap-Puffer, kopiert sie in private Userseiten und gibt den Puffer auf
jedem Erfolgs- und Fehlerpfad frei. Dadurch bootet CI den vollständigen
Ring-3-Test mit 32, 64, 256, 512 und 1024 MiB; bei 512 MiB werden sowohl der
kalibrierte LAPIC-Pfad als auch der PIT-Fallback ohne APIC ausgeführt.

Nicht Teil von R1.2 waren systematische Failure-Injection für jede
Teilallokation, ein Highmem-/`kmap`-Fenster oberhalb 1 GiB, Guardpages und ein
IRQ-tauglicher Allocator. Die Kernel-Guardpages waren der erste umgesetzte
Teil von S0.2; die automatisierte QEMU/VMware-Baseline ist inzwischen
abgeschlossen.

##### R1.2a Experimenteller resilienter Seitenspeicher — abgeschlossen

Der erste Memory-Resilience-Schritt ergänzt die abgenommene allgemeine
Speicherverwaltung um höchstens vier explizite, kernel-eigene 4096-Byte-
Objekte. Zwei als A und B bezeichnete simulierte Fehlerdomänen besitzen je
zwei Copy-on-write-Bänke. Nutzdaten werden durch CRC32, veröffentlichte
Generation, Bankauswahl und Zustandsübergang zusätzlich durch den bestehenden
Critical-Object-Umschlag geschützt.

Die Abnahme verlangt die unveränderte letzte Commit-Generation nach einem
Fehler vor oder zwischen der Replica-Vorbereitung, `DEGRADED` nach Verlust
genau einer Domäne, fail-closed `FAILED` nach Verlust aller gültigen Replicas
und einen begrenzten Rebuild auf feste Ersatzspeicherplätze. Der Pfad enthält
keinen Heap, keine VFS-, DMA- oder blockierende Operation und verändert keine
beliebigen PTEs.

Diese Scheibe ist ein Softwarezustands- und Fehlerinjektionsnachweis. Reale
Frame-Zonen, SMBIOS/IMC-Adressdekodierung, MCE/EDAC, physische
DIMM-/Rank-/Channel-Unabhängigkeit und transparente Prozessseiten bleiben
getrennte, hardwaregebundene Folgestufen.

**Abnahme erfüllt:** Der Hostvertrag prüft vier Slots, stale Handles,
Copy-on-write-Commitgrenzen, Einzel- und Doppelverlust, CRC-Korruption,
gleichgenerationigen Konflikt sowie erfolgreichen und unterbrochenen Rebuild.
Der vollständige QEMU-VGA-Paketbuild linkt das Modul in das normale
Kernelimage. R1.2a macht noch keinen Laufzeitclaim im Gast.

##### R1.2b Resilienter Boot-Fehlernachweis — abgeschlossen

Ein ausschließlich im Testbuild vorhandener Bootpfad führt die feste
Domänenverlust-/Rebuild-Kampagne einmal vor allgemeiner Prozessaufnahme aus.
Geordnete Marker werden erst nach bytegenauer committed-Datenprüfung,
Weiterbetrieb eines unabhängigen Objekts und validiertem HEALTHY-Rebuild
ausgegeben. Der QEMU-Runner muss danach weiterhin Scheduler, Userspace und
`TEST_OK` beobachten und Panic, Fehler, fehlende oder vertauschte Marker
ablehnen.

**Abnahme erfüllt:** Der compile-time-only QEMU-Gast meldet genau einmal und
in Reihenfolge Commit-Generation 2, degradierte committed Daten, ein weiterhin
lesbares unabhängiges Objekt, HEALTHY-Rebuild in die simulierte Domäne C und
den Abschluss. Danach erreicht er ohne Desktop-Autostart die Userspace-Shell,
den gezielten Unicode-Desktopprozess und `TEST_OK` innerhalb der festen
180-Sekunden-Grenze. Der normale QEMU-VGA-Build besteht ohne Ausführung der
Kampagne. Daraus folgt ausschließlich ein Softwarezustandsnachweis, keine
physische Speicherfehler-Isolation.

#### R1.3 Synchronisations- und Diagnosevertrag — M

**Status (13. August 2026): Abgeschlossen und abgenommen.** Der Kernel
unterscheidet harten IRQ-Kontext, IRQ-deaktivierten Foreground-Kontext,
präemptionsgeschützten Foreground-Kontext und schlaffähigen Taskkontext.
Benannte Assertions prüfen die erlaubten IRQ-, Interrupt-, Präemptions- und
Schlafzustände. IRQ-Verschachtelung endet vor jedem Scheduler-Tail; blockierende
Operationen dürfen keine Präemptionsgrenze überschreiten.

Die globale Lockordnung lautet `VFS -> DATEISYSTEM -> TREIBER -> SCHEDULER`,
innerhalb der Speicherverwaltung gilt `HEAP -> FRAME`. VFS-Operationen und die
ATA-/FDD-Datenpfade sind entsprechend serialisiert. Netzwerk- und HPET-ISRs
beschränken sich auf Quittierung und Pending-Markierung; die eigentliche Arbeit
läuft außerhalb des harten IRQ-Kontexts.

Der Logger unterstützt `TRACE`, `DEBUG`, `INFO`, `WARN` und `ERROR` mit
Komponentenpräfix und Mindestlevel. Panic- und Exceptionausgaben enthalten den
vollständigen Registerframe, CR2 und die 40-stellige SHA1-Build-ID des Kernels.
Vertrags-/Regressionstests, Windows-Referenzbuild und QEMU-Ring-3-Smoke sichern
die Umsetzung ab.

- Festlegen, welche APIs in IRQ-Kontext, mit deaktivierter Präemption oder
   schlafend aufgerufen werden dürfen.
- Lock-Reihenfolge für Scheduler, VFS, Dateisysteme und Treiber dokumentieren.
- Assertions für IRQ-/Lock-Zustand sowie strukturierte Log-Level ergänzen.
- Panic-Ausgabe um vollständige Register, CR2 und Build-ID erweitern.

#### R1.4 Grafischer Desktop-MVP — M

**Status (13. August 2026): Abgeschlossen und abgenommen.** Dieser Meilenstein
wurde auf Wunsch bewusst vor R2.1 eingeschoben. Der native Stage-2-Loader
wählt in Framebuffer-Builds bevorzugt VBE 1024x768x32 und danach 800x600x32.
Ein fehlender oder ungültiger Modus fällt ohne gesetztes Framebuffer-Flag auf
BIOS-Modus 03h und VGA-Text zurück.

Die angehängten Syscalls 44 bis 46 bilden eine versionierte Ring-3-Display-ABI
für Modusinformationen, geclippte Rechtecke und geclippte Pixelschrift. Farben
verwenden `0x00RRGGBB`; alle Userdaten werden geprüft kopiert und der LFB bleibt
Supervisor-only. Framebuffer-Console-Ausgaben erscheinen zusätzlich auf COM1.
Die Pixelschrift interpretiert Textbytes nun als vollständig vorvalidiertes
RFC-3629-UTF-8, zählt Skalarzellen für Clipping und Damage und verwendet eine
generierte Unicode-zu-CP437-Abbildung samt sichtbarer Ersatzglyph. ABI-Längen
und Erfolgswerte bleiben Bytezahlen. Breite Fonts und Text-Shaping bleiben
außerhalb dieser schmalen Kernelprimitive.

`DESKTOP.PRG` wird nur bei einem echten Framebuffer vor `SHELL.PRG` gestartet.
Vier tastaturbediente Karten öffnen Shell, Dateiliste, Editor oder
Systeminformationen als Vollbild-Kindprozess. Der Desktop wartet auf dessen
Ende und zeichnet sich danach neu. Ein realer QEMU-Lauf erreicht den seriellen
Marker `DESKTOP_OK`. Maus, Compositor, Windowmanager und Fokusmodell gehören
ausdrücklich nicht zu diesem MVP.

Dieser Absatz hält die Abnahmegrenze von R1.4 fest. Der aktuelle Stand ist
weiterentwickelt: `desktop` kann Grafik aus VGA-Text aktivieren und besitzt
Explorer, Maus, Window Manager und externe Surface-Clients. Maßgeblich ist der
[grafische Desktop-Workflow](GRAPHICAL_DESKTOP_WINDOW_MANAGER_WORKFLOW.md).

### Sicherheits-Gate S0 — vor weiterer Funktionsentwicklung

**Status (23. August 2026): S0.1 abgeschlossen, Gesamtgate nicht abgenommen.**
S0 ist die Eintrittsbedingung für alle folgenden Phasen. Bis zum vollständigen
S0-Abschluss sind nur Änderungen zulässig, die Sicherheit, Isolation,
Diagnose, Verifikation oder Reproduzierbarkeit erhöhen.

#### S0.1 Einsatzprofil, Gefahren und Assurance Case — M

**Abgeschlossen für die explizite generische Forschungsbaseline:** Scope und
Gefahrenregister inventarisieren alle ausgewählten Komponenten; nicht
ausgewählte Produktprofile und Zielhardware-Claims bleiben ausgeschlossen.

- Zielsystem, Einsatzprofil, Umgebung und vorhersehbaren Fehlgebrauch festlegen.
- Essential Functions, sichere/degradierte Zustände und je Gefahr die FTTI
   definieren.
- Ein versioniertes Gefahrenregister mit Ursache, Kontrolle, Restrisiko und
   Verifikationsnachweis anlegen.
- Traceability `Gefahr -> Anforderung -> Design -> Code -> Test -> Ergebnis`
   automatisiert prüfen.

#### S0.2 Stack-, Exception- und Panic-Containment — L

**Abgeschlossen für die automatisierte QEMU/VMware-Forschungsbaseline:**
Kernel-Taskstacks besitzen beidseitige nicht-präsente
Guardpages; der Bootstack eine volle untere Guardpage. `#DF` läuft über eine
dedizierte TSS und einen unabhängigen Emergency-Stack in einen begrenzten,
heap-/lockfreien Crashrecord-/COM1-Pfad. Explizite beidseitige
User-Stack-Guardpages samt Ring-3-Fault-Test sind umgesetzt. Dynamische Frames
und compilerseitig erkannte statische Einzelframes über 4096
Byte brechen jeden Kernelbuild ab. Ein prüfsummengeschützter, redundant in
reserviertem RAM und CMOS/NVRAM gespeicherter Crashrecord überlebt den nativen
Reset, wird beim Boot einmal gemeldet und
der #DF-Pfad fordert begrenzt einen Reset an. Das QEMU-Profil besitzt nun einen
echten IB700-Watchdog, der nur nach Schedulerfortschritt gefüttert wird und im
Fatalpfad ausläuft. Ein separater GCC-Stack-/Callgraph-Gate prüft jetzt 83
C-Objekte, 1279 Stackrecords, 2298 Graphknoten, Rekursionsfreiheit und vier
kumulative Entry-Budgets; der schlimmste Syscall-Pfad liegt bei 7140/7168 Byte.
Der Panic-Screen zeigt zusätzlich einen redundanten, prüfsummengeschützten
Breadcrumb mit Bootphase, Komponente beziehungsweise Treiber, Operation,
Programm/Objekt, Ergebniscode, PCI-ID/BDF-Details und Panic-Aufrufadresse an.
Laufzeit-Stack-Watermarks sind inzwischen im bestehenden Scheduler-Stats-ABI
umgesetzt. S0.2a legt zusätzlich mit
`safety/external_safety_monitor.toml`, einem fail-closed Validator und dem
External-Safety-Monitor-Vertrag die unveränderlichen FTTI-, Protokoll-,
Unabhängigkeits-, Fence-Readback- und physischen Abnahmekriterien fest. Das
Profil bleibt ausdrücklich `unbound`. S0.2b ergänzt eine begrenzte VMware-
Abnahme des frisch erzeugten disponiblen Build-Pakets und verlangt fehlendes externes
Backend, überwachte Probe-Recovery, `BOOT_OK` und die Ring-3-Shell ohne Panic.
Ein von CPU, Versorgung und Zeitbasis unabhängiger Zielhardware-Monitor samt
elektrischem Fencing und realer Kampagne bleibt manuelle Nutzerevidenz und ist
keine Voraussetzung für den Emulatorabschluss. Der echte
Double-Fault-Task-Gate-Pfad wird inzwischen in einem isolierten Testimage bis
zum Watchdog-Warmstart, Crashrecord-Recovery und anschließenden Gasttest geprüft.

- Nicht gemappte Guardpages für jeden Kernel- und Userstack, statische
   Stackbudgets, Rekursionsverbote und Laufzeit-Watermarks sind umgesetzt.
- Einen reservierten Exception-/Double-Fault-/NMI-Notfallstack mit
   vorallokiertem, beschränktem Crashdatensatz bereitstellen.
- Wiederherstellbare Prozess-/Dienstfehler von möglicher globaler
   Kernelkorruption trennen; betroffene Domänen einfrieren und aus bekannt
   gutem Zustand neu starten.
- `panic()` darf nicht nur `halt()` ausführen: Ausgänge zuerst in den
   gefahrenspezifisch sicheren Zustand bringen, Diagnose begrenzt sichern und
   einen unabhängigen Supervisor Failover oder Neustart ausführen lassen.
   In-Place-Weiterlauf nach unbekannter Kernelkorruption bleibt verboten.

#### S0.3 Fehlerdomänen, Supervisor und Redundanz — XL

**Teilstatus:** Ein fester, allokationsfreier Supervisor-Kern verwaltet acht
ECC-geschützte Domänenzustände mit Deadlines, Generation/Epoche,
Restartbudgets und der zwingenden Reihenfolge `timeout -> fence -> restart ->
self-test -> reintegrate`. Eine statische, bis zum Reboot verriegelte
Output-Fence-Registry ist mit dem Fatalpfad verbunden. Ihr erster realer Hook
sperrt Netzwerk-TX logisch und schaltet die Sender der unterstützten NICs
best-effort ab. Die Migration realer Dienste in eigene Fehlerdomänen sowie
rücklesbare externe Interlocks und unabhängige Supervisorhardware bleiben
offen.

**Architektur-Gate:** Der Supervisor innerhalb des heutigen `kernel.bin`
verbessert Erkennung und Fencing, erzeugt aber noch keine unabhängige
Failure Domain. S0.3a stellt nun die erste begrenzte IPC-/Capability-Basis
bereit; Storage, Netzwerk und komplexe Treiber verbleiben trotzdem in Ring 0.
Erst getrennte, capability-beschränkte und neu startbare Dienste schließen das
Gate. Die Abnahme verlangt Fault-Injection, die einen ganzen Dienst beendet
oder korrumpiert, während nicht betroffene Essential Functions innerhalb ihrer
Profilbudgets weiterlaufen.

##### S0.3a Bounded IPC/Capabilities v1 — umgesetzt

Der Kernel besitzt 16 statische Endpoints, acht lokale Capabilities je Prozess,
vier Nachrichtenplätze je Endpoint und eine maximale Nutzlast von 128 Byte.
Nach der Initialisierung benötigt der Pfad keinen Heap. Ein 32-Bit-Handle
verbindet einen Endpoint-Slot im unteren Byte mit einer 24-Bit-Generation;
zusätzlich werden Halter und Eigentümer über PID und Prozessgeneration geprüft.
Die Rechte sind `SEND`, `RECEIVE` und `CONTROL`. Der Erzeuger behält `CONTROL`.
Eine explizite Delegation bindet eine nichtleere Teilmenge von `SEND|RECEIVE`
an die aktuelle Prozessgeneration der Ziel-PID; Spawn selbst vererbt keine
IPC-Rechte.
Mehrparteienrouting ist nicht Bestandteil von v1. Send und Receive blockieren
auf festen Wait-Queues und besitzen endliche, auf `pit_monotonic_ms` basierende
Deadlines. Timeout null liefert ohne Blockierung `EAGAIN`, eine abgelaufene
positive Deadline `ETIMEDOUT`. Die kompatiblen Syscalls 50/51 verwenden einen
endlichen Standard; die angehängten Syscalls 53/54 nehmen den Timeout explizit
entgegen. Ein fester `MAX_TASKS`-Scan weckt abgelaufene Warter, ohne den
einzigen intrusiven Wait-Knoten doppelt einzureihen.
Close oder Eigentümer-Exit widerrufen den Endpoint, entfernen alle abgeleiteten
Einträge und wecken blockierte Peers. Host- und Ring-3-Gasttests decken
Nachrichtenaustausch, Rechteabschwächung, Ressourcenlimits, Close-Wakeup und
Exit-Revoke ab.

S0.3a ist ausdrücklich nur der Mechanismus-Unterbau. Endliche Deadlines,
CRC-/`critical_object`-Schutz und abschwächende Delegation sind umgesetzt.
Das versionierte Probeprofil ist default-deny und lässt nur die begrenzten
Lifecycle-, Zeit-, Diagnose- und IPC-Operationen zu. Datei-, Display-, Spawn-,
Prozesslisten-, Delegations- und Kill-Autorität bleiben gesperrt. Bestehende
Programme laufen ausschließlich über ein explizites Kompatibilitätsprofil;
auch dort ist Kill auf generation-sicher gebundene eigene Kinder begrenzt.

##### S0.3b Supervised Userspace Probe Domain — abgeschlossen

Die statisch profilierte Ring-3-Probe ist umgesetzt. Eine deterministische
Testsequenz injiziert Crash, Heartbeat-Hang und ungültige Antwort. Der
Supervisor führt jeweils `fence -> revoke/reap -> recreate -> self-test ->
reintegrate` mit einer 2-s-Heartbeat- und 1-s-Recovery-Deadline sowie einem
Budget von vier Restarts aus. Ein
versionierter Health-Syscall 56 bindet Meldungen an PID und Generation; jeder
Ersatzprozess muss einen neuen IPC-Endpoint nachweisen. QEMU bestätigt die
geordnete Markerfolge und parallelen GTEST-Fortschritt mit LAPIC, PIT,
IB700-Watchdog sowie 32/64/256/1024 MiB RAM. Das ist ein belastbarer
Prozess-Failure-Domain-Nachweis, jedoch keine Unabhängigkeit von Kernel, CPU
oder RAM. S0.3c migriert nun echte Dienste aus Ring 0.

Das Restart-Gate akzeptiert keine vertrauensbasierte Fence-Bestätigung mehr:
Jede Domäne muss einen Apply- und einen separaten Verify-Hook bereitstellen.
Der atomar beanspruchte Zustand `FENCING` verhindert Doppelaufrufe; nur eine
positive Rückleseprüfung führt zu Restart, jeder Fehler zu `SAFE_STATE`.
Deadlineprüfungen laufen zusätzlich in einem festen 10-ms-Raster aus der
monotonen PIT-Zeit. Der Clockpfad persistiert ausschließlich `ISOLATED`; die
potenziell blockierende Hardwareaktion verbleibt im Foreground. Ein Aufruf von
`supervisor_service_one()` verarbeitet maximal eine Fence-/Verify-Aktion;
Restart und Selbsttest bleiben explizite Folgeereignisse. Der Executor läuft in
einem reservierten Kernel-Worker alle 10 ms; dieser beansprucht einen der acht
Task-Slots. Zusätzlich bleiben ein Taskslot, ein Prozessslot und 32 Frames per
Admission Control exklusiv für einen Supervisor-Restart verfügbar.
Restart-/Safe-State-Ereignisse bleiben level-triggered.
Beim Safe State wird derzeit konservativ das globale Output-Fence verriegelt.
Mit `network-tx` ist die erste reale Domäne registriert. Ihre 250-ms-Deadline
gilt ausschließlich während einer Sendetransaktion; Idle erzeugt daher keinen
falschen Ausfall. Der 64-Bit-Fortschritt vermeidet ein Langzeit-Wrap. Nach
Timeout folgen Software-Latch, NIC-Abschaltung und Register-Rückleseprüfung;
der Restart-Budgetwert null erzwingt bis zu einem implementierten,
qualifizierten Reinitialisierungspfad den Safe State.
`storage-write` überwacht nun als zweite reale Domäne jede physische
ATA-/AHCI-/FDD-Schreibtransaktion mit einer 10-s-Deadline und explizitem Idle.
Timeout oder fehlgeschlagene Ruhestellung sperren weitere Writes ohne
Restartversuch. ATA liest `BSY/DRQ`, AHCI `CI` und `TFD`, FDD Motorbits und
Controller-Busy zurück; ein fehlgeschlagener
ATA-Flush wird als Schreibfehler weitergereicht. Das Storage-Fence allein kann
einen Teilwrite nicht zurückrollen; für markierte native FAT32-Images übernimmt
dies inzwischen das nachfolgende Undo-Journal v2 mit Flush-Barrieren und
Boot-Recovery. Größere Transaktionen/COW und die Power-Cut-Matrix auf
Zielhardware bleiben S0.5.
Als dritte Domäne überwacht `filesystem-write` alle mutierenden öffentlichen
VFS-Aufrufe. I/O-Fehler oder Deadlineverletzung verriegeln das VFS Read-only,
während Lesen und Diagnose weiter möglich bleiben. Fatal-Fencing sperrt VFS-
und physische Storage-Writes gemeinsam. Markierte FAT32-Images koppeln diese
Schranke an die Journaltransaktion; FAT12, EXT2 und fremde Medien besitzen noch
keine entsprechende atomare Mehrsektorgarantie.
Fortschrittssequenz und Interlockzustand beider Persistenzdomänen sind als
SECDED-/CRC-geschützte Primary/Shadow-Objekte ausgeführt. Korrigierbare Fehler
werden repariert; unbrauchbare Kopien führen fail-closed zur Schreibsperre.
Das native FAT32-Image enthält nun außerdem ein explizit markiertes
Einzelsektor-Undo-Journal in reservierten BPB-Sektoren. Die Reihenfolge
`old-data flush -> ACTIVE flush -> target flush -> CLEAN flush` ermöglicht
Boot-Recovery nach Abbruch an jeder Barriere. Journal v2 fasst bis zu 20
unterschiedliche Sektoren zu einer vollständigen VFS-Mutation zusammen und
rollt sie beim Boot in umgekehrter Reihenfolge zurück; wiederholte Zielsektoren
werden dedupliziert. CRC und Volumegrenzen werden vor Rollback geprüft;
fremde Medien ohne Marker werden nicht verändert. Kapazitätsüberschreitung
führt fail-closed zu Read-only. Offen bleiben größere Transaktionen/COW und
eine Power-Cut-Matrix auf Zielhardware.
Journal-v2-Metadaten besitzen nun zwei CRC-geschützte Superblöcke in den
reservierten Sektoren 8 und 31. Sequenzwahl, konservatives `ACTIVE` bei einem
unterbrochenen Mirror-Update und automatische Einzelkopie-Reparatur beseitigen
den bisherigen Header-Single-Point-of-Failure. Der persistente QEMU-Test
zerstört absichtlich die Primärkopie und verlangt Rollback plus Mirror-Reparatur.
Die Apply-/Verify-Callbacks samt Kontext sind ebenfalls redundant über
`critical_object` geschützt. Single-Bit-Fehler werden korrigiert; bei zwei
unbrauchbaren Kopien wird kein Funktionszeiger ausgeführt und unmittelbar zum
Safe State eskaliert. Noch ungeschützt sind Slot-Belegung und Domänenname; die
Belegung, Generation und Name liegen inzwischen ebenfalls in einem
versionierten Primary/Shadow-Descriptor. Dieser wird bei Registrierung zuletzt
publiziert; unkorrektierbare Scanfehler erzeugen fail-closed ein Safe-State-
Ereignis. Offen bleibt die unabhängige externe Kopie der gesamten
Supervisor-Konfiguration über eine zweite Fehlerdomäne.

- S0.3a um IPC-Deadlines, Metadatenintegrität, explizite Delegation,
   Service-Taskreservierung und Capability-Gates ergänzen.
- S0.3b als überwachte, neu startbare Least-Privilege-Probedomäne abnehmen;
   danach Netzwerk, Storage und komplexe Treiber schrittweise migrieren.
- Fortschritts-/Deadline-Watchdogs, Restart-Budgets, Fencing, Selbsttest und
   sichere Reintegration für jede migrierte Domäne nachweisen.
- Hot-Standby oder Dual-Controller-Handover mit regelmäßigem realem
   Failover-Test aufbauen.
- Common-Cause-Fehler bewerten; wo nötig unabhängige Hardware,
   Stromversorgung, Takte, Sensorpfade oder diverse Implementierungen nutzen.

#### S0.4 Determinismus und garantierte Ressourcen — L

- S0.4a ersetzt die ungewichtete Taskauswahl durch drei statische Klassen. Der
   feste Zyklus `Safety, Safety, Service, Ambient` bildet die Gewichte 2:1:1
   direkt und in konstanter Klassenlänge ab. Jede Klasse besitzt einen eigenen
   Round-Robin-Cursor; blockierte Tasks werden in höchstens `MAX_TASKS`
   Schritten übersprungen. Damit kann ein Klassen- oder Slotwechsel keine
   laufbereite niedrigere Klasse verhungern lassen. Der Pfad allokiert nicht.
- S0.4b begrenzt jede Klasse zusätzlich in absoluten 100-ms-Fenstern auf
   60 ms Safety, 25 ms Service und 15 ms Ambient. Die monotone Abrechnung
   erkennt auch übersprungene Fenster nach langen nicht präemptierbaren
   Abschnitten ohne unbeschränkte Schleife. Eine erschöpfte Klasse wird bis zur
   nächsten absoluten Fenstergrenze aus der Taskauswahl entfernt; der
   Kernelkontext bleibt für Diagnose und Recovery ausführbar. Rückläufige Zeit
   sperrt alle Klassen fail-closed. Zähler und aktueller Drosselzustand sind in
   der Taskdiagnose sichtbar. Host-Verhaltenstest, vollständiger i386-Build und
   realer Scheduler-Gastlauf sind grün.
- S0.4c-1 hebt bei einem blockierenden IPC-Send/Receive den Gegenprozess auf
   die effektive Klasse des Wartenden. Die Beziehung bindet Taskslot und
   Prozessgeneration, propagiert in höchstens `MAX_TASKS` Durchläufen transitiv
   und wird bei Wakeup, Timeout, Cancel oder Exit entfernt. Mehrere IPC-Peers
   oder beschädigte Capability-Metadaten werden vor dem Blockieren fail-closed
   abgewiesen. Spinlocks und Präemptions-Guards erhalten bewusst keine
   Inheritance, weil ihr Kontextvertrag Blockieren verbietet.
- S0.4c-2a bindet die statischen Task-, Stack-, IPC- und Storage-Grenzen im
   versionierten Register `safety/resource_budgets.toml` an ihre konkreten
   C-Makros und Verifikationstests. Der begrenzte Validator wertet nur sichere
   ganzzahlige Konstantenausdrücke aus und lehnt Drift, doppelte Einträge,
   Pfadflucht und fehlende Evidenz fail-closed ab. Die Compiler-Gates gegen VLA
   und Kernel-Stackframes über 4096 Byte bleiben verbindlich. Laufzeit-High-
   Water-Marken und zielhardwarebezogene WCET-Nachweise folgen in 2b/2c.
- S0.4c-2b1 ergänzt versionierte, lockgeschützte und saturierende
   Laufzeitdiagnostik für aktive beziehungsweise maximale IPC-Endpunkte,
   Capabilities, Nachrichten und Storage-Requests. Die C-Verhaltenstests
   erzwingen die jeweilige statische Kapazität, prüfen den fail-closed
   Fehlercode und belegen danach aktive Zähler von null bei erhaltenem
   High-Water-Wert. Die Zähler sind Diagnose, keine Autoritätsentscheidung.
- S0.4c-2b2a erweitert Syscall 43 append-only um Memory-Statistik v2. Der
   unveränderte 88-Byte-v1-Präfix bleibt verhandelbar; v2 ergänzt historische
   Frame-/Heap-Peaks und saturierende Allokationsfehlerzähler. Ein realer
   Ring-3-Test belegt Peak-Monotonie und Frame-Rückgewinnung. Der dabei
   entdeckte inkrementelle ABI-Mischbuild ist ebenfalls geschlossen: jeder
   C-Compile erzeugt jetzt explizit eine `.d`-Datei, und der Kernel-Link bricht
   bei fehlender oder falscher Dependency-Evidenz ab.
- S0.4c-2b2b1 ergänzt append-only Syscall 84. Seine feste 32-Byte-v1-Struktur
   meldet aktuelle und maximale Taskslot-Belegung, Kapazität, reservierten
   Supervisor-Slot und saturierende Ablehnungen. Der Gast füllt die Ambient-
   Kapazität bis zur definierten Ablehnung und weist danach Rückgewinnung bei
   erhaltenem High-Water nach.
- S0.4c-2b2b2 ergänzt ausschließlich für getrennte Testimages begrenzte Heap-
   und Frame-Countdowns. Der Boot-Selbsttest erzwingt Heap-ENOMEM ohne
   Belegungsänderung sowie einen Framefehler nach der ersten Kernelstackseite,
   prüft die exakte Framebilanz und verwendet den Stackslot anschließend
   erneut. Das QEMU-Image erreicht danach weiterhin den vollständigen
  Ring-3-`TEST_OK`-Marker.
- S0.4c-2b2c registriert die vier kernel-eigenen mediated-DMA-Slots mit
  64-KiB-Standardkapazität,
  führt lockgeschützte aktive/maximale Belegung und saturierende echte
  Kapazitätsablehnungen und veröffentlicht die aggregierte 32-Byte-v1-
  Diagnose ausschließlich an Treiberdomänen. Der Hosttest erzwingt `4/4`,
  `ENOSPC`, Freigabe, Wiederbelegung und vollständige Rückgewinnung; der reale
  QEMU-HDA-Treiber bindet einen Pool und publiziert den validierten Marker über
  den bestehenden Supervisor-Diagnosekanal.
- S0.4c-2c1 erzeugt mit einem unabhängigen GCC-Analysecompile für alle 75
   Kernel-C-Objekte `.su`- und `.ci`-Evidenz. Der Validator lehnt fehlende oder
   ungepaarte Dateien, dynamische beziehungsweise über 4096 Byte große lokale
   Frames und direkte oder transitive Rekursionszyklen ab. Der erste Lauf
   erfasste 1.214 Stackdatensätze und 5.816 Callgraph-Kanten; dabei wurde der
   rekursive PCI-Topologiescan durch feste Bus-/Slot-/Funktionsschleifen
   ersetzt.
- S0.4c-2c2a summiert nun die statischen Stackkosten entlang der direkten
   Legacy- und Scheduler-IRQ-Callgraphen. Das maschinenlesbare Register
   `safety/stack_budgets.json` bindet alle acht registrierten IRQ-Handler, die
   drei CPU-Exception-Handler und die im Exitpfad erreichbaren VFS-Callbacks
   ein; neue oder entfernte Registrierungen, unbekannte indirekte Aufrufe,
   fehlende Kosten und Budgetüberschreitungen stoppen das Gate. Der
   Referenzcompile belegt 1.744 von 7.168 Byte für den Legacy-IRQ-Pfad, 720 von
   4.096 Byte für den Scheduler-IRQ-Pfad und 2.000 von 7.168 Byte für CPU-
   Exceptions. Konservative Reserven für Assembly- und Validator-/Fence-
   Callbacks sind explizit im Register begründet. S0.4c-2c2b1 bindet nun auch
   den vollständigen INT-80-Pfad: einschließlich 128 Byte Assemblyreserve
   belegt der schlechteste Syscallpfad 6.880 von 7.168 Byte. Alle VFS-
   Operationstabellen und EXT2-Verzeichnisbesucher werden mit den
   Produktionsquellen abgeglichen; neue unbekannte Callbackziele stoppen das
   Gate. Zielplattform-WCET bleibt S0.4c-2c2b2.
- S0.4c-2c2b2a misst mit serialisiertem `RDTSC` ausschließlich den begrenzten
   Scheduler-Entscheidungspfad vor dem Kontextwechsel sowie den
   nicht blockierenden Diagnose-Syscall 116. Die feste 72-Byte-v1-ABI liefert
   saturierende Sample-, Summen-, Maximum- und Anomaliezähler an den
   default-deny Probe-Dienst. Nur der generationsprüfende Supervisor darf den
   normalisierten Marker veröffentlichen; fehlende Evidenz beendet den Dienst
   nicht. QEMU und VMware müssen jeweils mindestens 64
   Samples, null Zeitquellenanomalien und die maschinenlesbare 10-ms-Grenze aus
   `safety/wcet_budgets.json` einhalten. Diese Emulatorwerte sind keine
   Zielhardware-WCET; deren manuelle Abnahme bleibt S0.4c-2c2b2b. Die frische
   Abnahme vom 23. August 2026 lag auf QEMU bei rund 0,613/0,102 ms und auf
   VMware bei rund 0,051/0,034 ms für Scheduler/INT 0x80, jeweils ohne
   Zeitquellenanomalie.
- Kritische Tasks erhalten feste Prioritäten, CPU-/Speicher-/Queue-Budgets,
   Admission Control und nachgewiesene Worst-Case-Laufzeiten.
- Im kritischen Modus nur reservierte Pools verwenden; unbeschränkte
   Allokation, Rekursion, Retries und Warteschlangen sind dort unzulässig.
- Priority Inversion ist generationssicher behandelt. Interruptstürme und
   rückläufige Scheduler-/Device-Zeit lösen nun einen getesteten fail-closed
   Zustand aus: Schedulerklassen bleiben bis zur Neuinitialisierung gesperrt,
   betroffene Geräte werden vollständig gefenct. Vollständiger
   Zeitquellenausfall auf Zielhardware bleibt Teil der manuellen Plattform-
   Qualifikation.
- Kritische Kernelobjekte selektiv über den `critical_object`-Umschlag mit
   wortweisem SECDED, CRC32, Version/Sequenz, semantischem Validator und
   Primary/Shadow schützen; Bitflip-Injection misst Korrektur und Eskalation.

#### S0.5 Datenintegrität, Boot und unterbrechungsarme Updates — XL

- [x] Das BIOS-Bootmanifest ist als v3 mit festem 336-Byte-Header versioniert
  und bindet den exakten Kernelinhalt über SHA-256 gemäß NIST FIPS 180-4. Ein
  vom Imageerzeuger unabhängiger Hostvalidator prüft HDD und Floppy nach jedem
  Build einschließlich Layout, Grenzen, Manifest-Prüfsumme, SHA-256 und CRC32.
  Der Header behält die bisherigen Feldpositionen bei und bettet ab Offset 80
  die verpflichtende 256-Byte-Signatur ein. Die BIOS-Stufen akzeptieren nur
  v3. Stage 2 berechnet SHA-256 und CRC32 in
  einem einzigen begrenzten Kernel-Lesedurchlauf mit ausschließlich festen
  Puffern und prüft den Digest vor ELF-Parsing oder Kernelstart. Ein negativer
  QEMU-Lauf verändert den Kernel bei weiterhin gültiger CRC32 und
  Manifest-Prüfsumme und wird ausschließlich am SHA-256-Vergleich gestoppt.
  Ein zweiter negativer Lauf verändert ausschließlich die eingebettete
  Signatur bei reparierter Manifest-Prüfsumme und muss vor `BOOT_OK` am
  RSA-PSS-Fehlerpfad stoppen.
- [x] Der Host-Build signiert das exakte `kernel.bin` nach RFC 8017 mit
  RSA-2048-PSS, SHA-256, MGF1-SHA-256 und 32-Byte-Salt. Ein unabhängiger
  Validator pinnt Version, Algorithmus, Parameter und den SHA-256-Fingerprint
  des DER-SubjectPublicKeyInfo, bevor ein Image veröffentlicht wird. Der
  eingecheckte private Schlüssel ist nur eine reproduzierbare Research-
  Testfixture und wird vom Release-Modus abgelehnt. Stage 2 bindet Modulus und
  Exponent 65537 fest ein und prüft die Signatur mit begrenzter
  RSA-2048-PSS-/MGF1-SHA-256-Logik vor dem ELF-Parser. Das authentifiziert den
  Kernel relativ zu Stage 2. Weil Stage 1 und Stage 2 auf dem beschreibbaren
  Medium verbleiben, entsteht daraus weder Secure Boot noch ein
  unveränderlicher Plattformvertrauensanker oder Anti-Rollback.
- [x] Das native HDD-Image enthält zwei nicht überlappende signierte
  Bootkandidaten: Manifest A/B an den partitionrelativen LBAs 0/96 und Kernel
  A/B an 128/3136. Die MBR-Stufe lädt ohne Manifestparser eine feste
  64-Sektoren-Reserve für Stage 2. Stage 2 prüft A vollständig und versucht B
  bei einem Fehler vor dem Kernel-Handoff genau einmal; beide Kandidaten
  durchlaufen unabhängig Manifest-Prüfsumme, Bounds, SHA-256, RSA-PSS und
  ELF-Prüfung. QEMU weist sowohl den erfolgreichen A-zu-B-Fallback als auch
  den geschlossenen Stopp bei zwei beschädigten Signaturen nach. Floppy bleibt
  single-slot; persistenter Updatezustand folgt über die nächsten Punkte.
- [x] Zwei feste 512-Byte-Boot-Control-Kopien an den partitionrelativen LBAs
  97/98 schützen Version, Sequenz, bestätigten/pending Slot, Versuchszahl und
  Erfolgsmaske mit CRC32. v1 bleibt auf A-zu-B begrenzt; v2 erlaubt nur den
  gegenüberliegenden inaktiven Slot. Der Offline-Updater prüft ELF und
  RSA-PSS, schreibt ausschließlich diesen Slot und veröffentlicht Pending erst
  nach Revalidierung. Stage 2 persistiert zwei Versuchsdekremente und Rollback
  auf den vorher bestätigten Slot vor dessen Ausführung. Power-Loss-Tests
  decken jede dauerhafte Schreibgrenze für beide Richtungen ab; QEMU weist
  B-Bestätigung, A-Bestätigung, stabilen A-Neustart und bestätigten
  B-zu-A-Fallback nach.
- [x] Stage 2 publiziert erst nach vollständiger Kandidatenprüfung einen
  CRC-geschützten v1-Handoff. Syscall 117 gibt ihn erst nach `BOOT_OK` und nur
  an den gebundenen Storage-Service frei. Ring 3 revalidiert Manifest,
  Ressource, Sequenz und beide Control-Kopien, bestätigt den ausgewählten
  Pending-Slot mit zwei
  verifizierten Writes und heilt eine benachbarte Kopie. QEMU weist dauerhaften
  B-Neustart und persistenten A-Rollback nach beschädigter bestätigter
  B-Signatur nach. A ist nun ebenfalls atomar als inaktiver Slot aktualisierbar.
- [x] Ein festes REIST-Offline-Bundle v1 transportiert genau einen signierten
  ELF32-Kernel in einem CRC-geschützten 512-Byte-Header plus begrenztem
  Payload. Ein vom Erzeuger unabhängiger Parser verwirft Größenabweichungen,
  Nachlaufdaten, unbekannte Algorithmen, Reserven, Digest-, Policy- und
  Signaturfehler, bevor der bestehende inaktive Slot beschrieben wird. Das
  QEMU-A/B-Gate verwendet diesen Consumer in beiden Richtungen. Weil das
  Bundle weder Rollen-/Expiry-Metadaten noch eine vertrauenswürdige monotone
  Version trägt, wird keine TUF-/Uptane-Kompatibilität oder Anti-Rollback-
  Eigenschaft behauptet. Online-Verteilung, Recovery-Image, Release-Key-
  Verwahrung und Anti-Rollback bleiben offen.
- Sicherheitsrelevanten Zustand transaktional, checksummiert, versioniert und
   redundant speichern; Stromausfall an jeder Commitstelle injizieren.
- Verifizierten Boot, signierte Artefakte, reproduzierbare Builds, Provenienz
   und SBOM einführen.
- Updates als atomaren A/B-Wechsel mit Selbsttest und automatischem Rollback
   ausführen; Standby-Kanäle nacheinander statt gleichzeitig aktualisieren.
- Kernel-Livepatching bleibt eine eng begrenzte Ausnahme mit Quieszenzpunkt,
   Zustandskompatibilität, Vorabnachweis und sicherem Rollback.

#### S0.6 Verifikation und Langzeitbetrieb — XL

`S0.6a` ergänzt einen deterministischen Host-Campaign-Runner für den realen
Offline-Bundle-Consumer. Der Gate-Lauf erzeugt aus dem Referenzkernel ein
gültiges Bundle und führt mit festem 32-Bit-Seed genau 64 Fälle aus: 16
strukturierte Fehler für Format, Geometrie, Algorithmus, Flags, Reserven,
CRC32, SHA-256, Signatur, Policy-Fingerprint, Payload und Länge sowie 48
Einbitmutationen über den gesamten Eingang. Jeder Fall läuft durch den echten
Bundle-zu-Inactive-Slot-Einstieg und muss vor Erzeugung eines Output-Images
scheitern; Image, Kernel und Signatur bleiben hashidentisch. Das ist ein
kurzer reproduzierbarer Parser-/Seiteneffekt-Nachweis, keine Zufallsabdeckung,
Langzeit-, Gast-, VMware- oder Zielhardwarekampagne.

`S0.6b` erzeugt nach dem Windows- und Makefile-Image-Build ein SPDX-2.3-JSON-
SBOM für den exakten Kernel, seine Signatur, das BIOS-Image und die unmittelbar
paketierten Ring-3-Programme. Jeder Eintrag bindet kanonischen Build-Pfad,
Bytegröße im standardisierten Kommentarfeld sowie das erforderliche SHA-1 und
zusätzlich SHA-256; ein separater Parser prüft die Live-Artefakte sowie die
vollständigen `DESCRIBES`-/`CONTAINS`-Beziehungen. Die Verarbeitung ist auf
160 Dateien, 128 MiB je Datei, 512 MiB Gesamteingang und 2 MiB JSON begrenzt.
Ungeklärte Lizenz- und Copyrightangaben bleiben sichtbar `NOASSERTION`. Das
Dokument ist weder signierte Provenienz noch vollständiges Quellen-,
Abhängigkeits-, Lizenz- oder Schwachstelleninventar und belegt noch keine
reproduzierbaren Builds.

`S0.6c` schließt ausschließlich die automatisierte Emulatorbaseline. Der
geschlossene Vertrag `safety/automated_s0_gate.toml` pinnt Baseline, Ziele,
Evidenzbefehle, manuelle Ausschlüsse und Restrisiken; ein unabhängiger
Validator verwirft unbekannte Felder, abgeschwächte Befehle, Queue-Drift und
unzulässige Assurance-Claims. Die Abnahme umfasst den vollständigen Hostlauf,
frische QEMU-/VMware-Pakete, QEMU-PIT, Watchdog, Storage-Recovery, Speicher-
matrix und Framebuffer sowie begrenztes VMware-Containment. Damit darf R2 für
die generische Forschungsbaseline beginnen. Zielhardware, externer Monitor,
elektrisches Fence-Readback, physische Fault-Injection, Ziel-WCET,
Langzeitbetrieb und Produktqualifikation bleiben außerhalb dieses Abschlusses.

- Statische Stack-/Code-/WCET-Analyse, Fuzzing, modellbasierte Tests und
   unabhängige Reviews als Gates einführen.
- Fault-Injection für Bitfehler, Speichererschöpfung, Timingfehler,
   Geräteverlust, beschädigte Eingaben, Stromausfall und Updates automatisieren.
- Soak-/Alterungstests, ECC/EDAC, Medien-Scrubbing und Hardwaretausch über die
   geplante Produktlebensdauer nachweisen.
- Toolchain, Schlüssel, Abhängigkeiten, Feldtelemetrie, Schwachstellen,
   Beschwerden, Patches und Rückrufe kontrolliert über den Lebenszyklus führen.

### Phase 2 — Deskriptoren, VFS und zuverlässige Datenträger

#### R2.1 ABI v1 und vollständige Dateideskriptoren — L

- [ ] Read-only VFS-Metadaten schrittweise in den überwachten Ring-3-Storage-
  Service verlagern. Das erste Paket verwendet einen exakt 512 Byte großen,
  CRC-redundant geschützten Shadow-Frame für `stat`, eine maximale absolute
  Pfadlänge von 191 Byte und eine monotone 1000-ms-Gastdeadline. Es entfernt
  noch keine Kernelautorität.
  Der zweite Schritt verwendet append-only Frameoperation 2: Ring 3 wählt den
  längsten Mount, validiert FAT32-BPB/FAT-Ketten und löst 8.3-/ASCII-VFAT-
  Komponenten mit höchstens 64 Sektorreads auf. Status und öffentliche
  Metadaten müssen bytegenau mit `SYS_STAT` übereinstimmen; erst dann wird das
  unabhängig geparste Ergebnis zurückgegeben. Das kurzlebige `STAT.PRG` nutzt
  diesen Pfad inzwischen ohne Legacy-Fallback und beendet sich nach Timeout,
  damit die Prozessbereinigung offene Requests widerruft. Der allgemeine
  Cutover weiterer langlebiger Clients und EXT2 bleiben offen; er benötigt zuvor
  eine explizite requestbezogene Cancel-ABI. Diese Voraussetzung ist mit
  append-only Syscall 118 umgesetzt: queued/complete werden sofort widerrufen,
  claimed bleibt bis zur Dienstquittierung `cancel-pending` und publiziert kein
  Ergebnis. Das beendet oder reversiert keinen physischen I/O.
  Als erster lang laufender Verbraucher ist `HTTPD.PRG` für Metadaten unter
  `/htdocs` umgestellt. Append-only Operation 3 erweitert den kontrollierten
  Parser um FAT12-BPB, feste Rootdirectory und 12-Bit-Clusterketten; Operation 2
  bleibt FAT32-spezifisch unverändert. Append-only Operation 4 verwendet den
  begrenzten FAT-Parser autoritativ und lässt `SYS_STAT` vollständig aus ihrem
  Ergebnisweg; Operationen 1 bis 3 bleiben unverändert. Der normale Gast
  vergleicht Operation 4 als unabhängige Testevidenz weiterhin bytegenau mit
  Legacy-`stat`. Append-only Operation 5 ergänzt den festen EXT2-Subset und
  wird der gemeinsame autoritative Metadatenpfad. Append-only Operationen 6
  und 7 ergänzen pfadbasiertes, auf 256 Byte begrenztes `read-at` sowie genau
  einen indexierten Verzeichniseintrag. FAT12/FAT32 und EXT2 werden dabei
  ausschließlich über die unabhängigen Ring-3-Parser gelesen. `CAT.PRG` und
  `LS.PRG` nutzen diesen Pfad ohne `SYS_OPEN`, `SYS_READ`, `SYS_READDIR` oder
  Legacy-Fallback. Darauf liegt ein pro Prozess fester Vier-Slot-Sessionlayer:
  kanonischer Pfad, 32-Bit-Offset, generationcodiertes Handle sowie
  `SEEK_SET`/`SEEK_CUR`/`SEEK_END`, `fstat` und `close`. `HTTPD.PRG` verwendet
  damit auch für Dateiinhalt und Operation 7 für Listings keinen Kernel-VFS-
  Fallback mehr. Die Session ist bewusst pfadgebunden und revalidiert; stabile
  Inode-Identität, Deskriptorvererbung, Shell und Desktop bleiben getrennte
  Folgepakete.
  Die read-only Shell-Walker `FIND.PRG` und `TREE.PRG` verwenden inzwischen
  ebenfalls ausschließlich Operationen 5 und 7. Zusätzlich zu 256 Byte Pfad,
  16 Ebenen und 512 Knoten begrenzt eine absolute monotone Fünf-Sekunden-
  Deadline den vollständigen Lauf; jeder Request erhält höchstens eine Sekunde
  der verbleibenden Zeit. Der Desktop-Explorer verwendet für
  Verzeichnisvalidierung, Einträge und Leer/Voll-Ordnerproben nun ebenfalls
  ausschließlich Operationen 5 und 7. Pro atomarem Snapshot gelten 32
  sichtbare und 128 gescannte Einträge sowie eine absolute monotone
  Fünf-Sekunden-Deadline; Fehler bewahren das zuvor veröffentlichte Fenster.
  Die langlebige Userspace-Shell verwendet für Programmsuche und
  Tab-Vervollständigung nun ebenfalls ausschließlich diese Operationen. Eine
  Aktion teilt eine absolute monotone Fünf-Sekunden-Deadline, einsekündige
  Requestgrenzen und höchstens 128 akzeptierte Verzeichniseinträge; Fehler
  verändern die Eingabezeile nicht. Mutierende Shell-/Papierkorbpfade und große
  Desktop-Ressourcen bleiben getrennte Folgepakete.
  Der begrenzte Bulk-Transport ist nun als append-only Storage-Operation 32,
  Frameoperation 15 und Syscall 124 umgesetzt. Zwei feste kernel-eigene Slots
  transportieren je höchstens 128 KiB; Client- und Servicegeneration, CRC,
  Deadline und Cancel werden vor Veröffentlichung geprüft. Der Objektclient
  verschiebt seinen Offset erst nach vollständiger Frame- und Datenprüfung.
  Der normale Gast liest `GUEST.TMP` mit 1537 Byte in genau einem Request und
  markiert `STORAGE_VFS_BULK_READ_OK`. Der Bildbetrachter verwendet diesen Pfad
  nun für höchstens acht 128-KiB-Requests je 1-MiB-Datei. Ein requestlokaler
  FAT-Sektorcache hält späte Reads trotz vollständiger Zyklusprüfung unter dem
  unveränderten 320-Sektor-Limit. Die anschließend auf 6400 Cluster erweiterte
  Dateiinhaltsgrenze verwendet eine konstante Brent-Zykluswache und gilt nicht
  für Verzeichnisläufe. Splash, Icons und Dateitypzuordnungen des Desktops
  verwenden denselben 128-KiB-Bulkpfad ohne künstliche Lesepause. Der
  2,5-MiB-Voll-Unicode-Font wird erst beim ersten Nicht-VGA-Zeichen geladen;
  der Unicode-Probelauf fordert ihn weiterhin explizit an. Dadurch sank der
  gemessene QEMU-Desktopstart von 8280 ms reproduzierbar auf 1907 bis 1984 ms.
- [x] Syscallnummern und den bestehenden Fehlercode-Subset aus einem
  gemeinsamen ABI-Header für Kernel und SDK deterministisch generieren;
  lückenlose v1-Indizes 0 bis 124 und beide Buildpfade prüfen Drift fail-closed
- [x] Open-Flags und Rechte je Handle ergänzen; Standarddeskriptoren 0/1/2 sind
  als getrennte feste, richtungsgebundene Einträge umgesetzt. Syscall 120
  ergänzt `RDONLY`/`WRONLY`/`RDWR`, `CREAT` und `APPEND` ohne Änderung der
  Legacy-Syscalls 14/19. `TRUNC` ist für journalmarkierte REIST-FAT12/FAT32
  als Nullschnitt umgesetzt; read-only/EXT2 bleiben fail-closed.
- [x] Append-only Syscalls 121/122 für `lseek` und node-basiertes `fstat`
  ergänzen. `SET`/`CUR`/`END`, Teilzugriffe, EOF, Positionen hinter EOF,
  Append-Interaktion, ungültige und nicht seekbare Handles sowie
  Fehleratomizität werden im normalen QEMU-Gast geprüft. FAT12, FAT32 und EXT2
  revalidieren die geöffnete On-Disk-Identität ohne Pfadauflösung.
- [x] Append-only Syscall 123 für descriptorbasiertes `ftruncate` und
  beliebige 32-Bit-Ziellängen innerhalb von Medien- und Transaktionsgrenzen
  implementieren. FAT12 arbeitet in einer vorab kapazitätsgeprüften festen
  Undo-Transaktion; FAT32 nullt Erweiterungen vor Größenpublikation und trennt
  Schrumpfsuffixe erst nach dem kleineren sicheren Präfix. Der Descriptoroffset
  bleibt unverändert. Rename und `fsync` liegen bereits im gemeinsamen
  ABI-Header; offene Handle-Semantik und spätere `dup`-/Vererbungsregeln bleiben
  getrennte Pakete.

#### R2.2 VFS- und FAT-Zuverlässigkeit — L

- **Teilstatus:** Atomisches Same-Directory-Rename/Replace ist für FAT32
   umgesetzt. Offene FAT12-/FAT32-Objekte sperren jetzt aliasfest `unlink`,
   `rmdir` sowie Quell- und Zielseite von Rename/Replace über eine feste
   256-Slot-VFS-Tabelle. Cross-Directory, FAT12-Rename und POSIX-artige
   Unlink-while-open-Lebensdauer fehlen weiterhin.
- **Teilstatus:** Der Editor nutzt `TEMP -> fsync -> close -> rename`; FAT12
   und künftige Blockgeräte benötigen noch einen gleichwertigen Sync-Vertrag.
- **Teilstatus:** FAT32-LFN akzeptiert validiertes RFC-3629-UTF-8 und bildet es
  begrenzt auf Microsoft-kompatibles UTF-16 samt Surrogatpaaren und
  checksum-validiertem 8.3-Fallback ab. Die unabhängige Ring-3-Auswertung
  dekodiert identisch fail-closed. Unicode-15-NFC-/Full-Casefold-Gleichheit ist
  über generierte, feste Tabellen umgesetzt. Ein bestehendes reguläres
  LFN-Ziel wird nun innerhalb der vorhandenen VFS-/Undo-Journal-Transaktion
  ersetzt, ohne seine validierte LFN-/Aliasidentität neu zu erzeugen.
- [x] Open/Delete/Unmount-Regeln für den vorhandenen FAT-Vertrag vereinheitlichen:
  erfolgreiche Opens registrieren exakt einen festen Node, Close-Fehler halten
  die Sperre, Objektmutationen liefern vor Wirkung `BUSY`, und Unmount bleibt
  bis zum letzten offenen Node gesperrt.
- [x] Fehler nach jedem einzelnen Sektorwrite injizieren und das resultierende
  Image mit einem Hostprüfer untersuchen. FAT12 und FAT32/ATA führen jeweils
  eine echte 0→700-Byte-VFS-Erweiterung aus demselben Basisabbild aus und
  schneiden nach jedem gemessenen vollständigen Rohwrite. Recovery darf nur
  vollständig alt, vollständig neu oder bei echter Headerambiguität explizit
  fail-closed liefern. FAT32 verwendet dazu denselben festen Journal-v2-Kern
  wie der Produktionscontroller und publiziert je Zielsektor erst die
  endgültige Pending-Fassung.
- [x] FAT-Zeitstempel vollständig publizieren: FAT12 und FAT32 setzen
  Create-/Write-/Access-Felder vor der Erzeugung, aktualisieren mtime mit
  Inhalts-/Größenmutationen und ändern über `touch` nur mtime plus date-only
  atime. Lese- und Stat-Pfade bleiben medienseitig wirkungsfrei.
- [x] VFAT-Namen als begrenztes UTF-8↔UTF-16-Roundtrip einschließlich
  Surrogatpaaren, Fehlkodierungsablehnung und 255-UTF-16-Unit-Grenze umsetzen.
- [x] Unicode-15-NFC und vollständiges Default Case Folding als heapfreien,
  tabellengebundenen Identitätsvergleich für FAT32 und Shadowparser umsetzen;
  Originalschreibweise auf Medium und in Readdir bewahren.
- [x] Grafische Textprimitive und Desktop-Clipping auf vollständig validierte
  Unicode-Skalarläufe mit CP437- und sichtbarer Ersatzglyph abbilden.
- [x] Einen standardisierten, fest begrenzten Ring-3-PSF2-Decoder einführen,
  den Referenzfont in beiden Images verankern und Erweiterungsglyphen geclippt
  überlagern; U+20AC beweist den Pfad außerhalb CP437.
- [x] Breite BMP-Glyphabdeckung aus allen 57.086 eindeutigen Abbildungen der
  gepinnten GNU-Unifont-16.0.04-Quelle erzeugen, im HDD-Image lizenzkonform
  ausliefern und nur für nicht durch CP437 gedeckte Skalare überlagern; große
  Ressourcen in 65.536-Byte-Syscall-Abschnitten mit expliziter
  Schedulerabgabe laden, damit unabhängige Supervisor-Heartbeats planbar
  bleiben, ohne den Desktopstart durch hunderte Scheduling-Umläufe zu bremsen.
- [x] Den Desktopfont auf die gepinnte GNU-Unifont-16.0.04-All-Quelle mit
  126.086 eindeutigen Abbildungen und 65.568 Supplementary-Plane-Mappings
  erweitern; die 2,47-MiB-Ressource in festen 64-KiB-Abschnitten laden und
  nicht gemappte gültige Skalare weiterhin sichtbar ersetzen.
- [x] Eine nackte Escape-Taste auf lokale Menü-, Dialog-, Drag- und
  Surface-Abbrüche begrenzen; den Desktop nur über den expliziten
  Startmenüeintrag mit validierter Display-Deaktivierung beenden.
- [x] Den festen mehrzeiligen Editor auf vorvalidierte UTF-8-Dokumente,
  Skalarspalten sowie sequenzsicheres Editieren und Clipping umstellen; die
  visuelle Beispieldatei unter `/usr/share/fonts/unicode.txt` damit öffnen.
- [x] Atomaren Same-Directory-LFN-Replace für bestehende reguläre Ziele durch
  Erhalt der Ziel-LFN-Folge, journalisierte Metadatenpublikation,
  Quelltombstone und nachgelagerte Kettenfreigabe ergänzen.
- EXT2 weiterhin ausdrücklich read-only mounten. Eingabemethoden,
  Bidirektionalität, Shaping, Combining-Positionierung und Graphemnavigation bleiben eigene
  Ring-3-GUI-/Textpakete.

#### R2.3 Blockgeräte und Partitionen — L

- [x] ATA, FDD und AHCI hinter eine gemeinsame Blockgeräte-API legen.
- [x] MBR-Partitionen als Child-Geräte erzeugen und mehrere Partitionen
  mounten.
- [x] Bereichsprüfung und `flush` zentral erzwingen.
- [x] ATA-LBA48 über IDENTIFY und PIO-EXT-Einsektorbefehle ergänzen.
- [x] Gebündelte, fest begrenzte ATA-PIO-Transfers ergänzen.
- [x] GPT, AHCI und NVMe als getrennte Folgepakete behandeln; GPT ist als
  begrenztes Folgepaket umgesetzt, NVMe bleibt bewusst offen.

**Teilstatus 16. August 2026:** Die transportneutrale Einsektor-API umfasst
ATA, FDD und AHCI. Primäre MBR-Einträge werden nach Signatur-, Bootflag-,
Kapazitäts-, Überlauf- und Überlappungsprüfung als feste Child-Geräte
veröffentlicht. Protective-GPT-MBRs werden nur mit gültigem GPT-Primärheader
angenommen. Jeder
Child-I/O wird vor dem Zugriff auf einen validierten Parent-LBA übersetzt;
Whole-Disk-Parents mit Kindern werden nicht zusätzlich gemountet. FAT32 und
EXT2 können dadurch partitionierte ATA-/AHCI-Medien über denselben Vertrag
mounten. Der QEMU-AHCI-Gastlauf mit zwei erkannten MBR-Children, FAT32-Root,
Datei-I/O und vollständigem `TEST_OK` ist abgenommen. ATA-LBA48 validiert die
IDENTIFY-Fähigkeit und Kapazität, verwendet oberhalb der LBA28-Grenze
READ/WRITE PIO EXT sowie FLUSH CACHE EXT und lehnt nicht unterstützte hohe
LBAs vor Port-I/O ab. Gebündelte PIO-Aufträge sind auf 20 Sektoren begrenzt,
prüfen den vollständigen Bereich vor dem ersten Portzugriff und verwenden
eine einzige Sector-Count-Programmierung mit DRQ-Deadline je Sektor. Writes
werden geflusht und vollständig zurückgelesen; journalisierte Bereiche fallen
innerhalb derselben Supervision auf geordnete Journal-Writes zurück. FAT32-
Cluster-I/O nutzt diesen Vertrag. Ein vollständiger QEMU-IDE-Gastlauf über den
ATA-PIO-Pfad erreicht `FILE_IO_OK` und `TEST_OK`. GPT validiert Protective MBR,
Revision 1.0, Disk-GUID, Header-CRC32, die CRC32 der auf 128 Einträge begrenzten
Partitionstabelle, eindeutige Partition-GUIDs sowie nutzbare Bereiche und
Überlappungen. Erst danach werden höchstens 16 feste Child-Geräte
veröffentlicht; hybride oder teilweise gültige Layouts bleiben fail-closed.
Der geschützte Storage-Fingerprint bindet GPT-Children über Schema,
Entry-Index und CRC32 von Typ- und Unique-GUID.

#### R2.4 SATA/AHCI-Unterstützung — XL

SATA wird als AHCI-Folgepaket umgesetzt. Der bestehende ATA-PIO- und FDD-Pfad
bleibt während jedes Schritts unverändert funktionsfähig. Jeder Schritt ist
einzeln abnahmefähig; ein fehlgeschlagener Schritt blockiert die folgenden.

1. [x] **Blockgerätevertrag festschreiben:** Eine feste, transportneutrale API für
   `read`, `write`, `flush`, Sektorgröße, Kapazität, Status und Fehler-Fence
   definieren. Keine VFS- oder Userspace-Schicht darf mehr ATA-Ports direkt
   annehmen.
2. [x] **ATA/FDD migrieren:** Bestehende ATA-PIO- und FDD-Operationen hinter den
   Vertrag legen. Alte interne Aufrufer bleiben zunächst als Kompatibilitäts-
   wrapper erhalten. Hosttests müssen identische Fehler- und Boundsregeln
   nachweisen.
3. [x] **PCI-AHCI erkennen:** PCI-Klasse `01/06/01` und BAR5 sicher validieren;
   32-/64-Bit-BARs, nicht unterstützte BAR-Typen, fehlende Ports und
   Controller-Reset-Timeouts fail-closed behandeln. Keine MMIO-Adresse aus
   ungeprüften PCI-Daten verwenden.
4. [x] **AHCI-Speicher reservieren:** Command List, FIS-Bereich und Command Tables
   aus festen, DMA-tauglichen Bereichen zuweisen. Alignment, physische Grenzen,
   Cache-/Ownership-Regeln und maximal einen aktiven Auftrag pro Port prüfen.
5. [x] **Port initialisieren:** `GHC.HR`, `PxCMD`, `PxSSTS`, `PxSIG` und
   `PxIS/PxIE` mit monotonen Deadlines behandeln. Nur aktive SATA-Ports mit
   gültigem Gerät werden registriert; Linkfehler oder Hängestatus führen zur
   Port-Quarantäne.
6. [x] **IDENTIFY DEVICE:** Feste ATA-Identifikation über AHCI ausführen, Modell,
   LBA28/LBA48-Kapazität und 512-Byte-Sektorvertrag validieren. Kapazitäts-
   überläufe und Geräte mit nicht unterstützter Sektorgröße werden abgelehnt.
7. [x] **Sektor-I/O:** `READ DMA EXT`/`WRITE DMA EXT` beziehungsweise den
   unterstützten AHCI-Befehl mit genau einem begrenzten Command ausführen.
   PRDT-Längen, LBA-Bereich, Busy/DRQ/TFD und Completion-Timeout vor und nach
   jedem Auftrag prüfen; Writes erhalten Readback und Flush-Verifikation.
8. [x] **Storage-Service anbinden:** SATA-Ressourcen in Fingerprint, Quarantäne,
   Maintenance-Lease, Storage-Request-Pool und Medien-Reintegration aufnehmen.
   ATA-, SATA- und FDD-Fehler dürfen keine unterschiedlichen Sicherheitsregeln
   umgehen.
9. [x] **VFS und Partitionen anbinden:** MBR-Child-Geräte zuerst, GPT erst in einem
   separaten Folgepaket. FAT32/EXT2-Mounts auf SATA testen; FAT12 bleibt auf
   FDD/Superfloppy begrenzt, sofern kein expliziter Layoutvertrag ergänzt wird.
10. [ ] **Abnahme und Fault-Injection:** Hosttests für Register-, Bounds-, DMA-,
    Timeout- und Quarantänefälle; anschließend QEMU-AHCI, VMware-AHCI und, wenn
    verfügbar, reale SATA-Hardware. Nach jedem Fehler müssen Fence, Diagnose und
    unabhängiger Prozessfortschritt nachgewiesen werden.

**Definition of Done für R2.4:** SATA erscheint mit stabiler Resource-ID in
`DRIVES.PRG`, ein getestetes FAT32- oder EXT2-Dateisystem kann gelesen und
geschrieben werden, `flush` und Readback sind bestätigt, ein Controller- oder
Medienfehler quarantänisiert nur die betroffene Ressource, und ATA/FDD-
Regressionstests bleiben vollständig grün. Ohne reale oder emulierte AHCI-
Laufzeitabnahme gilt SATA nur als Quellcode-Unterstützung, nicht als
unterstützte Plattform.

**Arbeitsstand 16. August 2026:** Die Implementierungsschritte 1 bis 9 sind
umgesetzt. Schritt 10 ist für QEMU einschließlich Timeout-, TFES- und TFD-
Injektion sowie für den normalen VMware-AHCI-Boot belegt. Offen bleiben die
reproduzierbare VMware-Fault-Injection und ein Lauf auf realer SATA-Hardware.
Für diesen Lauf steht `SATAWR.PRG` bereit: Der begrenzte Ring-3-Test schreibt
zehn Sekunden lang sequenzierte 512-Byte-Datensätze mit CRC und `fsync`, meldet
den erwarteten I/O-Abbruch beim Abziehen, wartet höchstens 65 Sekunden auf die
Reintegration und akzeptiert anschließend nur ein vollständiges altes oder
ein lückenloses CRC-gültiges Dateipräfix. Partition und Systemprogramm werden
separat erneut gelesen; reale Ergebnisse bleiben als Hardwareevidenz offen.
Ein beobachteter permanenter Read-only-Zustand nach erfolgreichem Reconnect
lag am zuvor nur einseitigen Zustandsübergang: Die Medienquarantäne wurde
entfernt, Resource-, Storage-, Filesystem- und Controller-Fences blieben nach
einem unklaren Write jedoch gesetzt. Die Reintegration verwendet nun für
ATA/AHCI frische, cachefreie Bootsektorreads; AHCI führt begrenzten COMRESET
und IDENTIFY aus. Nur wenn Fingerprint und Modell unverändert sind und das
redundante REIST-Undo-Journal den unterbrochenen Write vollständig
zurückrollt, werden abgebrochene Safety-Operationen bereinigt und alle Fences
gemeinsam gelöst. Nicht journalisierte oder nicht eindeutig wiederherstellbare
Volumes bleiben weiterhin fail-closed read-only.

**S0.3c-hw11:** Ein erneut beobachteter Hänger nach dem
Abziehen und Wiederanstecken der System-HDD während eines Writes wird nicht als
erfolgreiche Reintegration gewertet. Das Paket ersetzt die derzeit unbegrenzt
fortgesetzten automatischen Medienprobes durch ein festes Versuchslimit oder
eine absolute monotone Recovery-Deadline. Danach bleibt die Ressource
diagnostizierbar `ONLINE_RO` beziehungsweise `OFFLINE`. Vor einer Rückkehr zu
`ONLINE_RW` müssen COMRESET, IDENTIFY, zwei frische Fingerprint-Reads,
autoritative Undo-Journal-Recovery und ein Read-Selbsttest erfolgreich sein.
Ein neues QEMU-AHCI-Backend-Gate verwendet ausschließlich ein Wegwerfabbild und
weist unabhängigen Gastfortschritt während Backend-Ausfall und Recovery nach.

Der Implementierungskandidat begrenzt automatische Probes auf acht Versuche
und beendet sie danach mit `RECOVERY_EXHAUSTED`, während Quarantäne und Fences
geschlossen bleiben. Physische Probeabbrüche tragen nun auch außerhalb von
Fault-Injection-Builds eine konkrete Stufenkennung. Eine erfolgreiche
Journal-Recovery wird vor der Fence-Freigabe durch zwei weitere frische
Bootsektorreads bestätigt. Da der emulierte `ich9-ahci`-Bus Festplatten-
Frontends nicht hotpluggen kann, deaktiviert der QEMU-Runner stattdessen den
benannten Blockknoten während `SATAWR.PRG` aktiv schreibt und reaktiviert ihn
nach dem beobachteten I/O-Abbruch. Das hält Controller-, Port- und Geräte-
Identität stabil und erzeugt einen deterministischen Backend-Ausfall, ersetzt
aber ausdrücklich nicht den realen Kabeltest. Der Runner verlangt geordnet
I/O-Abbruch, `RESOURCE_REINTEGRATED_RW`, Fortschritt eines zuvor gestarteten
Ring-3-Kindprozesses, verifizierten erneuten Write und `TEST_OK`. Die
eingefrorenen Gates und die erneute physische Abnahme stehen noch aus.

#### Administrationspakete

- **S0.3c-admin1 — Storage-Administration (umgesetzt):** capability- und leasegebundene
  Werkzeuge für Status, `device down/up`, `mount` und `umount`. Zunächst sind
  ausschließlich Hilfsvolumes administrierbar; Root-Volume und Storage-Parent
  des laufenden Systems werden abgewiesen. Down sperrt neue Opens, drainiert
  vorhandene Handles begrenzt, flusht oder fencet und unmountet in Child-vor-
  Parent-Reihenfolge. Up verlangt Transport-Requalifizierung, unveränderte
  Medienidentität und Dateisystemprüfung vor Remount und Veröffentlichung.
  Ein festes, integritätsgeprüftes RAM-Rescue-Abbild hält Shell, Anzeige-,
  Diagnose- und Storage-Adminprogramme auch nach Verlust des Root-Backends
  startbar; es enthält keine veränderlichen Nutzdaten.
- **S0.3c-admin2 — Komponentensteuerung (umgesetzt):** feste Registry für ausdrücklich
  unterstützte Treiber und überwachte Dienste mit `status`, `down`, `up` und
  einem einzelnen begrenzten `restart`. Kernelcode wird nicht entladen. Jede
  Komponente deklariert Abhängigkeiten, Quiesce-/Fence-Aktion, Selbsttest und
  nicht deaktivierbare kritische Basiskomponenten. Teilfehler enden terminal
  fail-closed statt in automatischen Retry-Schleifen. `SVCCTL.PRG` besitzt ein
  eigenes default-deny Profil; der reale QEMU-Nachweis fährt den überwachten
  Netzwerkdienst vor dem Netzwerktreiber herunter, startet beide in umgekehrter
  Reihenfolge, startet den Storage-Dienst neu und prüft die sichtbare Diagnose
  eines manuellen `STORAGE.PRG`-Starts.
- **S0.3c-layout1 — Systemprogramm-Hierarchie (umgesetzt):** FAT12- und
  FAT32-Systemimages verwenden die begrenzten kleingeschriebenen Verzeichnisse
  `/bin`, `/sbin`, `/usr/bin` und `/libexec/reist`. Shell und Kernel-Rettungskonsole
  suchen über eine feste Reihenfolge; privilegierte Profile und das RAM-Rescue-
  Abbild werden ausschließlich durch exakte kanonische Pfade ausgewählt. Eine
  feste vollständige Legacy-Tabelle hält alte Root-Aufrufe kompatibel, ohne
  basename-basierte Autorität oder doppelte Images. Der QEMU-Nachweis prüft
  Layout, PATH, Administration, Legacy-Aufruf und Storage-Diagnose.

`drivers/block/block_device.[ch]` bietet einen festen,
transportneutralen Einsektor-Vertrag mit Bereichsprüfung, Read, Write und
Flush. ATA-PIO und FDD werden darüber als bestehende Backends angesprochen;
ein Host-Vertragstest und der vollständige Windows-Build sind erfolgreich.
Der AHCI-Pfad erkennt ausschließlich PCI 01/06/01, validiert BAR5, mappt den
MMIO-Bereich, setzt den Controller mit doppelter Zeit-/Pollgrenze zurück und
erkennt aktive ATA-SATA-Ports über `PI`, `PxSSTS` und `PxSIG`. Feste DMA-Pools
für Command-List, Received-FIS und Command-Table sind ausgerichtet und
adressgeprüft vorbereitet. Ein validierter IDENTIFY-Command wird pro
vorbereiteten SATA-Port aus Command-Header, 20-Byte-H2D-FIS, einer PRDT und
einem festen 512-Byte-Puffer aufgebaut und mit begrenzter PxCI-/PxIS-/PxTFD-
Completion sowie Quarantäne bei Fehler oder Timeout ausgeführt. Die
IDENTIFY-Daten werden jetzt auf LBA28/LBA48-Kapazität, 512-Byte-Sektoren und
Modellname geprüft und portbezogen gespeichert. Validierte Ports werden nach
der ATA-Erkennung als eigene `DRIVE_TYPE_AHCI`-Ressourcen mit stabiler
Controller-/Port-Zuordnung veröffentlicht. READ DMA EXT, WRITE DMA EXT und
FLUSH CACHE EXT verwenden je Port feste 512-Byte-Puffer, genau einen aktiven
Auftrag und monotone Completion-Deadlines. Jeder SATA-Sektorwrite wird unter
derselben Port-Exklusivität geflusht, erneut gelesen und vollständig mit einer
festen erwarteten Kopie verglichen; Fehler und Mismatch stoppen den Port und
werden fail-closed an die Blockschicht gemeldet. Der Blockgerätevertrag,
FAT32-VFS, Storage-Fingerprint, Quarantäne/Reintegration und globales
Write-Fencing sind angebunden. Ein QEMU-ICH9-AHCI-Boot mit FAT32-Datei-I/O,
Storage-Reintegration und vollständigem `TEST_OK` ist abgenommen. Die
generierte VMware-Konfiguration nutzt SATA und wurde mit erfolgreichem
AHCI-Boot, FAT32-Mount und gestarteter Userspace-Shell manuell abgenommen.
Eine deterministische, einmalige Compile-Time-Fault-Injection für Timeout,
TFES und TFD-Fehler ist ergänzt; sie nutzt ausschließlich die normale
begrenzte Completion-/Port-Stoppbehandlung. Alle drei Fehlerbilder wurden in
QEMU mit AHCI-Gastlauf nachgewiesen: Der Gast bootet, meldet den begrenzten
Storage-I/O-Fehler und erreicht keine Kernel-Panic. Der allgemeine Datei-I/O-
Smoke-Test bleibt in diesen absichtlich fehlerhaften Builds erwartungsgemäß
negativ. QEMU-AHCI und VMware-AHCI gelten damit für das Erfolgsprofil als
unterstützt; die Fault-Injection ist für QEMU abgenommen, aber nicht für
VMware als reproduzierbarer Acceptance-Lauf.

Ein auf realer Hardware beobachteter Panic beim Öffnen von `/REIST.PRG` wurde
als nichtdeterministische Root-Volume-Auswahl reproduziert: Bislang wurde das
erste erfolgreich erkannte Dateisystem als `/` veröffentlicht, unabhängig
davon, von welchem Datenträger der Kernel gestartet worden war. Der Mountpfad
validiert nun vor der Veröffentlichung höchstens `MAX_DRIVES` Partitionen und
zieht genau ein strukturell gültiges FAT32-Volume mit dem exakten Label
`X86 SYSTEM` vor. Meldet der BIOS-Loader dagegen ein Floppy-Bootlaufwerk,
bleibt genau dieses FAT12-Laufwerk bevorzugtes Root-Volume. Doppelte
Systemlabels und ein fehlgeschlagener Mount dieses
Volumes brechen fail-closed ab; weitere Volumes können das Default-Laufwerk
und Systemprogramme nicht mehr überschreiben. FAT32-Verzeichnis-I/O-Fehler
bleiben als `VFS_ERR_IO` von einem fehlenden Namen unterscheidbar. Ein
Zwei-AHCI-Plattenlauf mit fremdem FAT32 an Port 0 und dem bootfähigen
REIST-Volume an Port 1 bestätigt Root-Auswahl, `/REIST.PRG`, Shell-Start und
die vollständige Probe-Recovery-Sequenz.

Das betroffene physische AMD-System bietet im BIOS ausschließlich den
IDE-Kompatibilitätsmodus für seine einzelne SATA-HDD. Dafür erkennt der
ATA-Treiber nun begrenzt PCI-Funktionen der Klasse `01/01`, aktiviert und
prüft deren I/O-Decoding und verwendet entweder die festen
Kompatibilitätsports `1F0/3F6` und `170/376` oder validierte native I/O-BARs.
Jeder Kanal wird höchstens einmal mit fester Zeitgrenze zurückgesetzt; leere
Master-/Slave-Slots sind eine stille negative IDENTIFY-Probe. Entscheidend
ist, dass jeder nachfolgende PIO-Zugriff zuerst sein tatsächliches Ziel
auswählt: Ein vom zuletzt geprüften, nicht vorhandenen Slave stammendes
`ERR` darf den ersten Zugriff auf die vorhandene Master-HDD nicht mehr vor
der neuen Befehlsausgabe abbrechen. Erst der Status des neu gestarteten
Lese- oder Schreibbefehls gilt als Gerätefehler. Eine PCI-IDE-QEMU-Bootprobe
mit nur einer Master-HDD sowie die Zwei-Platten-AHCI-Probe erreichen
`REIST_PROBE RECOVERY_SEQUENCE_OK`. Die erneute Abnahme auf dem physischen
AMD-System bleibt offen und ist Voraussetzung für eine Hardwareaussage.

Die physische Gegenprobe mit exakt diesem Build auf einem zweiten Board
(`ASUS H81M-K`, Intel-H81-PCH) ergab denselben Panic. Der erweiterte Kontext
`Details 0xFFFFFFFF 0x00000000` bedeutet dabei eindeutig: Es existiert kein
Root-Resource und `drive_count` ist null. Der MBR-/CSM-Loader und der Kernel
selbst wurden bereits erfolgreich geladen; ein allgemeiner UEFI-Bootfehler
ist deshalb ausgeschlossen. Offen war stattdessen die Firmware-abhängige
Initialisierung des SATA-Controllers vor dem VFS.

Der physische Storage-Probe erfasst nun bis zu vier PCI-IDE-Funktionen und
acht eindeutige Command-/Control-Kanäle, statt nach der ersten IDE-Funktion
abzubrechen. Doppelte oder widersprüchliche Portzuordnungen werden nicht
zweimal veröffentlicht. Für AHCI setzt der Treiber nach dem begrenzten
HBA-Reset die standardisierten Spin-up-/Power-on-Bits, löst für noch nicht
verbundene implementierte Ports einmal COMRESET aus und wartet mit einer
einzigen controllerweiten monotonen Frist auf die Links. Eine vor Start der
FIS-Engine noch null oder `0xFFFFFFFF` lautende Portsignatur ist nur ein
IDENTIFY-Kandidat; veröffentlicht wird weiterhin ausschließlich ein gültig
identifiziertes ATA-Gerät. Bleiben ATA, AHCI und FDD vollständig leer, stoppt
der Boot nun unmittelbar vor Partition/VFS mit getrennt codierten
ATA-/AHCI-Probezählern. QEMU-IDE und QEMU-AHCI erreichen anschließend jeweils
`REIST_PROBE RECOVERY_SEQUENCE_OK`; die erneute H81-/AMD-Abnahme bleibt als
reale Hardwareevidenz offen.

Nach dem erfolgreichen realen SATA-Boot blieb das FAT32-Systemvolume zunächst
nur lesbar: `MKDIR.PRG` und der Datei-I/O-Teil von `GTEST.PRG` scheiterten beim
Lesen des Root-Verzeichnisclusters. Ursache war kein Medien- oder Write-Fence,
sondern der ATA-Kompatibilitätswrapper. Seine partition-relative
Mehrsektor-Lesefunktion übersetzte den LBA korrekt zum Elternlaufwerk, leitete
den Zugriff danach aber bedingungslos an den PIO-IDE-Pfad weiter. Bei einem
AHCI-Elternlaufwerk wurden dadurch virtuelle Kompatibilitätsports statt
`READ DMA EXT` verwendet. Der Wrapper wählt nun vor dem PIO-Pfad den validierten
Transport des Elternlaufwerks und liest eine begrenzte Anzahl von Sektoren über
AHCI. Der vollständige QEMU-SATA-Gastlauf bestätigt anschließend
`FILE_IO_OK`, alle Scheduler-/IPC-/Storage-Stufen und `TEST_OK`. Die erneute
Schreibabnahme auf dem physischen H81M-K bleibt als Hardwareevidenz offen.

Die anschließende H81M-K-Gegenprobe mit Build
`9C103ECA7A2568136B5B4DB744C9459990E5E781` bestätigt die korrigierte
Storage-Erkennung: Systemvolume, `REIST.PRG`, Storage-Dienst und die komplette
Recovery-Sequenz starten. Der danach scheinbar bei der zweiten Ausgabe
`UDP_BOUND 9003` stehende Boot war kein Scheduler- oder Storage-Hänger. Die
vierte Dienstgeneration hatte ihre UDP-Kapazitäts-, Duplicate- und
Stale-Handle-Tests beendet und lief anschließend absichtlich in ihrer
dauerhaften Ereignisschleife. Weil kein unterstütztes NIC-Backend vorhanden
war, übersprang der Kernel jedoch die Service-Wartebarriere, gab `BOOT_OK` und
den Shell-Prompt zu früh frei und ließ spätere Dienstdiagnosen den Prompt
optisch überschreiben.

Die Probe veröffentlicht nun nach den begrenzten UDP-Starttests den neuen,
append-only Report `SERVICE_READY`. Der geschützte Supervisorzustand bindet
ihn an PID und Prozessgeneration und löscht ihn bei Fence oder Respawn. Der
Kernel wartet unabhängig von der NIC-Verfügbarkeit höchstens zehn Sekunden
auf diesen Zustand; ein Ablauf panikt vor `BOOT_OK`, ein fehlendes NIC wechselt
danach explizit in `local-only`. Der No-NIC-Gastlauf bestätigt die Reihenfolge
`RECOVERY_SEQUENCE_OK -> SERVICE_READY -> local-only -> BOOT_OK -> C:\\>`.
Das ASUS H81M-K besitzt laut Hersteller einen Realtek RTL8111G Gigabit-LAN-
Controller. REIST unterstützt RTL8139, E1000, NE2000 und nun den gebundenen
RTL8168/8111G-Pfad für PCI-ID `10EC:8168`. Der neue Treiber verwendet eigene
MMIO-Register und eigene feste DMA-Ringe; die inkompatiblen RTL8139-Register
werden nicht wiederverwendet. Die reale Link-/DHCP-Gegenprobe auf dem H81M-K
bleibt offen, weil die lokale QEMU-Version kein RTL8168-Modell anbietet.

Die danach auf dem H81M-K beobachtete fehlende PS/2-Eingabe war ebenfalls kein
LAN- oder Userspace-Fehler. Die Tastatur funktionierte im BIOS, der ursprüngliche
Treiber verließ sich beim Übergang an den Kernel jedoch vollständig auf den von
der Firmware hinterlassenen i8042-Zustand. Die erste Korrektur übernahm den
Controller begrenzt, prüfte `ACK`/`RESEND`, aktivierte IRQ1 und ergänzte einen
zehn Millisekunden langen Polling-Fallback. Die reale Gegenprobe blieb dennoch
ohne Eingabe und NumLock-Funktion, obwohl der Controller die Initialisierung
akzeptiert hatte. Damit war die verbleibende Abhängigkeit von seiner
Set-2-zu-Set-1-Translation nicht mehr als Hardwareinvariante haltbar.

Der Treiber schaltet die Controller-Translation nun aus, fordert mit `F0 02`
explizit Scan Set 2 an und wertet Make-, `F0`-Break-, `E0`-Extended- sowie die
feste Pause-Sequenz selbst aus. Unbekannte oder unvollständige Sequenzen erzeugen
keine Eingabe. AUX- sowie Paritäts-/Timeout-Bytes bleiben ausgeschlossen. Die
Lock-Tasten aktualisieren ihre Zustände im Decoder, führen den begrenzten
`ED`-LED-ACK-Austausch aber verzögert im Taskkontext statt im IRQ-Handler aus.
NumLock steuert jetzt außerdem tatsächlich Ziffern- beziehungsweise
Navigationssemantik des Nummernblocks. Der QEMU-i8042-Lauf schaltet NumLock
zweimal und sendet danach `help`; die Ring-3-Shell antwortet weiterhin. Die
frühe Diagnose lautet nun
`PS/2 keyboard ready: config=0x21 scanset=2-raw input=IRQ1+poll`. Eine Aussage
für das H81M-K war zu diesem Zeitpunkt noch offen.

Die nächste H81M-K-Abnahme bestätigte zwar reagierende NumLock-LEDs, aber keine
sichtbaren Zeichen am Userspace-Prompt. Damit sind i8042-Zugriff, mindestens ein
Lock-Make-Code, der Set-2-Decoder, der periodische Taskpfad und der verzögerte
`ED`-LED-Befehl nachgewiesen. Ein temporärer, fest begrenzter Trace wies danach
auf realer Hardware auch gewöhnliche Make-/Break-Codes und die erfolgreiche
Queue-Veröffentlichung nach. Das Ende nach genau 16 Einträgen war lediglich die
Diagnosekapazität und kein Stillstand des Controllers. Damit lag der Fehler
hinter der PS/2-Queue: Der Keyboard-API war zusätzlich der historische
COM1-Eingang vorgeschaltet. Auf realer Hardware kann ein nicht dekodierter
Legacy-UART-Bereich ausschließlich Einsen liefern und diesen gemischten
Eingabepfad dominieren.

Die Eingabequellen sind deshalb jetzt vollständig getrennt. `getchar`,
`getchar_nonblocking` und `get_input_line` konsumieren ausschließlich die
PS/2-Queue; der serielle Treiber enthält weder RX-Ring noch IRQ4-Handler oder
Keyboard-Wakeup. COM1 wird vor jeder Nutzung mit zwei Schreib-/Lesemustern im
Scratch-Register erkannt, stellt dessen ursprünglichen Wert wieder her und
bleibt bei fehlgeschlagener Erkennung deaktiviert. Auch der TX-Wait ist fest
begrenzt. Der automatisierte Gasttest schreibt weiterhin sein Protokoll auf
COM1, injiziert `GTEST` aber über begrenzte QEMU-`sendkey`-Befehle als echte
emulierte PS/2-Eingabe. Der QEMU-Gesamtlauf bis `TEST_OK` bestätigt diese
Trennung. Die anschließende reale Abnahme bestätigt funktionierende Eingabe an
der Userspace-Shell. Der nur zur Ursachenanalyse dienende Tastendruck-Trace ist
deshalb wieder vollständig aus Treiber und Laufzeitausgabe entfernt.

### Phase 3 — Unix-artige CLI-Grundfunktionen

#### R3.1 Pipes, Signale und TTY — XL

- Pipeobjekt mit blockierendem Ringpuffer auf Wait-Queues bauen.
- `dup`/`dup2` und Deskriptorvererbung beim Spawn ergänzen.
- Minimale Signale `SIGINT`, `SIGTERM`, `SIGKILL`, `SIGCHLD` implementieren.
- Prozessgruppen, Vordergrundgruppe und TTY-Modi hinzufügen.
- `waitpid` einschließlich `WNOHANG` bereitstellen.

#### R3.2 Userspace-Shell und Init — L

- Parser für Quotes und Escapes erstellen.
- `<`, `>`, `>>` und `|` auf Deskriptoren/Pipes abbilden.
- Hintergrundjobs, persistenten Verlauf, Umgebungsvariablen und Exitcodes ergänzen;
  der begrenzte flüchtige Up/Down-Verlauf ist umgesetzt.
- Ein kleines `INIT.PRG` als Reaper und Starter der Shell einführen.

### Phase 4 — Netzwerk bis zu Anwendungen

#### R4.1 IPv4/UDP härten und Sockets einführen — L

- ARP-Erneuerung, DHCP-Renew/Rebind und ICMP-Fehler ergänzen.
- Paketparser mit aufgezeichneten Frames, Grenzfällen und Fuzzing testen.
- Socketobjekte in die FD-Schicht integrieren: `socket`, `bind`, `sendto`,
   `recvfrom`, `close` und Timeouts.
- UDP-Echo zwischen Gast und Host als automatisierten Test betreiben.

#### R4.2 DNS — M

- DNS-Namen sicher kodieren/dekodieren, Kompressionszeiger begrenzen.
- A-Records, CNAME-Ketten, Timeouts und Caching implementieren.
- Lokalen deterministischen Testserver statt öffentliches Internet verwenden.

#### R4.3 TCP — XL

- Zustandsautomat und Connection Control Block erstellen.
- Sequenz-/ACK-Prüfung, Retransmission, RTO und Empfangsfenster ergänzen.
- Verbindungsaufbau, geordnete Daten, Reset und aktiven/passiven Close testen.
- Erst danach einen kleinen HTTP-Client oder `NETCAT.PRG` bauen.

### Phase 5 — USB und Plattform

#### R5.1 ACPI- und DMA-Basis — L

- RSDP/RSDT/XSDT mit Checksummen und Längengrenzen zentral parsen.
- MADT und HPET aus dieser Schicht beziehen; Poweroff/Reboot ergänzen.
- DMA-Puffer mit physischer Adresse, Alignment und Below-4-GiB-Grenze
   bereitstellen.

#### R5.2 xHCI in überprüfbaren Stufen — XL

**Status (28. August 2026): R5.2x auf der ASUS-Referenz physisch abgenommen.**
Die append-only Diagnose-ABI enthält das vollständige fehlgeschlagene USB-
Setup-Paket sowie Completion, Restlänge und Eventstufe. Ein xHCI-Short wird nur
für einen host-to-device Request mit `wLength=0`, ohne Data-TRB, mit terminalem
Status-TRB-Zeiger und Restlänge null als vollständig angenommen. Mandatory
`SET_CONFIGURATION` und `SET_PROTOCOL` bleiben ansonsten fail-closed. Die
gezielten USB-Tests, das VMware-VGA-Paket und der virtuelle RFB-Mauspfad sind
bestanden. Das HID-Gerät funktioniert am separaten USB-2.0-Controller. Am
USB-3.0-Port wurde es zunächst erst parallel zu einer PS/2-Tastatur nutzbar.
Deshalb sammelt nun jeder xHCI-Controller nach dem Start höchstens 500 ms lang
sichtbar werdende Root-Ports; zuvor galt dieses Fenster nur nach Intel-Port-
Routing. Der anschließende ASUS-Nachtest bestätigt USB-Tastatur und `DMESG`
mit dem neuen Image. Das schließt die konkrete Regression, nicht die breite
xHCI-Hardwarematrix.

Das abschließend erzeugte VMware-Paket wurde vom Benutzer zusätzlich manuell
erfolgreich gestartet und geprüft. Ein unmittelbar davor gestarteter
automatischer Nachlauf hatte den Gast wegen eines Host-VMware-Startfehlers
nicht erreicht; er wird deshalb nicht als Runtime-Erfolg gewertet. Die
automatische Nichtregression stützt sich weiterhin auf den zuvor bestandenen
begrenzten RFB-Mauslauf.

Das dafür gebaute `real_hw/vga`-Image startet wie alle anderen Images niemals
automatisch den Desktop, sondern endet zuerst in der Ring-3-Shell. Sein
SHA-256 lautet
`DBA5E5794405180CC11CBCB3FDB2BF81FDA001B2206BFF17700ED29135EDA3C3`;
`DESKTOP` bleibt ein ausschließlich manueller Benutzerbefehl.

Das nach einer zweiten Manifestprüfung veröffentlichte aktuelle
Installerartefakt `build/reist-os-real-hw.img` ist 64 MiB groß und hat SHA-256
`BF774039CF11370093B49E4E0D20094FB1315E15E440E84E6778B23F0E4DBFE9`.

Ein nachfolgender Kaltstart desselben Images reproduzierte ohne parallel
angeschlossene PS/2-Tastatur einen früheren EP0-Ausfall bereits beim ersten
acht Byte großen Device-Descriptor-Request. Die Diagnose belegt kein
Transfer-Event (`cc=0`, volle Restlänge, Stage 0) und damit keinen erneut
akzeptierten Control-Short. R5.2y schließt vor R6.2o die fehlende feste
SetAddress-Recovery zwischen erfolgreichem xHCI `Address Device` und dem ersten
EP0-Doorbell; die ASUS-Abnahme muss ohne PS/2 mit einem Ersatzimage erfolgen.

Der Ersatzkandidat besteht 10 Tastatur- und 16 Maustests sowie den
VMware-VGA-Paketbuild in 15 Sekunden. Das zweimal manifestgeprüfte kanonische
Hardwareimage `build/reist-os-real-hw.img` hat SHA-256
`93E416F48327CA62E7056B6CD9D791D983E62782A0B3318B2A83B6488691B8A5`;
der anschließende ASUS-Kaltstart ohne angeschlossene PS/2-Tastatur bestätigt
die USB-Tastatur am betroffenen xHCI-Port. R5.2y ist damit für die konkrete
Controller-/Gerätekombination abgeschlossen; eine breite USB-Hardwarefreigabe
wird daraus nicht abgeleitet.

Künftige `real_hw`-Builds veröffentlichen nach einer zweiten Manifestprüfung
atomar `build/reist-os-real-hw.img`, auch wenn der Quellbuild in einem
Diagnose-Unterordner liegt. Nur dieses zielprofilspezifische Artefakt wird von
den physischen Installationsskripten akzeptiert; Emulatorbuilds schreiben es
nicht.

Frühe Ring-0-Ausgaben werden für diese physische Diagnose zusätzlich in einem
festen 32-KiB-Speicherring gehalten. Der append-only Lesesyscall und das
Ring-3-Werkzeug `DMESG` liefern einen am ersten Head begrenzten Snapshot und
pausieren ohne VFS- oder Heap-Abhängigkeit nach jeweils 22 Textzeilen. Die
Compatibility-Profilgrenze umfasst den neuen Syscall 125; eingeschränkte
Dienstprofile erhalten ihn nicht automatisch.

- Controller stoppen/resetten und Capability-/Operational-Register validieren.
- DCBAA, Command Ring, Event Ring und Interrupter initialisieren.
- Root-Port-Anschluss erkennen, Slot aktivieren und Control Transfers testen.
- Deskriptoren lesen, Adresse und Konfiguration setzen.
- Erst HID-Boot-Tastatur, dann Hub und Mass Storage implementieren.

Jede Stufe benötigt einen QEMU-xHCI-Test und darf bei unbekannter Hardware das
Gerät nur deaktivieren, nicht den Bootvorgang blockieren.

### Phase 6 — optionale Modernisierung

Erst nach den vorherigen Meilensteinen einzeln entscheiden:

#### R6.1 Begrenzter SMP-Bootstrap — M

**Status (27. August 2026): Erste Stufe umgesetzt und in QEMU abgenommen.**
Die validierte ACPI-Schicht inventarisiert MADT-Prozessoren und startet bis zu
15 APs seriell über deadlinebegrenzte INIT/SIPI-Sequenzen. Jeder AP beweist
geschützte, gepagte C-Ausführung auf einem eigenen Stack und wird anschließend
nach Installation einer privaten GDT, eines Runtime-/Double-Fault-TSS und
CPU-lokalen IRQ-, Präemptions- und CR3-Zustands mit deaktivierten Interrupts
geparkt. QEMU bootet automatisiert mit einem und vier Prozessoren sowie ohne
APIC bis zur Ring-3-Shell.

Die zweite Fundamentstufe ergänzt CPU-besitzende, endlich wartende SMP-Locks,
einen realen Cross-CPU-Locknachweis, CPU-lokalen aktuellen Task und
Kernel-Schedulerkontext sowie einen eigenen Lock für Seitentabellenmutationen.
Administrative Tasktabellen-Transaktionen sind inzwischen ebenfalls
CPU-besitzend gesperrt und außerhalb des Schedulers nur über
generationsgeprüfte Snapshots sichtbar. Die atomare RUNNING-Besitz-/Runqueue-
Übergabe ist nun ebenfalls umgesetzt: Der Zielkontext gibt den alten Besitzer
erst nach dem vollzogenen `swtch()` frei. Der globale Scheduler serialisiert
Runqueue-Transaktionen und beachtet eine explizite CPU-Affinitätsmaske. Ein
generationsgebundener, real
von allen QEMU-APs quittierter TLB-Shootdown und eine explizite CPU-0-
Affinität für alle Legacy-PIC-IRQs sind umgesetzt. Jeder LAPIC-Timer wird
zusätzlich CPU-lokal gegen den PIT kalibriert und bleibt zunächst maskiert.
Nach `BOOT_OK` aktiviert ein eigener IPI die AP-Timer; je ein ausschließlich
AP-affiner, guard-page-geschützter Kernel-Probetask weist parallelen Eintritt,
Exit und Rückkehr in den lokalen Idlekontext nach. Reguläre Kernel- und Ring-3-
Dienste bleiben CPU-0-affin. Waitqueue-, IPC-, Tastaturpuffer- und UDP-/TCP-
Socketzustände sind bereits SMP-gesperrt und verwenden eine atomare
Condition-to-Waitqueue-
Übergabe ohne Spinlock über `swtch()`. Prozessliste, PID-/Generationsvergabe,
Exit-Commit und `wait` sind über den festen Rang `Prozess -> Scheduler`
serialisiert; der speicherresidente Storage-Request-/Bulk-Pool besitzt einen
eigenen SMP-Lock. Ein neuer rekursiver Timed-Mutex überträgt auch lange
Foreground-Transaktionen atomar in die Scheduler-Waitqueue. VFS, FAT32, ATA,
AHCI und FDD verwenden ihn bereits unter
`VFS -> FAT32 -> ATA/AHCI -> Scheduler` beziehungsweise
`VFS -> FDD -> Scheduler`; ein
Vier-CPU-QEMU-Probezug erzwingt Konkurrenz zwischen drei AP-Tasks und bestätigt
`MUTEX_READY workers=3 mask=0000000E`. Dieselben Tasks lesen barriere-synchron
einen Root-Sektor über ATA-PIO und ICH9-AHCI und bestätigen bytegleiche
Ergebnisse mit `SUBSYSTEM_READY workers=3 mask=0000000E`. Der Lebenszyklus der
AP-Probetasks endet generationsgeprüft mit
`REAP_READY workers=3 reaped=3`. Ein eigener blockierender Gasthelfer belegt
anschließend gleichzeitig alle 32 öffentlichen Taskslots; 48 feste
Kernelstack-Slots halten diese Kapazität auch bei bis zu 15 privaten AP-Idle-
Stacks vollständig verfügbar. Der wiederholte SMP4-Gastnachweis erreicht
`TASK_CAPACITY_OK` und `TEST_OK`. Die Migration der verbleibenden Treiberlocks
sowie parallele Fault-Injection bilden die verbleibende R6.2-Grenze vor
allgemeiner Mehrkern-Dienstausführung. Details stehen im
[SMP-Subsystemvertrag](../architecture/SMP_SUBSYSTEM.md).

- UEFI-Boot und GPT
- x86-64-Port mit neuem ABI
- allgemeine SMP-Dienstverteilung sowie IOAPIC/MSI; grundlegende per-CPU-Daten
  und der begrenzte xAPIC-Bootstrap sind bereits umgesetzt
- AHCI/NVMe, USB-Massenspeicher und Hotplug
- IPv6
- Mehrbenutzer-Identitäten, Dateirechte/ACLs und kryptografisch verifizierter
  Boot; dies ist getrennt von der bereits begonnenen Kernel-Capability-Basis
- dynamischer Linker, Shared Libraries, Paketverwaltung
- allgemeine 3D-/Multi-Monitor-Grafikbeschleunigung sowie WLAN

Diese Punkte sind groß genug für eigene Entwurfsdokumente und sollten nicht
nebenbei in die 32-Bit-Basis eingebaut werden.

## 8. Empfohlene Arbeitsreihenfolge

- [x] **1 · R0.1 Wait/Wakeup** — Größe S; keine Abhängigkeit
- [x] **2 · R0.2 Exception-Frames** — Größe S; keine Abhängigkeit
- [x] **3 · R0.3 PRG-v1-Vertrag** — Größe S; keine Abhängigkeit
- [x] **4 · R0.4 Gast-Smoke-Test** — Größe M; abhängig von R0.1–R0.3
- [x] **5 · R1.1 Wait-Queues/Sleep/Zeit** — Größe L; abhängig von R0.1
- [x] **6 · R1.2 Speicherverwaltung** — Größe L; abhängig von R0.4
- [x] **7 · R1.3 Synchronisation/Diagnose** — Größe M; abhängig von R1.1
- [x] **8 · R1.4 Grafischer Desktop-MVP** — Größe M; abhängig von R0.4 und R1.1
- [x] **8a · R1.5 Laufzeitgrafik und Desktop aus VGA** — Größe L; abhängig
  von R1.4
- [x] **8b · R1.6 kernelvermittelte Ring-3-Gerätedomänen** — Größe XL;
  abhängig von S0.3b
- [x] **8c · R1.7 PCI-HDA und Userspace-Audiobibliothek** — Größe XL;
  abhängig von R1.6
- [x] **8d · R1.8 Überwachter VMware-SVGA-II-2D-Treiber** — Größe L;
  `RECT_COPY`, Software-Fallback und der VGA-Shell-/Desktop-Lifecycle in QEMU
  und VMware abgenommen, ohne DMA
- [x] **9 · S0.1 Profil/Gefahren/Assurance Case** — Größe M;
  abhängig von R1.3
- [x] **10 · S0.2 Stack/Exception/Panic-Containment (QEMU/VMware)** — Größe L;
  abhängig von S0.1
- [x] **11 · S0.3a Bounded IPC/Capabilities v1** — Größe L; abhängig von
  S0.1 und S0.2
- [x] **12 · S0.3b Supervised Userspace Probe Domain** — Größe L; abhängig
  von S0.3a
- [ ] **13 · S0.3c Dienstmigration/Redundanz (teilweise)** — Größe XL;
  abhängig von S0.3b
- [ ] **14 · S0.4 Determinismus/Ressourcengarantie** — Größe L; abhängig von
  S0.1 und S0.3
- [ ] **15 · S0.5 Integrität/Boot/A-B-Updates** — Größe XL; abhängig von S0.1
  und S0.3
- [ ] **16 · S0.6 Verifikation/Langzeitbetrieb** — automatisiertes
  QEMU-/VMware-Forschungsgate abgeschlossen; Langzeit- und Hardwareevidenz
  offen; Größe XL; abhängig von S0.1–S0.5
- [ ] **17 · R2.1 ABI und FDs** — Größe L; für `REIST-research` durch das
  automatisierte S0-Gate freigegeben
- [ ] **18 · R2.2 VFS/FAT-Zuverlässigkeit** — Größe L; abhängig von R2.1 und
  S0.5
- [x] **19 · R2.3 Blockgeräte/Partitionen** — Größe L; abhängig von R1.3 und
  S0.5
- [ ] **20+ · R3 bis R6** — Größe L–XL; abhängig vom abgenommenen S0-Gate und
  der jeweiligen Basis

R2 bis R6 dürfen für die generische Forschungsbaseline nun nacheinander
fortgeführt werden. Ein daraus entstehender Hardware- oder Produktclaim bleibt
hinter den weiterhin offenen physischen und produktbezogenen S0-Nachweisen.

## 9. Definition of Done für jedes Paket

Ein Paket gilt nur dann als fertig, wenn alle folgenden Punkte erfüllt sind:

- [ ] öffentliche API, Fehlerfälle und Nebenläufigkeitsregeln sind dokumentiert
- [ ] positive, negative und mindestens ein Ressourcenfehler-Test existieren
- [ ] Hosttests laufen erfolgreich
- [ ] Windows-Referenzbuild läuft erfolgreich
- [ ] betroffene Laufzeitfunktion wird im automatisierten Gast geprüft
- [ ] kein Test beweist eine Laufzeiteigenschaft ausschließlich durch Quelltextsuche
- [ ] relevante QEMU-, VMware- oder Hardwarematrix ist dokumentiert
- [ ] alte Stubs, widersprüchliche Dokumentation und tote Pfade sind entfernt oder
  ausdrücklich als nicht unterstützt markiert
- [ ] betroffene Gefahren, Essential Functions, FTTI und sicherer/degradierter
  Zustand sind identifiziert; das Restrisiko ist begründet
- [ ] Anforderungen, Design, Code, Testfall und Ergebnis sind bidirektional
  rückverfolgbar und unabhängig geprüft
- [ ] Laufzeit-, Stack-, Speicher-, Queue- und I/O-Grenzen werden unter Überlast
  sowie durch Fault-Injection geprüft
- [ ] jeder Dienst besitzt Health-Monitoring, einen begrenzten Fehlerpfad und einen
  getesteten Restart-, Failover- oder Safe-State-Mechanismus
- [ ] Releaseartefakte sind signiert, reproduzierbar und ihrer SBOM sowie exakten
  Toolchain zuordenbar (signierter Kernel und Artefakt-SBOM sind umgesetzt;
  Reproduzierbarkeit, vollständige Toolchainbindung und signierte Provenienz
  bleiben offen)

Aktuelle Referenzbefehle:

```powershell
make test
.\scripts\build-windows.ps1 -Target qemu -RunTests
.\scripts\build-windows.ps1 -Target qemu -Video framebuffer -RunTests
```

Zusätzliche und geplante Ziele:

```text
make test-smoke
make test-smoke-pit
make test-smoke-memory
make test-generated-images
make test-fuzz
```

## 10. Unmittelbar nächster Schritt

Der vom Benutzer gewählte nächste Hardwarepfad ist native NVIDIA-GK208-2D-
Beschleunigung für das ASUS-Board. Das abgeschlossene Paket
`R2.2-nvidia-gk208-bringup` bindet ausschließlich `10de:1280`, startet einen
überwachten Ring-3-Treiber und prüft passive PMC-/PTIMER-/PFIFO-/PGRAPH-
Erreichbarkeit, ohne VBE-Scanout, Busmaster oder DMA anzutasten. QEMU und
VMware können GK208 nicht emulieren; nach den bestandenen automatisierten
Nichtregressionsgates bleibt daher genau ein manueller ASUS-Image-Lauf für die Marker
`NVIDIA_GK208_PROBE` und `NVIDIA_GK208_READY`. Das anschließende Engine-Paket
darf erst auf dieser Evidenz einen festen GPFIFO-Kanal, kernelvalidierte
FERMI_TWOD_A-Methoden und einen deadlinebegrenzten Fence aktivieren. Bis dahin
bleiben die Capabilitybits null und der CPU-/Shadow-Framebuffer verbindlich.
Der erste ASUS-Lauf meldete den optionalen Dienst als nicht vorhanden. Das
abgeschlossene Paket `R2.2a-nvidia-vbe-fallback` trennt deshalb veröffentlichte
Boot-Framebuffer-Metadaten vom tatsächlich aktiven Hardwaremodus und
reaktiviert bei `ENODEV` ausdrücklich den versiegelten VBE-/Softwarepfad.
Das abgeschlossene Paket `R2.2b-desktop-startup-splash` nutzt diesen validierten
Framebuffer unmittelbar nach der Aktivierung: Noch vor optionalem Datei-I/O
wird ein sichtbarer REIST-OS-Textfallback publiziert, anschließend ein fest
begrenztes 512x288-BMP in Ring 3 decodiert und bis zum ersten vollständigen
Desktop-Frame angezeigt. Fehlende oder ungültige Bilddaten bleiben nicht
fatal; Decoder- und Fontinitialisierung teilen sich statische Startpuffer.
Der reale ASUS-Nachweis hat zugleich `DRIVER_DEGRADED result=-36` aufgedeckt:
Die kanonische Identität `nvidia-gk208-ring3` passt nicht in den bisherigen
16-Byte-Supervisor-Namenspuffer und wird vor dem Spawn abgewiesen. Das
abgeschlossene Paket `R2.2c-nvidia-driver-name-capacity` hebt die feste Grenze
auf 32 Bytes an, bewahrt die Nicht-Trunkierung und hält den geschützten
Deskriptor unter 64 Bytes, bevor ein neues Hardwareimage erzeugt wird. Das
abgeschlossene `R2.2d` ersetzt die terminalabhängigen ANSI-Farbcodes in der
frühen Framebuffer-VFS-Ausgabe durch portablen Klartext bei unveränderten
Zählern.
Der zweite ASUS-Lauf erreichte danach `NVIDIA_GK208_READY`, aber ein
Treiberrestart deaktivierte den weiterhin kernelverwalteten VBE-Scanout und
ließ `DESKTOP_OK` nur im VGA-Textmodus sichtbar. Das abgeschlossene `R2.2e`
beschränkt die Scanout-Deaktivierung auf den tatsächlich gerätemodusbesitzenden
VMware-Treiber. Der passive GK208-Pfad behält VBE, ohne Quieszenz,
Generation-Fence oder Device-Recovery abzuschwächen.
Die QEMU-Reproduktion zeigte zusätzlich, dass ein Yield zwischen den festen
Fontreads keinen Service-Zeitslot garantiert. Feste 24-KiB-Abschnitte mit
einem begrenzten 1-ms-Sleep liefern beim 3-MiB-Maximalfont 128
Scheduling-Punkte, ohne die Heartbeat- oder WCET-Deadline aufzuweichen.
Gezielte Regressionen, QEMU- und VMware-Framebuffer-Pakete sowie der
QEMU-Runtime-Lauf bis `TEST_OK` bestehen; der sichtbare Übergang auf dem ASUS-
Ziel bleibt der abschließende manuelle Hardware-Nachweis.
Ein späterer physischer Lauf zeigte bereits vor Ausführung des Compositors
`VFS_ERR_IO` beim Öffnen von `/usr/gui/bin/desktop.prg`, danach einen ebenfalls
fehlgeschlagenen Shell-Fallback und schließlich einen Supervisor-Neustart als
Epoch 2. R2.2af und der reine Lifecycle-Versuch R2.2ag wurden deshalb ohne
Kandidatencommit abgebrochen. Dass dasselbe Image auf dem ASUS-Board normal
startet, begrenzt den Befund auf das derzeit nicht mehr benutzte AMD-Board.
Auch R2.2ah bleibt daher abgebrochen; eine allgemeine Storage-/Startup-
Admission-Änderung ist aus diesem Befund nicht gerechtfertigt. Eine spätere
Wiederaufnahme erfordert zuerst einen neuen reproduzierbaren AMD-Hardwarelauf.
Der nächste Hardwarelauf zeigte einen fehlenden `/trash`-Root. Das
abgeschlossene `R2.2f` erweitert den gemeinsamen nativen FAT32-Imagebaum um
explizite, leere und weiterhin begrenzte Verzeichnisse. `/trash/files` und
`/trash/info` sind
damit bereits vor dem ersten Desktopstart vorhanden; der Laufzeit-`mkdir`
bleibt nur Kompatibilitätsfallback für ältere schreibbare Medien.
Die Hostregressionen und beide vollständigen Framebuffer-Paketbuilds bestehen;
der ASUS-Ersatzimage-Test bleibt der manuelle Hardware-Nachweis.
Das abgeschlossene `R2.2g` friert als nächsten sicheren Schnitt die
`FERMI_TWOD_A`-Kommandoseite ein: Ein fester 64-Dword-Puffer erlaubt nur
pitch-lineares XRGB8888-Fill und überlappungssicheres Same-Surface-Copy und
wird vor jeder späteren Übergabe nochmals gegen exakte Paket-, Methoden-,
Wert- und Bereichsregeln geparst. Zwei read-only BAR0-Snapshots mit genau
einer begrenzten Millisekundenpause müssen zusätzlich stabile Identität und
einen fortschreitenden PTIMER zeigen. Dieses Paket schreibt keine GPU-Register
und aktiviert weder Busmaster noch DMA, IRQ, GPFIFO oder Capabilitybits.
Das abgeschlossene `R2.2h` verschiebt anschließend auch die passive
Registerprobe aus `display_control` in den überwachten Ring-3-Treiber. Der
generische Device-Domain-Pfad darf große physische BARs nur als auf höchstens
8 MiB geclippte Policy-Apertur vorbereiten; für GK208 waren zunächst exakt
`0x400104` Byte von BAR0 lesbar. `R2.2p` erweiterte das unverändert read-only
Fenster bis `0x41a1c8` für den GPCCS-DMEM-Port; `R2.2q` erweitert es bis
`0x5fa60c`, damit der überwachte Treiber höchstens 32 upstream-`GPC_UNIT`-
Topologieeinträge lesen kann. Keine Schreibregeln sind
installiert. Alle Registerreads
sind ausgerichtet und generationgebunden, PTIMER-Kohärenz ist auf vier
Versuche begrenzt. Mapping-, DMA-, IRQ-, Busmaster- und Capabilityrechte
bleiben null.
Das abgeschlossene `R2.2i` friert zusätzlich den hardwarewirkungslosen
Submission-Vertrag ein. Ein fester 72-Dword-Umschlag bindet
`FERMI_TWOD_A` auf Subchannel 3, übernimmt genau einen validierten Fill- oder
Copy-Strom, hängt eine wait-for-idle 4-Byte-Fence-Freigabe an und erzeugt
genau einen Kepler-GPFIFO-Eintrag mit ausgerichteter 40-Bit-Adresse und exakter
Dword-Länge. Ein unabhängiger Parser verwirft reservierte Privileg-,
Subroutine-, Conditional-Fetch- und Sync-Wait-Bits sowie Padding- und
Querverweisabweichungen. Der Ring-3-Selbsttest führt den Strom nicht aus und
erteilt keine neue Geräteautorität.
Das abgeschlossene `R2.2j` verbindet diesen Vertrag mit genau einem
kernelverwalteten 64-KiB-mediated-DMA-Pool. Die ersten 4 KiB bleiben dem
Kernel vorbehalten; feste getrennte Fenster enthalten genau einen
GPFIFO-Eintrag, den nullaufgefüllten 72-Dword-Pushbuffer und ein Null-Fence.
Der überwachte Treiber überträgt und verifiziert bytegenau 300 Byte, erhält
aber weder physische Adresse noch Mapping. Reine `MEDIATED_IO`-Profile können
nun grundsätzlich keinen DMA-Pool mehr binden; nur die exakte GK208-Auswahl
kombiniert IO und mediated DMA, während VMware DMA-frei bleibt. Die festen
GPU-VAs sind lediglich der Platzierungsvertrag für das folgende GPU-VM-Paket.
Das abgeschlossene `R2.2k` versiegelt außerdem das hardwareinaktive
GK208-Einzelkanalbild. GK208 wählt upstream `gk110_chan` und `gk110_runl`; der
Kanal übernimmt das GK104-RAMFC und verwendet für den nicht gruppierten Pfad
genau einen 8-Byte-Runlist-Eintrag. Ein 4-KiB-Instanz/RAMFC-Block, 512 Byte
USERD und die Runlist liegen in festen getrennten Poolfenstern, jedes
nicht dokumentierte Wort muss null bleiben. Die RAMFC-USERD-Adresse bleibt
ungesetzt und wird lediglich durch genau eine ungelöste kernelverwaltete
64-Bit-Relokation beschrieben. Ring 3 liest die Bilder nach begrenztem
Chunk-Staging bytegenau zurück, ohne Adresse, MMIO-Schreibrecht oder
Hardwarewirkung zu erhalten.
Das abgeschlossene `R2.2l` beschreibt nun auch beide vollständigen
GPU-VM-Imagevarianten. Das exakte GK208-Profil erhält dafür 512 KiB festen
kernelverwalteten Pool, ohne die 64-KiB-Verträge anderer Treiber zu ändern.
Feste PGD-/PT-Reservierungen tragen je nach 64-KiB- oder 128-KiB-FB-Seite die
upstream 14+14- oder 13+15-Bit-Geometrie für 4-KiB-GPU-Seiten. Genau fünf noch
ungelöste Relokationen verbinden RAMFC, PGD, PT und die drei NCOH-Datenseiten;
Pushbuffer und GPFIFO sind read-only, das Fence ist schreibbar. Alle
Adressziele bleiben null, nur `2^40-1` wird als gemeinsames VM-Limit gestaged.
`R2.2m` hat den Relokationsteil inzwischen geschlossen. Das append-only
Device-Control-Kommando 19 akzeptiert nur eine vor dem Claim installierte,
bytegenau passende Regelmenge, validiert alle sechs Ziele vor der ersten
Änderung, löst die Pooladressen ausschließlich in Ring 0 auf und versiegelt
danach den Pool gegen weitere Ring-3-Lese- oder Schreibzugriffe. GK208 wählt
Nouveaus Standard-Bigpage 17; Template 16 bleibt als geprüfte Alternative
erhalten. Das folgende Paket ist daher noch für die kontrollierte
Page-Mode-Registersetzung und GPU-VM-Aktivierung, GK208-GR-Initialisierung,
Runlist-/USERD-Publikation und den echten Fence zuständig. `R2.2n` trennt den
ersten dieser Seiteneffekte inzwischen als rückrollbare Transaktion ab:
Kommando 20 akzeptiert nur Gerät, read-only BAR-Handle und bereits versiegelten
DMA-Pool derselben Generation, ändert bei `0x100c80` ausschließlich das von der
Policy vorgegebene Bit 0 und prüft alle erhaltenen Bits. Der ausgewählte
Standard 17 löscht das Bit, Policy 16 setzt es. Fehlgeschlagene Aktivierung
rollt sofort zurück; Fence/Release deaktivieren zuerst Busmaster, stellen das
ursprüngliche Policy-Bit wieder her und bleiben bei fehlendem Readback gesperrt
und wiederholbar. Der tatsächliche PGD-Verbrauch beginnt erst mit dem noch
offenen Channel-Instance-Bind und Runlist-Commit; GR-Initialisierung,
USERD-Publikation, echter Fence und Capabilityfreigabe bleiben ebenfalls offen.
`R2.2o` schließt jetzt den unveränderlichen Firmwareeingang für die nächste
GR-Stufe. Die vier MIT-lizenzierten Nouveau-GK208-nofw-Arrays für FECS und
GPCCS sind auf Linux-Commit
`45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229` festgelegt. Ein 64-Byte-Manifest
und ein bounded Selbsttest prüfen 193/640/27/384 Dwords sowie vier feste
IEEE-CRC32-Werte, bevor der Treiber den DMA-Pool öffnet. Es gibt weder
Laufzeit-Dateizugriff noch einen veränderlichen Firmwarezeiger. `R2.2p`
schließt inzwischen den gesamten sicheren Pre-Start-Hardware-Slice:
Ring 3 staged alle vier Images in festen Poolfenstern vor dem Seal; Kommando 21
prüft ihre CRCs, resettiert ausschließlich GR, wartet höchstens 100 ms auf
beendetes Falcon-Scrubbing, lädt DMEM/IMEM samt Block-Tags und liest jedes Wort
zurück. Cleanup deaktiviert Busmaster, resettiert den Upload und stellt danach
den Page-Mode wieder her. FECS und GPCCS bleiben bewusst angehalten, weil die
geprüfte Nouveau-Reihenfolge zuvor die vollständigen topologyabhängigen GR-
MMIO- und Context-Switch-Listen verlangt. `R2.2q` friert diese statische
Planbasis nun als einen gemeinsamen Slice ein: Ein reproduzierbarer Generator
übernimmt aus demselben gepinnten MIT-lizenzierten Nouveau-Stand exakt 30
MMIO-Pakete mit 115 Tupeln und fünf Kontextstreams mit 199 Tupeln. Feste
Counts/CRCs, eine begrenzte GPC/ROP/TPC/PPC-Topologievalidierung und der
maximal 32 Register umfassende Kontexttransfer-Compiler laufen vollständig in
Ring 3. Auch HUB-Start, Readiness-Maske, Kontextgrößen-Readback und 2000-ms-
Referenzdeadline sind manifestiert, werden aber noch nicht ausgeführt. Als
nächster gemeinsamer Slice waren die dynamischen topologieabhängigen Werte und
die vollständige Ausführungsreihenfolge offen. `R2.2r` schließt diese
hardwareinaktive Lücke nun gemeinsam: Ein fester 64-Byte-Header und höchstens
2048 jeweils 16 Byte große Operationen binden Topologie, benutzte Länge,
Abschnittszähler und CRC. Der Compiler expandiert die statischen Tupel,
berechnet Tile-Map/ZCULL, GPC/TPC/PPC/ROP-Exceptions, alle nutzbaren
LTC-/PGRAPH-ZBC-Slots und die fünf Kontextgruppen in der gepinnten
`gf100_gr_init`-Reihenfolge. Laufzeitregister bleiben als semantische
Copy/Mask-Operationen erhalten. Zwei getrennte 128-KiB-Faultbuffer bleiben
typisierte, ungelöste Geräte-VRAM-Offsets statt erfundener DMA-/CPU-Adressen.
Ring 3 validiert das Abbild unabhängig, staged nur den belegten Präfix ab
`0x72000` und liest ihn vor dem bestehenden Seal vollständig zurück; keine
Operation wird ausgeführt. `R2.2s` validiert nun über append-only Kommando 22
das versiegelte Abbild, seine semantischen Operationen und die zweimal stabil
gelesene Live-Topologie nochmals in Ring 0. Der tatsächliche, den VBE-Scanout
enthaltende PCI-BAR wird aus geprüfter Loader- und PCI-Geometrie gewählt; feste
GK104/GK208-FB-/LTC-Proben begrenzen VRAM, aktive LTCs und Tag-RAM. Der Kernel
merkt sich zwei ausgerichtete Faultbuffer und einen Nouveau-konform berechneten
Tag-Bereich hinter dem sichtbaren Scanout, gibt aber keine Adresse an Ring 3
zurück und verändert weder VRAM noch MMIO. `R2.2t` schließt den gemeinsamen
LTC-/Resolve-/GR-Commit nun über append-only Kommando 23: Ring 0 wiederholt vor
dem ersten Schreibzugriff alle Image-, Topologie- und Planprüfungen, nullt nur
die zwei zugeschnittenen Faultbuffer, initialisiert den gepinnten GK104-LTC-/CBC-
Zustand und interpretiert den typisierten GR-Ablauf unter einer gemeinsamen
monotonen 5-s-Deadline. Kontextgruppen sind unmittelbar und vollständig
gerahmt; Erfolg verlangt FECS-Readiness und eine nichtleere Kontextgröße.
Teilfehler lösen vor Retry/Fence einen GR-Reset aus. Channel-Bind, Runlist,
USERD, Busmaster, IRQ, Submission, echter Fence und Capabilityfreigabe bilden
weiterhin das nächste Hardwaregate.
`R2.2u` schließt davor die bislang fehlende Kontext-Speichergeometrie über
append-only Kommando 24. Ring 3 und Ring 0 leiten unabhängig aus stabiler
Topologie und FECS-Kontextgröße Pagepool, Bundle, Attributpuffer sowie die
512-KiB-CB-Reserve mit ausgerichtetem Kontext ab. Der Kernel reserviert diese
Fenster nach erneuter Image-/Topologieprüfung nur opak hinter dem Tag-Bereich;
der Aufruf hat keinen Hardware-Schreibeffekt und veröffentlicht keine Adresse.
`R2.2v` versiegelt nun auch den vollständigen hardwareinaktiven
Golden-Context-Plan. Der gepinnte Generator enthält zusätzlich 245 ICMD- und
311 klassengebundene MTHD-Tupel mit festen CRC32-Werten. Vier opake,
seitenweise GPU-VA-Spannen liegen gemeinsam im vorhandenen 128-MiB-Small-Page-
Table-Fenster; der topologieabhängige Patchplan deckt die ausgewählten
GK104/GF100/GF117-Callbacks mit maximal 80 von 96 Einträgen ab. Zwölf geordnete
Phasen reichen von FE-Power/Reset bis FECS-Bind, Golden-Save und Retain. Ring 3
prüft diesen Plan vor Kommando 24, führt aber keinen Teil davon aus.
Als nächstes folgt die unabhängige Kernel-Rekonstruktion und atomare
Golden-Context-Erzeugung samt PTE-Anwendung unter einer gemeinsamen Deadline
und GR-Reset-Rollback. Erst danach dürfen Channel-Bind, Runlist, USERD-Kick,
Submission und echter Fence in einem abschließenden Capability-Gate aktiviert
werden.

`R2.2w` schließt diese Kernel-Ausführung jetzt mit append-only Kommando 25.
Eine temporäre, vollständig VRAM-interne Instance/PGD/PGT-Domäne bildet die
vier reservierten Puffer mit 4-KiB-Seiten ab; die GPU erhält weiterhin keinen
System-RAM- oder Busmaster-Zugriff. Ring 0 validiert eigene gepinnte Kontext-,
ICMD- und MTHD-Tabellen, führt die GK208-Folge unter einer gemeinsamen
Fünf-Sekunden-Deadline aus, speichert den opaken Golden Context samt CRC und
entfernt anschließend Bindung und temporäre Seitentabelleneinträge. Bei jedem
Teilfehler erfolgt zuerst der vorhandene GR-Reset-Rollback.

`R2.2x` schließt den verbleibenden Channel-/Capability-Slice in einem Paket.
Die append-only Kommandos 26--28 bauen einen privaten Kanal-VM-Vertrag,
aktivieren die begrenzte TOP-Runlist und PBDMA, übersetzen ausschließlich feste
XRGB8888-Fill-/Copy-Anfragen und quittieren sie über einen echten
Hardware-Semaphore-Fence. Fill und Copy werden beim Aktivieren als 1x1-
Operationen geprüft; erst danach meldet der Dienst beide Capabilities. Jeder
Hardwarefehler leert die Runlist, deaktiviert Busmastering, bereinigt privaten
Zustand und setzt GR zurück. Automatisiert sind Host-, QEMU- und VMware-
Nichtregression; der elektrische Fence- und Bildnachweis auf GK208 bleibt der
abschließende manuelle ASUS-Test.

`R2.2z` beseitigt die anschließend gemessene Publikationsbremse. Der Kernel
akzeptiert einen beschleunigten Frame nun für VMware-RECT_COPY oder einen
aktiven, eingezäunten GK208-GR-Kanal. Nach dem unverändert synchronen Fence
wird die CPU-Schattenkopie aktualisiert, das bereits beschleunigte Ziel aber
aus zusammengeführten Schadensrechtecken ausgeschnitten; nur höchstens vier
Reststreifen gehen noch über die CPU zum Scanout. Fehler bleiben vollständige
Software-Fallbacks. Start- und Sitzungsmarker unterscheiden angebotene von
tatsächlich verwendeter Beschleunigung.

S0.6c hat die ausdrücklich begrenzte automatisierte QEMU/VMware-
Forschungsbaseline abgeschlossen. Das externe Profil bleibt `unbound`; reale
Monitorhardware, elektrisches Fence-Readback und physische Fault-Injection
prüft der Benutzer manuell und QEMU-/Hostevidenz ersetzt diese Auswahl nicht.
R2.1 transportiert read-only `stat` über den statischen,
generationsgebundenen Storage-Pool zum überwachten Ring-3-Storage-Service. Die
unabhängige, feste FAT12-/FAT32-/ASCII-VFAT-Parsersemantik publiziert nur
bytegenaue Legacy-Äquivalenz. Operation 2 bleibt FAT32-spezifisch; die neue
append-only Operation 3 wählt FAT12/FAT32, weist FAT16 ab und behandelt die
feste FAT12-Rootdirectory sowie 12-Bit-Ketten einschließlich Sektorgrenzen.
Append-only Operation 4 publiziert denselben begrenzten Parser autoritativ,
ohne `SYS_STAT` aufzurufen oder darauf zurückzufallen; Operationen 1 bis 3
bleiben eingefroren. `STAT.PRG` ist als erster kurzlebiger Client kontrolliert
auf Operation 4
umgestellt: feste Pfadnormalisierung, monotone Deadline,
vollständige Antwortvalidierung und kein Legacy-Fallback. Der QEMU-Gast startet
das wirklich paketierte Programm auf einer FAT32-Testdatei. Weitere und
insbesondere langlebige Clients bleiben bis zu getrennten Cutoverpaketen am
Kernel-VFS. Die dafür erforderliche generation- und handlegebundene Cancel-ABI
ist mit append-only Syscall 118 vorhanden. Geclaimte Requests bleiben bis zur
Dienstquittierung `cancel-pending`; ihre Ergebnisse werden verworfen, ohne
einen physischen I/O-Abbruch oder Rollback zu behaupten. Ein echter
QEMU-FDD-Hotplug-Lauf prüft das paketierte `STAT.PRG` auf
`/mnt/fdd0/HOTPLUG.TXT`; EXT2 ist noch nicht Teil des Parsers.
`HTTPD.PRG` nutzt den Pfad inzwischen als erster lang laufender Client für
`/htdocs`, ohne Legacy-`stat`-Fallback. Ein eigener QEMU-Modus führt zwölf echte
HTTP/TCP-Anfragen aus, verlangt `HTTPD_VFS_STAT_CLIENT_OK`, hält den Server bis
`Ctrl+C` aktiv und gewinnt anschließend die Userspace-Shell zurück. Weitere
Operation 5 ergänzt nun einen heapfreien EXT2-Parser für Revision 0/1,
1--4-KiB-Blöcke und lineare Directories mit direkten oder einfach-indirekten
Directory-Blöcken. Der feste Vertrag begrenzt Ressourcen, Komponenten,
Directory-Blöcke und Sektorreads; HTree, Extents, Symlinks und 64-Bit-Größen
werden abgewiesen. Der eigene QEMU-Modus hängt eine deterministische zweite
IDE-Platte ein und führt das paketierte `STAT.PRG` auf
`/mnt/hdd1/readme.txt` aus. Weitere Clients warten auf die jeweils benötigte
Handle-Abdeckung.

R1.8 ist ebenfalls abgeschlossen: Der generationsgebundene Ring-3-SVGA-II-
Treiber nutzt ausschließlich den festen Kernelmediator für Aktivierung,
`UPDATE`, `RECT_FILL` und capability-geprüftes `RECT_COPY`. QEMU und VMware
Workstation bestätigen Aktivierung, Kopier-Selbsttest und `BOOT_OK`; bei
fehlender Capability oder Treiberfehler bleibt der Shadow-Framebuffer-
Softwarepfad maßgeblich. Direkte FIFO-/Framebuffer-Mappings, DMA, GMR, 3D und
Multi-Monitor bleiben ausgeschlossen.

Die Lifecycle-Korrektur R1.8a lässt den Boot-Selbsttest SVGA vor `READY` mit
Register-Readback deaktivieren. Nur der kanonisch identifizierte Desktop erhält
den generationgebundenen Service-Endpunkt; er aktiviert den Modus und gibt ihn
auf allen Ausstiegspfaden über denselben Ring-3-Treiber wieder frei. Der
begrenzte QEMU-`vmware-vga`-Lauf weist die Reihenfolge VGA-Shell, beschleunigter
Desktop und wiederhergestellte VGA-Shell maschinenlesbar nach; der VMware-Lauf
prüft die Deaktivierung vor `BOOT_OK`.

Phase 0, R1.1 bis R1.4, **S0.3a Bounded IPC/Capabilities v1** und
**S0.3b Supervised Userspace Probe Domain** sind umgesetzt und abgenommen.
Die dafür geschlossenen IPC-/Isolationsinkremente sind:

- [x] endliche Send-/Receive-Deadlines mit eindeutigem Timeoutstatus — umgesetzt,
- [x] CRC- und `critical_object`-Schutz für Queue-, Endpoint- und
   Capability-Metadaten einschließlich deterministischer Bitflip-Injection —
   umgesetzt; unkorrektierbare Objekte quarantänisieren den Endpoint und
   wecken beide begrenzten Warteschlangen mit eigenem Integritätsstatus,
- [x] explizite selektive Delegation mit ausschließlich abschwächbaren Rechten —
   umgesetzt; Ziel-PID und Prozessgeneration werden atomar gebunden, Spawn
   vererbt keine IPC-Autorität mehr,
- [x] mindestens ein reservierter Service-/Restart-Taskslot mit Admission Control
   — umgesetzt; normale Spawns können weder den letzten Task-/Prozessslot noch
   das 32-Frame-Restartbudget verbrauchen,
- [x] Capability-/Domänen-Gates für `kill` und alle ambienten Datei-, Display-,
   Prozess- und sonstigen Syscalls der Probedomäne — umgesetzt; Autorisierung
   erfolgt zentral vor Seiteneffekten, das Probeprofil ist default-deny.

- [x] überwachte Ring-3-Probe mit begrenztem Ablauf `fence -> revoke -> reap ->
   recreate -> self-test -> reintegrate` — umgesetzt und in realem QEMU mit
   unabhängiger Prozess-/Zeitfortschrittsmessung abgenommen.

Der damalige nächste Schritt war **S0.3c Dienstmigration/Redundanz**: zuerst einen
unkritischen echten Dienst hinter die bestehende Capability-/Supervisorgrenze
verschieben, danach Netzwerk und Storage schrittweise aus Ring 0 lösen. Jede
Migration benötigt Fault-Injection, Ressourcenbudgets und einen nachweisbaren
degradierten Betrieb ohne Rückfall auf ambienten Kernelzugriff.

**S0.3c-1 ist umgesetzt:** Die gesunde Ersatzdomäne stellt einen begrenzten
Diagnosedienst bereit. Ein neuer append-only Service-Connect-Syscall 57 prüft
den Userpuffer vor Delegation, validiert die aktuelle Dienst-PID und
-Generation und vergibt ausschließlich `SEND|RECEIVE`. `CONTROL` verbleibt
beim Dienst. Der reale Gast sendet `DIAG`, erhält `REIST_DIAG_OK` innerhalb
endlicher IPC-Deadlines und läuft danach bis `TEST_OK`.

**S0.3c-2 ist umgesetzt:** Der append-only Syscall 58 gibt eine delegierte
Client-Capability frei, ohne den Endpoint des Dienstbesitzers zu zerstören.
Freigabe und Exit-Cleanup entfernen den generation-gebundenen Datensatz atomar
und wecken blockierte Peers. Der reale Gast prüft Freigabe, Ablehnung des stale
Handles, erneute Verbindung und einen zweiten Diagnose-Request/Reply ohne
Verbrauch zusätzlicher Capability-Slots.

**S0.3c-3a ist umgesetzt:** Der Ring-3-Dienst klassifiziert einen begrenzten
Ethernet-v1-Header als ARP, IPv4 oder sonstigen EtherType. Mindestlänge,
Nachrichtengröße und Antwort sind fest begrenzt; der Pfad allokiert nicht und
besitzt keine Hardware- oder Ausgabeautorität. GTEST überträgt einen
synthetischen ARP-Frame und der QEMU-Runner verlangt `NETWORK_PARSER_OK`.

**S0.3c-3b ist umgesetzt:** Der echte `netdev`-RX-Pfad spiegelt genau den
14-Byte-Ethernet-Header als feste `NET1`-Nachricht an den gesunden Dienst. Der
Ingress ist nichtblockierend, heapfrei und verwirft bei Queue-Druck oder ohne
aktiven Client. IPC bindet den Absender an den einzigen generation-geprüften
Peer, sodass der Client die eigene Ingress-Nachricht nicht konsumieren kann.

**S0.3c-3c ist umgesetzt:** Der gesunde Dienst kann über den ausschließlich im
Default-Deny-Profil erlaubten Syscall 59 einen festen Gateway-ARP-Probe
anfordern. Die Supervisorgrenze prüft PID plus Generation und begrenzt Aufrufe
auf einen pro 250 ms. Nur ein realer `NETR`-RX-Header erzeugt
`NETWORK_HANDOFF_OK`; ohne NIC antwortet der Dienst definiert mit
`REIST_NET_UNAVAILABLE`. Der QEMU-Runner besitzt dafür ein separates striktes
Abnahmeflag. Der 10-ms-Supervisor-Worker ruft den begrenzten `netdev_poll()`
als garantierten Bottom-Half auf; RX-Fortschritt hängt nicht mehr von einem
opportunistischen Shell-/Netzwerkkommando ab.

**S0.3c-3d ist umgesetzt:** Nur während einer ausstehenden, rate-limitierten
Probe kann ein ARP-Header übernommen werden. Nach erfolgreicher IPC-Publikation
wird dieses Frame nicht zusätzlich in die Kernel-Netstack-Queue gestellt. Bei
fehlendem Dienst, falschem EtherType oder Queue-Druck bleibt der Kernelpfad
zuständig; Fence/Restart löscht die Pending-Autorität. Damit existiert für das
übernommene Frame kein paralleler autoritativer Klassifikationspfad mehr.

**S0.3c-3e ist umgesetzt:** Nach einem echten NIC-Handoff fordert GTEST einen
zweiten ARP-Probe an; der Dienst führt unmittelbar danach absichtlich `UD2`
aus. Der Fence löscht Pending-Autorität, Exit-Cleanup widerruft den alten
Endpoint und der Client erwartet einen Kanalfehler. Innerhalb von höchstens
100 × 20 ms verbindet er sich mit der Ersatzgeneration, wiederholt den
Diagnose-Request und erreicht `NETWORK_RECOVERY_OK` sowie `TEST_OK`.
Die Basis-Recovery gilt im Runner über `RECOVERY_SEQUENCE_OK` als abgeschlossen;
dieser kumulative Marker entsteht ausschließlich nach der vierten erfolgreich
selbstgetesteten Generation und ersetzt flüchtige Zwischenzeilen als Gate.

**S0.3c-3f ist umgesetzt:** Der Diagnoseclient füllt nach einem begrenzten
`NETPRESSURE`-Handshake alle vier statischen IPC-Slots, während der Dienst
kurz schläft und anschließend eine echte Gateway-ARP-Probe auslöst. Der
nichtblockierende Ingress meldet Queue-Druck, verbraucht die einmalige
Probe-Autorität und überlässt das Frame dem bestehenden Kernelpfad. Erst nach
vier korrekt beantworteten Lastnachrichten wird `NETWORK_PRESSURE_OK`
ausgegeben; der strikte RTL8139-Smoke verlangt die geordnete Fallback- und
Fortschrittskette.

**S0.3c-3g ist umgesetzt:** Das versionierte Dienstprotokoll bindet jede
Anfrage und Antwort an eine von Null verschiedene 32-Bit-ID. Zusammen mit der
Endpointgeneration bildet sie die Korrelationsidentität; ein Restart zerstört
die alte Queue, und der Client akzeptiert auf der neuen Generation nur seine
aktuelle ID. Der Gast lässt den Dienst absichtlich `request_id + 1` senden,
verwirft diese Antwort und beweist anschließend mit einer korrekt korrelierten
Diagnoseanfrage weiteren Fortschritt über `SERVICE_CORRELATION_OK`.

**S0.3c-3h ist umgesetzt:** Der exklusive Probe-Handoff transportiert jetzt
den vollständigen festen 42-Byte-Ethernet/ARP-Header. Die restartbare
Ring-3-Domäne validiert Hardwaretyp Ethernet, Protokolltyp IPv4,
Adresslängen 6/4 und den begrenzten Request/Reply-Opcode ohne Heap oder
variable Schleifen. Ein absichtlich falscher Hardware-Adresslängenwert erzeugt
keine Antwort; erst danach wird ein gültiges ARP-Frame klassifiziert. Der Gast
und Runner verlangen `ARP_VALIDATION_OK`.

**S0.3c-3i ist umgesetzt:** Beim Start einer Gateway-Probe friert der
Supervisor Gateway-IP sowie lokale IP/MAC generationsgebunden ein und hängt
sie an den 42-Byte-Frame an. Ring 3 akzeptiert ausschließlich ARP-Reply,
dessen Sender-IP der Gateway-IP entspricht, dessen Ziel-IP/MAC lokal sind und
dessen Ethernet-Quell-/Zieladressen mit dem ARP-Inhalt übereinstimmen. Eine
synthetisch verfälschte Gateway-Identität bleibt unbeantwortet; die korrekte
Identität erzeugt `ARP_IDENTITY_OK`, bevor der echte NIC-Handoff folgt.

**S0.3c-3j ist umgesetzt:** Syscall 60 ergänzt append-only eine Probe-v2-API,
die nach vollständiger Pointerprüfung eine von Null verschiedene monotone
32-Bit-Probe-ID liefert. Der 64-Byte-Ingress trägt diese ID; Ring 3 akzeptiert
das Frame nur für seine aktuell ausstehende ID. Der Supervisor bestätigt
genau einen passenden Dienstbericht mit `PROBE_ID_OK`, verwirft Replay und
fenced die ID bei Recovery. Nach Erschöpfung wird mit `-EOVERFLOW` gestoppt,
nicht auf Null zurückgesprungen; Syscall 59 bleibt kompatibel erhalten.

**S0.3c-3k ist umgesetzt:** Jede Probe-ID besitzt eine saturierend berechnete
absolute 250-ms-Deadline. Die heapfreie Autoritätszustandsmaschine erlaubt nur
eine aktive ID, konsumiert sie genau einmal und verwirft sie bei `now >=
deadline`; der 10-ms-Supervisor-Worker räumt auch ohne eingehendes Frame auf.
Der Hosttest deckt Frühzugriff, exakten Ablauf, Einmalverbrauch,
`UINT64_MAX`-Sättigung und endgültige 32-Bit-ID-Erschöpfung ab. Dadurch kann
eine späte semantisch gleiche ARP-Antwort keine alte Autorität verwenden.

**S0.3c-3l ist umgesetzt:** Saturierende 32-Bit-Zähler unterscheiden
abgelaufene Autoritäten, Queue-Fallback und semantisch abgelehnte Ingress-
Frames. Ablauf wird im 10-ms-Worker, Queue-Druck im Foreground-Handoff und
semantische Ablehnung durch den generation-validierten Dienstbericht erfasst;
kein Zählerpfad läuft im IRQ. Der Hosttest beweist getrennte Inkremente und
Sättigung bei `UINT32_MAX`, sodass Diagnosewerte nie still zurückspringen.

**S0.3c-3m ist umgesetzt:** Syscall 61 liefert eine feste 24-Byte-v1-Struktur
mit Version, Strukturgröße und den drei saturierenden Zählern. Pointer,
Schreibbereich, Version und Mindestgröße werden vor dem Snapshot geprüft; die
ABI bietet absichtlich keine Reset- oder Mutationsoperation. GTEST prüft
EFAULT, Header/Reserved-Feld, monotone Werte und beim echten Vier-Slot-Druck
einen gestiegenen Queue-Fallback-Zähler über `NETWORK_STATS_OK`.

**S0.3c-3n ist umgesetzt:** Der Diagnose-Snapshot liegt als versioniertes,
redundantes Critical Object vor. Lesen repariert eine beschädigte Kopie aus der
gültigen Replica. Bei Doppelkorruption liefert Syscall 61 `-84`, bevor Daten in
den Userbereich kopiert werden. Deterministische Hostinjektion beweist beide
Pfade und den Erhalt eines zuvor gezählten Ereignisses nach Einzelkorrektur.

**S0.3c-3o ist umgesetzt:** Probe-ID, absolute Deadline und monotone ID-Sequenz
liegen in einem versionierten Critical Object. Begin, Take, Expire und Cancel
lesen und publizieren ausschließlich validierte Snapshots. Ein beschädigter
CRC wird aus der zweiten Kopie rekonstruiert; Doppelkorruption liefert `-84`,
erteilt keine Autorität und isoliert eine aktive Probe-Domäne im Worker.

**S0.3c-3p ist umgesetzt:** Zugestellte Probe-ID, Gateway, lokale IP und MAC
bilden einen einzigen versionierten Critical-Object-Snapshot. Prepare,
Snapshot, Publish, Consume und Clear sind atomar unter der Supervisor-Sperre;
unvollständige Identitäten und falsche Bestätigungs-IDs werden abgelehnt. Eine
beschädigte Kopie wird rekonstruiert, Doppelkorruption erzeugt `-84` und führt
vor einem Handoff zur Isolation. Der Hosttest injiziert beide Fehlerklassen.

**S0.3c-3q ist umgesetzt:** PID/Generation, Endpoint, Supervisor-Handle,
Health/Fence, Launch-Zähler und Rate-Limit-Zeit bilden einen versionierten
Control-Snapshot. Registrierung, Restart, Self-Test, Delegation, Handoff und
Worker lesen und publizieren ausschließlich validierte Kopien. Einzelkorruption
wird rekonstruiert; Doppelkorruption sperrt alle Dienste und Probes, der Worker
fenced sämtliche Ausgänge. Hosttests beweisen Korrektur und fail-closed Read/
Write; direkte ungeschützte Probe-Control-Felder wurden entfernt.

**S0.3c-3r ist umgesetzt:** Die monotone Probe-ID dient zugleich als gemeinsame
Transaktionsepoche von Control, Autorität und Identitätskontext. Begin/Prepare/
Control-Publish sowie Snapshot/Take/Delivery-Publish laufen jeweils unter einer
kurzen gemeinsamen Supervisor-Sperre. Ablauf und Dienstbestätigung verlangen
dieselbe Epoche. Hosttests kombinieren absichtlich einzeln gültige Snapshots
verschiedener Epochen; Take, Publish und Consume lehnen sie ohne Mutation ab.

**S0.3c-4a ist umgesetzt:** Der append-only Syscall 62 übernimmt eine feste,
versionierte 24-Byte-ARP-Bindung ausschließlich von der generation-validierten
Probe-Domäne. Probe-ID, Gateway-IP und Sender-MAC müssen bytegenau zu Control-
Epoche und geschütztem Ingress-Kandidaten passen. Der Mediator verbraucht die
Autorität vor dem begrenzten 32-Slot-Cache-Update; Replay, falsche Epoche,
falsche IP/MAC, Broadcast/Null-MAC und fremde Prozesse scheitern vor der
Mutation. Der RTL8139-Gast bestätigt den realen Pfad mit `ARP_BINDING_OK`.

**S0.3c-4b ist umgesetzt:** Vermittelte ARP-Bindungen liegen in einem eigenen,
festen 32-Slot-Cache. Jeder Slot ist als versioniertes Critical Object mit
SECDED/CRC-geschützter Primär- und Schattenkopie gespeichert und bindet IP/MAC,
Quellepoche und eine saturierend berechnete monotone 30-s-Deadline zusammen.
Einzelkopiefehler werden rekonstruiert. Eine unlesbare Doppelkopie sperrt den
gesamten Lookup fail-closed, statt möglicherweise den beschädigten Slot zu
übersehen. Bei Ablauf wird die Bindung zu einem bleibenden Sperreintrag; der
Lookup fällt für diese IP nicht auf den unvalidierten Legacy-Cache zurück.
Auch Kapazitätserschöpfung verdrängt keine frühere Vertrauensentscheidung,
sondern lehnt die neue Mutation ab. Hosttests prüfen Ablaufgrenze, Sättigung,
Einzel-/Doppelkorruption und Poolgrenze; Paket-, normaler Gast- und echter
RTL8139-Smoke sind grün.

**S0.3c-4c ist umgesetzt:** Neben der Transaktionsepoche speichert jeder Slot
PID und Prozessgeneration des erzeugenden Dienstes. Der Fence wandelt vor dem
Prozessabbruch alle noch gültigen Einträge genau dieser vollständigen Identität
in bleibende Sperreinträge um. Der Supervisor-Worker scrubbt
hardwareunabhängig höchstens
einmal pro Sekunde alle 32 Slots; Ablauf wird publiziert, Einzelkorruption
gezählt und repariert, Doppelkorruption löst Isolation bzw. den globalen
Output-Fence aus. Die Cachebasis wird vor Hardwareerkennung initialisiert,
sodass auch ein No-NIC-System sicher recovern kann. Der echte RTL8139-Gast
erzwingt nach einer vermittelten Bindung einen Dienstcrash und akzeptiert
`NETWORK_RECOVERY_OK` erst nach `ARP_BINDINGS_REVOKED`. Hosttests prüfen zudem
falsche/richtige Generation sowie Scrub-Ablauf und Integritätsfehler.

S0.3c-5 verschiebt als nächsten Schritt die nächste echte ARP-/IPv4-
Verarbeitungsentscheidung vollständig in den isolierten Dienst und entfernt
den entsprechenden Ring-0-Parallelpfad. Fehler-, Queue-Druck- und
Restart-Injektion müssen weiterhin unabhängigen Gastfortschritt beweisen.

**S0.3c-5a ist umgesetzt:** Der Legacy-Cache darf die konfigurierte
Gateway-IP weder aus einem ARP-Paket noch implizit aus der Quell-MAC eines
IPv4-Pakets lernen. Eine zentrale, hostgetestete Policy blockiert diese
Mutation vor jedem Cache-Seiteneffekt. Wird eine Route manuell oder durch DHCP
publiziert, entfernt der Kernel außerdem eine möglicherweise zuvor gelernte
Legacy-Bindung. Gateway-Autorität kann damit ausschließlich über den
generation- und epochengebundenen Ring-3-Mediator in den geschützten Cache
gelangen. Nicht-Gateway-Peers verbleiben bis S0.3c-5b im klar bezeichneten
Kompatibilitätspfad.

**S0.3c-5b1 ist umgesetzt:** Ein an die lokale IP gerichteter ARP-Request wird
bei gesundem Dienst als festes 60-Byte-`NETQ`-Objekt exklusiv nach Ring 3
übergeben. Ring 3 validiert Ethernetziel, ARP-Struktur, Absenderidentität,
lokale Zielidentität und Request-ID, bevor Syscall 63 eine einzige Antwort
anfordert. Der Kernel gleicht PID, Prozessgeneration, 250-ms-Einmalautorität
und einen redundant geschützten Request-Kontext ab; erst nach atomarem
Verbrauch darf der NIC-Sendemechanismus laufen. Dienst-, Queue- oder
Sendefehler verwerfen den Request und zählen Degradation, statt den alten
Ring-0-Responder zu reaktivieren oder den Dienst zu beenden. Hosttests prüfen
ABI, Autorität, Einzelkorrektur und Doppelkorruption.

**S0.3c-5b2a ist umgesetzt:** Der QEMU-Runner verbindet User-Netzwerk,
Socket-Injektor und RTL8139 über einen virtuellen Hub. Nach der expliziten
Gastbereitschaft sendet er höchstens drei korrekt gerahmte ARP-Requests und
verlangt eine am Socket-Injektor empfangene, vollständig validierte
Ethernet-ARP-Antwort sowie anschließend `TEST_OK`. Erfolgreicher Paketverkehr
bleibt auf der normalen Konsole still; Ablehnungen bleiben sichtbar. Der Lauf deckte zwei zuvor synthetisch
verdeckte Fehler auf: Request-ID und Dienstgeneration waren unzulässig
gleichgesetzt, und der Ring-3-Parser prüfte die Quell- statt der
Broadcast-Zieladresse. Hostvertrag, Paketbuild und der echte Runtime-Modus
`arp-reply` sind grün. **S0.3c-5b2b ist ebenfalls umgesetzt und abgenommen:**
Ein Cache-Miss veröffentlicht eine feste `NETA`-Nachricht mit Request- und
Probe-ID an den überwachten Ring-3-Dienst. Eine geschützte,
generationgebundene 250-ms-Einmalautorität
bindet Request-ID und Zieladresse; erst Syscall 64 darf nach vollständigem
Abgleich den echten ARP-Request senden. Fehler aktivieren keinen alten
Ring-0-Fallback. Der RTL8139-QEMU-Lauf fordert die Auflösung über Syscall 65 an
und prüft `ARP_RESOLUTION_QUEUED`, `ARP_RESOLUTION_MEDIATED` sowie den am
QEMU-Socket ausgesendeten ARP-Frame für `10.0.2.99`. Damit ist S0.3c-5b
geschlossen.

**S0.3c-5c ist umgesetzt und abgenommen:** Gültige IPv4-/ICMP-Prüfsummen und
Paketgrenzen werden noch im Kernel geprüft; ein Echo-Request erzeugt danach
ausschließlich eine feste `NETI`-Nachricht für den gesunden Ring-3-Dienst.
Request-ID, Prozessgeneration, Quell-IP/-MAC, Identifier, Sequenz und höchstens
32 Payloadbytes liegen in einem redundanten Critical Object und einer auf
250 ms begrenzten Einmalautorität. Nur der neue append-only Syscall 72 darf
nach vollständigem Abgleich Kontext und Autorität atomar verbrauchen; erst
außerhalb der Supervisor-Sperre wird gesendet. Es existiert kein Ring-0-
Antwortfallback. Der Runtime-Modus `icmp-echo` injiziert einen echten Request
in RTL8139, verlangt `ICMP_ECHO_QUEUED -> ICMP_ECHO_MEDIATED -> TEST_OK` und
prüft Zieladressen, Identifier, Sequenz, Nutzdaten und Checksumme des wirklich
am QEMU-Socket beobachteten Reply. Als nächstes vermittelt S0.3c-5d die noch
im Kernel liegenden UDP-/DHCP-Entscheidungen schrittweise.

**S0.3c-5d1 ist umgesetzt und abgenommen:** Der begrenzte Kerneltransport
parst DHCP weiterhin und validiert die angebotenen IPv4-Werte, darf die aktive
Netzkonfiguration aber nicht mehr selbst publizieren. Stattdessen legt er
Request-ID, IP, Netzmaske, Gateway und DNS in einen redundant geschützten
Kontext und sendet ein festes 28-Byte-`NETD`-Objekt einschließlich der
Leasezeit direkt an den exakten
Endpoint-Besitzer. Dieser Kernel-zu-Owner-Ingress verwendet die reservierte
Absenderidentität `(0,0)` und benötigt weder einen erfundenen Prozess noch
eine vorher delegierte Client-Capability. Der Ring-3-Dienst prüft Maske,
Hostbereich und Gateway-Subnetz erneut; nur sein append-only Syscall 73 darf
die generationgebundene 1-s-Einmalautorität verbrauchen. Kontext und Autorität
werden vor der Netzmutation gelöscht. Der reale RTL8139-Modus `dhcp-config`
verlangt `DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED -> BOOT_OK`; Normalbetrieb,
ARP und ICMP bleiben zusätzlich grün. UDP-Transport sowie Renew/Rebind
verbleiben bewusst als S0.3c-5d2 im Kernel.

**S0.3c-5d2a ist umgesetzt und abgenommen:** Als erster echter UDP-
Dataplane-Schnitt akzeptiert Ring 0 ausschließlich Datagramme an Port 9000,
mit gültiger IPv4-UDP-Prüfsumme und höchstens 32 Nutzdatenbytes. Quell-IP,
Quell-MAC, beide Ports und Nutzdaten liegen in einem redundanten Critical
Object; eine generationgebundene 250-ms-Einmalautorität schützt die Antwort.
Der Ring-3-Dienst validiert das feste `NETU`-Objekt erneut und darf nur über
den append-only Syscall 74 antworten. Kontext und Autorität werden vor dem
einzigen NIC-Sendepunkt verbraucht; es gibt keinen Ring-0-Echo-Fallback. Der
Runtime-Modus `udp-echo` injiziert ein echtes Datagramm über RTL8139 und prüft
am QEMU-Socket Ports, IP-Adressen, Payload und Antwortprüfsumme. Allgemeine
Weitere Bindings, größere Nutzdaten, DHCP-Renew/Rebind und eine Socket-ABI
blieben zu diesem Stand S0.3c-5d2b2/R4.1; 5d2b2a schließt inzwischen den
statisch begrenzten Dienst-Binding-Vertrag.

**S0.3c-5d2b1 ist umgesetzt und abgenommen:** DHCP-Option 51 ist nun
verpflichtend und wird auf 60 Sekunden bis sieben Tage begrenzt. Vorschlag,
Ring-3-Prüfung und Commit tragen denselben Wert. Nach dem Commit speichert der
Supervisor Dienstgeneration, IP, Leasezeit und absolute monotone Deadline als
redundantes Critical Object. Ablauf, Integritätsfehler oder Fence entziehen
IP, Maske, Gateway und DNS einschließlich der alten Gateway-Bindung; eine
veraltete Generation kann die Autorität nicht behalten. Das dedizierte
Buildprofil verkürzt ausschließlich die Testdeadline auf 2500 ms. Der reale
RTL8139-Lauf verlangt `BOOT_OK -> DHCP_LEASE_EXPIRED` und weist danach noch
laufenden Kernel und Shell nach.

**S0.3c-5d2b2a ist umgesetzt und abgenommen:** Der überwachte Ring-3-Netzdienst
kann bis zu vier Ports ab 1024 binden. Ein 24-Bit-Generationsanteil im Handle,
die gebundene Dienstgeneration und eine pro Slot geschützte Einmalautorität
verhindern stale Antworten und Portübernahme. Jeder Slot besitzt einen
`critical_object`-geschützten Descriptor und Transaktionskontext; Payload,
Antwortfenster und Pool sind auf 32 Byte, 250 ms und vier Slots begrenzt.
Unbind, Deadline, Dienst-Fence und Neustart räumen Kontext und Autorität
idempotent auf, während die Generation monoton bleibt. Die append-only
Syscalls 75–77 validieren beide Userbereiche vor Publikation und rollen einen
Bind bei fehlgeschlagenem Copy-out zurück. Der echte RTL8139-Lauf bindet neben
Port 9000 auch Port 9001 und prüft dort Request, Ring-3-Revalidierung, Reply,
Ports, Payload und UDP-Prüfsumme. Das ist bewusst noch keine allgemeine
Anwendungs-Socket-ABI.

**S0.3c-5d2b2b ist umgesetzt und abgenommen:** Nach jedem erfolgreichen
Lease-Commit erhält der Ring-3-Netzdienst ein festes `NETL`-Objekt mit T1,
T2 und Ablaufdeadline. Ein heapfreier Zustandsautomat verwendet ausschließlich
absolute monotone Zeit, höchstens drei Versuche pro Renew-/Rebind-Phase und
keine Polling-Schleife. Der append-only Syscall 78 veröffentlicht genau einen
DHCPREQUEST; Prozessgeneration, erwartete IP, Operation, Transaktions-ID und
1,5-s-Deadline liegen bis ACK/NAK in geschützten Supervisorobjekten. Der
Kernel-Worker verarbeitet pro Durchlauf höchstens ein Reply und übergibt ein
gültiges ACK erneut an die bestehende Ring-3-Commit-Grenze. NAK, Deadline,
Fence oder Dienstneustart widerrufen die Transaktion fail-closed. Das
Testprofil verkürzt nur die effektive Lease auf fünf Sekunden; der reale
RTL8139-Lauf bestätigt `DHCP_RENEW_REQUESTED -> DHCP_RENEWED` bei weiter
laufender Shell. S0.3c-5e migriert als Nächstes den verbliebenen allgemeinen
IPv4-/UDP-/DHCP-Protokollzustand aus Ring 0.

**S0.3c-5e1 ist umgesetzt und abgenommen:** Eine vom Monitor- und Legacy-
Demux getrennte statische Acht-Slot-Queue spiegelt vollständige Ethernetframes
mit maximal 1518 Byte an den gesunden Netzdienst. Syscall 79 ist ausschließlich
im Default-Deny-Profil der aktuellen Dienstgeneration freigegeben, prüft den
gesamten User-Zielbereich vor dem Dequeue und liefert bei leerer Queue sofort
`EAGAIN`. Jeder Dienstneustart verwirft alte Queueinhalte. Ring 3 revalidiert
ABI, Länge, Padding und EtherType; erst ein erfolgreicher Copy-out erzeugt die
einmal konsumierbare Bestätigung für `FRAME_HANDOFF`. Der reale RTL8139-Lauf
weist diesen Weg bis Ring 3 nach. Der bestehende Ring-0-Demux bleibt in dieser
Schattenphase absichtlich aktiv. S0.3c-5e2 übernimmt darauf IPv4, UDP und DHCP
und entfernt erst nach äquivalenten Fault-/Drucktests den Parallelpfad.

**S0.3c-5e2a ist umgesetzt und abgenommen:** Der Netzdienst verarbeitet den
rohen Frame nun zusätzlich mit einem heapfreien IPv4-v1-Parser. Er begrenzt
Ethernetframe und IPv4-Header auf 1518 beziehungsweise 60 Byte, prüft Version,
IHL, Total Length, TTL und Headerprüfsumme und verwirft Fragmente sowie fremde
EtherTypes fail-closed. Das Ergebnisobjekt ist fest 28 Byte groß und wird vor
jeder Prüfung genullt. Ein generationsgebundener Liefernachweis erlaubt genau
den ersten ICMP- oder UDP-Parserreport; der RTL8139-Lauf bestätigt
`IPV4_PARSED_RING3`. Dies ist bewusst nur ein Shadow-Nachweis: Ausgabe und
Legacy-Demux bleiben bis S0.3c-5e2b im Kernel, und der Report erteilt keinerlei
Netzwerkautorität.

**S0.3c-5e2b1 ist umgesetzt und abgenommen:** Ein zweiter fester Ring-3-
Parser akzeptiert nur vom IPv4-v1-Parser validierte UDP-Datagramme. Er verlangt
ein nichtleeres Portpaar, eine UDP-Länge ab acht Byte, exakte Übereinstimmung
mit der IPv4-Nutzlast und eine nichtnull, über den Pseudoheader validierte
Prüfsumme. Ungerade Nutzdatenlängen sind abgedeckt; das 20-Byte-Ergebnis wird
bei jedem Fehler vollständig genullt. Der generationsgebundene UDP-
Liefernachweis ist weiterhin reine Diagnose. Ein realer RTL8139-Lauf bestätigt
`UDP_PARSED_RING3` sowie im selben Lauf einen vermittelten UDP-Echo-Request.
S0.3c-5e2b2a ist ebenfalls umgesetzt: Syscall 80 übernimmt ausschließlich
einen zum zuletzt ausgelieferten Frame passenden, CRC32-, generation- und
deadlinegebundenen Ring-3-Entscheid. Gültige Datagramme für aktive
Dienstbindings erzeugen eine geschützte Einmalautorität; ungültige oder
ungebundene Datagramme werden kanonisch verworfen. Der Kernel unterdrückt für
diese dienstbesessenen Ports die parallele Legacy-Zustellung. Der reale
RTL8139-Lauf bestätigt `UDP_INGRESS_RING3 -> UDP_ECHO_MEDIATED -> TEST_OK`.
S0.3c-5e2b2b muss als Nächstes DHCP-Eingang und verbleibenden UDP-Demux über
denselben validierten Ring-3-Pfad führen. Der erste Teil S0.3c-5e2b2b1 ist
abgenommen: Ein festes 52-Byte-Ergebnis entsteht ausschließlich aus einem
vollständig validierten BOOTP/DHCP-Reply mit Ports 67 nach 68, Ethernet/IPv4-
Grenzen, BOOTREPLY-Typ, Hardwaretyp/-länge, Transaktions-ID, Client-MAC,
Magic-Cookie und begrenzt durchlaufener Optionsliste. OFFER, ACK und NAK sind
die einzigen Nachrichtentypen; fehlendes END, abgeschnittene oder doppelte
kritische Optionen werden fail-closed verworfen. Eine vorhandene UDP-
Prüfsumme wird vollständig geprüft; der nur für DHCP/IPv4 zulässige Nullwert
„keine Prüfsumme“ bleibt explizit erkennbar. Ein CRC32- und generations-
korrelierter Diagnosebericht erzeugt keinerlei Konfigurationsautorität. Der
reale RTL8139-Lauf bestätigt `DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED ->
DHCP_PARSED_RING3 -> BOOT_OK -> TEST_OK`. S0.3c-5e2b2b2 ist vollständig
umgesetzt: Der feste 52-Byte-Syscall 81 akzeptiert für Renewal/Rebind nur die
aktuelle gesunde Dienstgeneration und korreliert Frame-CRC, absolute
Lieferdeadline,
Client-MAC sowie die geschützte DHCP-Transaktions-ID. ACK benötigt die
vollständigen Netzmasken-, Gateway-, DNS- und Lease-Optionen; NAK entzieht die
Lease fail-closed. Der RTL8139-Test bestätigt `DHCP_RENEW_REQUESTED ->
DHCP_RENEW_INGRESS_RING3 -> DHCP_RENEWED`. S0.3c-5e2b2b2b1 ist ebenfalls
abgenommen: Der append-only Syscall 82 startet nur für die aktuelle gesunde
Dienstgeneration eine geschützte 1.500-ms-Boot-Transaktion. Ring 3 validiert
OFFER und ACK, während Ring 0 ausschließlich je ein DISCOVER beziehungsweise
REQUEST sendet. Transaktions-ID, Frame-CRC, Client-MAC, Server-ID, angebotene
Adresse und Lieferdeadline werden vor jedem Zustandswechsel geprüft. Der
Dienst führt höchstens drei Versuche aus; der Kernel wartet insgesamt höchstens
sechs Sekunden und bleibt danach ohne IP fail-closed. Der RTL8139-Lauf bestätigt
`DHCP_BOOT_DISCOVER_RING3 -> DHCP_BOOT_OFFER_RING3 -> DHCP_BOOT_ACK_RING3 ->
DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED -> BOOT_OK`. S0.3c-5e2b2b2b2 hat
anschließend die synchronen Ring-0-Parserroutinen, die dedizierte Vier-Slot-
DHCP-Queue und den Supervisor-Poller entfernt. Der statische Service-Frame-
Handoff ist damit der einzige DHCP-Eingang. Boot und Renewal wurden erneut mit
RTL8139 abgenommen; der Bootlauf enthält zusätzlich Dienst-Crash, Restart und
Queue-Druck. Der allgemeine Ring-0-UDP-Parser, seine Legacy-Einspeisung und die
unbenutzte direkte Echo-Sendehilfe sind ebenfalls entfernt. Ring 0 verwirft
UDP-Eingang fail-closed; ausschließlich der CRC-/generation-/deadlinegebundene
Ring-3-Ingress darf für ein aktives Binding Antwortautorität erzeugen. Reale
RTL8139-Läufe bestätigen den primären und einen zweiten gebundenen Port sowie
Boot-DHCP. Damit ist S0.3c-5e2b abgeschlossen. S0.3c-5e2c ergänzt nun einen
heapfreien ICMP-Echo-v1-Shadow-Parser. Er akzeptiert ausschließlich vom
IPv4-v1-Parser validierte Echo-Requests oder -Replies, verlangt Code null,
prüft die vollständige ICMP-Prüfsumme einschließlich ungerader Nutzdaten und
publiziert ein kanonisches 28-Byte-Ergebnis. Ein PID-/generations- und
Frame-CRC-gebundener Liefernachweis erzeugt nur den Diagnosemarker
`ICMP_PARSED_RING3`; er verleiht keine Ausgabeautorität. S0.3c-5e2d bindet
darauf jede ICMP-Eingangsentscheidung an ein redundanzgeschütztes Ticket mit
PID, Prozessgeneration, Frame-CRC und absoluter 250-ms-Deadline. Syscall 83
akzeptiert ausschließlich kanonisches `DROP`, `ECHO_REQUEST` oder
`ECHO_REPLY`; erst danach darf eine Echo-Autorität entstehen beziehungsweise
ein erwarteter Ping abgeschlossen werden. Der alte Ring-0-ICMP-Parser ist
entfernt und ICMP fällt dort geschlossen aus. Der reale RTL8139-Lauf bestätigt
`ICMP_PARSED_RING3 -> ICMP_ECHO_QUEUED -> ICMP_INGRESS_RING3 ->
ICMP_ECHO_MEDIATED -> TEST_OK`; der bisherige ICMP-Echo-Lauf bleibt grün.
S0.3c-5e2e entfernt anschließend auch `handle_ip_packet`: Fallback-IPv4-Frames
werden in Ring 0 weder geparst noch demultiplext und dürfen keine implizite
ARP-Lernmutation mehr auslösen. S0.3c-5f schließt danach den gesamten
Legacy-Eingang: Die separate 64-Slot-Ring-0-RX-Queue, `netstack_process_packet`,
der ARP-Parser, der ungeschützte ARP-Cache und seine Lernrichtlinie sind
entfernt. Alle ARP-Lookups verwenden ausschließlich den redundant geschützten,
generations- und leasegebundenen Cache; Routenwechsel widerrufen alte und neue
Gateway-Bindungen vor der Konfigurationspublikation. Vollständige Frames gehen
nur noch an die statische Ring-3-Servicequeue, während die Monitorqueue rein
diagnostisch bleibt. Damit ist S0.3c-5 abgeschlossen.

**S0.3c-6a ist umgesetzt:** Storage-Schreiboperationen und VFS-Mutationen
besitzen nun jeweils einen redundant geschützten Aktivzustand und eine
saturierend gebildete absolute Deadline. Überlappende Operationen werden vor
dem ersten Seiteneffekt abgewiesen. Progress-, Integritäts- oder Idle-Fehler
verriegeln den Hardware-Schreibpfad beziehungsweise den VFS-Zustand
fail-closed. **S0.3c-6b ist umgesetzt:** Der gemeinsame Dataplane-Vertrag
besitzt acht statische Slots, 24-Bit-Generationshandles und versionierte
Block- sowie VFS-Operationen. Nutzdaten sind auf 512 Byte begrenzt und liegen
als CRC-geschützte Primär-/Schattenkopie vor; Metadaten und gebundene
Dienstidentität verwenden `critical_object`. Claim, Complete und Collect sind
generationgebunden, Rechte werden vor Zustandsänderungen geprüft und
Prozessende widerruft offene Requests. Der 128-Byte-IPC-Kanal muss damit nur
Handle und Status transportieren. S0.3c-6c bindet diesen Pool als Nächstes an
einen restartbaren Ring-3-Storage-Service.

**S0.3c-6c ist umgesetzt:** `STORAGE.PRG` läuft in einem eigenen
Default-Deny-Profil und besitzt ausschließlich Zeit-/Fortschritts- sowie
Storage-Bind/Claim/Block-Read/Complete-Syscalls. Eine geschützt gespeicherte
Dienstidentität bindet den Dataplane generationsgenau. Der Supervisor erkennt
Starttimeout oder Prozessverlust, widerruft alte Slots und startet höchstens
drei Ersatzinstanzen; danach werden Blockschreibpfad und VFS-Mutationen
fail-closed gefenct. Requests besitzen zusätzlich eine monotone Maximaldauer
von fünf Sekunden und ein Zwei-Slot-Clientlimit. Der reale Gasttest liest über
Client -> Pool -> Ring-3-Dienst -> kernelvermittelten ATA-Zugriff -> Pool den
MBR und validiert `0x55AA`. S0.3c-6d ergänzt als Nächstes echte
Crash-/I/O-/Power-Loss-Injektion.

**S0.3c-6d1 ist umgesetzt:** Ein ausschließlich im separaten Testimage
kompilierter Hook beendet genau die erste Storage-Dienstgeneration nach einem
erfolgreichen ATA-Read und vor Copyout/Complete. Der beanspruchte Request wird
beim Exit generationssicher widerrufen. Der Supervisor erkennt den Verlust,
startet innerhalb seines festen Budgets eine neue Instanz und bindet nur deren
neue Generation. Der Client akzeptiert den alten Handle nicht erneut, stellt
höchstens einen Ersatzrequest und validiert danach wieder den echten MBR. Der
QEMU-Gate verlangt die geordnete Folge `TEST_CRASH_INJECTED ->
SERVICE_FAILURE_DETECTED -> SERVICE_RESTARTED -> SERVICE_READY ->
STORAGE_RESTART_OK -> TEST_OK`. S0.3c-6d2 ergänzt als Nächstes einen realen,
kontrollierten ATA-I/O-Fehler; Power-Loss bleibt S0.3c-6d3.

**S0.3c-6d2 ist umgesetzt:** Der vermittelte Block-Read führt höchstens zwei
ATA-Versuche aus. Scheitern beide, erhält der Client `-EIO` und die Ressource
wird in der redundant geschützten Dienstkontrolle quarantänisiert. Bis zur in
S0.3c-6e eingeführten vollständigen Requalifizierung endet jeder Folgezugriff
derselben Ressource vor dem Hardwareaufruf mit `-EHOSTDOWN`; andere Kernel- und
Dienstfunktionen laufen weiter. Ein separater Testbuild erzwingt beide
Fehlschläge. Der normale Build enthält keinen Injektionspfad.

**S0.3c-6d3 ist umgesetzt:** Der persistente QEMU-Test
erzeugt eine ACTIVE-Undo-Transaktion, verändert zwei Zielsektoren, hält die
alten Daten redundant vor und beschädigt zusätzlich die primäre
Journal-Metadatenkopie. Beim Neustart wählt der Kernel konservativ die gültige
Kopie, restauriert beide Sektoren, repariert die Header und schreibt CLEAN.
Erst nach vollständiger Probe-Reintegration startet GTEST; der neu gebundene
Ring-3-Storage-Dienst muss danach den echten MBR-Selbsttest bestehen. Der Lauf
prüft abschließend die persistenten Sektoren und beide identischen Header. Das
Gate deckte außerdem einen Start-Race auf: Der Supervisor-Worker konnte den
noch nicht explizit aktivierten Dienst vor `storage_service_start()` starten.
Ein eigener Aktivierungszustand trennt nun „noch nicht gestartet“ von
„ausgefallen“, und IRQ-serialisierte Kontrollzugriffe verhindern konkurrierende
Reparatur der redundanten Kopien.

**S0.3c-6e ist umgesetzt:** ATA- und FDD-Ressourcen erhalten beim Boot einen
redundant geschützten Fingerprint. Nach einem I/O-Ausfall prüft ein begrenzter
Hintergrundlauf Controller- und Medienidentität sowie zwei frische, identische
Bootsektor-Reads. Ein reiner Lesefehler darf das unveränderte Medium wieder
`ONLINE_RW` schalten; der QEMU-Gate verlangt dafür `RESOURCE_QUARANTINED ->
RESOURCE_REINTEGRATED_RW -> STORAGE_MEDIA_REINTEGRATED_OK -> TEST_OK`.
Unsicher abgeschlossene Schreibzugriffe werden nie blind wiederholt: Sie
fencen Storage und VFS und erlauben höchstens `ONLINE_RO`.

Das VMware-A:-Reconnect-Problem ist zusätzlich als echter QEMU-QMP-Hotplug-
Lauf reproduziert und geschlossen. Ein normaler FAT12-Lesefehler meldet die
FDD-Ressource und quarantänisiert sie. Nach dem Wiedereinlegen setzt die Probe
den FDC zurück, leert die Reset-Interrupts, programmiert ihn neu, kalibriert
das Laufwerk und liest das Medium zweimal außerhalb des normalen gesperrten
Pfads. Erst danach folgen `RESOURCE_REINTEGRATED_RW 1`, eine erneut
erfolgreiche Lektüre von `HOTPLUG.TXT` und `TEST_OK`. Der identische
Disconnect/Reconnect-Ablauf wurde am 15. August 2026 außerdem manuell unter
VMware mit erfolgreicher Wiederverwendung von A: bestätigt.

**S0.3c-6f ist teilweise umgesetzt:** Markierte REIST-FAT12-Medien besitzen
jetzt ein verifiziertes Undo-Journal, begrenzte Remaps, kritische Replikate,
geordnete Dateitransaktionen und eine deterministische Host-Fehlermatrix über
alle 29 Persistenzbarrieren. QEMU-FDD-Reconnect ist abgenommen; reale VMware-
und Power-Loss-Laufzeitevidenz bleibt offen. Fremde FAT32-Volumes sind nun
kompatibel lesbar, aber ohne gültigen REIST-Journalmarker fail-closed read-only;
auch ein Wechsel der globalen ATA-Journalbindung kann keinen unjournalisierten
FAT32-Write mehr freigeben. Fremde FAT12-Volumes sind ebenfalls kompatibel
lesbar, aber ohne vollständig validiertes REIST12-Journal, Remap und
Replikatlayout fail-closed read-only; auch direkte FAT- und Sektormutationen
werden vor einer Zustandsänderung abgewiesen. EXT2 sowie künftige
USB-/Flash-/NVMe-Backends benötigen für beschreibbaren Betrieb weiterhin einen
eigenen nachgewiesenen Undo/COW/Journal-Vertrag. Vor diesem Nachweis darf ein Medium nach unklarem
Schreibabschluss nicht automatisch wieder beschreibbar werden.
Bei Wechselmedien fehlen außerdem noch eine stärkere Ganzmedien-Identität und
kontrollierte Cache-Invalidierung beziehungsweise ein Remount, wenn sich der
Inhalt außerhalb von REIST bei unverändertem Boot-Fingerprint geändert hat.
S0.3c-6 bleibt deshalb teilweise offen; S0.3c-7 kann parallel fortgesetzt
werden.

Die FAT12-Arbeit wird in vier Sicherheitsgrenzen aufgeteilt. Zuerst erhält nur
ein explizit markiertes natives REIST-FAT12-Image ein fest begrenztes,
gespiegeltes Undo-Journal; fremde Medien werden dadurch niemals stillschweigend
umformatiert. Danach folgen eine persistente Defektsektor-/Remap-Tabelle und
Copy-on-Write beziehungsweise replizierte Inhalte für ausgewählte kritische
Dateien. Ein FAT12-Bad-Cluster-Marker `0xFF7` verhindert lediglich eine neue
Belegung des Clusters und ist kein Daten-Recovery-Verfahren. Unlesbare
Nutzdaten dürfen daher nur aus einer CRC-, Sequenz- und invariantengeprüften
Kopie rekonstruiert werden; andernfalls wird der Verlust gemeldet und das
Medium bleibt read-only.

Das Zielbild der Wartung führt ausschließlich über den überwachten Storage-
Dienst. Darin verwaltet `FDISK.PRG` Partitionstabellen nur auf dafür geeigneten
Medien, `FORMAT.PRG` führt einen begrenzten Oberflächentest durch und erzeugt
das markierte FAT12-Layout, und `CHKDSK.PRG` trennt read-only Diagnose von
einer explizit bestätigten Reparatur. Der aktuelle engere Werkzeugstand ist
unten separat dokumentiert. Vor jeder Mutation sind exklusives
Maintenance-Lease, Unmount, Handle-Prüfung und erneute Medienidentifikation
Pflicht. Jeder Reparaturschritt läuft durch Journal/COW und verifizierendes
Readback; Auswurf, Timeout oder unklarer Schreibabschluss dürfen höchstens zu
einem konsistenten alten Zustand oder zu `ONLINE_RO` führen.

### Ausführungsplan S0.3c-6f für kleine Modelle

Dieser Abschnitt ist die verbindliche Arbeitszerlegung für S0.3c-6f. Ein Lauf
bearbeitet genau ein Paket. Ein Modell darf keine späteren Pakete vorziehen,
keine Dateiliste selbst erweitern und keine Sicherheitsprüfung durch einen
Kommentar oder einen Userspace-Workaround ersetzen. Vor jeder Änderung sind
der Worktree und die bereits vorhandenen Mechanismen zu prüfen. Bei einem
Widerspruch zwischen diesem Plan und dem High-Assurance-Vertrag gilt der
strengere Vertrag.

Gemeinsame Regeln für alle Pakete:

- Alle Schleifen, Requests, Wiederholungen und Medienoperationen besitzen eine
  feste Kapazität oder monotone Deadline.
- Nur der generationsgebundene Storage-Dienst führt Block-I/O aus. Normale
  Programme erhalten keinen direkten Controller-, DMA- oder Raw-Blockzugriff.
- Vor einer Mutation werden Version, Strukturgröße, Resource-ID, Medientyp,
  Blockbereich, Rechte, Lease und Medienidentität vollständig geprüft.
- Ein Write gilt erst nach erfolgreichem Readback und Bytevergleich als
  abgeschlossen. Ein unklarer Abschluss führt zu `ONLINE_RO`.
- Fremde FAT12-Medien bleiben kompatibel lesbar und konsequent read-only,
  erhalten aber weder Journal noch Remap-Tabelle und werden niemals
  automatisch konvertiert.
- Es werden keine bestehenden Syscallnummern geändert. Neue Syscalls werden
  ausschließlich am Ende angefügt.
- Tests prüfen zuerst die Negativpfade. Source-Pattern-Tests allein gelten
  nicht als Laufzeitnachweis.

#### S0.3c-6f0 — Vermittelte FDD-Blockoperationen

Ziel: Der überwachte Storage-Dienst kann genau einen 512-Byte-FDD-Block lesen
oder schreiben. Clients reichen weiterhin Requests über den bestehenden Pool
ein. Nur das gebundene Storage-Domain-Programm claimt und erfüllt sie.

Erlaubte Dateien: `kernel/syscall/syscall_table.c`, `kernel/proc/process.c`,
`lib/libc/stdlib.h`, `userspace/sdk/include/x86os.h`,
`userspace/sdk/x86os.c`, `userspace/programs/storage_service.c`,
`test/test_fat12_maintenance.py`.

Definition of Done:

- FDD-LBA wird mit der erkannten Geometrie geprüft und deterministisch nach
  CHS übersetzt.
- Blockread und Blockwrite lehnen unbekannte, quarantänisierte oder außerhalb
  der Geometrie liegende Ressourcen vor dem I/O ab.
- Blockwrite lehnt read-only Ressourcen ab, schreibt genau einen Sektor und
  verifiziert ihn durch frischen Readback.
- Readback-Fehler oder Datenabweichung melden einen unklaren Schreibabschluss
  und degradieren die Ressource.
- Das Storage-Domain-Profil enthält nur die dafür benötigten append-only
  Syscalls. Probe- und normale eingeschränkte Profile erhalten sie nicht.

Stop-Bedingung: Wenn der Pfad die bestehende Fence-/Storage-Safety-Schicht
umgehen müsste, Paket blockieren und keine alternative Direkt-I/O-API bauen.

#### S0.3c-6f1a — Exklusives Maintenance-Lease

Ziel: Mutierende Wartung ist nur mit einem geschützten, generations- und
mediengebundenen Lease möglich. Das Lease hat eine maximale monotone Laufzeit
und wird bei Prozessende, Dienstrestart, Auswurf oder Identitätswechsel
widerrufen.

Erwartete Dateien: `include/kernel/storage_maintenance.h`,
`kernel/init/storage_maintenance.c`, `include/kernel/storage_service.h`,
`kernel/init/storage_service.c`, `kernel/proc/process.c`,
`kernel/syscall/syscall_table.c`, SDK-Dateien und neue Hosttests.

Definition of Done:

- `acquire` prüft Resource-ID, FDD-Typ, aktuellen Fingerprint, Schreibstatus,
  Mountzustand und offene Handles vor jeder Zustandsänderung.
- Es existiert höchstens ein Lease pro Ressource; Token enthalten Slot,
  Generation und Mediengeneration und sind nach Wiederverwendung ungültig.
- `renew` verlängert nur das eigene, noch gültige Lease bis zu einer festen
  Höchstdauer. `release` und Cleanup sind idempotent.
- Mutation beginnt erst nach erfolgreichem Unmount. Remount erfolgt nur nach
  erfolgreicher Verifikation; sonst bleibt die Ressource `ONLINE_RO`.

Stop-Bedingung: Wenn offene VFS-Handles nicht zuverlässig inventarisiert oder
ein Mount nicht atomar für neue Opens gesperrt werden kann, Paket blockieren.

#### S0.3c-6f1b — Markiertes FAT12-Layout und Journalformat

Ziel: Nur ein neu formatiertes, explizit als REIST-FAT12 markiertes Medium
besitzt ein festes Undo-Journal. Marker, Journalbereiche und Ersatzsektoren
liegen außerhalb aller FAT12-Datencluster und werden im BPB berücksichtigt.

Definition of Done:

- Der Layoutcode berechnet alle Bereiche mit 64-Bit-Zwischenwerten und lehnt
  Überlauf, Überschneidung oder zu kleine Medien ab.
- Primär- und Spiegelheader enthalten Magic, Version, Strukturgröße,
  Medien-ID, Sequenz, Zustand, Eintragszahl und CRC32.
- Journalkapazität und maximale Zielsektoren sind Compile-Time-Konstanten.
- Ein einzelner beschädigter Header kann aus der gültigen Kopie rekonstruiert
  werden. Zwei ungültige oder semantisch widersprüchliche Header führen zu
  read-only Mount beziehungsweise Mountablehnung.
- Fremde FAT12-Bootsektoren werden nicht verändert und verwenden weiterhin den
  nicht-journalisierten, strikt read-only Kompatibilitätspfad.

#### S0.3c-6f1c — Undo-Transaktion und Boot-Recovery

Ziel: Vor jedem FAT-, Verzeichnis- oder anderen Metadatenwrite wird der alte
Sektor persistent im Journal gesichert. Recovery läuft vor dem ersten Lesen
veränderlicher Metadaten.

Persistenzreihenfolge:

1. Alten Zielsektor lesen und CRC berechnen.
2. Undo-Daten in einen freien festen Slot schreiben und zurücklesen.
3. Redundanten Header als `ACTIVE` mit monotoner Sequenz schreiben und prüfen.
4. Zielsektor schreiben und zurücklesen.
5. Nach vollständiger Transaktion beide Header als `CLEAN` schreiben und
   prüfen.

Recovery spielt bei `ACTIVE` ausschließlich validierte Undo-Sektoren zurück,
prüft jeden Readback und markiert erst danach `CLEAN`. Kapazitätserschöpfung,
ungültige Zielbereiche, doppelte Headerkorruption oder unklare Writes führen
zu `ONLINE_RO`; es gibt kein blindes Retry.

#### S0.3c-6f2 — Defektsektoren und Remap

Ziel: Datencluster werden bei bestätigtem Defekt mit `0xFF7` dauerhaft von
neuer Belegung ausgeschlossen. Metadaten-/reservierte Sektoren dürfen nur über
eine redundante, CRC- und sequenzgeschützte Tabelle auf vorher reservierte
Ersatzsektoren abgebildet werden.

Ein Defekt gilt erst nach einem begrenzten Wiederholungstest als bestätigt.
Bestehende Nutzdaten werden nur aus einer validierten Replik rekonstruiert;
ohne solche Kopie wird Datenverlust gemeldet. Die Remap-Tabelle hat feste
Kapazität. Erschöpfung führt zu `ONLINE_RO`, niemals zu Überschreiben eines
anderen Ersatzsektors.

#### S0.3c-6f3/6f4 — Replikation und geordnete Writes

Ziel: Nur explizit markierte kritische 8.3-Dateien erhalten zwei unabhängige
Datenkopien mit Sequenz, Länge und CRC. Beim Lesen gewinnt nur eine Kopie, die
CRC, Sequenz und FAT12-Invarianten erfüllt. Bei gleicher Sequenz und
unterschiedlichem Inhalt wird keine Kopie gewählt.

Geordnete Writes verwenden die Reihenfolge Datencluster, FAT-Spiegel,
Verzeichniseintrag und Journal-Clean. Jede Stufe besitzt Readback. Abbruch
liefert entweder den alten konsistenten Zustand oder `ONLINE_RO`.

#### S0.3c-6f5 — Fault-Injection und Laufzeitabnahme

Für jede der 8 Journal-, 3 Remap- und 18 Replica-Persistenzbarrieren existiert
eine stabile, begrenzte Kennung. Die Host-Matrix injiziert an jeder Barriere
einen vollständigen Schreibfehler, einen per Readback erkannten Teilwrite und
Medienentfernung. Zusätzlich prüft sie eine und zwei beschädigte redundante
Kopien, fehlerhafte Undo-/Daten-/FAT-/Root-Sektoren, ausschließlich vollständige
alte oder neue Zustände, Fail-Closed-Verhalten, begrenzte Schreibarbeit und
unabhängigen Fortschritt. Das Referenzabbild wird vor und nach der Matrix per
SHA-256 verglichen; derselbe Schutz umfasst den QEMU-FDD-Hotplug-Lauf. Die
Zielsektorprüfung erzwingt fehlgeschlagene Readbacks getrennt für Daten-, FAT-
und Root-Klassen, Replica-Recovery prüft sowohl den Ausfall einer Datenkopie als
auch beider Datenkopien. QEMU-FDD-Reconnect ist erfolgreich; VMware-Reconnect
bleibt die nachgelagerte reale Laufzeitabnahme. Ohne diese Evidenz wird keine
reale Power-Loss-Garantie behauptet.

#### S0.3c-6f6 — Wartungsprogramme

Die Programme enthalten keine Dateisystem- oder Controllerimplementierung;
sie validieren Eingaben und senden versionierte Requests an den Storage-Dienst.

- `CHKDSK.PRG` ist standardmäßig read-only und begrenzt Knoten, Pfadlänge und
  gelesene Bytes. `--fat12 <resource>` delegiert BPB- und FAT-Spiegelanalyse an
  den Storage-Dienst. `--repair --confirm` repariert nur dann, wenn genau eine
  der beiden Kopien strukturell gültig ist. Der Dienst erwirbt dazu ein an die
  aktuell qualifizierte Medienidentität gebundenes Maintenance-Lease, prüft
  nach dem Unmount erneut und protokolliert alle neun Zielsektoren im
  vorhandenen Undo-Journal. Sind beide Kopien gültig, aber verschieden, beide
  ungültig oder Journal/Identität uneindeutig, findet keine Mutation statt.
  Der gleiche Check traversiert Root und höchstens 256 Unterverzeichnisse,
  ordnet alle erreichbaren Cluster mit einer festen Owner-Map zu und meldet
  ungültige Links, Loops, Crosslinks, kurze/überlange Ketten, Orphans sowie
  Kapazitätserschöpfung. `--repair-chains --confirm` kürzt nur normal
  EOC-terminierte, eindeutig besessene reguläre Dateiketten auf die durch die
  Dateigröße bestimmte Länge. Beide FATs werden sektorweise im Undo-Journal
  gesichert und erst nach einem vollständig sauberen Rescan als `CLEAN`
  bestätigt. `--repair-short --confirm` reduziert ausschließlich bei der
  exakten Gesamtdiagnose einer kurzen Kette die Directory-Dateigröße normal
  EOC-terminierter, eindeutig besessener regulärer Dateien auf die lesbare
  Kettenkapazität. Alle betroffenen Directory-Sektoren werden vor Mutation
  journalisiert; Startcluster null, gemischte Schäden und jede uneindeutige
  Kette bleiben unverändert. `--reclaim-orphans --confirm` gibt bei einer
  reinen Orphan-Diagnose ausschließlich allokierte Cluster ohne Owner frei,
  lässt Bad- und erreichbare Cluster unangetastet und verwirft unerreichbare
  Inhalte ausdrücklich, statt Eigentum zu raten. `--repair-loops --confirm`
  beendet bei einer reinen Loop-Diagnose jede ausreichend lange reguläre
  Dateikette am deklarierten Sollende und gibt nur ihren unmarkierten Suffix
  frei. `--repair-dir-loops --confirm` scannt jeden eindeutigen Cluster eines
  loopenden Unterverzeichnisses einmal und ersetzt bei reiner Diagnose nur den
  letzten Rücksprung durch EOC. `--repair-short-loops --confirm` kombiniert bei
  der exakten Loop-/Short-Diagnose EOC am letzten eindeutigen Cluster mit der
  journalisierten Größenbegrenzung auf dessen lesbare Kapazität.
  `--repair-crosslinks --confirm` trennt bei der exakten Crosslink-/Excess-
  Diagnose nur überlange Tail-Verweise, wenn höchstens eine Sollkette jeden
  mehrfach referenzierten Cluster benötigt.
  `--repair-required-crosslinks --confirm` akzeptiert dagegen ausschließlich
  eine reine Pflicht-Crosslink-Diagnose regulärer Dateien. Die erste Sollkette
  bleibt kanonisch, spätere kollidierende Dateien werden vollständig und mit
  verifiziertem Readback in höchstens 48 freie Cluster kopiert; Ziel-Daten,
  beide FATs und Directory-Sektoren liegen vor dem ersten Write gemeinsam im
  64-Einträge-Undo-Journal.
  `--repair-directory-crosslinks --confirm` trennt bei reiner Diagnose nur
  Same-Parent-Aliase strikt leerer, einclusteriger Unterverzeichnisse. Die
  verifizierte Kopie ändert ausschließlich ihren `.`-Self; beide FATs und der
  gebundene Parent-Startcluster werden danach journalisiert publiziert.
  `--repair-directory-topology --confirm` reduziert zusätzlich vollständig
  attribuierte nichtleere, mehrclusterige, Same-Parent- und
  parentübergreifende Aliasgruppen auf den über `..` eindeutig bestimmten
  kanonischen Parent. Spätere Short-Einträge und streng gebundene VFAT-LFN-
  Slots werden gemeinsam journalisiert gelöscht; Verzeichniskette, FAT und
  Inhalte bleiben unverändert. `--salvage-orphans --confirm` veröffentlicht
  vollständig gültige, eindeutige Orphan-Ketten unter `FOUND.000` als
  `FILEnnnn.CHK`; `nnnn` entspricht dem ursprünglichen Startcluster, während
  die Dateigröße bewusst die gesamte Allokation umfasst. Erzeugung oder
  Erweiterung des Recovery-Directory, beide FATs und alle Directory-Sektoren
  teilen eine Undo-Transaktion. Mehrdeutige Verzeichnis-, Journal- und
  Remap-Reparatur bleibt offen.
  `--repair-dir-size --confirm` traversiert
  ansonsten gültige Unterverzeichnisse trotz unzulässiger Größe und setzt bei
  reiner Diagnose ausschließlich dieses Feld auf null.
- `FORMAT.PRG` akzeptiert ausschließlich erkannte FDD-Ressourcen. Ohne
  `--reist-fat12` erzeugt es kein REIST-Journal. Oberflächentest,
  Layoutberechnung, Initialisierung und vollständiger Metadaten-Readback sind
  begrenzt; ein Fehler lässt das Medium ungemountet und read-only.
- `FDISK.PRG` zeigt FDD-Superfloppies nur an und partitioniert sie nicht. Eine
  Mutation ist ausschließlich auf geeigneten partitionierten Medien erlaubt,
  benötigt Lease plus explizite Bestätigung und validiert MBR-Bereiche vor dem
  ersten Write.

Jedes Tool muss in `scripts/build_system_programs.py` registriert sein und mit
der normalen Ring-3-Toolchain gebaut werden. Erfolgsnachrichten dürfen erst
nach Kernelantwort, verifiziertem Readback und kontrolliertem Remount erscheinen.
`SATAWR.PRG` ergänzt diese Werkzeuge als bewusst destruktiver, aber begrenzter
Hardware-Abnahmetest für das Systemvolume; es besitzt weder direkten
Controller- noch DMA-Zugriff.

#### Historischer Implementierungsstand vom 16. August 2026

Dieser Snapshot bleibt zur Nachvollziehbarkeit erhalten und ist durch die
später abgeschlossenen Pakete `S0.3c-6f5` und die Partitionierungs-/
Formatierungsarbeit teilweise überholt. Der aktuelle Betriebsstand steht in
[`PROJECT_STATUS.md`](PROJECT_STATUS.md) und
[`../filesystems/FAT12_IMPROVEMENTS.md`](../filesystems/FAT12_IMPROVEMENTS.md).

Die Pakete S0.3c-6f1 bis S0.3c-6f4 sind umgesetzt und durch ihre eingefrorenen
Host-, Paket- und FDD-Hotplug-Gates abgenommen. Vermittelte Blockoperationen
und Maintenance-Leases sind über versionierte Kernel- und Ring-3-Schnittstellen
angebunden. Die Werkzeuge `CHKDSK.PRG`, `FDISK.PRG` und `FORMAT.PRG` werden mit
der normalen Userspace-Toolchain gebaut und in die Systemimages aufgenommen.

Für S0.3c-6f1 liegt das verifizierte Journalformat Version 2 vor:
Undo-Daten, Entry-Metadaten sowie beide Header werden nach jedem Write
zurückgelesen; Entry-Metadaten tragen eine eigene CRC32. Gleich alte, aber
inhaltlich widersprüchliche Header, nicht monotone Sequenzen, reservierte
Journalziele und ein unsicherer CLEAN-Abschluss werden fail-closed abgelehnt.
Targeted-, QEMU-Paket- und FDD-Hotplug-Gate sind erfolgreich. S0.3c-6f2
integriert Defektbestätigung, `0xFF7` und redundante Remaps. S0.3c-6f3
publiziert verifizierte kritische Replikate, und S0.3c-6f4 erzwingt die
Reihenfolge Daten, beide FATs, Verzeichniseintrag, Replikat und Journal-Clean.
Daraus folgt noch kein vollständiger FAT12-Resilienznachweis; die
Persistenz-Fehlermatrix S0.3c-6f5 ist implementiert, während reale
Reconnect-/Power-Loss-Evidenz weiterhin aussteht.

Der damalige Funktionsumfang der Werkzeuge war bewusst enger als das Ziel:

- `CHKDSK.PRG` führt eine begrenzte read-only Bestandsaufnahme aus; der
  journalisierte Reparaturmodus fehlt noch.
- `FDISK.PRG` zeigt erkannte Medien read-only an; validierte MBR-Mutationen sind
  noch nicht implementiert.
- `FORMAT.PRG` validiert FDD-Ressource, Schalter und Bestätigung und sendet einen
  begrenzten Storage-Request. Der Storage-Dienst erzeugt nun das feste
  1,44-MB-FAT12-Layout mit fest berechnetem reserviertem Safety-Bereich, zwei
  FAT-Kopien, Root- und Datenbereich, REIST-Journal-/Remap-/Replikatmetadaten
  sowie vollständigem Metadaten-Readback. Der
  abschließende Lease-/Remount-Nachweis und Fehlerrollback bleiben offen.

Die beim Boot von A: beobachtete Panic `Unable to start REIST Ring-3 probe`
wurde auf ein nicht initialisiertes `maintenance_blocked` im VFS-Mountobjekt
zurückgeführt und durch deterministische Initialisierung behoben. Ein
Regressionstest sichert diese Initialisierung ab. A:-Boot und Reconnect wurden
unter VMware beobachtet; die vollständige Fault-Injection-Matrix in Host,
QEMU und VMware bleibt offen. Daraus wird noch kein Resilienz- oder
Fail-operational-Claim abgeleitet.

### Abschluss-Arbeitsliste FAT12

Die folgenden Pakete werden strikt in dieser Reihenfolge bearbeitet. Ein Lauf
bearbeitet genau einen Punkt; ein fehlgeschlagener Punkt blockiert alle späteren
Punkte.

1. **Build-Basis reparieren:** `-Werror=frame-larger-than` korrekt mit einem
   Grenzwert konfigurieren und danach Kernel- und Userspace-Build ausführen.
   Unter Windows ist dafür ausschließlich der in
   [`BUILD_MODES.md`](BUILD_MODES.md) beschriebene Einstieg
   `.\scripts\build-windows.ps1` zu verwenden. Ein nacktes `make kernel` ist
   dort nur mit ausdrücklich gesetzter ELF-i386-Cross-Toolchain zulässig und
   darf nicht versehentlich den MinGW-PE-Linker verwenden.
2. **Lease abschließen:** Medien-Fingerprint, Mountzustand, offene Handles,
   Timeout, Prozessende und idempotentes Release vollständig verbinden.
3. **REIST-Layout festschreiben:** Journal-, Remap- und Replikatbereiche im
   BPB reservieren, Überschneidungen ablehnen und Fremdmedien unverändert lassen.
4. **Undo-Journal fertigstellen:** jeden Header-, Daten- und Zielwrite per
   Readback prüfen; Erschöpfung und doppelte Korruption führen zu `ONLINE_RO`.
5. **Remap integrieren:** Defekt durch begrenzte Wiederholungsreads bestätigen,
   `0xFF7` setzen und Ersatzsektoren für Daten-, FAT- und Rootbereiche nutzen.
6. **Kritische Replikate integrieren:** nur die Allowlist-Dateien replizieren;
   gleiche Sequenz mit verschiedenem CRC-/Inhalt fail-closed ablehnen.
7. **Geordnete Transaktionen integrieren:** Dateiinhalt, FAT-Kopien,
   Directory-Eintrag und Replikatstatus journalisieren und verifizieren.
8. **Maintenance-Requests fertigstellen:** versionierte Diagnose-, Format-,
   Reparatur- und FDISK-Requests ausschließlich über Storage-Service und Lease.
9. **Tools fertigstellen:** `CHKDSK.PRG` read-only plus bestätigte Reparatur,
   `FORMAT.PRG` für markierte FAT12-Images und `FDISK.PRG` ohne FDD-Partitionierung.
10. **Fault-Injection ausführen:** Teilwrites, Headerkorruption, Remap-/Replica-
    Konflikte, defekte FAT-/Rootsektoren, Auswurf und Stromverlust in Host,
    QEMU und VMware nachweisen.
11. **Status synchronisieren:** erst nach allen Gates Roadmap und Projektstatus
    aktualisieren; keine unbelegten Zertifizierungs- oder Fail-operational-
    Aussagen ergänzen.

**S0.3c-7a ist umgesetzt:** Ein statischer, `critical_object`-geschützter
Zwei-Knoten-Protokollkern verwaltet aktive und Standby-ID, monotone Lease,
64-Bit-Epoche, Fence-Epoche und Transitionssequenz. Nur der aktive Knoten darf
seine aktuelle, noch nicht abgelaufene Epoche verlängern. Eine Übernahme ist
erst nach Leaseablauf und expliziter Bestätigung möglich, dass genau diese
Epoche extern gefenct wurde. Der Rollenwechsel erhöht die Epoche; alter Active,
alte Fence-Bestätigungen und alte Kandidaten verlieren damit dauerhaft ihre
Autorität. Alle Kapazitäten sind statisch, Überläufe enden fail-closed, und
Host-Fault-Tests decken Split-Brain, verfrühte Übernahme, stale Epoch sowie
einfach und doppelt beschädigte Kontrollkopien ab. Das ist nur der
Protokollbaustein: Ohne S0.3c-7b existieren weder unabhängiger Transport noch
rücklesbares Hardware-Fence; ein fail-operationaler Claim bleibt unzulässig.

**S0.3c-7b1 ist umgesetzt:** Der Protokollkern kann erst initialisiert werden,
nachdem genau ein statisches Fence-Backend mit getrennten Request- und
Readback-Funktionen gebunden wurde. Leaseablauf allein erzeugt keine
Bestätigung. `handover_request_fence()` fordert das externe Fence für Active-ID
und Epoche an; `handover_confirm_fenced()` akzeptiert den Zustand nur, wenn das
Backend exakt dieselbe ID/Epoche rückmeldet. Die potenziell langsamen
Backendoperationen laufen nicht mit deaktivierten IRQs. Danach werden Epoche,
Active-ID, Lease und Transitionssequenz unter Lock erneut geprüft, sodass ein
zwischenzeitlicher Rollen- oder Leasewechsel die Bestätigung verwirft. Das
Hostbackend beweist Negativpfade und idempotentes Readback. S0.3c-7b2b bleibt
offen, weil noch kein physisch unabhängiger Transport oder Interlock existiert.

**S0.3c-7b2a ist umgesetzt:** Ein isoliertes QEMU-Testprofil bindet den
Handover-Kern an COM2 statt an den Konsolenkanal. Der Kern sendet ein festes,
heapfreies 24-Byte-Request mit Version, Active-ID, 64-Bit-Epoche und CRC32. Ein
getrennter Hostprozess validiert den vollständigen Frame und antwortet nur mit
einem CRC-geschützten Ack für exakt dasselbe Tupel. Alle UART-Wartepfade haben
eine monotone 1-s-Deadline. Der reale Gastlauf beweist geordnet
`REQUEST_SENT -> FENCE_CONFIRMED -> TAKEOVER_OK -> BOOT_OK -> TEST_OK`.
Das trennt Transport und Supervisorprozess, aber nicht Strom, CPU oder
Zeitquelle; daher bleibt S0.3c-7b2b auf Zielhardware offen und ein
Fail-operational-Claim weiterhin unzulässig.

**S0.3c-7c1 ist umgesetzt:** Zwei separat gebaute Images laufen gleichzeitig
in zwei QEMU-Prozessen. Der Active sendet seinen versionierten Epoch-Snapshot;
der Host leitet ihn erst nach einem CRC-geschützten `STANDBY_READY` an den
Standby weiter. Nach Leaseablauf fordert ausschließlich der Standby das Fence
an. Der Host beendet den Active-Prozess, prüft dessen Ende und sendet erst dann
den Ack. Danach übernimmt der Standby, startet seine überwachten Dienste und
besteht den vollständigen Ring-3-Gasttest. Drei Läufe des finalen Standes bestätigten
die Reihenfolge. Zum Stand von 7c1 waren kontinuierliche Nutzdaten-Replikation
und Reintegration noch nicht nachgewiesen; 7c2a/7c2b schließen diese beiden
Lücken. Physisch unabhängige Hardware bleibt weiterhin offen.

**S0.3c-7c2a ist umgesetzt:** Ein fester Referenz-Dienstzustand liegt als
redundantes `critical_object` mit ECC, CRC und semantischem Validator vor. Der
Active veröffentlicht vor dem Failover drei CRC-geschützte 52-Byte-Frames mit
identischer Quelle/Epoche und lückenlos steigender 64-Bit-Sequenz. Der Standby
akzeptiert ausschließlich den unmittelbaren Nachfolger; Replay, Lücke,
Quellen-/Epochenwechsel und doppelte Kopienkorruption enden fail-closed. Nach
dem extern bestätigten Fence übernimmt er mit erhöhter Epoche und publiziert
den neuen Zustand. Ein drittes, separat gestartetes QEMU-Image erhält diesen
Zustand, besteht die Integritätsprüfung, darf aber weder die alte Lease
verlängern noch ohne neues Fence übernehmen. Drei vollständige Läufe bewiesen
parallel den Weiterbetrieb des übernommenen Kanals bis `TEST_OK`. Das ist ein
prozessgetrennter Referenznachweis. Das folgende Paket 7c2b bindet diesen
Vertrag an den Storage-Produktionszustand; physisch unabhängige Zielhardware
bleibt unter 7b2b offen.

**S0.3c-7c2b ist umgesetzt:** Der replizierte Produktionszustand ist der
CRC32-Fingerprint des tatsächlich erkannten ATA-Bootvolumes einschließlich
gültiger MBR-Signatur. Standby und reparierter Kanal aktivieren vor dem Mount
ein separates Storage-Handover-Gate; jede überwachte ATA-/FDD-Schreiboperation
prüft dieses Gate. Der Standby übernimmt drei lückenlos sequenzierte
Fingerprint-Checkpoints und liest seinen eigenen Datenträger zur lokalen
Gegenprüfung. Erst nach nachgewiesen beendetem Active, extern bestätigtem
Fence, Epoch-Promotion, erfolgreicher Publikation des promovierten Zustands und
erneutem Volume-Selbsttest wird das Gate freigegeben. Der anschließende
Ring-3-Test führt reale VFS-Mutationen aus und erreicht `TEST_OK`. Der reparierte
dritte Kanal validiert denselben Zustand, bleibt jedoch ausdrücklich gehalten
und ohne Takeover-Autorität. Falscher Fingerprint, I/O-Fehler, Replay/Lücke oder
vorzeitige Freigabe bleiben fail-closed. Drei vollständige Läufe des finalen
Stands waren erfolgreich. Damit ist das QEMU-Referenzpaket 7c abgeschlossen;
das elektrisch unabhängige Interlock auf Zielhardware (7b2b) und
Common-Cause-/Hardware-Failover-Gates (7d) bleiben offen.

Ein einzelner monolithischer Kernel kann nach unbekannter Eigenkorruption nicht
glaubwürdig störungsfrei weiterlaufen. Unterbrechungsfreie Essential Functions
bei Kernel-Panic benötigt S0.3: eine unabhängige Supervisor-/Standby-Domäne,
die Ausgänge einzäunt und innerhalb der FTTI übernimmt. S0.3a allein erfüllt
diese Forderung ausdrücklich nicht. Bereits vorgezogene Funktionsinkremente
gelten daher nicht als Abnahme des S0-Gates.

Systematische Allocation-Failure-Injection, ein IRQ-tauglicher Allocator,
weitere Reaper-Stresstests und Highmem/`kmap` bleiben zusätzliche
Speicherhärtung.
