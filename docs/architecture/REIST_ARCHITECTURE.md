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

REIST ist branchenunabhängig aufgebaut:

```text
REIST OS
├── Generic High-Assurance Core
├── Medical reference profile
├── Spacecraft reference profile
├── Industrial-control reference profile
└── Experimental FPGA profile
```

Dieses Dokument beschreibt die technische Zielarchitektur. Der aktuelle
32-Bit-Kernel ist noch ein modularer Monolith und erfüllt sie nicht. Die
verbindlichen, profilunabhängigen Regeln stehen im
[High-Assurance-Core-Vertrag](HIGH_ASSURANCE_CORE_CONTRACT.md). Der
[Medical-High-Assurance-Vertrag](MEDICAL_HIGH_ASSURANCE_CONTRACT.md) ist nur
ein optionales Referenzprofil.

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

Der entscheidende nächste Architekturwechsel ist damit ausdrücklich:

```text
modular-monolithisches kernel.bin
-> minimaler Safety Kernel mit IPC und Capabilities
-> isolierte Storage-, Network- und Driver-Domänen
```

Erst diese Trennung erzeugt Failure Domains, in denen ein kompletter Dienst
gezielt beendet, blockiert oder korrumpiert werden kann, ohne den übrigen Kern
und dessen Essential Functions mitzunehmen.

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

### S0.3a: begrenzte IPC- und Capability-Basis

S0.3a ist als erster ausführbarer Mechanismus umgesetzt. Der Kernel verwaltet
statisch 16 Endpoints, acht lokale Capability-Einträge pro Prozess und je
Endpoint eine FIFO mit vier Nachrichten zu höchstens 128 Byte Nutzlast. Der
Pfad ist nach `ipc_init()` heapfrei; Queue-, Endpoint- und Capability-Grenzen
sind feste Teile des gegenwärtigen Vertrags.

Eine lokale Capability ist ein 32-Bit-Handle. Das untere Byte kodiert den
Endpoint-Slot, die oberen 24 Bit dessen Generation. Slot null und Generation
null sind ungültig; ein Slot wird nach ausgeschöpfter Generation stillgelegt,
statt ein altes Handle erneut gültig werden zu lassen. Zusätzlich bindet der
Kernel jeden Capability-Eintrag an PID und Prozessgeneration. S0.3a kennt
`SEND`, `RECEIVE` und `CONTROL`. Der Erzeuger erhält alle drei Rechte. Beim
Spawn werden noch ungebundene Endpoints vor der READY-Publikation an genau ein
Kind vererbt, aber auf `SEND|RECEIVE` abgeschwächt; `CONTROL` bleibt beim
Erzeuger. Mehrparteienrouting ist bewusst noch kein Bestandteil von v1.

Nachrichten tragen in Version 1 `version`, `struct_size`, `length` und die
begrenzte Nutzlast. `send` blockiert bei voller Queue, `receive` bei fehlender
Nachricht über die vorhandenen Wait-Queues. Schließt der Eigentümer den
Endpoint oder endet sein Prozess, widerruft der Kernel den Endpoint, entfernt
alle zugehörigen Capability-Einträge und weckt blockierte Peers. Ein
Peer-Prozessverlust wird als geschlossener Kanal sichtbar, statt einen
wartenden Prozess dauerhaft an einem verwaisten Endpoint zu halten. Send und
Receive besitzen nun endliche monotone Deadlines. Timeout null ist explizit
nichtblockierend und liefert `EAGAIN`; eine abgelaufene positive Deadline
liefert `ETIMEDOUT`. Die alten Syscalls 50/51 verwenden einen endlichen
Standardwert, während 53/54 den Timeout explizit entgegennehmen. Der PIT weckt
abgelaufene IPC-Warter über einen festen `MAX_TASKS`-Scan, ohne einen zweiten
intrusiven Wait-Knoten zu verwenden.

Diese Basis ist noch kein vollständiger High-Assurance-IPC-Vertrag. Es fehlen:

- CRC beziehungsweise `critical_object`-Schutz für Nachrichten, Endpoint- und
  Capability-Metadaten,
- explizite selektive Delegation und Rechteabschwächung unabhängig vom Spawn,
- reservierte Task-Slots und Admission Control für neu startbare Dienste,
- Capability-Gates für `kill` und die weiterhin ambient verfügbaren Datei-,
  Display-, Prozess- und sonstigen Syscalls,
- eine überwachte, neu startbare Ring-3-Domäne als S0.3b-Abnahmeobjekt.

Bis diese Punkte erfüllt sind, beweist S0.3a begrenzte Nachrichtenübertragung,
Handlegeneration und Exit-Widerruf, aber weder Least Privilege für den gesamten
Syscallraum noch eine vom modularen Monolithen unabhängige Failure Domain.

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

Als erste konkrete S0.3-Sperre besitzt der Kernel eine statische, begrenzte
Output-Fence-Registry. Ein Fatalereignis verriegelt sie vor jeder Diagnose und
vor dem Watchdog-Handover dauerhaft bis zum Neustart. Der Netzwerk-Hook sperrt
alle Software-TX-Pfade und deaktiviert best-effort die Sender von E1000,
RTL8139 und NE2000; Wiederholung ist wirkungslos und benötigt weder Heap noch
Locks. Das verhindert weitere Netzwerkausgabe aus dem fehlerhaften Kanal, ist
aber kein Ersatz für ein elektrisch unabhängiges Interlock für gefährliche
Aktoren. Solche Ausgänge benötigen je Hazard einen rücklesbaren, extern
überwachten Safe-State-Pfad.

Supervisor-Domänen registrieren deshalb getrennte `apply`- und `verify`-Hooks.
Nach einem Deadlinefehler beansprucht der Supervisor die Domäne atomar als
`FENCING`, führt beide Hooks außerhalb seines IRQ-off-Zustandslocks aus und
erlaubt einen Restart ausschließlich nach positiver Rückleseprüfung. Ein
fehlgeschlagenes Apply oder Verify eskaliert unmittelbar in `SAFE_STATE`;
eine bloße Software-Bestätigung kann das Gate nicht mehr passieren.
Der PIT-Clockpfad prüft die feste Tabelle in einem begrenzten 10-ms-Raster und
schreibt abgelaufene Deadlines dauerhaft als `ISOLATED` in den
ECC-geschützten Domänenzustand. Er führt dort weder Fencing noch Restart aus.
Der spätere Foreground-Executor kann das wiederholbar angebotene
`FENCE_REQUIRED` daher verlustfrei übernehmen; ein verzögerter Executor kann
keine Timeoutmeldung versehentlich konsumieren.
`supervisor_service_one()` ist der erste Foreground-Executor: Er ist außerhalb
von IRQ-Kontext und mit aktivierten Interrupts aufzurufen und führt pro Aufruf
höchstens eine Fence-Aktion samt Verify aus. Restart und Selbsttest bleiben als
explizite Folgeereignisse beim Domänen-Orchestrator; damit entstehen weder
unbegrenzte Ereignisschleifen noch versteckte Restartketten.
Ein reservierter Kernel-Worker ruft diesen Executor alle 10 ms auf. Er belegt
bewusst einen der acht Task-Slots und kann daher nicht durch Userprozesse
verdrängt werden. `RESTART_REQUIRED` und `SAFE_STATE_REQUIRED` bleiben
level-triggered sichtbar, bis ein Orchestrator den geschützten Zustand ändert.
Ein deterministischer Round-Robin-Cursor verhindert dabei, dass ein dauerhaft
anstehender Safe State die Ereignisse anderer Domänen verdeckt.
Beim sicheren Zustand verriegelt der heutige konservative Fallback alle
registrierten Ausgänge; ein späterer Hazard-Orchestrator darf dies nur durch
nachgewiesene, mindestens gleich sichere domänenspezifische Interlocks ersetzen.

