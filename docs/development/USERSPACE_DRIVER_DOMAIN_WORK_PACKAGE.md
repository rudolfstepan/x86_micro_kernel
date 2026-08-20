# Ring-3 device-domain foundation work package

Stand: 20. August 2026

Status: abgeschlossenes Arbeitspaket; normativer Vertrag in
[`../architecture/USERSPACE_DRIVER_MODEL.md`](../architecture/USERSPACE_DRIVER_MODEL.md)

Dieses Arbeitspaket schafft die generische, geräteunabhängige Voraussetzung
für PCI-Audio und alle späteren Ring-3-Treiber. Es implementiert noch keinen
HDA-Zustandsautomaten.

## Umsetzung

- [x] Vorhandene PCI-, Prozess-, Capability-, IPC-, IRQ-, Paging- und
  Supervisorpfade vollständig inventarisieren.
- [x] Feste PCI-Isolationsgruppen und unveränderliche Geräteprofile definieren.
- [x] Generationgebundenes exklusives Claim/Release mit idempotentem Cleanup
  implementieren.
- [x] Versionierte Region-, IRQ- und DMA-Objekte als getrennte, unabhängig
  generationgebundene Capabilities definieren; eine Regionsbeschreibung
  erzeugt noch kein MMIO-Mapping.
- [x] Bus Mastering bis nach erfolgreicher IRQ- und DMA-Bindung sperren.
- [x] ACPI-DMAR-Inventarisierung mit Prüfsummen und festen Grenzen
  implementieren; Tabellenfund, aktive Translation und sichere Direktzuweisung
  als getrennte Zustände führen.
- [x] DMAR-Fund strikt von aktiver IOMMU-Translation trennen und direkte
  DMA-fähige Geräteabbildung bis zu einem späteren, nachgewiesenen
  Translationstreiber fail-closed ablehnen.
- [x] Bounded IRQ-Notification mit Mask/Acknowledge/Complete und Deadline
  implementieren.
- [x] Kernel-eigene, feste mediated-DMA-Pools mit Richtung, Quote, Nullung und
  tokengebundener Adresspublikation bereitstellen.
- [x] PIO/MMIO nur über unveränderliche Lesefenster und exakte, maskierte
  Schreibregeln vermitteln; keine BAR-Abbildung nach Ring 3 veröffentlichen.
- [x] Driver-Host-Startprofil, Restartreserve, Fencing, Reset, Selbsttest und
  Reintegration in den Supervisor integrieren.
- [x] Prozessgenerationsgebundene Recovery-Transaktion bereitstellen, die erst
  nach vollständigem Fence und Reset alle Handles atomar invalidiert.
- [x] Manuelles `svcctl restart` auf denselben Zustandsautomaten abbilden.
- [x] Hosttests für Handles, Gruppenbesitz, Teilinitialisierungs-Cleanup und
  Quoten ergänzen.
- [x] QEMU-Fault-Injection für Crash, Hang, stale IRQ, fehlgeschlagenen Reset
  und erschöpftes Restartbudget ergänzen.
- [x] Erst nach bestandenem Paket das Ring-3-PCI-Audio-Paket aktivieren.

## Nicht zulässig

- gerätespezifische Zustandsautomaten oder Protokollparser in Ring 0,
- direkter IOPL-Zugriff eines Userspace-Prozesses,
- direkte DMA-fähige MMIO-Zuweisung ohne nachgewiesene IOMMU-Gruppe,
- globale BDF-IDs als Autorisierung,
- Wiederverwendung alter Handles nach Restart,
- unbeschränkte IRQ-Queues, Pinning, Pollschleifen oder Neustartversuche.
