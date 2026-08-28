# REIST x86_64 bootstrap contract

Stand: 28. August 2026

## Zweck und Grenze

R8.1a fuehrt ein getrenntes Architektur-Prototypartefakt ein. Es beginnt im
von Multiboot definierten 32-Bit-Protected-Mode, prueft die benoetigten
CPU-Faehigkeiten, aktiviert mit statischen Tabellen IA-32e Paging und springt
in einen 64-Bit-Codesegment. Erst dort darf der serielle Erfolgsmarker
`REIST_X86_64_LONG_MODE_BOOT_OK` erscheinen.

Das Artefakt ist kein vollstaendiger REIST-Kernel. Es besitzt weder produktive
Interrupt- oder Exceptionbehandlung noch Prozessmodell, Syscall-ABI,
Userspace, Treiber, Dateisystem oder signiertes natives Medienlayout. Es
begruendet deshalb keine ELF64-, Hardware- oder Fail-operational-Kompatibilitaet.

## Referenzstandard

Der Eintritt folgt Intel 64/IA-32 SDM: CPUID weist die erweiterte Funktion
`0x80000001` und Long-Mode-Bit EDX[29] nach; CR4.PAE, EFER.LME und CR0.PG
werden in dieser Reihenfolge aktiviert. Nach dem Far-Transfer prueft der
64-Bit-Code CR0.PG, CR4.PAE und EFER.LMA erneut. Der Ladevertrag ist Multiboot
Version 1; er bleibt auf das separate Bootstrap-Artefakt begrenzt.

## Feste Ressourcen

- eine statische, page-aligned PML4, PDPT und Page Directory;
- genau eine 2-MiB-Identity-Map ab physischer Adresse null;
- ein statischer 16-KiB-Bootstack innerhalb dieser Map;
- maximal 65.536 Statusabfragen je gesendetem COM1-Byte;
- genau eine vCPU, 32 MiB RAM und zehn Sekunden im automatisierten QEMU-Gate.

Nicht unterstuetzte CPU-Faehigkeiten und inkonsistente Kontrollregister enden
mit einem eigenen seriellen Fehlermarker und anschliessendem `hlt`. Der
produktive i386-Build verwendet keine Datei aus `arch/x86_64` und bleibt die
Standardauswahl aller bisherigen Build- und Installationswege.