Die erste produktiv verdrahtete Domäne ist `network-tx`. Sie bleibt im
Leerlauf ohne künstliche Deadline und bewaffnet nur eine aktive
Sendetransaktion mit 250 ms. Der Fortschrittszähler ist 64 Bit. Ein Timeout
erlaubt keinen automatischen Restart, setzt das logische TX-Latch, deaktiviert
die vorhandenen Sender und liest bei E1000, RTL8139 und NE2000 die relevanten
Register zurück. Erst diese Rückleseprüfung bestätigt das Fence; andernfalls
bleibt die Domäne ebenfalls im global eskalierten Safe State.

Die zweite reale Domäne ist `storage-write`. ATA- und FDD-Schreibtransaktionen
bewaffnen eine 10-s-Deadline und melden danach wieder explizit Idle. Ihr
Restartbudget ist null. Bei einer Fristverletzung verriegelt REIST alle
weiteren Schreibaufrufe, prüft ATA auf gelöste `BSY`-/`DRQ`-Signale und stellt
beim FDC die Motorleitungen ab; DOR und Controller-Busy werden zurückgelesen.
Ein ATA-Cache-Flush-Timeout wird als Fehler propagiert und nicht mehr als
erfolgreicher Write gemeldet. Diese Sperre verhindert Folgeschäden, kann aber
einen bereits an das Gerät übergebenen Sektor allein nicht zurückrollen. Für
markierte native FAT32-Images übernimmt dies das unten beschriebene
Undo-Journal v2 einschließlich Flush-Barrieren und Boot-Recovery. Ein
skalierbares Journal beziehungsweise COW sowie die Power-Cut-Matrix auf
Zielhardware bleiben Aufgabe von S0.5.

Darüber liegt `filesystem-write` als dritte reale Domäne. Alle öffentlichen
VFS-Mutationen (`write`, `create`, `delete`, `rename`, `mkdir`, `rmdir`,
`unmount`)
werden mit einer 15-s-Deadline überwacht. Ein I/O-Fehler oder Timeout schaltet
das VFS dauerhaft bis zum Neustart auf Read-only; Lese- und Diagnosezugriffe
bleiben verfügbar. Fatal-Fencing verriegelt sowohl diese VFS-Schranke als auch
den physischen Storage-Write-Pfad. Der Modus ist bewusst fail-closed und hat
kein automatisches Restartbudget. Für markierte FAT32-Images umfasst ihn die
nachfolgende Journaltransaktion; FAT12, EXT2 und fremde Medien besitzen diese
Mehrsektor-Transaktionsgarantie weiterhin nicht.

Auch die Steuerdaten beider Persistenzdomänen sind jetzt `critical_object`s:
Fortschrittssequenz und Fence-/Read-only-Zustand liegen jeweils als
Primary/Shadow-Kopie mit SECDED, CRC und semantischem Validator vor.
Einzelbitfehler werden korrigiert, eine brauchbare Kopie rekonstruiert die
andere. Sind beide Kopien unbrauchbar, verriegeln Storage beziehungsweise VFS
fail-closed. Ein vor der redundanten Aktualisierung gesetztes monotones
Softwareinterlock schließt das Zeitfenster während des Fence-Vorgangs.

