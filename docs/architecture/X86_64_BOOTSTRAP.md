# REIST x86_64 bootstrap contract

Stand: 28. August 2026

## Zweck und Grenze

R8.1a fuehrt ein getrenntes Architektur-Prototypartefakt ein. Es beginnt im
von Multiboot definierten 32-Bit-Protected-Mode, prueft die benoetigten
CPU-Faehigkeiten, aktiviert mit statischen Tabellen IA-32e Paging und springt
in einen 64-Bit-Codesegment. Erst dort darf der serielle Erfolgsmarker
`REIST_X86_64_LONG_MODE_BOOT_OK` erscheinen.

R8.1b ergaenzt danach eine begrenzte Exceptiongrundlage. Sie veroeffentlicht
genau die Vektoren 0 bis 31, normalisiert CPU- und synthetische Fehlercodes,
sichert alle allgemeinen Register und laedt eine 64-Bit-TSS. Nur Double Fault
verwendet deren festen IST1. Ein einzelner `UD2`-Probe darf ausschliesslich bei
Vektor 6, Fehlercode null und exakt passender RIP zur festen Fortsetzung
zurueckkehren.

R8.1c fuegt einen kanonischen Higher-Half-Alias ab
`0xFFFFFFFF80000000` hinzu. Die physische Adresse bleibt dabei als Offset
erhalten. Nur die gelinkten Text-, RoData-, Data- und BSS-Seiten werden mit
4-KiB-PTEs abgebildet. Nach dem Wechsel von RIP und RSP auf diesen Alias wird
die niedrige Uebergangsabbildung aus PML4[0] entfernt und CR3 neu geladen.
Ein Sprung auf ein festes Byte in der NX-Datenseite muss danach exakt einen
Supervisor-Instruction-Fetch-Page-Fault erzeugen.

R8.1d erfasst den Multiboot-v1-Handoff noch im ungepageten 32-Bit-Einstieg.
Die variable Speicherkarte wird zweimal innerhalb fester Grenzen ausgewertet:
zuerst werden vollstaendige nutzbare 4-KiB-Frames aufgenommen, danach
ueberstimmen alle nicht nutzbaren Eintraege jede Ueberlappung. Bootstrap,
ausgewertete Multiboot-Strukturen, Modultabelle und Modulnutzdaten werden vor
der Freigabe reserviert. Zwei feste Bitmaps trennen verwaltbare Frames von
laufenden Allokationen.

R8.1e baut ein eigenstaendiges freestanding x86_64-`ET_EXEC` mit NASM und dem
ELF64-Linker und bettet genau dieses Artefakt in den Bootstrap ein. Der
Gastloader validiert die ELF64-/System-V-Identitaet und hoechstens zwei
`PT_LOAD`-Segmente vollstaendig, bevor er Frames allokiert. Er staged Datei-
und BSS-Bytes in ein festes Acht-Seiten-Fenster, prueft Inhalt und W^X-
Metadaten und gibt danach alle Frames frei. Das Probeprogramm wird nicht
ausgefuehrt.

Das Artefakt ist kein vollstaendiger REIST-Kernel. Seine physische Verwaltung
endet bei 128 MiB und verwendet weder dynamische Seitentabellen noch NUMA oder
Highmem. Die spaeteren R8.1-/R8.2-Nachweise besitzen einen begrenzten
Prozess-, Syscall- und Ring-3-Shellpfad, aber keine produktive
Hardwareinterruptbehandlung, Treiber, VFS-Integration oder signiertes natives
64-Bit-Medienlayout. Die isolierten Nachweise begruenden deshalb keine
vollstaendige ELF64-Prozess-, Hardware- oder Fail-operational-Kompatibilitaet.

## Referenzstandard

Der Eintritt folgt Intel 64/IA-32 SDM: CPUID weist die erweiterte Funktion
`0x80000001`, NX-Bit EDX[20] und Long-Mode-Bit EDX[29] nach. CR4.PAE,
EFER.LME zusammen mit EFER.NXE sowie CR0.PG zusammen mit CR0.WP werden in
dieser Reihenfolge aktiviert. Nach dem Far-Transfer prueft der 64-Bit-Code die
Kontrollbits erneut. Die vierstufige Seitentabellenstruktur, kanonischen
Adressen und Page-Fault-Fehlerbits folgen Intel 64/IA-32 SDM. Der Ladevertrag
ist Multiboot Version 1; er bleibt auf das separate Bootstrap-Artefakt begrenzt.

## Feste Ressourcen

- eine statische, page-aligned PML4, PDPT, niedrige und hohe Page Directory
  sowie eine hohe Page Table;
- genau eine temporaere 2-MiB-Identity-Map ab physischer Adresse null;
- feste 4-KiB-Higher-Half-Abbildungen nur fuer die gelinkten Abschnitte;
- Text read-only/executable, RoData read-only/NX und Data/BSS read-write/NX;
- maximal 4.096 Byte Multiboot-Speicherkarte mit 128 Eintraegen und 32 Modulen;
- zwei feste 4.096-Byte-Bitmaps fuer 32.768 Frames unter 128 MiB;
- eine feste RW/NX-Direct-Map mit 64 Page Tables und ausschliesslich
  verwaltbaren RAM-Frames;
- ein separat gelinktes ELF64-`ET_EXEC` mit maximal 64 KiB, vier Program
  Headern, zwei `PT_LOAD`-Segmenten und acht staged Userseiten;
- ein statischer 16-KiB-Bootstack innerhalb dieser Map;
- ein getrennter 16-KiB-C-Entry-Stack und ein exakt 128 Byte grosser
  versionierter Assembly-Handoff;
- genau 32 statische 16-Byte-IDT-Gates und eine gepackte 104-Byte-TSS;
- ein statischer 16-KiB-IST ausschliesslich fuer Double Fault;
- maximal 65.536 Statusabfragen je gesendetem COM1-Byte;
- genau eine vCPU, 128 MiB RAM und zehn Sekunden im automatisierten QEMU-Gate.

Nicht unterstuetzte CPU-Faehigkeiten und inkonsistente Kontrollregister enden
mit einem eigenen seriellen Fehlermarker und anschliessendem `hlt`. Der
produktive i386-Build verwendet keine Datei aus `arch/x86_64` und bleibt die
Standardauswahl aller bisherigen Build- und Installationswege.

