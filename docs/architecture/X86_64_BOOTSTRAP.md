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
