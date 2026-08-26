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

Auch ein großer physischer BAR wird nicht automatisch zum vermittelten
Zugriffsbereich. Das unveränderliche Geräteprofil berechnet je Region die
kleinste Apertur aus Leselimit und exakten Schreibregeln; nur diese Apertur
wird vorbereitet und im generationgebundenen Ressourcenobjekt publiziert.
Sie ist zusätzlich auf 8 MiB begrenzt und muss vollständig in der physischen
BAR-Länge liegen. Das GK208-Profil nutzt seit `R2.2q` davon `0x5fa60c` Byte
read-only für die feste PMC/PTIMER/PFIFO/PGRAPH- und GPC-Topologieprobe und
weiterhin keine Schreibregel. Seit `R2.2j` darf dieses exakte Profil zusätzlich einen
kernelverwalteten mediated-DMA-Pool ausschließlich zum Staging binden. Ein
reines `MEDIATED_IO`-Profil wird beim DMA-Bind vor Ressourcenallokation mit
`ENOTSUP` abgewiesen. Ein Describe-only-Deskriptor, eine bekannte BAR-Basis
oder ein Pooltoken verleiht weiterhin weder Mapping-, physische Adress-,
Busmaster- noch Geräteaktivierungsvollmacht.

Die aktuelle mediated-DMA-ABI stellt einen kernel-eigenen, beim Binden und
Entziehen vollständig genullten Pool bereit. Der Standard bleibt exakt 64 KiB.
Ein unveränderliches Profil darf zusätzlich den festen 512-KiB-Pool wählen,
aber nur gemeinsam mit ausdrücklicher `MEDIATED_DMA`-Autorität; derzeit nutzt
dies ausschließlich GK208 für seine bounded GPU-VM-Images. Die vier
Allokationsslots und der standardisierte `pool_bytes`-Diagnosewert bleiben
unverändert, während `DMA_INFO.capacity` die tatsächlich gewählte Kapazität
meldet. Ring 3 erhält weder dessen
physische Adresse noch eine BAR-Abbildung. Ein unveränderliches Geräteprofil
definiert stattdessen begrenzte Lesefenster, einzelne maskierte
Schreibregister und zulässige 64-Bit-DMA-Adressregister. Der Mediator löst erst
beim Schreiben ein generationgebundenes DMA-Token in die tatsächliche Adresse
auf. Fehler beim Registerzugriff fence'n das Gerät.

Die mediated-DMA-ABI reserviert die ersten 4 KiB des Pools als ausschließlich
kernelbeschreibbaren Deskriptorbereich. Ring 3 benennt für einen Eintrag nur
Datenoffset, Länge und erlaubte Flags. Der Mediator validiert Bereich,
Ausrichtung, Anzahl und Generation, konstruiert daraus einen versiegelten
16-Byte-Deskriptor mit physischer Adresse und veröffentlicht ausschließlich
dessen DMA-Token. Der übrige Pool bleibt der beschreibbare Datenbereich.

Device-Control-Kommando 19 ergänzt einen atomaren Relokations- und
Versiegelungsschritt für Gerätebilder, die physische Adressen enthalten
müssen. Vor dem ersten Claim installiert das unveränderliche Geräteprofil
höchstens zwei feste Templates mit je höchstens acht Regeln. Ring 3 muss eine
Regelmenge bytegenau wiederholen; der Kernel prüft Policy-ID, Anzahl,
Ausrichtung, Bereiche, Shift, feste Bits, eindeutige Ziele, unbenutzte Felder
und ausschließlich nullinitialisierte Zielwörter vollständig vor dem ersten
Schreiben. Erst danach löst er die kernel-eigenen Pooladressen auf und schreibt
alle 64-Bit-Werte. Erfolg sperrt weitere Ring-3-Reads, Writes und
Deskriptoränderungen dieses Pools. Generationgebundene Freigabe nullt den
gesamten gewählten Pool und entfernt damit auch die Versiegelung. Weder der
Syscall-Rückgabewert noch eine öffentliche Struktur enthält eine physische
Adresse oder den versiegelten Inhalt.

