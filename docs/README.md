# Dokumentationsindex

Stand: 20. August 2026.

Die Dokumentation unterscheidet zwischen aktuellen Referenzen und
historischen Arbeitsberichten. Für Aufbau, Start und Bedienung sind die hier
als **aktuell** bezeichneten Dokumente maßgeblich. Historische Berichte
erklären frühere Entscheidungen, können aber alte Dateipfade, Befehle oder
bereits behobene Fehler enthalten.

## Globale Struktur und Zuständigkeit

Jede Information besitzt genau einen fachlich autoritativen Ort. Andere
Dokumente geben nur eine kurze Einordnung und verlinken dorthin; sie kopieren
keine vollständigen Statuslisten, ABI-Tabellen oder Bedienungsabläufe.

| Ort | Einzige Aufgabe | Darf nicht duplizieren |
|---|---|---|
| `README.md` | Projekteinstieg, kleinster Schnellstart, wichtigste Grenzen | vollständige Architektur- oder Subsystemverträge |
| `docs/README.md` | globaler Index, Dokumentrollen und Aktualitätsregeln | Subsystemdetails |
| `docs/development/PROJECT_STATUS.md` | kompakter, belegter Ist-Stand und offene Systemgrenzen | Implementierungsanleitungen und ABI-Definitionen |
| `docs/architecture/` | normative, versionierte Architektur-, Sicherheits- und API-Verträge | historische Arbeitsprotokolle |
| `docs/features/`, `docs/filesystems/`, `docs/hardware/`, `docs/networking/` | aktuelles beobachtbares Verhalten je Fachgebiet | globale Roadmap und Paketqueue |
| `docs/development/` | Build-/Testworkflows, Roadmap und klar markierte Arbeitspakete | normative API-Verträge |
| `userspace/*/README.md`, `assets/*/README.md`, `scripts/*.md` | lokale Quellbaum-, Asset- oder Werkzeugreferenz | globalen Projektstatus |

Für konkurrierende Aussagen gilt folgende Reihenfolge:

1. ausführbarer Code und bestandene Tests;
2. `automation/reist-s03b.toml` für Paketstatus und aktiven Scope;
3. normative Architektur- und Subsystemreferenz;
4. `PROJECT_STATUS.md` als komprimierter Snapshot;
5. Roadmap und abgeschlossene Arbeitspakete;
6. historische Berichte.

Pflegevorgaben:

- Ein Statuswort wie **aktiv** darf nur die Paketqueue ableiten. Abgeschlossene
  Pakete tragen Datum und Status **abgeschlossen**.
- Aktuelle, statusabhängige Zentralreferenzen tragen einen `Stand:`.
  Historische Dokumente beginnen mit einem deutlich sichtbaren Hinweis und
  werden inhaltlich nicht auf einen heutigen Betriebsweg umgedeutet.
- Öffentliche ABI/API-Details stehen im zuständigen Architekturvertrag und in
  den inline dokumentierten Headern. Quickstarts und Statusseiten verlinken
  dorthin.
- Befehle werden in der normalen Ring-3-Shell und in beiden Image-Layouts
  geprüft; ein Rescue-Shell-Befehl allein wird nicht als reguläre Funktion
  dokumentiert.
- Hardwarebeobachtung, Hosttest, QEMU-/VMware-Gasttest und allgemeine
  Unterstützung bleiben sprachlich getrennte Evidenzstufen.
- Screenshots werden mit dem versionierten
  [QEMU-Capture](assets/screenshots/README.md) erzeugt, nur an fachlich
  passenden Stellen eingebunden und enthalten keine alleinige Status- oder
  ABI-Aussage.

## Einstieg und aktueller Stand