## Abnahme R8.1a

Der Quellvertrag bestand sechs Tests. Der getrennte Windows-Build erzeugte ein
14.360 Byte grosses Bootstrap-ELF. Der begrenzte QEMU-x86_64-Lauf mit einer
vCPU und 32 MiB RAM veroeffentlichte nach den erneuten 64-Bit-Zustandspruefungen
`REIST_X86_64_LONG_MODE_BOOT_OK` in 1,8 Sekunden. Dieser Nachweis gilt nur fuer
den Emulator und den beschriebenen Architekturuebergang.

## Abnahme R8.1b

Der erweiterte Quellvertrag bestand neun Tests. Der warnungsfreie Windows-Build
linkte getrennte Entry- und Exceptionobjekte zu einem 16.844 Byte grossen ELF.
Der weiterhin auf eine vCPU, 32 MiB und zehn Sekunden begrenzte QEMU-Lauf
meldete in dieser Reihenfolge `REIST_X86_64_LONG_MODE_BOOT_OK`,
`REIST_X86_64_EXCEPTION_IDT_READY`, `REIST_X86_64_EXCEPTION_UD_OK` und
`REIST_X86_64_EXCEPTION_RECOVERY_OK`. Erweiterte Seitentabellen, Hardware-IRQs,
Prozesse, Syscalls, ELF64-Userspace und physische Hardware bleiben offen.

## Abnahme R8.1c

Der Quellvertrag prueft den festen Tabellenumfang, seitengetrennte
Linkergrenzen, W^X/NX/WP, den Higher-Half-Stack, den Low-Map-Widerruf und die
exakte NX-Fehlerbehandlung. Die Laufzeit-Selbstpruefung vergleicht physische
Adresse und Schutzbits exakt und ignoriert dabei ausschliesslich die von der
CPU gepflegten Accessed-/Dirty-Bits. Der QEMU-Lauf muss geordnet zusaetzlich
`REIST_X86_64_HIGHER_HALF_PAGING_OK` und `REIST_X86_64_PAGING_NX_OK`
veroeffentlichen. Erst danach darf der bestehende
`REIST_X86_64_EXCEPTION_RECOVERY_OK`-Abschlussmarker erscheinen. Der Nachweis
bleibt auf eine vCPU, 32 MiB und zehn Sekunden begrenzt.

Der finale Quellvertrag bestand elf Tests. Der warnungsfreie Windows-Build
erzeugte ein 26.180 Byte grosses ELF. Nach einer gezielten Korrektur der
PTE-Selbstpruefung, die nun ausschliesslich CPU-eigene Accessed-/Dirty-Bits
maskiert, veroeffentlichte der QEMU-Lauf alle sechs geforderten Marker in
exakter Reihenfolge innerhalb einer Sekunde. Damit sind Higher-Half-Wechsel,
Low-Map-Widerruf, bestehender UD2-Resume und die exakte NX-Schutzwirkung fuer
dieses isolierte Artefakt nachgewiesen.

## Abnahme R8.1d

Der Quellvertrag fordert exakte Multiboot-Magic-, Pointer-, Laengen-,
Entrygroessen-, Kapazitaets- und 64-Bit-Ueberlaufpruefungen vor jeder
Publikation. Reservierte Bereiche muessen jede nutzbare Ueberlappung
ueberstimmen. Der QEMU-Lauf muss nach dem bestehenden NX-Probe drei eindeutige
Frames allozieren, deren RW/NX-Direct-Map beschreiben, einen Frame freigeben
und exakt wiederverwenden, danach alle Frames freigeben sowie unaligned und
doppeltes Free ablehnen. Erst nach wiederhergestelltem Freizaehler darf
`REIST_X86_64_PHYSICAL_MEMORY_OK` vor dem Abschlussmarker erscheinen.

Der finale Quellvertrag bestand vierzehn Tests. Der warnungsfreie Windows-
Build linkte Entry-, Exception- und Physikalspeicherobjekt zu einem 29.788 Byte
grossen ELF. Der Ein-vCPU-/32-MiB-QEMU-Lauf verarbeitete die reale
Multiboot-v1-Speicherkarte und bestand Allokation, Direct-Map-Schreibprobe,
Free/Reuse, unaligned Free, Double Free und Freizaehlerwiederherstellung.
`REIST_X86_64_PHYSICAL_MEMORY_OK` erschien innerhalb einer Sekunde in der
geforderten Reihenfolge. Der Nachweis gilt nicht fuer Speicher oberhalb
64 MiB oder physische Hardware.

## Abnahme R8.1e

Der Quellvertrag verlangt einen unabhaengigen ELF64-Toolchainlauf und eine
vollstaendige Vorabvalidierung von Identitaet, Maschine, Typ, Headergroessen,
Programmtabelle, allen 64-Bit-Bereichen, Alignment, Entry-Point und W^X. Erst
danach duerfen hoechstens acht R8.1d-Frames belegt werden. Der Gast muss alle
Dateibytes sowie die genullten Speicherenden nachpruefen und vor
`REIST_X86_64_ELF64_LOAD_OK` jeden Frame sowie den urspruenglichen
Freizaehler wiederherstellen. Ring-3-Transfer, User-Seitentabellen, Syscalls
und Payload-Ausfuehrung sind ausdruecklich R8.1f vorbehalten.

Alle 17 Quellvertragstests bestanden. Nach einer gezielten Korrektur eines
mehrdeutigen NASM-Labels erzeugte der warnungsfreie Windows-Build ein
45.156-Byte-Bootstrap und ein unabhaengig gelinktes 9.008-Byte-ELF64-`ET_EXEC`.
Der auf eine vCPU, 32 MiB und zehn Sekunden begrenzte QEMU-Lauf meldete
`REIST_X86_64_ELF64_LOAD_OK` innerhalb einer Sekunde geordnet zwischen
`REIST_X86_64_PHYSICAL_MEMORY_OK` und
`REIST_X86_64_EXCEPTION_RECOVERY_OK`. Der Nachweis gilt nicht fuer
Payload-Ausfuehrung, Ring 3, physische Hardware oder HPASA.

## Abnahme R8.1f

