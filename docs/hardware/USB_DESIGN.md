# USB-/xHCI-Status und Design

Stand: 16. August 2026.

USB bleibt experimentell. Der Build nimmt `drivers/usb/` auf und probiert
PCI-USB-Controller, aber daraus folgt kein freigegebener End-to-End-Stack. Die
VMware-Referenz deaktiviert USB und verwendet PS/2-Eingabe sowie SATA/AHCI-
Storage. Ein physisches USB-FDD wird von VMware als klassischer FDC emuliert.

## Vorhandene Bausteine

- USB-Core-Strukturen und PCI-Probing
- xHCI-MMIO-/Controllergrundlagen
- Hub- und HID-Tastaturquellen
- Einbindung der USB-Quellen in den Kernelbuild

## Noch erforderlicher Vertrag

```text
PCI -> xHCI -> USB-Core -> Hub/Port -> Klasse -> HID oder Mass Storage
```

Erforderlich sind validierte MMIO-BARs, Bus Mastering, korrekt ausgerichtete
DMA-Strukturen, Cycle-Bit-Ringe, endliche Reset-/Transferdeadlines, sauberer
Disconnect, begrenzte Fehlerdiagnose und eine Blockgeräteanbindung ohne
Umgehung von Quarantäne, Fingerprint, Flush oder Write-Fencing.

## Verifikationsreihenfolge

1. Controller, BAR und Interruptweg eindeutig erkennen.
2. Reset und Ringinitialisierung mit Deadlines ausführen.
3. Root-Port und Deskriptoren lesen.
4. HID-Bootkeyboard getrennt vom verifizierten PS/2-Pfad testen.
5. Bulk-Only/SCSI-Mass-Storage an die Blockgeräteschicht anbinden.
6. VFS-Mount, I/O-Fehler, Abziehen und Reintegrationsregeln prüfen.

## Offene Grenzen

- kein stabil freigegebener xHCI-End-to-End-Pfad
- kein dokumentierter USB-Mass-Storage-Mount
- kein abgeschlossener Hotplug-/Disconnect-Lebenszyklus
- keine IOMMU-/DMA-Isolation
- keine EHCI/OHCI/UHCI-Kompatibilitätszusage

`drivers/usb/usb_recommendations.md` ist eine historische Entwurfsnotiz.