- [Projektübersicht](../README.md) – Schnellstart, Funktionen und Grenzen
- [Quickstart](development/QUICKSTART.md) – nativer Windows-Build und erster Start
- [Projektstatus](development/PROJECT_STATUS.md) – verifizierte Komponenten und offene Grenzen
- [Fehlstellenanalyse und Roadmap](development/OS_GAP_ANALYSIS_AND_ROADMAP.md) – priorisierte Implementierungspakete mit Abhängigkeiten und Abnahmekriterien
- [REIST High-Assurance Core Contract](architecture/HIGH_ASSURANCE_CORE_CONTRACT.md) – verbindliche, branchenunabhängige Regeln für Fehlerbegrenzung, Recovery und Nachweisführung
- [Resilienz- und Degradierungsvertrag](architecture/RESILIENCE_AND_DEGRADATION_CONTRACT.md) – globales Stabilitätsversprechen, Restartbudgets und terminale Systemzustände
- [Ring-3-Treibermodell](architecture/USERSPACE_DRIVER_MODEL.md) – Gerätebesitz, IRQ-, MMIO-/PIO- und DMA-Grenzen für neu startbare Treiber
- [Audiosubsystem](architecture/AUDIO_SUBSYSTEM.md) – Ring-3-HDA-Treiber,
  PCM-Service, `libreistaudio` und vollständig vermitteltes DMA
- [Medical Reference Profile](architecture/MEDICAL_HIGH_ASSURANCE_CONTRACT.md) – optionale medizinische Verschärfung; keine klinische Freigabe
- [REIST-Zielarchitektur](architecture/REIST_ARCHITECTURE.md) – Detect, Contain, Recover, Validate und Reintegrate als technisches Systemmodell
- [Build-Modi](development/BUILD_MODES.md) – `qemu`, `vmware`, `real_hw` und Videoauswahl
- [Nativer Bootdatenträger](development/BOOTABLE_DISK.md) – BIOS/MBR, Stage 2 und FAT32-Image
- [Bootfähige Diskette](development/FLOPPY_BOOT.md) – 1,44-MB-CHS-Image für echte BIOS-PCs
- [Externe Programme](development/USER_PROGRAM_TOOLCHAIN.md) – SDK, ABI und MYPR-Toolchain
- [Userspace-SDK und Portabilität](architecture/USERSPACE_SDK_AND_PORTABILITY.md) – modulare Bibliotheken, Upstream-Toolchain und API-Dokumentationsvertrag
- [GUI-Komponenten, Controls und Dialoge](architecture/GUI_CONTROLS_AND_DIALOGS.md) – unterstützte UI-Bausteine, Dialogstandard und schrittweise Control-Roadmap
- [GUI-Quellbaum und installierte Anwendungen](../userspace/gui/README.md) –
  Compositor, Surface-Clients, SDK-Beispiele und kanonische Imagepfade
- [Image-Library und Bildbetrachter](architecture/IMAGE_SUBSYSTEM.md) –
  wiederverwendbare BMP-/GIF-Decoder und der windowed Surface-Client
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
- [PCI-Audio-Arbeitspaket](development/PCI_AUDIO_WORK_PACKAGE.md) –
  abgeschlossenes Implementierungsprotokoll der ersten HDA-Wiedergabe
- [Fertige VMware-VM](hardware/VMWARE.md) – VMX/VMDK, LAN-Bridge und Fehlerdiagnose
- [Netzwerkstack](networking/NETWORK.md) – Ethernet-Treiber, DHCP, ARP,
  IPv4/ICMP, UDP, DNS, TCP und HTTP/1.0
- [TAP-Netzwerk](networking/TAP_NETWORKING.md) – optionaler Linux/QEMU-Testweg
- [USB-Design](hardware/USB_DESIGN.md) – begrenzter xHCI-/HID-Stand und
  offene Geräteklassen
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

Abgeschlossene Arbeitspakete wie
`development/RUNTIME_GRAPHICS_DESKTOP_WORK_PACKAGE.md`,
`development/USERSPACE_DRIVER_DOMAIN_WORK_PACKAGE.md` und
`development/PCI_AUDIO_WORK_PACKAGE.md` dokumentieren Scope und Abnahme eines
bestimmten Entwicklungsstands. Für den heutigen Betriebsweg gelten die oben
verlinkten Referenzdokumente.
