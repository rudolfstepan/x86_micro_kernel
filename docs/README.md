# Dokumentationsindex

Stand: 19. August 2026.

Die Dokumentation unterscheidet zwischen aktuellen Referenzen und
historischen Arbeitsberichten. Für Aufbau, Start und Bedienung sind die hier
als **aktuell** bezeichneten Dokumente maßgeblich. Historische Berichte
erklären frühere Entscheidungen, können aber alte Dateipfade, Befehle oder
bereits behobene Fehler enthalten.

## Einstieg und aktueller Stand

- [Projektübersicht](../README.md) – Schnellstart, Funktionen und Grenzen
- [Quickstart](development/QUICKSTART.md) – nativer Windows-Build und erster Start
- [Projektstatus](development/PROJECT_STATUS.md) – verifizierte Komponenten und offene Grenzen
- [Fehlstellenanalyse und Roadmap](development/OS_GAP_ANALYSIS_AND_ROADMAP.md) – priorisierte Implementierungspakete mit Abhängigkeiten und Abnahmekriterien
- [REIST High-Assurance Core Contract](architecture/HIGH_ASSURANCE_CORE_CONTRACT.md) – verbindliche, branchenunabhängige Regeln für Fehlerbegrenzung, Recovery und Nachweisführung
- [Resilienz- und Degradierungsvertrag](architecture/RESILIENCE_AND_DEGRADATION_CONTRACT.md) – globales Stabilitätsversprechen, Restartbudgets und terminale Systemzustände
- [Ring-3-Treibermodell](architecture/USERSPACE_DRIVER_MODEL.md) – Gerätebesitz, IRQ-, MMIO-/PIO- und DMA-Grenzen für neu startbare Treiber
- [Medical Reference Profile](architecture/MEDICAL_HIGH_ASSURANCE_CONTRACT.md) – optionale medizinische Verschärfung; keine klinische Freigabe
- [REIST-Zielarchitektur](architecture/REIST_ARCHITECTURE.md) – Detect, Contain, Recover, Validate und Reintegrate als technisches Systemmodell
- [Build-Modi](development/BUILD_MODES.md) – `qemu`, `vmware`, `real_hw` und Videoauswahl
- [Nativer Bootdatenträger](development/BOOTABLE_DISK.md) – BIOS/MBR, Stage 2 und FAT32-Image
- [Bootfähige Diskette](development/FLOPPY_BOOT.md) – 1,44-MB-CHS-Image für echte BIOS-PCs
- [Externe Programme](development/USER_PROGRAM_TOOLCHAIN.md) – SDK, ABI und MYPR-Toolchain
- [Userspace-SDK und Portabilität](architecture/USERSPACE_SDK_AND_PORTABILITY.md) – modulare Bibliotheken, Upstream-Toolchain und API-Dokumentationsvertrag
- [GUI-Komponenten, Controls und Dialoge](architecture/GUI_CONTROLS_AND_DIALOGS.md) – unterstützte UI-Bausteine, Dialogstandard und schrittweise Control-Roadmap
- [System- und Programmkonfiguration](architecture/SYSTEM_CONFIGURATION.md) – `/etc/reist`, versionierte Einstellungsdateien und sichere Rückfallwerte
- [Grafischer Desktop-Workflow](development/GRAPHICAL_DESKTOP_WINDOW_MANAGER_WORKFLOW.md) – Window-Manager-, Surface-, GUI-SDK- und VMware-Arbeitsschritte
- [Userspace-Dateisystemwerkzeuge](development/USERSPACE_FILESYSTEM_TOOLS.md) –
  Werkzeuginventar, Systemhierarchie und Timestamp-Vertrag
- [Synchronisationsvertrag](architecture/SYNCHRONIZATION_CONTRACT.md) – Ausführungskontexte, Lock-Reihenfolge und Diagnose

## Bedienung und Laufzeit

- [Shell und Pfade](features/SHELL_ENHANCEMENTS.md) – DOS-artige Befehle und Tastaturbearbeitung
- [Laufwerke und Mounts](filesystems/DRIVE_MOUNTING_SYSTEM.md) – Zuordnung, VFS-Pfade und Laufwerkswechsel
- [VFS-Architektur](filesystems/VFS_ARCHITECTURE.md) – gemeinsame Dateisystemschnittstelle
- [BASIC-Interpreter](features/BASIC_INTERPRETER.md) – Syntax, Laden und Speichern
- [Tastaturkürzel](features/KEYBOARD_SHORTCUTS.md) – Zeilenbearbeitung und Verlauf
- [Framebuffer](features/FRAMEBUFFER.md) – nativer VBE-Pfad, Ring-3-Display-ABI und Desktop-MVP

## Hardware und Netzwerk

- [PCI-Geräte und Treiberstatus](hardware/PCI_DEVICES.md) – übliche PCI-Klassen,
  erkannte Geräte und tatsächlich unterstützte REIST-Treiber
- [Fertige VMware-VM](hardware/VMWARE.md) – VMX/VMDK, LAN-Bridge und Fehlerdiagnose
- [Netzwerkstack](networking/NETWORK.md) – E1000, DHCP, ARP und ICMP
- [TAP-Netzwerk](networking/TAP_NETWORKING.md) – optionaler Linux/QEMU-Testweg
- [USB-Design](hardware/USB_DESIGN.md) – experimenteller USB-/xHCI-Stand
- [EXT2](filesystems/EXT2_SUPPORT.md), [FAT12](filesystems/FAT12_IMPROVEMENTS.md) und
  [FAT32](filesystems/FAT32_OPTIMIZATIONS.md) – Dateisystemstatus und Grenzen

## Tests

- [Tests ausführen](../scripts/README_TESTING.md) – aktuelle Befehle
- [Testabdeckung](../scripts/TESTING_SUMMARY.md) – geprüfte Invarianten und Grenzen

Der derzeitige Referenzdatenträger ist ein BIOS/MBR-Image mit einer markierten
FAT32-Systempartition. VMware verwendet SATA/AHCI, QEMU hält zusätzlich den
ATA/IDE-Regressionspfad bereit. Für reale Datenträger dürfen ausschließlich
die zielgebundenen Installationsskripte nach Prüfung von Modell, Seriennummer
und Größe verwendet werden.

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
- `features/BASIC_INTERPRETER_UPDATES.md`
- `filesystems/FAT12_ANALYSIS.md`

Bei Widersprüchen gilt in dieser Reihenfolge: ausführbarer Code und Tests,
aktuelle Referenzdokumente, historische Berichte.