Der erste User-Ausfuehrungspfad bleibt auf dasselbe eingebettete ELF64-Abbild,
hoechstens acht Abbildseiten und eine feste separate NX-Stackseite begrenzt.
Eine private Vier-Ebenen-Hierarchie uebernimmt nur die bestehenden Supervisor-
Eintraege fuer Higher-Half-Kernel und Direct Map; User-PTEs werden
ausschliesslich aus den validierten ELF-Rechten abgeleitet. DPL3-Code und
-Daten, TSS-RSP0, Entry, Stack und feste RFLAGS muessen vor `IRETQ` geprueft
sein.

Alle 21 Quellvertragstests bestanden. Der warnungsfreie Windows-Build erzeugte
ein 50.980-Byte-Bootstrap und ein unabhaengig gelinktes 9.048-Byte-ELF64-
`ET_EXEC`. Der auf eine vCPU, 32 MiB und zehn Sekunden begrenzte QEMU-Lauf
beruehrte die User-Stackseite, nahm exakt `EXIT` 9 mit Status 100 an, enthielt
anschliessend den erwarteten CPL3-`UD2` und meldete
`REIST_X86_64_USER_EXECUTION_OK` innerhalb einer Sekunde zwischen
`REIST_X86_64_ELF64_LOAD_OK` und `REIST_X86_64_EXCEPTION_RECOVERY_OK`.
Die wiederholte Tabellenpruefung maskiert ausschliesslich CPU-eigene
Accessed-/Dirty-Bits; der Faultpfad verlangt das architektonische Resume-Flag.
Scheduler, allgemeine Syscalls, produktiver 64-Bit-Userspace und physische
Hardware bleiben offen; HPASA bleibt ein getrenntes Projekt.

Der Syscall-Nachweis folgt Intel 64 `SYSCALL`/`SWAPGS` und der System-V-AMD64-
Registerkonvention. Nur der append-only REIST-v1-Index 9 (`EXIT`) mit dem
erwarteten Status ist zulaessig. Der Entry muss vor dem Lesen des Requests auf
den festen Kernelstack wechseln; `SYSRET` ist nicht zulaessig. Ein zweiter
Eintritt provoziert `UD2`; nur Vektor 6 mit Fehlercode null, CPL3-Selektoren und
einer RIP in einer validierten ausfuehrbaren ELF-Seite darf enthalten werden.
Vor `REIST_X86_64_USER_EXECUTION_OK` muessen Original-CR3 und Kernelzustand
wiederhergestellt, die temporaeren Syscall-MSRs deaktiviert, alle User-PTEs
geloescht, alle Frames freigegeben und der urspruengliche Freizaehler erreicht
sein.

## Abnahme R8.1g

Der Prozessnachweis bleibt auf zwei feste Slots und eine endliche kooperative
Abfolge begrenzt. Jede Generation besitzt eine private Vier-Ebenen-Hierarchie,
private writable ELF-Seiten und eine private NX-Stackseite. Ausschliesslich
validierte nichtschreibbare RX-Seiten duerfen physisch geteilt werden. Die
Probe muss ihre jeweils eigene Datenseite nach mehreren CR3-Wechseln
wiedererkennen.

Der AMD64-`SYSCALL`-Pfad akzeptiert nur die vorhandenen REIST-v1-Indizes
`YIELD` 40 und `EXIT` 9. `YIELD` speichert einen festen Userkontext und der
Chooser untersucht hoechstens zwei Slots. Ein exakt validierter CPL3-`UD2`
von Task B muss nur diese Generation isolieren und reapen; Task A muss danach
weiterlaufen und mit dem erwarteten Status beenden. Vor
`REIST_X86_64_PROCESS_SCHEDULER_OK` muessen Original-CR3, TSS und Syscall-MSRs
wiederhergestellt, alle Taskdaten genullt und alle Frames freigegeben sein.
Timerpreemption, Hardwareinterrupts, SMP und produktive Prozessintegration
bleiben ausserhalb dieser Scheibe.

Alle 26 Quellvertragstests bestanden. Der warnungsfreie Windows-Build erzeugte
ein 62.612-Byte-Bootstrap und ein 9.264-Byte-ELF64-`ET_EXEC`. Der auf eine
vCPU, 32 MiB und zehn Sekunden begrenzte QEMU-Lauf fuehrte die exakte Folge
`A:YIELD`, `B:YIELD`, `A:YIELD`, `B:UD2`, `A:EXIT(101)` aus. Task B wurde vor
der Fortsetzung von Task A generation-gebunden reaptiert. Erst nach der
vierzehnteiligen Lebenszykluspruefung, dem Widerruf aller temporaeren
Architekturzustande und vollstaendiger Framefreigabe erschien
`REIST_X86_64_PROCESS_SCHEDULER_OK` zwischen `USER_EXECUTION_OK` und
`EXCEPTION_RECOVERY_OK`.

## Abnahme R8.1h

Die feste IDT wird ausschliesslich um den standardmaessigen PIC-IRQ0-Vektor 32
erweitert. Beide PIC-Masken werden vor dem Remap auf 32/40 gesichert; nur
Master-IRQ0 darf waehrend des Nachweises unmaskiert sein. PIT-Kanal 0 verwendet
den standardisierten 1.193.182-Hz-Eingang und einen festen 100-Hz-Divisor.
Exakt drei normalisierte Kernel-Frames muessen Generation, CR3, Vektor,
Fehlercode, Selektoren und RIP-Bereich bestehen und je ein Master-EOI senden.

Der Warteloop besitzt eine feste TSC-Obergrenze und verwendet kein `HLT`. Vor
`REIST_X86_64_TIMER_IRQ_OK` muessen IF deaktiviert, IRQ0 wieder maskiert, beide
gesicherten PIC-Masken restauriert und die temporaere Generation inaktiv sein.
CPL3-Praeemption, LAPIC, IOAPIC und SMP bleiben offen.

Alle 27 Quellvertragstests bestanden. Der warnungsfreie Windows-Build erzeugte
ein 68.888-Byte-Bootstrap bei unveraendertem 9.264-Byte-ELF64-Probeabbild. Der
Ein-vCPU-/32-MiB-QEMU-Lauf nahm genau drei Vektor-32-Frames an und meldete
`REIST_X86_64_TIMER_IRQ_OK` geordnet zwischen `PROCESS_SCHEDULER_OK` und
`EXCEPTION_RECOVERY_OK`. Jeder Frame bestand Generation, CR3, Fehlercode,
Kernel-CS, IF und Higher-Half-Text-RIP; drei Ereignisse erzeugten drei
Master-EOIs. Danach waren IF, IRQ0, beide PIC-Masken und der temporaere Zustand
restauriert.

