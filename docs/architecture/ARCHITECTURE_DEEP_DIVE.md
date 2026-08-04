# Architekturüberblick

Dieses Dokument beschreibt die aktuelle 32-Bit-x86-Architektur. Das System
startet ausschließlich über den eigenen BIOS-Bootloader. Einen alternativen
Legacy-Einstieg gibt es nicht mehr.

## Bootkette

```text
BIOS
  -> arch/x86/boot/bios/stage1_mbr.asm
  -> Manifest in der aktiven RAW-Bootpartition
  -> arch/x86/boot/bios/stage2_bios.asm
  -> A20, E820, ELF32-Laden und CRC32-Prüfung
  -> Protected Mode
  -> Multiboot-1-kompatibler Handoff
  -> arch/x86/boot/multiboot.asm
  -> kernel_main()
```

Stage 1 ist exakt ein MBR-Sektor. Stage 2 liest Kernel und Manifest per BIOS
EDD/INT 13h, validiert 32-Bit-i386-ELF und lädt nur `PT_LOAD`-Segmente in
zulässige physische Bereiche. Bereiche mit `p_memsz > p_filesz` werden für
BSS genullt. Die Multiboot-Struktur bleibt eine interne Übergabeschnittstelle;
sie bedeutet nicht, dass der native Weg GRUB benötigt.

## Kernelinitialisierung

`kernel_main()` arbeitet in klaren Stufen:

1. Multiboot-Magic und Informationszeiger prüfen
2. Bootinformationen und Speicherkarte auswerten
3. VGA oder optionalen Framebuffer initialisieren
4. Kernel-Allocator initialisieren
5. TSS, GDT, IDT, Exceptions, IRQs, PIT und PS/2-Tastatur aufsetzen
6. APIC-Timer, PCI und experimentelles USB probing starten
7. Netzwerktreiber passend zu erkannten PCI-Geräten registrieren
8. ATA und Disketten erkennen
9. VFS initialisieren und Laufwerke automatisch mounten
10. Netzwerkstack und DHCP starten
11. in die interaktive Shell wechseln

COM1 wird früh initialisiert, damit auch Fehler vor der VGA-Shell in einem
seriellen Log sichtbar bleiben.

## CPU-Tabellen und Interrupts

- Die GDT enthält Kernelsegmente und einen TSS-Deskriptor.
- Die IDT deckt CPU-Ausnahmen, Hardware-IRQs und den Syscall-Einstieg ab.
- Assembly-Stubs sichern den Registerzustand und rufen C-Handler auf.
- PIT-Ticks liefern Millisekundenzeit und Scheduler-Ereignisse.
- Der lokale APIC-Timer wird zusätzlich initialisiert, wenn verfügbar.
- Präemptionskritische Kernelbereiche verwenden eigene Guards.

IRQ-Handler sollen kurze Hardwarearbeit erledigen und keine dauerhaften
VGA-Debugmeldungen ausgeben. Insbesondere Netzwerk-RX wird über die
gemeinsame `netdev`-Schicht weiterverarbeitet, ohne die Shellausgabe mit jedem
Interrupt zu unterbrechen.

## Speicher

Der Kernel verwendet einen eigenen Heap-Allocator in `mm/kmalloc.c` und die
x86-Paging-Unterstützung in `arch/x86/mm/paging.c`. Bootloaderdaten liefern
die verfügbare physische Speicherkarte. Der aktuelle Programmlader reserviert
einen gemeinsamen 8-MiB-Bereich ab `0x02100000` für MYPR-Programme.

Dieser Bereich ist noch keine Sicherheitsgrenze: Programmtasks laufen in
Ring 0, teilen Kerneladressraum und Ladebereich und besitzen nur getrennte
Kernelstacks. Vollständiger Userspace benötigt Ring-3-Übergang, eigene
Seitentabellen, Userstacks und geprüfte Kopierfunktionen für Syscall-Pointer.

## Scheduler und Prozesse

Der Scheduler verwaltet bis zu acht Tasks mit je 8 KiB Stack und den Zuständen
ready, running, sleeping und finished. Ein Prozessdatensatz ordnet PID,
Task-ID, Namen und Programmbild zu. Timerpräemption kann für kritische
Operationen vorübergehend unterdrückt werden.

`RUN`/`EXEC` laden eine MYPR-Datei über VFS, validieren den vollständigen
Header und erzeugen erst danach einen Task. Der Startup-Code externer
Programme ruft `main()` auf und endet über den Exit-Syscall.

## Syscalls

Die öffentliche SDK-Schicht kapselt den Low-Level-Einstieg. Aktuell stehen
Zeichenausgabe, Zahlenausgabe, Delay, Tastatureingabe, Speicheroperationen und
Exit bereit. Externe Programme sollen nur `userspace/sdk/include/x86os.h`
einbinden, keine internen Kernelheader.

## Dateisysteme

Die VFS-Mounttabelle wählt anhand des längsten passenden Mountpfades einen
Adapter. FAT32, FAT12 und EXT2 werden beim Boot registriert. Shell und
Programmlader arbeiten mit absoluten VFS-Pfaden; DOS-Laufwerksnotation wird
vorher im Shellresolver normalisiert.

## Treiber

| Bereich | Aktuelle Komponenten |
|---|---|
| Block | ATA/IDE, Floppy |
| Eingabe | PS/2-Tastatur, experimentelles USB-HID |
| Anzeige | VGA-Text, optionaler RGB-Framebuffer |
| Bus | PCI, USB-Hostcontroller-Probing |
| Netzwerk | E1000, RTL8139, NE2000 über `netdev` |
| Zeit | PIT, RTC, lokaler APIC-Timer |

Die generierte VMware-Referenzmaschine verwendet IDE, VGA, PS/2 und E1000.

## Wichtige Grenzen

- BIOS/MBR statt UEFI
- i386 statt x86-64
- noch kein isolierter Ring-3-Userspace
- kein SMP-Scheduler
- Minimalnetzwerk ohne TCP/DNS/IPv6
- USB und Framebuffer sind nicht so umfassend verifiziert wie der
  VMware-VGA-/PS/2-/E1000-Weg

## Quellreferenzen

- `arch/x86/boot/bios/` – native Bootstufen
- `arch/x86/cpu/` – GDT, IDT, ISR, IRQ und Syscalls
- `kernel/init/kernel.c` – Initialisierungsreihenfolge
- `kernel/sched/` und `kernel/proc/` – Tasks und Programme
- `kernel/shell/` – Shell und Pfadauflösung
- `fs/vfs/` – Mount- und Dateisystemabstraktion
- `drivers/` – Hardwaretreiber
- `userspace/sdk/` – externe Programmschnittstelle
