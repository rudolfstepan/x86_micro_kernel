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
endet bei 64 MiB und verwendet weder dynamische Seitentabellen noch NUMA,
Highmem oder eine produktive Prozessintegration. Es besitzt keine produktive
Hardwareinterruptbehandlung und weder Prozessmodell noch Syscall-ABI,
ausfuehrbaren Userspace, Treiber, Dateisystem oder signiertes natives
Medienlayout. Der Loadernachweis allein begruendet deshalb keine vollstaendige
ELF64-Prozess-, Hardware- oder Fail-operational-Kompatibilitaet.

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
- zwei feste 2.048-Byte-Bitmaps fuer 16.384 Frames unter 64 MiB;
- eine feste RW/NX-Direct-Map mit 32 Page Tables und ausschliesslich
  verwaltbaren RAM-Frames;
- ein separat gelinktes ELF64-`ET_EXEC` mit maximal 64 KiB, vier Program
  Headern, zwei `PT_LOAD`-Segmenten und acht staged Userseiten;
- ein statischer 16-KiB-Bootstack innerhalb dieser Map;
- genau 32 statische 16-Byte-IDT-Gates und eine gepackte 104-Byte-TSS;
- ein statischer 16-KiB-IST ausschliesslich fuer Double Fault;
- maximal 65.536 Statusabfragen je gesendetem COM1-Byte;
- genau eine vCPU, 32 MiB RAM und zehn Sekunden im automatisierten QEMU-Gate.

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