## Abnahme R8.1i

Task A gibt genau einmal ueber `YIELD` 40 an die CPU-gebundene Task B ab. Erst
dann wird PIT-IRQ0 fuer eine Generation armiert. Der normalisierte CPL3-Frame
muss B, deren privaten CR3, Userselektoren, IF, Stack und ausfuehrbare RIP
exakt validieren, IRQ0 maskieren, genau einen EOI senden und nur B reapen. Task
A muss danach ihre private Datenseite bestaetigen und ueber `EXIT` 9 mit Status
102 enden. Bs TSC-Limit verhindert einen unbegrenzten Userloop.

Alle 28 Quellvertragstests bestanden. Der warnungsfreie Windows-Build erzeugte
ein 70.964-Byte-Bootstrap und ein 9.400-Byte-ELF64-Probeabbild. Der auf eine
vCPU, 32 MiB und zehn Sekunden begrenzte QEMU-Lauf nahm genau einen validierten
CPL3-Vektor-32-Frame an, setzte nur B auf `PREEMPTED`, reapte diese Generation
und setzte A bis `EXIT` 9/Status 102 fort. Nach dem zehnteiligen Lebenszyklus
und der vollstaendigen Wiederherstellung erschien
`REIST_X86_64_TIMER_PREEMPTION_OK` zwischen `TIMER_IRQ_OK` und
`EXCEPTION_RECOVERY_OK`.

## Abnahme R8.1j

Eine feste vierteilige PIT-Generation schaltet zwei private CPU-gebundene
CPL3-Tasks exakt in der Folge A-B-A-B-A um. Jeder IRQ validiert Generation,
CR3, Userframe, IF, Stack und ausfuehrbare RIP vor Zustandsaenderung und EOI
und speichert danach alle allgemeinen Register sowie den IRET-Kontext. Beide
Tasks muessen aus dem unterbrochenen Kontext fortfahren und unabhaengigen
privaten Fortschritt erzeugen. Tick vier reapt nur B und signalisiert A den
begrenzten Abschluss; A bestaetigt ihre private Datenseite und liefert `EXIT`
9/Status 103. Ein globales TSC-Limit begrenzt den gesamten Nachweis.

Alle 29 Quellvertragstests bestanden. Der warnungsfreie Windows-Build erzeugte
ein 73.820-Byte-Bootstrap und ein 9.688-Byte-ELF64-Probeabbild. Der auf eine
vCPU, 32 MiB und zehn Sekunden begrenzte QEMU-Lauf nahm genau vier validierte
CPL3-Vektor-32-Frames und vier Master-EOIs an. Beide privaten Tasks setzten
ihren vollstaendigen unterbrochenen Kontext mit eigenem Fortschritt fort. Tick
vier reapte nur B; A lieferte danach `EXIT` 9/Status 103. Erst nach der
vollstaendigen Wiederherstellung erschien `REIST_X86_64_QUANTUM_SWITCH_OK`
zwischen `TIMER_PREEMPTION_OK` und `EXCEPTION_RECOVERY_OK`.

## Abgenommene FIFO-Lebenszyklen R8.1k

Vier feste private Prozessslots besitzen je eine nichtnull Generation. Eine
Vier-Eintrag-Ringqueue bindet jeden Eintrag an Slot und Generation und
validiert `READY`, Grenzen, Membership und Aktualitaet vor jeder Mutation. Die
exakte Laufreihenfolge 0-1-2-3-0-2 umfasst je ein `YIELD` von 0 und 2, direkte
Exits 110/111/112 und einen exakt validierten CPL3-`INT3` von Task 3. Ein
doppelter und ein staler Enqueue-Versuch muessen den Queuezustand unveraendert
lassen. Erfolg verlangt eine leere Queue, vier genullte freie Slots und den
urspruenglichen Framezaehler. Die Implementierung erfuellt diese Folge im
81.524-Byte-Bootstrap mit dem 9.936-Byte-Probeabbild. Vector 3 wird nur fuer
die Dauer des Nachweises fuer CPL3 freigegeben und danach auch auf Fehlerpfaden
auf seinen Ring-0-Gatezustand zurueckgesetzt. Der kurze Ein-vCPU-/32-MiB-Lauf
meldete `REIST_X86_64_RUNQUEUE_LIFECYCLE_OK` vor dem Abschlussmarker.

## Abgenommener Deadline-Sleep R8.1l

Die vorhandene Vier-Slot-FIFO besitzt zusaetzlich eine feste sortierte
Vier-Eintrag-Deadline-Liste. `SLEEP_MS` behaelt den REIST-v1-Index 41 und
Millisekunden als Einheit; die isolierte 100-Hz-Implementierung akzeptiert
hier exakt 10, 20 oder 30 ms und rechnet geprueft in ein, zwei oder drei
relative Ticks um. `MONOTONIC_MS` behaelt Index 42. Absolute Deadlines sind
64 Bit breit und auf einen festen Acht-Tick-Nachweishorizont begrenzt, damit
auch ein bereits anstehender PIT-Tick beim ersten CPL3-Eintritt sicher
beruecksichtigt wird.

Slot, Generation, `RUNNING`, Dauer, Ueberlauf, Membership und Kapazitaet
werden vor jeder Publikation validiert. Pro IRQ werden hoechstens vier
Deadline-Eintraege untersucht. Die vier privaten Tasks liefern Status 120 bis
123; nach dem Monotonic-Task werden die blockierten Tasks exakt in der Folge
1, 2, 0 geweckt. Erfolg verlangt die 27 festen Lebenszyklusereignisse, leere
und genullte Run- und Deadline-Queues, vier genullte Taskrecords sowie die
Wiederherstellung von Timer, PIC, CR3, TSS, Syscall-MSRs, Frames und
Freizaehler.

