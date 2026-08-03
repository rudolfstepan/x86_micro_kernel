# Dokumentationsindex

Stand: 3. August 2026.

Die Dokumentation unterscheidet zwischen aktuellen Referenzen und
historischen Arbeitsberichten. Für Aufbau, Start und Bedienung sind die hier
als **aktuell** bezeichneten Dokumente maßgeblich. Historische Berichte
erklären frühere Entscheidungen, können aber alte Dateipfade, Befehle oder
bereits behobene Fehler enthalten.

## Einstieg und aktueller Stand

- [Projektübersicht](../README.md) – Schnellstart, Funktionen und Grenzen
- [Quickstart](development/QUICKSTART.md) – nativer Windows-Build und erster Start
- [Projektstatus](development/PROJECT_STATUS.md) – verifizierte Komponenten und offene Grenzen
- [Build-Modi](development/BUILD_MODES.md) – `qemu`, `vmware`, `real_hw` und Videoauswahl
- [Nativer Bootdatenträger](development/BOOTABLE_DISK.md) – BIOS/MBR, Stage 2 und FAT32-Image
- [Externe Programme](development/USER_PROGRAM_TOOLCHAIN.md) – SDK, ABI und MYPR-Toolchain

## Bedienung und Laufzeit

- [Shell und Pfade](features/SHELL_ENHANCEMENTS.md) – DOS-artige Befehle und Tastaturbearbeitung
- [Laufwerke und Mounts](../DRIVE_MOUNTING_SYSTEM.md) – Zuordnung, VFS-Pfade und Laufwerkswechsel
- [VFS-Architektur](../VFS_ARCHITECTURE.md) – gemeinsame Dateisystemschnittstelle
- [BASIC-Interpreter](features/BASIC_INTERPRETER.md) – Syntax, Laden und Speichern
- [Tastaturkürzel](features/KEYBOARD_SHORTCUTS.md) – Zeilenbearbeitung und Verlauf
- [Framebuffer](features/FRAMEBUFFER.md) – VGA-Standardweg und experimenteller Grafikmodus

## Hardware und Netzwerk

- [Fertige VMware-VM](hardware/VMWARE.md) – VMX/VMDK, LAN-Bridge und Fehlerdiagnose
- [Netzwerkstack](networking/NETWORK.md) – E1000, DHCP, ARP und ICMP
- [TAP-Netzwerk](networking/TAP_NETWORKING.md) – optionaler Linux/QEMU-Testweg
- [USB-Design](hardware/USB_DESIGN.md) – experimenteller USB-/xHCI-Stand
- [EXT2](../EXT2_SUPPORT.md), [FAT12](../FAT12_IMPROVEMENTS.md) und
  [FAT32](../FAT32_OPTIMIZATIONS.md) – Dateisystemstatus und Grenzen

## Tests

- [Tests ausführen](../scripts/README_TESTING.md) – aktuelle Befehle
- [Testabdeckung](../scripts/TESTING_SUMMARY.md) – geprüfte Invarianten und Grenzen

## Historische Arbeitsberichte

Die folgenden Dokumente bleiben als Entwicklungsprotokoll erhalten. Ihre
Kopfzeile weist darauf hin, dass sie nicht den aktuellen Betriebsweg
beschreibt:

- `architecture/ARCHITECTURE_IMPROVEMENTS.md`
- `development/DIAGNOSTIC_REPORT.md`
- `development/REORGANIZATION.md`
- `development/FIXES_ISSUE_1_INTERRUPT_MANAGEMENT.md`
- `development/PRIORITY3_CURSOR_POSITIONING.md`
- `development/DEBUGGING_BOOT_SECTOR.md`
- `hardware/KEYBOARD_ANALYSIS.md`
- `hardware/KEYBOARD_IMPROVEMENTS.md`
- `networking/NE2000_LOOPBACK_FIX.md`
- `../BASIC_INTERPRETER_UPDATES.md`
- `../FAT12_ANALYSIS.md`

Bei Widersprüchen gilt in dieser Reihenfolge: ausführbarer Code und Tests,
aktuelle Referenzdokumente, historische Berichte.
