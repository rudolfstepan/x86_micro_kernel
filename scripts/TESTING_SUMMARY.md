# Testabdeckung

Stand: 20. August 2026.

## Host- und Strukturtests

- MBR, Manifest, Stage 2, ELF32, CRC32, FAT32-Image, VMDK und SATA-VMX
- FAT12-Journal, Remap, kritische Replikate und geordnete Transaktionen
- FAT32-Datei-/Verzeichnis-I/O, Journal, Rename, Replace und Transportwahl
- EXT2-Partitionen, Verzeichnisse und indirekte Blöcke
- VFS-Mountregeln, deterministische Systemvolume-Auswahl und DOS-Pfade
- ATA/PCI-IDE, AHCI, Partitionen, Blocktransaktionen und Storage-Safety
- Ring-3-MYPR-ABI, Syscalls, Userpointer, Prozesse, IPC und Dienste
- PS/2-Scan-Set-2, NumLock und Ausschluss serieller Tastaturinjektion
- Netzwerkparser, Capabilities, UDP-Bindings und DHCP-Zustände
- Fatalpfad, Panic-Kontext, Crashrecord, Watchdog und Handoverprotokolle
- xHCI-HID-Tastatur/-Maus, Ring-/TRB-Regeln und sichere VMware-HID-Konfiguration
- Window Manager, Explorer, Surface-Lifecycle, Notepad, GUI-Controls und Dialoge
- HDA-Geräteprofil, Audio-SDK/WAV-Loader sowie BMP-/GIF-Decoder und Image Viewer

## Gasttests

- nativer BIOS-/MBR-Boot bis Ring-3-Shell und `TEST_OK`
- ATA/IDE-Regression und expliziter AHCI/SATA-Boot
- FAT32-Datei-I/O über eine AHCI-Partition bis `FILE_IO_OK`
- überwachte Ring-3-Probe mit Crash-, Hang- und Invalid-Reply-Recovery
- Storage-Recovery und kontrollierter I/O-Fehler
- FDD-Auswurf, Quarantäne, Wiedereinlegen und Requalifizierung
- PS/2-NumLock und Texteingabe über QEMU-`sendkey`
- Netzwerk-, DHCP-, UDP-, PIT-, Memory-, Watchdog- und Handoverprofile
- VBE-/Runtime-Grafik, Desktop-Rendermetriken sowie sichtbare Notepad- und
  Image-Viewer-Surface-Clients
- PCI-HDA mit aufgezeichneter, validierter und nicht stummer Stereoausgabe
- aktives/passives TCP, DNS-A und zwölf HTTP/1.0-Testverbindungen

## Manuell beobachtet

- VMware-AHCI-Boot, FAT32-Mount und Userspace-Shell
- physischer Legacy-BIOS-Boot von SATA-Hardware
- reale PS/2-Tastatureingabe einschließlich NumLock
- VMware-Desktop mit Maus, Drag/Resize, Notepad- und Image-Viewer-Surfaces
  einschließlich des korrigierten Resize-Clippings
- hörbare VMware-HDA-Wiedergabe der paketierten 440-Hz-WAV-Datei mit
  bestätigtem Pegel
- USB-Maus und einfache USB-Boot-Tastatur auf dem ASUS H81M-K; das
  AULA/BY-Tech-Composite-Keyboard bleibt ausgenommen

Manuelle Beobachtung ist wertvolle Hardwareevidenz, aber keine breite
Kompatibilitäts- oder Langzeitqualifikation. Der jüngste AHCI-Partition-Batch-
Fix ist automatisiert in QEMU verifiziert und muss auf den jeweiligen realen
Zielplatten erneut bestätigt werden.

## Nicht nachgewiesen

- formale Vollständigkeit oder Zertifizierung
- SMP-/Mehrkern- und umfassende Race-Abdeckung
- reale Power-Loss-Matrix für jede FAT12-Persistenzstufe
- unabhängige elektrische Supervisor-/Fence-/Failover-Domäne
- breite BIOS-, AHCI-, PCI-IDE-, PS/2- und Medienmatrix
- allgemeiner USB/xHCI-/Composite-HID-/Mass-Storage-/Hotplugpfad
- UEFI, Secure Boot, NVMe, IOMMU und allgemeine DMA-Isolation
- IPv6, TLS/HTTPS, vollständige POSIX-Sockets und allgemeine
  Internet-Anwendungen

Die tatsächlich ausgeführte Testanzahl wird nicht eingefroren. Maßgeblich sind
die Paketqueue, das konkrete Gatekommando, dessen Rückgabecode und das
zugehörige Log unter `build/codex-agent/`.
