# REIST Ring-3 device-driver model

Stand: Architekturvertrag, 20. August 2026

Dieses Dokument definiert die allgemeine Grenze zwischen dem REIST-Microkernel
und neu startbaren Gerätetreibern. Es ist Voraussetzung für PCI-Audio und alle
weiteren Treibermigrationen. Der Vertrag übernimmt bewährte Konzepte, aber
behauptet keine Linux-, seL4- oder Fuchsia-Binärkompatibilität.

## Referenzmodelle

REIST orientiert sich an drei etablierten Mustern:

- Linux [VFIO](https://docs.kernel.org/driver-api/vfio.html): ein isolierbarer
  IOMMU-Geräteverbund ist die kleinste sichere Besitz- und DMA-Einheit;
  Gerätezugriff umfasst beschriebene Regionen, IRQ-Zustellung und DMA-Bindung.
- Linux [IOMMUFD](https://docs.kernel.org/userspace-api/iommufd.html): der
  DMA-Adressraum ist ein eigenes Objekt und muss vor vollem Gerätezugriff
  gebunden sein; versionierte Strukturen tragen Größe und Flags.
- Fuchsia
  [Driver Framework](https://fuchsia.dev/fuchsia-src/concepts/drivers/driver_framework):
  der Driver Host bildet die Adressraum- und Restartgrenze; gemeinsam gehostete
  Treiber sind keine getrennten Fehlerdomänen.

REIST übernimmt daraus Gerätebesitz, getrennte DMA-Adressräume, deklarierte
Regionen, nachrichtenbasierte IRQ-Zustellung und explizite Driver-Host-
Lebenszyklen. Linux-Dateideskriptoren, `ioctl`, `eventfd`, dynamisches Sysfs und
unbegrenztes Pinning werden nicht kopiert. REIST verwendet stattdessen
Capabilities, feste Tabellen, versionierte IPC und monotone Deadlines.

## Komponenten

```text
System Supervisor
├── Device-resource mediator (Ring 0 mechanism)
├── Driver host: one PCI isolation group (Ring 3)
│   └── Device-specific driver state machine
└── Service process (Ring 3)
    └── Application-facing policy and protocol
```

Ein Driver Host enthält standardmäßig genau einen Treiber für genau eine
nachweisbar isolierbare PCI-Gruppe. Mehrere Treiber im selben Adressraum teilen
eine Fehlerdomäne und sind nur nach expliziter Common-Cause-Bewertung zulässig.
Der anwendungsnahe Dienst ist vom Treiber getrennt. Anwendungen erhalten weder
Geräte- noch Driver-Host-Capabilities.

## Ring-0-Grenze

Der Microkernel darf ausschließlich generische Mechanismen enthalten:

- PCI-Funktion und Isolationgruppe inventarisieren,
- exklusiven, generationgebundenen Gerätebesitz vergeben,
- deklarierte PIO-/MMIO-Regionen beschreiben und begrenzen,
- IRQ maskieren, bestätigen und als bounded notification zustellen,
- DMA-Adressräume beziehungsweise vollständig vermittelte DMA-Pools verwalten,
- Bus Mastering erst nach gültiger DMA-Bindung freigeben,
- Gerät fence/reset ausführen und Besitz entziehen.

Controllerzustandsautomaten, Codecverben, Protokollparser, Policy, Mixing und
Dateiformate gehören nicht nach Ring 0. Eine gerätespezifische Tabelle erlaubter
Register und DMA-Bindungen ist Sicherheitsmetadatum des Mediators, kein
Gerätetreiber.

## Geräte- und DMA-Besitz

Die kleinste Besitzgrenze ist eine PCI-Isolationgruppe, nicht zwangsläufig eine
einzelne Funktion. Bridges, fehlendes PCIe ACS oder gemeinsame interne Pfade
können mehrere Funktionen in dieselbe Gruppe zwingen. Eine Gruppe hat zu jedem
Zeitpunkt höchstens einen DMA-Eigentümer.

Der Lebenszyklus lautet:

```text
DISCOVERED -> CLAIMED -> DMA_BOUND -> QUIESCED -> ACTIVE
                    \-> FENCED -> RESETTING -> QUIESCED
                                      \-> UNSUPPORTED
```

`CLAIMED` allein erlaubt kein Bus Mastering. `ACTIVE` ist erst nach gebundenem
DMA-Adressraum, registriertem IRQ-Endpunkt, Gerätereset und erfolgreichem
Treiber-Selbsttest erreichbar.

### Plattform mit IOMMU

Der Kernel erzeugt einen leeren I/O-Adressraum, bindet die vollständige
Isolationgruppe und veröffentlicht nur explizit gepinnte, ausgerichtete und
quotierte Bereiche. Unmap, Prozessende oder Generationwechsel invalidieren die
IOVA vor einer erneuten Gerätefreigabe.

### Plattform ohne IOMMU

Direkte DMA-fähige MMIO-Abbildung an Ring 3 ist unzulässig. Der Kernel behält
DMA-Puffer und alle adresshaltigen Register unter Kontrolle. Der Treiber kann
nur feste DMA-Objekte über Tokens referenzieren; der Mediator validiert
Richtung, Länge, Ausrichtung, Registerziel und Gerätegeneration. Ist das für
einen Controller nicht vollständig möglich, lautet der Zustand
`UNSUPPORTED`, nicht „unsicher unterstützt“.

Die aktuelle mediated-DMA-ABI stellt einen kernel-eigenen, beim Binden und
Entziehen vollständig genullten 64-KiB-Pool bereit. Ring 3 erhält weder dessen
physische Adresse noch eine BAR-Abbildung. Ein unveränderliches Geräteprofil
definiert stattdessen begrenzte Lesefenster, einzelne maskierte
Schreibregister und zulässige 64-Bit-DMA-Adressregister. Der Mediator löst erst
beim Schreiben ein generationgebundenes DMA-Token in die tatsächliche Adresse
auf. Fehler beim Registerzugriff fence'n das Gerät.

Diese Version deckt lineare DMA-Bereiche ab. Controller, die indirekte
Deskriptortabellen auswerten, bleiben so lange `UNSUPPORTED`, bis der Kernel
auch deren adresshaltige Einträge aus Tokens konstruiert und gegen spätere
Ring-3-Änderung versiegelt. Insbesondere darf HDA nicht mit frei beschreibbaren
BDL-Adressen aktiviert werden.

## Versionierte öffentliche Objekte

Jede Anfrage beginnt mit `version`, `struct_size` und `flags`. Erweiterungen
verwenden reservierte Felder oder neue angehängte Strukturen. Unbekannte Flags,
falsche Größen und nichtnullte Reserven scheitern vor jeder Zustandsänderung.

Der generische Vertrag verwendet Capability-Handles für:

- `device_group`: exklusive Isolationgruppe,
- `device`: Funktion innerhalb dieser Gruppe,
- `region`: beschriebener PIO-/MMIO-Bereich,
- `irq`: maskierbare Benachrichtigungsquelle,
- `dma_space`: IOMMU-Adressraum oder mediated-DMA-Pool.

Handles enthalten Objektindex und Generation. Sie sind nicht erratbare globale
PCI-Adressen und verleihen keine Rechte außerhalb des Startprofils.

## IRQ-Vertrag

Ein IRQ-Handler in Ring 0 führt nur begrenztes Acknowledge/Masking, EOI und die
Publikation einer festen Notification aus. Pro Quelle befindet sich höchstens
eine Notification in der Queue; weitere Flanken werden gezählt und
zusammengefasst. Der Driver Host verarbeitet den Geräteinterrupt in Taskkontext
und aktiviert die Quelle erst durch ein generationgeprüftes `irq_complete`.

Die aktuelle ABI setzt dafür eine monotone Abschlussfrist von 250 ms. Der
Hard-IRQ-Pfad maskiert ausschließlich die PIC-Leitung und setzt ein atomares
Pending-Bit. IPC-Zustellung, Capability-Prüfung und Fehlerbehandlung laufen im
begrenzten Supervisor-Worker und damit nie im Interruptkontext.

Eine nicht rechtzeitig abgeschlossene Notification führt zu Maskierung,
Fencing und Supervisor-Recovery. Alte IRQs einer beendeten Generation werden
nicht an den Nachfolger zugestellt.

## Crash- und Restartablauf

Bei Exception, Hang, ungültiger Antwort oder Quotenverletzung gilt:

```text
IRQ maskieren
-> Bus Mastering und Geräteausgaben sperren
-> Driver-/Service-Capabilities entziehen
-> DMA-Abbildungen invalidieren oder mediated DMA stoppen
-> alte Prozesse beenden
-> Gerät mit Gesamtdeadline zurücksetzen
-> Driver Host und Service aus Restartreserve neu erzeugen
-> Selbsttest und Abhängigkeiten prüfen
-> frische Generation veröffentlichen
```

Scheitert Fencing, Reset oder Selbsttest oder ist das Restartbudget erschöpft,
bleibt die Gruppe isoliert. Die Komponente erreicht ihre im
[Resilienz- und Degradierungsvertrag](RESILIENCE_AND_DEGRADATION_CONTRACT.md)
festgelegte terminale Stufe.

## Abnahme

Die Treiberdomänen-Grundlage gilt erst als implementiert, wenn Runtime-Tests
mindestens beweisen:

- exklusive Gruppen- und DMA-Eigentümerschaft,
- kein Bus Mastering vor gültiger DMA-Bindung,
- Abweisung fremder, alter und überlaufener Handles,
- begrenzte IRQ-Zusammenfassung und Maskierung bei Hang,
- idempotentes Cleanup nach jeder partiellen Claim-Stufe,
- Crash und Hang ohne Verlust unabhängigen Ring-3- oder Kernel-Fortschritts,
- erfolgreicher Neustart aus reservierter Kapazität,
- terminale Degradation nach erschöpftem Budget,
- Ablehnung direkten DMAs auf einem Profil ohne nachgewiesene Isolation.

Erst danach darf ein Gerätetreiber wie HDA auf diese Schnittstelle aufsetzen.
