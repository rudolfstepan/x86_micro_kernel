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

Das Artefakt ist kein vollstaendiger REIST-Kernel. Es besitzt keine Multiboot-
Speicherkartenauswertung, keinen physischen Allocator und keine Direct Map;
diese physische Adressierungsgrundlage ist R8.1d vorbehalten. Es besitzt keine produktive
Hardwareinterruptbehandlung und weder Prozessmodell noch Syscall-ABI,
Userspace, Treiber, Dateisystem oder signiertes natives Medienlayout. Es
begruendet deshalb keine ELF64-, Hardware- oder Fail-operational-Kompatibilitaet.

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