Alle 31 Quellvertragstests bestanden. Der Build erzeugte ein 89.188-Byte-
Bootstrap und ein 10.088-Byte-ELF64-Probeabbild. Der begrenzte Ein-vCPU-/
32-MiB-QEMU-Lauf meldete `REIST_X86_64_DEADLINE_SLEEP_OK` vor dem unveraenderten
Abschlussmarker. Dynamische Tasks, Prioritaeten, SMP und produktive
x86_64-Integration bleiben offen.

## Abgenommener Spawn-/Wait-Lebenszyklus R8.1n

Der dynamische Nachweis startet ausschliesslich Parent-Slot 0. `GETPID` 22
liefert dessen feste Test-PID 200. `SPAWN` 23 liest hoechstens 16 Bytes aus
einer privaten beschreibbaren ELF-Seite und akzeptiert nur den vollstaendig
terminierten Testpfad `/probe/child`. Erst danach werden die privaten Frames
fuer Slot 1 erzeugt und PID 201 mit Generation 31 beziehungsweise beim zweiten
Lebenszyklus Generation 32 veroeffentlicht.

`WAIT` 24 validiert die exakte Kindgeneration und einen ausgerichteten
privaten Vier-Byte-Statusausgang vor dem Zustandswechsel. Der Parent blockiert
ohne Polling, das Kind liefert Status 77, wird genau einmal reaptiert und erst
dann wird der Parent mit PID 201 und geschriebenem Status geweckt. Nullpfad,
doppelter Spawn, fremde PID, Null-Statuszeiger und ein bereits konsumiertes
stales Wait werden ohne Kind-, Queue- oder Ausgabemutation abgewiesen. Der
Parent beendet nach zwei Kindgenerationen mit Status 130.

Alle 32 Quellvertragstests bestanden. Der Build erzeugte ein 92.372-Byte-
Bootstrap und ein 10.264-Byte-ELF64-Probeabbild. Der begrenzte Ein-vCPU-/
32-MiB-QEMU-Lauf meldete `REIST_X86_64_SPAWN_WAIT_OK` vor dem unveraenderten
Abschlussmarker. Allgemeines VFS-Laden, `SPAWNV`, Argumentvererbung,
wait-any, Signale, Gruppen, SMP und produktive Integration bleiben offen.

## Abgenommener freestanding-C-Kern-Handoff R8.2a

Der fuer Multiboot v1 benoetigte aeussere Bootstrap bleibt ein ELF32-`ET_EXEC`.
Er bettet die Text-, RoData- und Data-Seiten eines separat vollstaendig
gelinkten ELF64-C-Payloads ein, das der normale x86_64-freestanding-C-Compiler
erzeugt. Die Produktionsziele fuer i386 bleiben davon getrennt. Assembly publiziert nach
allen bisherigen Markern genau den gepackten 128-Byte-Handoff Version 1 auf
einem eigenen 16-KiB-Stack nach SysV AMD64. C validiert die vollstaendige ABI
vor globaler Mutation, prueft initialisierte Data- und genullte BSS-Werte,
feste Arithmetik und eine auf 128 Byte begrenzte Kopie und ruft den ebenfalls
auf 64 Byte begrenzten seriellen Assembly-Adapter auf.

Der Abschluss ist fail-closed: C loescht sowohl den Handoff als auch alle
veraenderlichen C-Testwerte. Assembly prueft diese Bereiche erneut und meldet
erst danach `REIST_X86_64_C_CORE_HANDOFF_OK`. Der Build lehnt Red Zone,
Stackprotektor, Unwind- und Konstruktorabschnitte, Hosted-Runtime-Symbole,
undefinierte Endsymbole, verbleibende Relokationen sowie W+X-Abschnitte und
-Segmente ab. Der Nachweis fuehrt keine Geraete-, VFS-, DMA-, SMP- oder
produktive x86_64-Autoritaet ein und macht das Artefakt weiterhin nicht zu
einem vollstaendigen REIST-Kernel.

Alle 37 Quellvertragstests bestanden. Der Build erzeugte den 106.808-Byte-
Bootstrap, das 13.328-Byte-gelinkte ELF64-C-Payload, dessen 5.496-Byte-
Objekt und das unveraenderte 10.264-Byte-User-Probeabbild. Der begrenzte
Ein-vCPU-/32-MiB-QEMU-Lauf meldete nach allen R8.1-Markern geordnet
`REIST_X86_64_C_CALLBACK_OK` und `REIST_X86_64_C_CORE_HANDOFF_OK`.

## Abgenommener Ring-3-Shell-Schnitt R8.2b

R8.2b fuehrt ein getrennt gelinktes freestanding-C-ELF64 als einzigen
interaktiven Ring-3-Prozess aus. Nur die bestehenden REIST-v1-Indizes `READ`
15, `WRITE` 20, `YIELD` 40 und `EXIT` 9 werden fuer dessen feste serielle
Standardkanaele vermittelt. Reads blockieren nicht, Writes bleiben auf 64
Byte begrenzt und jeder nichtterminale Rueckweg verwendet einen validierten
`IRETQ`-Frame. Der automatisierte Dialog ist auf `INFO` und `EXIT` begrenzt;
allgemeines VFS-Laden und produktive Terminal- oder Geraeteautoritaet folgen
nicht aus diesem Nachweis. Die Abnahme umfasst 41 Quellvertragstests, einen
117.260-Byte-Bootstrap mit separat gelinkter 1.256-Byte-RX-Shell und den
begrenzten Ein-vCPU-/32-MiB-QEMU-Lauf. Der Gast meldet geordnet
`RING3_SHELL_READY`, `RING3_SHELL_INFO_OK`, die vollstaendige Bereinigung mit
`RING3_SHELL_EXIT_OK` und abschliessend `RING3_SHELL_OK`.

## Abgenommener Scheduler-Shell-Schnitt R8.2c

