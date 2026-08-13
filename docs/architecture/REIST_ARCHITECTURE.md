# REIST-OS-Zielarchitektur

Stand: 13. August 2026

**REIST OS** steht für **Resilient Execution, Isolation and Stability
Technology**. Das zentrale Architekturprinzip lautet:

```text
Detect -> Contain -> Recover -> Validate -> Reintegrate
```

Scheitert dieser Ablauf innerhalb des zulässigen Zeitbudgets:

```text
Degrade -> Safe State -> Controlled Restart
```

Das Ziel ist nicht die unbeweisbare Behauptung, dass das System niemals
abstürzt. Ein lokaler Fehler soll seine Fehlereindämmungsregion nicht verlassen.
Ist die Integrität des Kernels oder der Hardware nicht mehr vertrauenswürdig,
wird nicht innerhalb derselben Instanz weitergearbeitet. Ein unabhängiger
Supervisor übernimmt oder stellt den validierten sicheren Zustand her.

Dieses Dokument beschreibt die technische Zielarchitektur. Der aktuelle
32-Bit-Kernel ist noch ein modularer Monolith und erfüllt sie nicht. Die
verbindlichen Safety- und Nachweisregeln stehen im
[Medical-High-Assurance-Vertrag](MEDICAL_HIGH_ASSURANCE_CONTRACT.md).

## Minimaler REIST-Kern

Langfristig verbleiben nur Mechanismen mit globaler Schutzwirkung in Ring 0:

```text
REIST Kernel
├── Scheduling und Zeitisolation
├── virtuelle Speicherverwaltung
├── Interrupt- und Exception-Einstieg
├── versionierte IPC
├── Prozess- und Threadmechanismen
├── Capability- und Handleverwaltung
├── minimale Hardwareabstraktion
└── Recovery-, Fencing- und Watchdog-Primitiven
```

Dateisysteme, Netzwerk, GUI und komplexe Treiber werden schrittweise in eigene
Adressräume verlagert. Der Bootcode wird nach dem Handoff unzugänglich gemacht
oder verworfen. Paging und getrennte Kernel-/Userräume werden früh aktiviert;
NX, SMEP/SMAP, IOMMU und später CET werden nur auf Zielplattformen verwendet,
die diese Funktionen nachweislich besitzen. Für den aktuellen i386-Pfad sind
fehlende Hardwareeigenschaften explizite Grenzen, keine stillen Annahmen.

## Fehlereindämmungsregionen

Jede Anwendung, jeder Dienst und jeder Treiber besitzt:

- einen eigenen virtuellen Adressraum,
- Capabilities statt globaler Pointer oder frei erratbarer IDs,
- feste CPU-, Speicher-, Thread-, Handle-, Queue- und I/O-Quoten,
- einen versionierten Health-Zustand und eine definierte Lebensdauer,
- einen unabhängigen Restart- und Reintegrationsvertrag.

Ein Treiberabsturz führt damit zu `isolate -> revoke outputs -> cleanup ->
recreate -> reset device -> self-test -> reintegrate`, nicht zu einer
Kernel-Panic. DMA bleibt kernelvermittelt und wird, soweit verfügbar, durch
eine IOMMU auf die zugewiesene Domäne begrenzt.

## Supervisor-Hierarchie

```text
System Supervisor
├── Storage Supervisor
│   ├── Storage Driver
│   └── File-System Service
├── Network Supervisor
│   ├── NIC Driver
│   └── Network Stack
├── UI Supervisor
│   ├── Graphics Service
│   └── Shell/Desktop
└── Application Supervisor
```

Zulässige Health-Zustände sind `STARTING`, `HEALTHY`, `DEGRADED`,
`RECOVERING`, `FAILED`, `ISOLATED` und `SAFE_STATE`. `UNKNOWN` wird wie
`FAILED` behandelt. Heartbeats allein reichen nicht: Der Health Manager prüft
Fortschrittsmarken, Deadlines, Ressourcen, wiederholte Exceptions,
Speicherintegrität, Gerätefehler und den Zustand unabhängiger Watchdogs.