Device-Control-Kommando 20 verbindet einen solchen versiegelten Pool mit genau
einer vor dem Claim installierten, gerätespezifischen Page-Mode-Policy. Der
Aufruf benennt Geräte-, read-only Region- und DMA-Handle derselben
Eigentümergeneration sowie nur die Policy-ID. Ring 0 führt den einzigen
maskierten 32-Bit-Read-Modify-Write aus, prüft Zielbits und alle unveränderten
Bits und gibt weder Registerinhalt noch Schreibrecht zurück. Vor jedem Fence
wird zuerst Bus Mastering deaktiviert; danach stellt der Mediator nur die
ursprünglichen Policy-Bits wieder her, bewahrt die übrigen aktuellen Bits und
prüft das Readback. Eine nicht bestätigte Wiederherstellung bleibt `FENCED`
und wird beim nächsten Fence-/Release-Versuch erneut ausgeführt. Erst eine
bestätigte Wiederherstellung erlaubt die nächste Gerätegeneration.

Device-Control-Kommando 21 erweitert diesen geordneten Vertrag um einen
hardwarewirksamen, aber noch nicht ausführenden Falcon-Upload. Ein vor dem
Claim unveränderlich installiertes Profil bindet genau vier nicht
überlappende Poolbereiche, deren Wortzahlen und CRC32-Werte, zwei Falcon-
Basen sowie genau ein GR-Reset-Bit. Gerät, read-only Region und versiegelter
DMA-Pool müssen derselben Generation gehören; Kommando 20 muss zuvor
erfolgreich gewesen sein. Ring 0 prüft alle vier CRCs vor dem ersten
Registerzugriff, toggelt ausschließlich das erhaltene GR-Bit, wartet mit
monotoner 100-ms-Grenze auf beendetes IMEM-/DMEM-Scrubbing und überträgt die
DMEM-/IMEM-Wörter samt 256-Byte-IMEM-Tags. Jedes Wort wird per PIO
zurückgelesen. FECS und GPCCS werden dabei nicht gestartet. Fehler und Fence
deaktivieren zuerst Bus Mastering, resetten den GR-Zustand und stellen erst
danach den Page-Mode wieder her. Nicht bestätigte Schritte bleiben gesperrt
und wiederholbar; Ring 3 erhält weder MMIO-Schreibrecht noch Firmware- oder
physische Adressen.

Device-Control-Kommando 22 liegt beim exakten GK208-Profil zwingend vor den
Kommandos 20 und 21. Es akzeptiert nur dieselbe Gerätegeneration, die
read-only BAR0-Region und den bereits versiegelten großen DMA-Pool. Ring 0
validiert Header, belegten Präfix, CRC, zweimal gelesene Live-Topologie,
semantische Operationsgrenzen und genau zwei ungelöste 128-KiB-Faultbuffer.
Danach leitet der Mediator aus festen FB-/LTC-Registern eine begrenzte VRAM-
und Tag-RAM-Geometrie ab und merkt sich nicht überlappende Bereiche hinter dem
sichtbaren VBE-Scanout. Weder BAR-Basen noch VRAM-Offsets werden an Ring 3
gegeben. Diese generationgebundene Reservierung verändert keine Hardware und
wird beim Fence vollständig verworfen; ein Profil ohne Kommando-22-Policy
behält die bisherige ABI-Semantik.

Append-only Device-Control-Kommando 23 ist die einzige hardwareaktive
LTC-/GR-Pre-Start-Transaktion dieses Profils. Es ist erst nach den Kommandos
22, 20 und 21 zulässig und wiederholt vor dem ersten Schreibzugriff die
vollständige Image-, Topologie- und VRAM-/LTC-Planprüfung. Der Kernel nullt nur
die zwei reservierten 128-KiB-Faultbuffer über zugeschnittene VRAM-Fenster,
initialisiert LTC- und CBC-Tag-Zustand und interpretiert danach jede typisierte
Operation in fester Reihenfolge. Kontextgruppen müssen ihre unmittelbar
folgenden Transfers vollständig verbrauchen; Idle-, CBC- und FECS-Waits sind
durch operationseigene Grenzen und eine gemeinsame monotone 5-s-Deadline
begrenzt und geben zwischen Abfragen den Prozessor frei. Erfolg liefert nur
Operationszahl, nichtnull Kontextgröße und ein Readiness-Bit. Jeder Teilfehler
setzt GR zurück, bevor ein Wiederholungsversuch oder Fence zulässig ist;
Adressen, Busmaster-, IRQ-, Channel-, Runlist-, USERD- oder Submission-
Autorität werden nicht freigegeben.