R8.2c ersetzt den Boot-Sonderaufruf der Shell durch einen vorhandenen,
fest kapazitierten Prozessslot. Das unveraenderte Shell-ELF wird als genau eine
nichtnullige Generation READY publiziert, in die Runqueue aufgenommen und ueber
den gemeinsamen Scheduler-Eintritt nach Ring 3 dispatcht. READ, WRITE und YIELD
kehren nicht direkt ueber einen separaten Shellpfad zurueck, sondern speichern
den Kontext, stellen den Slot erneut READY und durchlaufen generationengepruefte
Queue und `IRETQ`-Wiederherstellung. EXIT wechselt ueber EXITED zu FREE und
raeumt erst danach Loaderauswahl, Seitentabellen, Stack, TSS und Syscall-MSRs
auf. Der Schnitt fuehrt weder VFS noch allgemeine Terminal-, Geraete-, SMP-
oder produktive i386-Autoritaet ein. Die Abnahme umfasst 42 Quellvertragstests,
den isolierten Build und den begrenzten Ein-vCPU-/32-MiB-QEMU-Dialog. Nach dem
exakten Reap und der vollstaendigen Bereinigung meldet der Gast geordnet
`RING3_SHELL_EXIT_OK`, `SCHEDULED_SHELL_OK` und `RING3_SHELL_OK`.

## Abgenommener C-Kernel-Control-Schnitt R8.2d

R8.2d behaelt den 128-Byte-Bootstrap-Handoff unveraendert und fuegt einen
getrennten gepackten 64-Byte-Control-Handoff Version 1 hinzu. Nach dem
abgenommenen C-Core-Nachweis autorisiert er genau Shell-Service 1 als
Generation 1 mit den vorhandenen festen Kapazitaeten und Syscall-ABI Version 1.
Der C-Kern validiert Adresse und alle Felder vor dem ersten Effekt und erreicht
den Scheduler nur ueber einen festen Assembly-Adapter, der Kernel-CR3, IF und
eine exklusive Lease prueft und die SysV-ABI erhaelt. C loescht den Vertrag und
meldet Erfolg erst nach Rueckkehr des vollstaendig bereinigten Shell-
Lifecycles; Assembly prueft den autoritaetsfreien Zustand erneut vor dem
unveraenderten finalen Marker. Der Schnitt fuehrt keine allgemeine Spawn-, VFS-,
Terminal-, Geraete- oder SMP-Autoritaet ein. Die Abnahme umfasst 44
Quellvertragstests, den isolierten Build und den begrenzten Ein-vCPU-/32-MiB-
QEMU-Dialog. Der Gast meldet `C_CORE_HANDOFF_OK` vor der Scheduler-Shell,
danach `C_KERNEL_CONTROL_OK` und abschliessend den unveraenderten
`RING3_SHELL_OK`-Marker.

## Abgenommener physischer 128-MiB-Schnitt R8.2e

R8.2e erweitert ausschliesslich die feste physische Verwaltungsgrenze des
isolierten Artefakts. Zwei 4.096-Byte-Bitmaps verwalten hoechstens 32.768
4-KiB-Frames; 64 feste Page Tables bilden nur von Multiboot als nutzbar
gemeldete und nicht reservierte Frames RW/NX in der Direct-Map ab. Die
internen C-Seiten beginnen nun bei physisch `0x00184000`, damit die vergroesserte
Assembly-BSS disjunkt bleibt; das gesamte Bootstrap-Artefakt muss weiterhin
unter der bestehenden 2-MiB-Identity-Grenze enden. Ein separater, auf die
obere Bitmaphaelfte begrenzter Selbsttest allokiert genau einen Frame ab
64 MiB, prueft Zero-Fill und einen 64-Bit-Schreib-/Lesezugriff ueber die
Direct-Map, gibt ihn frei, verwirft ein doppeltes Free und stellt den exakten
Freizaehler wieder her. Erst danach erscheint
`REIST_X86_64_PHYSICAL_MEMORY_128M_OK`. Der bestehende 128-Byte-C-Handoff
behaelt Layout und Rechte; nur sein vorhandenes Speicherlimit betraegt nun
128 MiB. Dynamische Seitentabellen, Speicher oberhalb 128 MiB, NUMA, SMP und
produktive Hardwareintegration bleiben offen.

## Begrenzter High-Frame-Verbraucherschnitt R8.2f

Der normale lowest-first Allocator und die bestehenden ELF64-, Userstack- und
Prozess-Seitentabellenvertraege verwenden gemeinsam die bereits abgenommene
128-MiB-Grenze. Ein nur im Boot-Selbsttest aktiviertes festes Fenster erzwingt
Frames aus `[64 MiB, 128 MiB)` fuer den unveraenderten Loader- und ersten
Prozessaufbau. Es wird vor dem Ring-3-Eintritt geloescht; Erfolg verlangt die
normalen Zero-fill-, W^X-, NX-, Release- und Duplicate-free-Pruefungen sowie den
exakten urspruenglichen Freizaehler. Dynamische Seitentabellen und Speicher
oberhalb 128 MiB bleiben ausgeschlossen.

## Dynamische Scheduler-Seitentabellen R8.2g

Die vier generationengebundenen Scheduler-Slots behalten ihre feste
Kapazitaet, beziehen PML4, PDPT, PD und PT aber einzeln aus dem gemeinsamen
Frame-Allocator. Eine feste Vier-mal-vier-Metadatenmatrix besitzt diese Frames
bis zum Reap. `TASK_CR3` und READY duerfen erst nach vollstaendigem W^X-/NX-
Aufbau sichtbar werden. Teilaufbau, normaler Reap und Force-Cleanup stellen
zuerst Kernel-CR3 her, nullen und geben PT bis PML4 exakt einmal frei und
muessen den anfaenglichen Freizaehler sowie eine leere Matrix wiederherstellen.
Der fruehe Einzelprozessnachweis behaelt unabhaengige statische Tabellen.

## Dynamische fruehe Ausfuehrungstabellen R8.2h

Auch der verbleibende Einzelprozess-/Shell-Sondernachweis bezieht PML4, PDPT,
PD und PT aus dem gemeinsamen Frame-Allocator. Eine feste Vier-Eintrag-
Besitzliste ersetzt seine letzte statische 16-KiB-Tabellenarena. `user_cr3`
wird erst nach vollstaendigem Direct-Map-Aufbau und W^X-/NX-Nachweis
publiziert. Erfolg, CPL3-Fehler und jeder Teilfehler stellen zuerst Kernel-CR3
her und geben Stack, ELF- sowie Tabellenframes exakt einmal zurueck. Damit
bleiben im isolierten x86_64-Prozesspfad keine statischen User-Tabellenarenen.