Recovery besitzt feste Versuchs- und Zeitbudgets. Erst nach Selbsttest,
Zustandsvalidierung und expliziter Freigabe darf eine Instanz erneut Ausgaben
autorisieren. Fencing und Epochen verhindern Split Brain und verspätete
Nachrichten einer alten Instanz.

Der erste Supervisor-Kern stellt dafür acht statische Domänenslots bereit. Sein
Zustand liegt im ECC-/Primary-Shadow-Umschlag. Eine verstrichene Deadline liefert
zunächst ausschließlich `FENCE_REQUIRED`; erst eine bestätigte Sperre erzeugt
`RESTART_REQUIRED`. Nach erschöpftem Restartbudget oder fehlgeschlagenem
Selbsttest folgt `SAFE_STATE_REQUIRED`. Handles enthalten Generation und Epoche,
sodass verspätete Fortschrittsmeldungen einer alten Instanz abgewiesen werden.

## Transaktionaler Zustand und Invarianten

Kritischer Zustand wird niemals direkt überschrieben:

```text
old -> candidate -> validate -> atomic commit -> new
```

Bei fehlgeschlagener Validierung wird der Kandidat verworfen und der letzte
gültige Zustand bleibt autoritativ. Persistent gespeicherter Recovery-Zustand
enthält Boot-Epoche, Clean-Shutdown-Kennzeichen, aktive Version, ausgeführte
Recoveryversuche, isolierte Komponenten und den letzten validierten Zustand.

Kernobjekte besitzen maschinell prüfbare Invarianten und erlaubte
Zustandsübergänge. Aktualisierungen werden vorbereitet, vollständig validiert
und erst danach atomar veröffentlicht. Ein Fehler zwischen Vorbereitung und
Commit darf für Leser keinen halben Zustand sichtbar machen.

### Software-ECC für kritische Objekte

Kleine kritische Metadaten können den generischen `critical_object`-Umschlag
verwenden: versionierte Nutzdaten, Sequenzzähler und Integritätszustand werden
wortweise durch SECDED geschützt und als Primary/Shadow gespeichert. CRC32
erkennt Mehrbitfehler außerhalb der SECDED-Garantie; ein objektspezifischer
Validator prüft anschließend semantische Invarianten.

```text
ECC -> CRC -> Version/Sequence -> Invariant -> Replica -> Recovery/Eskalation
```

Ein einzelner Bitfehler wird korrigiert und beide Kopien werden neu versiegelt.
Eine ungültige Kopie wird aus der validen rekonstruiert. Zwei ungültige oder
gleich alte, aber widersprüchliche gültige Kopien liefern ausschließlich
`UNCORRECTABLE`; es wird keine Autorität geraten. Primary und Shadow müssen bei
produktiver Nutzung räumlich getrennt platziert werden. Für höchste Kritikalität
bleiben 2oo3 oder eine unabhängige autoritative Recovery-Domäne erforderlich.

## Capability- und Ressourcenmodell

Eine Capability verbindet Objektidentität, Generation und minimale Rechte,
beispielsweise `READ`, `WRITE`, `CONTROL` oder `DELETE`. Besitz einer Adresse,
PID oder Gerätenummer begründet keine Berechtigung. Widerruf muss atomar sein
und laufende Operationen sicher abbrechen oder einer neuen Epoche zuordnen.

Quoten sorgen dafür, dass Lecks und Überlast lokal bleiben. Wird ein Limit
erreicht, wird die fehlerhafte Domäne gedrosselt, abgewiesen oder neu gestartet;
reservierte Ressourcen der Safety-Funktionen bleiben erhalten.

## Betriebsstufen

```text
FULL -> DEGRADED -> ESSENTIAL -> SAFE -> HALT
```

Jede Stufe besitzt erlaubte Funktionen, Ressourcenreservierungen, maximale
Dauer und Eskalationsbedingungen. Beispielsweise fällt Grafik auf einen
reservierten Text-/Alarmkanal zurück; ein Ausfall beider Bedienpfade darf die
autonome Safety-Funktion nicht blockieren. `HALT` ist nur zulässig, wenn ein
unabhängiger Kanal bereits den sicheren Zustand hält.

## Panic, Stackfehler und kontrollierter Neustart