Append-only Device-Control-Kommando 24 reserviert danach ausschließlich den
Speicherplan für genau einen GK208-GR-Kanal. Der Kernel prüft erneut dieselbe
Generation, versiegelte Ausführungsabbildung, zweimal stabile Topologie und den
erfolgreichen Kommando-23-Zustand. Pagepool (32 KiB), Bundle-Puffer (12 KiB),
der topologieabhängige Attributpuffer und der temporäre Golden-Context-Bereich
mit 512-KiB-CB-Reserve werden unabhängig von Ring 3 berechnet und ausgerichtet
hinter dem vorhandenen Tag-Bereich eingeplant. Der Aufruf schreibt weder VRAM
noch MMIO und gibt keine Adresse zurück; sichtbar sind nur Größen, Topologie-
CRC und Readiness. Fence oder Generationswechsel verwerfen den opaken Plan erst
nach dem vorhandenen GR-Reset.

Der anschließende hardwareinaktive `R2.2v`-Vertrag ergänzt die vollständigen,
gepinnten Golden-Context-Eingaben: 245 ICMD-Tupel, 311 klassengebundene
Methodentupel, feste CRC32-Werte, vier opake VRAM-zu-GPU-VA-Spannen und eine
maximal 96 Einträge große Patchliste. Für die maximal 32 GPCs werden tatsächlich
höchstens 80 Patches erzeugt. Alle Abbildungen liegen zusammenhängend im
vorhandenen 128-MiB-Small-Page-Table-Fenster; Ring 3 benennt nur Puffertyp,
virtuelle Adresse, PTE-Index und Seitenzahl, niemals eine physische Adresse
oder einen PTE-Wert. Zwölf geordnete Phasen machen FE-Power/Reset, SCC,
Kontextpacks, Idle-Grenzen, globale Patches, Floorsweep, ICMD, Methoden,
Post-Context, FECS-Bind, Golden-Save und Retain explizit. Dieser Compiler hat
keinen Hardwarezugriff. Erst ein nachfolgendes Kernelkommando darf den Plan
unabhängig rekonstruieren und unter einer gemeinsamen Deadline ausführen.

Append-only Device-Control-Kommando 25 führt diesen Plan jetzt als eine
kernelvermittelte Transaktion aus. Ring 0 prüft seine eigenen unveränderlichen
Kopien der 199 Kontext-, 245 ICMD- und 311 klassengebundenen Methodentupel per
CRC32, legt Instance, 64-KiB-PGD und 256-KiB-Small-PGT ausschließlich im opaken
VRAM an und erzeugt nur VRAM-PTEs. Alle direkten Kontext-, Floorsweep-, ICMD-,
Methoden- und FECS-Schritte teilen dieselbe monotone Fünf-Sekunden-Deadline.
Bei Erfolg werden Bindung und temporäre PDE/PTEs entfernt und nur das
gespeicherte Kontextabbild behalten; jeder Teilfehler läuft vor einer erneuten
Freigabe durch den vorhandenen GR-Reset-Rollback. Das Resultat enthält Größen,
Zähler und Kontext-CRC, jedoch keinerlei Adresse. Busmaster, IRQ, Channel,
Runlist, USERD, Submission, Fence und Capability bleiben gesperrt.

Device-Control-Kommando 18 stellt der bereits autorisierten Treiberdomäne eine
aggregierte 32-Byte-v1-Diagnose bereit. Sie enthält aktuelle und maximale
Belegung der vier Pools, Slotkapazität, Standard-Poolgröße und einen saturierenden
Zähler echter Kapazitätsablehnungen. Physische Adressen, Eigentümer und
generationgebundene Pooltokens bleiben verborgen. Der Zähler ist beobachtbar,
beeinflusst aber weder Zuteilung noch Bus-Mastering-Autorität.
Der QEMU-HDA-Nachweis komprimiert Aktiv-, Peak- und Kapazitätswert sowie die
unteren zwölf Bits des Ablehnungszählers in einen markierten 32-Bit-Wert und
meldet ihn über den bestehenden generationsgebundenen Supervisor-Diagnosekanal;
das Default-Deny-Treiberprofil erhält dafür keinen Terminal-Syscall.

Damit kann das HDA-Profil genau eine geprüfte BDL-Zeile verwenden, ohne Ring 3
eine adresshaltige Tabelle zu überlassen. `ACTIVE` sperrt weitere DMA-Writes;
ein explizites Deaktivieren maskiert IRQ und Bus-Mastering und führt zurück zu
`DMA_BOUND`, bevor der Datenbereich erneut befüllt werden darf. Andere
indirekte Controllerformate bleiben `UNSUPPORTED`, bis ihr eigenes
unveränderliches Profil dieselbe Konstruktion vollständig beschreibt.

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