Die Abnahme umfasst 48 Quellvertragstests, den warnungsfreien isolierten
125.944-Byte-Build und den begrenzten nativen Ein-vCPU-/128-MiB-QEMU-Lauf.
Nach leerer Besitzliste und wiederhergestelltem Freizaehler erschien
`EARLY_EXECUTION_TABLES_OK` geordnet vor allen Scheduler-, C-Control- und
Shell-Markern bis `RING3_SHELL_OK`.

## Reales Shell-Spawn/Wait R8.2i

Das exakte Shell-Kommando `RUN` verbindet den C-gesteuerten geplanten
Shellprozess mit `GETPID` 22, `SPAWN` 23 und `WAIT` 24. Nur PID 300 darf den
festen Stackpfad `/shell/child` verwenden. Kindslot 1/Generation 41 teilt
ausschliesslich das validierte RX-Shellabbild, erhaelt private dynamische
Tabellen und einen NX-Stack und startet mit einem festen Kindmodus in `RDI`.
Das Kind fuehrt kein I/O aus und beendet sich mit Status 77. WAIT blockiert
Generation 40 ohne Polling, publiziert den Status erst nach terminaler
Kindvalidierung und reapt alle Kindressourcen vor `RUN_OK`.

Die Abnahme umfasst 49 Quellvertragstests und ein 126.700-Byte-Bootstrap.
Der warnungsfreie isolierte Build erzeugte die 1.672-Byte-Shell. Der begrenzte
Ein-vCPU-/128-MiB-QEMU-Dialog fuehrte `INFO`, `RUN` und `EXIT` aus, erreichte
`RING3_SHELL_RUN_OK` und meldete nach vollstaendigem Kind-Reap alle bisherigen
Marker geordnet bis `RING3_SHELL_OK`.

## Generationensichere Shell-Kindwiederverwendung R8.2j

Der reale Shellpfad akzeptiert genau einen zweiten sequenziellen `RUN`-Zyklus.
Nach dem vollstaendigen Reap von Generation 41 muessen Slot 1, Stack- und
private ELF-Frames, alle vier Tabellenframes, Runqueue, Parentbeziehung und
WAIT-Metadaten null sein. Erst dann darf derselbe Slot mit frischen Ressourcen
als Generation 42 READY werden. PID 301 und Status 77 bleiben Teil der
unveraenderten REIST-v1-ABI; die Generation bleibt kernelintern.

50 Quellvertragstests und der warnungsfreie 126.868-Byte-Build bestanden. Der
begrenzte Ein-vCPU-/128-MiB-QEMU-Dialog sendete `INFO`, `RUN`, `RUN`, `EXIT`,
beobachtete exakt zwei geordnete `RING3_SHELL_RUN_OK`-Marker und erreichte nach
vollstaendigem Cleanup alle bisherigen Marker bis `RING3_SHELL_OK`.

## Separates Shell-Kind-ELF R8.2k

`/shell/child` waehlt nun ein unabhaengig assembliertes und gelinktes
System-V-AMD64-`ET_EXEC` mit genau einem RX-Segment. Das 360-Byte-Abbild fuehrt
keine Ein-/Ausgabe aus und beendet sich ausschliesslich ueber `EXIT` 9 mit
Status 77. Die Shell besitzt keinen Kindmodus mehr.

Der Loader verwaltet exakt drei feste 88-Byte-Kontexte fuer Probe, Shell und
Kind. Shell und Kind duerfen gleichzeitig aktiv sein, teilen aber weder
Frameeintraege noch Flags, Entry oder Cleanupzaehler. Nach jeder Generation
wird unter Kernel-CR3 zuerst der Task reaptiert, danach der Kindkontext
freigegeben und erst dann die Shell wieder ausgewaehlt. Gemeinsames Cleanup
laeuft Kind, Shell, Probe.

51 Quellvertragstests und der warnungsfreie 127.488-Byte-Build bestanden. Der
native Ein-vCPU-/128-MiB-QEMU-Dialog lud das Kind zweimal frisch, beobachtete
exakt zwei `RUN_OK`-Marker und erreichte nach leerem Kontext- und Framebesitz
alle bisherigen Marker bis `RING3_SHELL_OK`.

## Begrenztes SPAWNV und argv R8.2l

`RUN` verwendet nun den unveraenderten REIST-v1-Syscall `SPAWNV` 30. Im
privaten Shell-NX-Stack liegen exakt `/shell/child`, `token77` und ein
8-Byte-ausgerichteter Zwei-Pointer-Vector. Der Kernel akzeptiert nur Shell-PID
300/Generation 40, `argc == 2`, disjunkte 16-Byte-Bereiche und beide
NUL-terminierten Sollstrings, bevor Loader oder Taskzustand veraendert werden.

Nach ELF- und Adressraumvalidierung entsteht im privaten Kindstack ein
vollstaendig genullter 96-Byte-Startblock. `%rsp` ist 16-Byte-ausgerichtet und
zeigt auf `argc`; es folgen zwei `argv`-Pointer, NULL, eine leere Umgebung und
ein `AT_NULL`-Paar. Das RX-only-Kind prueft Stackadresse, Alignment, alle
Pointer, Terminatoren und Strings. Nur Erfolg beendet sich mit 77, jede
Abweichung mit 78.

52 Quellvertragstests und der nach einer fokussierten Immediate-Reparatur
warnungsfreie 128.328-Byte-Build bestanden. Der native QEMU-Dialog validierte
beide Generationen und erreichte mit exakt zwei `RUN_OK`-Markern alle
bisherigen Marker bis `RING3_SHELL_OK`.

## Generationsgebundene Syscallprofile R8.2m

Der unveraenderte 256-Byte-Taskrecord besitzt keine freie ABI-Flaeche. Deshalb
ordnet eine separate feste Vier-Slot-Tabelle jedem Taskslot genau eine
64-Bit-Generation und eine 64-Bit-Syscallmaske zu. Das Elternprofil fuer
Generation 40 enthaelt ausschliesslich `EXIT`, `READ`, `WRITE`, `GETPID`,
`SPAWN`, `WAIT`, `SPAWNV` und `YIELD`; die Kindprofile 41 und 42 enthalten nur
`EXIT`. Profilgeneration, exakte Rollenmaske und Indexgrenze werden vor dem
Shell-Dispatcher geprueft. Stale oder missgebildete Metadaten brechen
fail-closed ab, eine gueltige Ablehnung liefert `EACCES` ueber den normalen
gespeicherten Kontext und IRETQ-/Runqueue-Rueckweg.