Native REIST-FAT32-Images reservieren zusätzlich BPB-Sektor 8 für den primären
Header, 9 bis 28 für Undo-Daten und 31 für den gespiegelten Header eines
CRC-geschützten Undo-Journals. Vor jedem einzelnen Sektorwrite wird
das alte Abbild dauerhaft geschrieben, anschließend ein `ACTIVE`-Record
geflusht, erst dann der Zielsektor geändert und zuletzt der Record als `CLEAN`
markiert. Beim Mount wird ein gültiger aktiver Record vor dem Lesen veränderter
FAT-/FSInfo-/Verzeichnismetadaten zurückgerollt. Ziel-LBA, Volumegrenzen sowie
Header- und Daten-CRC werden geprüft; ein Fehler verriegelt Storage und
verweigert den schreibbaren Mount. Nur Medien mit dem expliziten Builder-Marker
aktivieren diesen Pfad, fremde FAT32-Volumes bleiben unverändert. Das liefert
atomare VFS-Mutationen mit bis zu 20 unterschiedlichen Sektoren. Wiederholte
Writes desselben Sektors benötigen nur einen Undo-Slot. Die VFS-Klammer hält
den Record über die komplette Operation `ACTIVE` und setzt `CLEAN` erst nach
erfolgreichem Abschluss. Kapazitätsüberschreitung oder ein I/O-Fehler lassen
den Undo-Satz für Boot-Recovery stehen und schalten das VFS Read-only. Größere
Operationen benötigen künftig ein skalierbares Journal beziehungsweise COW.
Journal-v1-Medien werden rückwärtskompatibel wiederhergestellt und anschließend
mit einem sauberen v2-Header migriert.
FAT32 Same-Directory-Rename und Replace laufen als eine solche VFS-Transaktion.
Der Editor schreibt zuerst eine PID-spezifische 8.3-Tempdatei, fordert über
Prozess-FD und VFS einen begrenzten ATA-`FLUSH CACHE` an und ersetzt das Ziel
erst nach erfolgreichem `fsync` und Close per Rename. Ein Fehler vor dem Commit lässt
die alte Zieldatei unangetastet; FAT12-, Cross-Directory- und Cross-Volume-
Rename bleiben explizit unsupported.
Der v2-Superblock liegt redundant in den reservierten Sektoren 8 und 31. Jeder
Statuswechsel wird primär und gespiegelt mit identischer Sequenz und CRC
persistiert. Beim Boot gewinnt die höchste gültige Sequenz; bei gleicher
Sequenz und `CLEAN`/`ACTIVE`-Divergenz wird konservativ `ACTIVE` gewählt und
zurückgerollt. Eine einzelne ungültige Kopie wird aus der gültigen Kopie
automatisch rekonstruiert. Zwei widersprüchliche, formal gültige Kopien mit
gleichem Zustand führen dagegen fail-closed zur Schreibsperre.

Auch die Recovery-Steuerung ist Teil des kritischen Zustands: Apply-/Verify-
Funktionszeiger und ihr Kontext liegen nicht mehr als ungeschützte Pointer in
der Domänentabelle, sondern als Primary/Shadow-`critical_object` mit SECDED,
CRC, Version und semantischer Nicht-Null-Prüfung. Korrigierbare Fehler werden
vor Benutzung repariert. Sind beide Kopien ungültig, wird kein Pointer
aufgerufen und die Domäne wechselt unmittelbar in `SAFE_STATE`.
Auch Slot-Belegung, Generation und der begrenzte Domänenname bilden nun einen
versionierten Primary/Shadow-Descriptor mit SECDED und CRC. Registrierung
veröffentlicht diesen Descriptor zuletzt. Ein beschädigter Deskriptor wird
korrigiert oder beim Scan fail-closed als globales Safe-State-Ereignis
gemeldet; ein gekipptes `occupied`-Bit kann keine Domäne mehr verschwinden
lassen.

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

1. Einsatzprofil, Hazards, Essential Functions, FTTI und Recoveryziele festlegen.
2. Guardpages, Emergency-/Double-Fault-Pfad, begrenzten Crashrecord und externen
   Watchdog implementieren.
3. Die umgesetzte begrenzte IPC-/Capability-Basis um Deadlines, Integrität,
   explizite Delegation, reservierte Service-Slots und Syscall-Gates härten.
4. Mit S0.3b eine capability-beschränkte Ring-3-Probedomäne überwachen, gezielt
   beenden und innerhalb eines begrenzten Restartvertrags reintegrieren.
5. GUI und Netzwerk, danach Dateisystem und komplexe Treiber aus Ring 0 lösen.
6. Deterministische Ressourcenreservierung und transaktionalen Zustand
   einführen.
7. Signierte A/B-Images, Boot-Failover und unabhängigen Standby-Kanal ergänzen.
8. Erst danach x86-64 und zusätzliche Schutzmechanismen als kontrollierte
   Plattformmigration qualifizieren.
