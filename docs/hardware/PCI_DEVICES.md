# PCI-Geräte und Treiberstatus

Stand: 20. August 2026.

Dieses Dokument trennt drei Aussagen, die bei PCI nicht verwechselt werden
dürfen:

- **enumeriert**: Vendor-ID, Device-ID, Klasse, BARs und Legacy-IRQ wurden in
  der festen PCI-Tabelle erfasst;
- **Treiber vorhanden**: ein Backend kennt die konkrete
  Programmierschnittstelle;
- **betriebsbereit**: Probe, Ressourcenprüfung und Initialisierung waren auf
  dem laufenden System erfolgreich.

Eine passende PCI-Klasse allein bedeutet niemals, dass ein Gerät unterstützt
wird. Der Kernel übergibt weder I/O-Ports noch MMIO- oder DMA-Adressen an Ring
3. Treiber binden nach Klasse/Programming Interface oder exakter Geräte-ID und
publizieren ein Gerät erst nach vollständiger Validierung.

## Übliche PCI-Klassen

| Klasse | Unterklasse / Prog-IF | Üblicher Gerätetyp | REIST-Status |
|---|---|---|---|
| `01` Mass Storage | `01` IDE | PCI-IDE/ATA | unterstützt über ATA-Backend |
| `01` Mass Storage | `06/01` SATA/AHCI | SATA-Controller | unterstützt |
| `01` Mass Storage | `08/02` NVM Express | NVMe-SSD | nicht unterstützt |
| `02` Network | `00` Ethernet | PCI/PCIe-Netzwerkkarte | ausgewählte IDs unterstützt |
| `03` Display | `00` VGA | VGA-kompatible Grafikkarte | VGA-Text; ausgewählte Runtime-Grafikbackends |
| `04` Multimedia | `01` Audio | AC'97/ältere PCI-Audiokarte | enumeriert, kein Backend |
| `04` Multimedia | `03/00` HDA | Intel High Definition Audio | Ring-3-Backend; QEMU-Wiedergabe nachgewiesen |
| `06` Bridge | diverse | Host-, PCI-, ISA- und PCIe-Bridges | enumeriert; Firmwarekonfiguration wird beibehalten |
| `0C` Serial Bus | `03/00` UHCI | USB 1.x | erkannt, nicht unterstützt |
| `0C` Serial Bus | `03/10` OHCI | USB 1.x | erkannt, nicht unterstützt |
| `0C` Serial Bus | `03/20` EHCI | USB 2.0 | erkannt, nicht unterstützt |
| `0C` Serial Bus | `03/30` xHCI | USB 3.x | begrenztes HID-Tastatur-/Mausbackend |
| `0C` Serial Bus | `05` SMBus | Sensoren/Boardverwaltung | enumeriert, kein Backend |

Andere häufige Klassen wie FireWire (`0C:00`), SD Host (`08:05`),
Cryptographic Controller (`10`) und Processing Accelerator (`12`) werden nur
enumeriert. Dafür existiert derzeit keine öffentliche Geräte-ABI.

## Gegenwärtig gebundene Geräte

### Storage

| Auswahl | Backend | Umfang |
|---|---|---|
| Klasse `01:01` | `drivers/block/ata.c` | PCI-IDE-Kanäle, ATA PIO, Cache-Flush |
| Klasse `01:06:01` | `drivers/block/ahci.c` | AHCI/SATA, validierte Ports und DMA-Strukturen |

NVMe, RAID-spezifische Controllerinterfaces und SAS werden nicht unterstützt.
Ein Controller im RAID-Modus (`01:04`) ist nicht automatisch AHCI-kompatibel.

### Netzwerk

| PCI-ID | Gerät | Status |
|---|---|---|
| `8086:100E` | Intel 82540EM | E1000-Treiber; QEMU-Referenz |
| `8086:100F` | Intel 82545EM | E1000-Treiber; VMware-Referenz |
| `10EC:8139` | Realtek RTL8139 | Treiber vorhanden und in QEMU geprüft |
| `10EC:8168` | Realtek RTL8168/RTL8111G | Treiber vorhanden; reale Zielhardware |
| `10EC:8029` | NE2000-kompatibel | Legacy-PCI-Backend vorhanden |
| `15AD:07B0` | VMware VMXNET3 | unvollständiger Quellcode, nicht im Bootpfad registriert |

Nur die ersten fünf Einträge gelten als gebundene Backends. Das Vorhandensein
von `vmxnet3.c` ist ausdrücklich keine Unterstützungszusage.

### Anzeige