Das RX-only-Kind ruft vor seiner bisherigen Stackpruefung einmal `GETPID` 22
mit Nullargumenten auf und akzeptiert ausschliesslich `-13`. Danach bleiben
96-Byte-System-V-Startstack und Exitstatus 77 unveraendert. Reap leert Maske
und Generation nur bei exaktem Match vor Task- und Abbildfreigabe; die finale
Pruefung verlangt alle vier Records null. 53 Quellvertragstests und der nach
einer fokussierten 64-Bit-Immediate-Reparatur warnungsfreie 129.680-Byte-Build
bestanden. Der native Ein-vCPU-/128-MiB-QEMU-Dialog durchlief beide
Kindgenerationen, meldete exakt zwei `RUN_OK`-Marker und erreichte alle
bisherigen Marker bis `RING3_SHELL_OK`.

## Expliziter IPC-Capability-Transfer R8.2n

Der reale `RUN`-Pfad verwendet die bestehenden REIST-v1-Indizes `IPC_CREATE`
49, `IPC_SEND` 50, `IPC_RECEIVE` 51, `IPC_CLOSE` 52, `IPC_DELEGATE` 55 und
`IPC_RELEASE` 58. Genau ein Endpoint, eine Nachricht mit festem 140-Byte-v1-
Layout und vier 24-Byte-Capabilityrecords sind statisch reserviert. Generation
40 besitzt `SEND | RECEIVE | CONTROL` und delegiert ausschliesslich `SEND` an
die bereits aufgebaute Generation 41 beziehungsweise 42. Das Kind erhaelt das
opaque Generation-plus-Slot-Handle ueber den REIST-privaten System-V-Auxv-Typ
`AT_REIST_IPC_HANDLE = 0x52534901`; der auf 128 Byte erweiterte Startstack
behaelt `argc = 2`, `argv`, leere Umgebung, `AT_NULL` und 16-Byte-Ausrichtung.

Nach `GETPID -> EACCES` prueft das Kind den Startstack, erhaelt fuer einen
strukturell gueltigen `IPC_RECEIVE` wegen des fehlenden Rechts erneut
`EACCES`, sendet die bytegenaue `token77`-Nachricht, gibt die Capability frei
und beendet sich mit 77. Der Parent wartet generationengenau, empfaengt und
validiert die Nachricht und schliesst den Endpoint vor `RUN_OK`. Reap,
Fehlercleanup und die finale Pruefung verlangen Endpoint, Nachricht sowie alle
vier Capabilityrecords null. 54 Quellvertragstests, der warnungsfreie
136.364-Byte-Build und der native Ein-vCPU-/128-MiB-QEMU-Dialog bestanden mit
exakt zwei `RUN_OK`-Markern bis `RING3_SHELL_OK`. Eine fokussierte Reparatur lud
den physischen Kind-Stackframe unmittelbar vor der Direct-Map-Initialisierung
neu und wird durch eine Regressionserwartung gesichert.

## Deadlinebegrenzter IPC-Receive R8.2o

Der reale `RUN`-Pfad verwendet nun zusaetzlich den bestehenden REIST-v1-Index
`IPC_RECEIVE_TIMEOUT` 54. Genau ein fester Waiterrecord bindet Generation 40,
Endpointhandle und den vorvalidierten privaten Outputframe an einen Eintrag der
vorhandenen Vier-Slot-Deadlinequeue. Der isolierte Nachweis akzeptiert exakt
10 ms, entsprechend einem Tick des bereits getesteten 100-Hz-PIT-Timers.

Unmittelbar nach `IPC_CREATE` blockiert ein leerer Receive ohne lauffaehigen
Peer, idlet mit `STI; HLT`, wacht nur durch die monotone Deadline und liefert
`ETIMEDOUT`, ohne Header oder Payload zu veraendern. Nach SPAWNV und der
abgeschwaechten SEND-Delegation blockiert der Parent erneut vor dem Kind. Der
Kind-SEND validiert Capability, Endpoint, Nachricht, Taskgeneration, Waiter,
privaten Direct-Map-Bereich und Deadline vor Queue-Publikation; vor Wake werden
dieselben Beziehungen erneut geprueft. Danach werden Deadline und Timer
widerrufen, `token77` wird in den privaten Parent-Frame kopiert und nur
Generation 40 wird READY. Erst nach der Receive-Pruefung folgt das bestehende
WAIT/Reap.

54 Quellvertragstests, der warnungsfreie 138.000-Byte-Build und der native
Ein-vCPU-/128-MiB-QEMU-Dialog bestanden mit zwei vollstaendigen Timeout-/Wake-
Zyklen, exakt zwei `RUN_OK`-Markern und `RING3_SHELL_OK`. Abschlusspruefung und
Fehlercleanup verlangen Timer-, Deadline-, Waiter-, Endpoint-, Nachrichten-,
Capability-, Task-, Loader- und Framezustand null. Mehrere Waiter,
blockierende Sender, tiefere Queues und produktive Integration bleiben
ausgeschlossen.

## Queue-Backpressure R8.2p

Das aktive Paket behaelt den einzelnen 140-Byte-Queue-Slot bei und prueft ihn
erstmals asynchron. Nach einem geplanten `YIELD` des Parents publiziert die
exakte Kindgeneration `token76`, ohne dass ein Receive-Wait besteht. Der
folgende strukturell gueltige `token77`-SEND muss `EAGAIN` liefern und darf
weder Queue noch Waiter, Deadline, Capability, Endpoint oder Usernachricht
veraendern. Das Kind gibt danach ebenfalls mit `YIELD` ab.

Generation 40 entnimmt und validiert `token76`, blockiert anschliessend mit dem
bestehenden 10-ms-Receive und wird erst vom generationengenauen `token77`-Retry
des Kindes geweckt. Ein fester Send-Phasenrecord verhindert Auslassung,
Doppelpublikation und stale Wiederverwendung. Blocking-Sender, Queue-Tiefe
groesser eins und mehrere Waiter bleiben explizit ausgeschlossen.