User-Stacks besitzen unterhalb und oberhalb des achtseitigen Stackbereichs
explizite, nicht abgebildete Guardpages. Ein Zugriff darauf wird als User-#PF
auf den Prozess begrenzt und im Gasttest erzwungen. Jeder Kernelbuild lehnt
dynamische Frames fail-closed ab und begrenzt compilerseitig erkannte statische
Einzelframes auf 4096 Byte. Ein vollständiger
Entry-/IRQ-Callgraph mit nachgewiesener Gesamttiefe bleibt zusätzlich nötig. Ein
Kernel-Stackfehler, Double Fault oder eine fundamentale Inkonsistenz macht den
betroffenen Rechnerkanal unvertrauenswürdig. Dann gilt:

```text
revoke/fence hazardous outputs
-> notify independent supervisor
-> write bounded crash record
-> external watchdog reset
-> authenticated boot and self-test
-> reconcile state
-> controlled reintegration
```

Der Fatalpfad nutzt einen eigenen Emergency-Stack und weder Heap, Dateisystem,
blockierende Locks noch unbeschränktes `printf`. Der native x86-Pfad schreibt
einen prüfsummengeschützten Record redundant in eine reservierte Low-Memory-
Seite und CMOS/NVRAM, veröffentlicht dessen Magic jeweils zuletzt und meldet
ihn nach einem Reset genau
einmal. Im qualifizierten QEMU-Profil überwacht ein eigenständiges emuliertes
IB700-Gerät den Schedulerfortschritt; bloße Timerinterrupts berechtigen nicht
zum Füttern. Der Fatalpfad stoppt das Füttern und armiert das kürzeste Intervall.
Danach fordert er mit festem Pollbudget einen Plattformreset an und erzwingt
ersatzweise einen CPU-Reset. Für Zielhardware ist weiterhin ein unabhängig
versorgter externer Watchdog mit Fencing erforderlich.
Unterbrechungsfreie wesentliche
Leistung bei Kernel-, CPU-, RAM- oder Stromfehlern setzt eine ausreichend
unabhängige zweite Ausführungslinie beziehungsweise ein Safety Island voraus.

Ein ausschließlich in separaten Testartefakten aktivierbarer Buildschalter löst
Vektor 8 über den realen Task-Gate-/TSS-Pfad aus. Die Abnahme verlangt die
geordnete Kette `armed -> fatal record -> watchdog reset -> record recovered ->
normaler Gasttest`. Das Produktionsartefakt enthält keinen erreichbaren
Auslösepfad.

## Fault-Injection als Produktschnittstelle

Fault-Injection wird als kontrollierte Testfähigkeit mit Build- und
Autorisierungsgate entworfen. Testfälle umfassen mindestens:

```text
process.page_fault
process.stack_overflow
driver.timeout
memory.allocation_failure
interrupt.drop_or_storm
service.crash
storage.partial_write
power_loss_during_commit
watchdog_and_failover
```

Jeder Test beantwortet mit objektiver Evidenz: erkannt, innerhalb der FTTI
eingegrenzt, korrekt wiederhergestellt oder degradiert, Zustand validiert und
sicher reintegriert? Die Schnittstelle ist in Produktionsimages entfernt oder
kryptografisch und physisch kontrolliert; sie darf keine Wartungs-Backdoor
werden.

## Übergang vom heutigen Kernel

1. Zweck, Hazards, wesentliche Leistung, FTTI und Recoveryziele festlegen.
2. Guardpages, Emergency-/Double-Fault-Pfad, begrenzten Crashrecord und externen
   Watchdog implementieren.
3. IPC, Capabilities, Quoten und Supervisor zunächst für neue Dienste bauen.
4. GUI und Netzwerk, danach Dateisystem und komplexe Treiber aus Ring 0 lösen.
5. Deterministische Ressourcenreservierung und transaktionalen Zustand
   einführen.
6. Signierte A/B-Images, Boot-Failover und unabhängigen Standby-Kanal ergänzen.
7. Erst danach x86-64 und zusätzliche Schutzmechanismen als kontrollierte
   Plattformmigration qualifizieren.