| PCI-ID / Auswahl | Backend | Status |
|---|---|---|
| `1234:1111`, Klasse `03:00` | QEMU Standard VGA/DISPI | Runtime-Grafik unterstützt |
| `15AD:0405` | VMware SVGA II | Runtime-Grafik unterstützt |
| `15AD:0710` | VMware Legacy SVGA | Runtime-Grafik unterstützt |
| `10DE:1280`, Klasse `03:00` | NVIDIA GK208 / GeForce GT 635 | nativer, passiver Ring-3-Bring-up; VBE-Scanout, Beschleunigung noch gesperrt |
| andere Klasse `03` | VBE-Kompatibilität | nur mit von Stage 2 validierter und versiegelter VBE-Information |

Für `10DE:1280` existiert jetzt eine exakte, überwachte native
GK208-Bring-up-Domäne. Sie liest über einen festen Kernelmediator nur PMC,
PTIMER, PFIFO und PGRAPH zur Identitäts- und Zustandsprüfung. Der sichtbare
Scanout bleibt vorerst der explizite Legacy-BIOS-VBE-Pfad; der Treiber meldet
keine `RECT_FILL`-/`RECT_COPY`-Capability, bevor ein echter GPFIFO-Fence auf
der Zielkarte bestätigt ist. Andere NVIDIA-, AMD- oder Intel-GPUs besitzen
weiterhin keinen nativen Treiber; 3D-Beschleunigung wird nicht behauptet.

### USB

| Auswahl | Backend | Status |
|---|---|---|
| Klasse `0C:03:30` | xHCI | begrenzte USB-HID-Tastatur-/Mausunterstützung |
| Intel-xHCI mit EHCI-Begleitcontroller | xHCI-Routing | begrenzter, boardspezifisch validierter Übergang |
| UHCI/OHCI/EHCI | keines | nur Diagnose, keine End-to-End-Unterstützung |

USB-Massenspeicher, USB-Audio, Hubs mit beliebiger Tiefe und isochrone
Transfers sind noch nicht freigegeben.

### Audio

PCI-Audio wird durch Paket `R1.7-pci-audio` eingeführt. Die erste
Unterstützungsgrenze ist HDA-Klasse `04:03:00` mit validiertem MMIO-BAR,
Controller-Reset, Codec-Erkennung und einem begrenzten PCM-Wiedergabestream.
QEMUs `intel-hda` mit `hda-output` erzeugt über den vollständigen Gastpfad eine
validierte, nicht stumme Stereo-S16-Aufzeichnung. VMware ist mit virtuellem
`hdaudio` konfiguriert. Die hörbare Ausgabe und der erwartete Pegel wurden mit
der paketierten 440-Hz-WAV-Datei manuell bestätigt. Der Treiber aktiviert dafür
den begrenzt ermittelten Standardpfad `DAC -> Mixer/Selector -> Ausgangspin`.
Reale Codec-/Pinvarianten auf dem ASUS-Board sind weiterhin kein
abgeschlossener Hardware-Nachweis.

HDA-Treiber und PCM-Service laufen in getrennten überwachten Ring-3-Domänen.
Der Kernel konstruiert die adresshaltige BDL aus geprüften DMA-Tokens und gibt
Bus-Mastering erst nach vollständiger Bindung frei. Anwendungen greifen über
`libreistaudio` auf den Service zu und erhalten keine Gerätecapability.

AC'97 (`04:01`), Ensoniq ES1370/ES1371, Sound Blaster, USB Audio Class und
HDMI/DisplayPort-Audio benötigen jeweils eigene Backends. Sie können später
dieselbe Userspace-PCM-Bibliothek verwenden, sind aber nicht
registerkompatibel.

## Diagnose und Erweiterung

Die PCI-Enumeration ist fest auf 256 Busse, 32 Slots und acht Funktionen sowie
maximal 1024 veröffentlichte Funktionen begrenzt. Ein neuer Treiber muss:

1. die konkrete Geräteidentität und alle benötigten BAR-Typen prüfen;
2. MMIO-/I/O-Größe, IRQ und DMA-Adressierbarkeit vor Bus-Mastering validieren;
3. den Interrupt-Handler vor Aktivierung des Geräts installieren;
4. alle Reset- und Ready-Waits begrenzen;
5. bei jedem Fehler Interruptquellen abschalten und Bus-Mastering zurücknehmen;
6. Ring 3 ausschließlich eine versionierte, mediierte Geräte-ABI anbieten;
7. mit Hosttests und einem echten Emulator- oder Hardwarelauf nachgewiesen
   werden.

Der historische Kernelbefehl `PCI` gehört nur zur Rescue-Diagnose. Eine neue
reguläre Diagnose muss zusätzlich als `.PRG` über `/bin/shell.prg` erreichbar
und in Makefile- und Windows-Systemimages identisch paketiert sein.
