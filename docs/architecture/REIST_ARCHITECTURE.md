# REIST-OS-Zielarchitektur

Stand: 18. August 2026

## Standard-first-Kompatibilitätsregel

REIST verwendet etablierte Schnittstellen als Architekturgrundlage, statt
unverwandte Gegenstücke neu zu erfinden. Öffentliche APIs, ABIs, Protokolle,
Dateiformate, Gerätemodelle und Buildartefakte behalten Terminologie,
Zustandsübergänge, Einheiten und Fehlerkonventionen ihres Referenzstandards,
soweit diese mit begrenzter und isolierter Ausführung vereinbar sind.

Die Subsystemdokumentation nennt den jeweiligen Referenzstandard. REIST
behauptet erst dann Quell-, Binär- oder Protokollkompatibilität, wenn ein
ausführbarer Test sie nachweist. Sicherheitsbedingte Abweichungen erhalten
einen REIST-Namensraum, eine explizite Version und append-only Evolution; Grund
und beobachtbares Verhalten werden dokumentiert und durch Regressionstests
abgedeckt. Insbesondere wird ein unendliches Standard-Warten als
REIST-Operation mit endlicher Deadline abgebildet und nie unter einem
Standardnamen mit stillschweigend anderer Semantik veröffentlicht.

„Standard-first“ bedeutet keine blinde Übernahme gewachsener Altlasten. REIST
übernimmt stabile Begriffe, Datenmodelle, Einheiten und beobachtbares Verhalten,
wenn sie Portierung und Wartung erleichtern. Unbegrenzte Wartepfade, impliziter
globaler Besitz, ungeprüfte rohe Pointer, direkte Geräteautorität und nur noch
historisch notwendige Alias- oder Sonderpfade werden nicht in neue
Kernschnittstellen kopiert. Wo bestehende Software solche Eigenschaften
erwartet, kapselt ein dokumentierter Userspace-Adapter die Differenz; der
begrenzte Kernelvertrag bleibt eindeutig und klein.

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
32-Bit-Kernel ist weiterhin ein modularer Monolith und erfüllt sie noch nicht
vollständig. Ring-3-Dienste, Capabilities, Supervisor, Fencing und begrenzte
Recoverypfade sind bereits ausführbar, bilden aber noch keine unabhängige
Hardware-Fehlerdomäne. Die
verbindlichen, profilunabhängigen Regeln stehen im
[High-Assurance-Core-Vertrag](HIGH_ASSURANCE_CORE_CONTRACT.md). Der
[Medical-High-Assurance-Vertrag](MEDICAL_HIGH_ASSURANCE_CONTRACT.md) ist nur
ein optionales Referenzprofil.

## Minimaler REIST-Kern

Die oberste Architekturregel ist die Stabilität der Microkernel-Grenze. Ein
gewöhnlicher Fehler in einem Treiber, Dienst, Prozess oder Programm darf weder
den Kernel beenden noch unabhängige Essential Functions verlieren lassen. Alle
solchen Komponenten laufen als getrennte Ring-3-Fehlerdomänen und werden über
versionierte IPC- und Capability-Verträge angebunden. Das messbare Versprechen,
die Restartbudgets und terminalen Zustände definiert der
[Resilienz- und Degradierungsvertrag](RESILIENCE_AND_DEGRADATION_CONTRACT.md).

Das erste PCI-Audio-Backend setzt diese Grenze konkret als getrennten
[HDA-Treiber und PCM-Service](AUDIO_SUBSYSTEM.md) mit kernelvermitteltem DMA um.

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

Dateisysteme, Netzwerk, GUI und alle Gerätetreiber werden in eigene Adressräume
verlagert. In Ring 0 verbleiben nur minimale, geräteunabhängige IRQ-,
MMIO/PIO-, DMA- und Reset-Mediatoren, soweit diese für die Isolation auf der
jeweiligen Plattform zwingend sind. Der Bootcode wird nach dem Handoff unzugänglich gemacht
oder verworfen. Paging und getrennte Kernel-/Userräume werden früh aktiviert;
NX, SMEP/SMAP, IOMMU und später CET werden nur auf Zielplattformen verwendet,
die diese Funktionen nachweislich besitzen. Für den aktuellen i386-Pfad sind
fehlende Hardwareeigenschaften explizite Grenzen, keine stillen Annahmen.

Der aktuelle BIOS-Referenzpfad verwendet ein festes Manifest v3 mit
unveränderten bisherigen Feldpositionen, 336-Byte-Header und eingebetteter
256-Byte-Kernelsignatur. Es bindet das Kernelartefakt mit SHA-256 gemäß NIST
FIPS 180-4; ein unabhängiger Hostvalidator prüft nach der Erzeugung Manifest,
Datenträgergrenzen und tatsächliche Kernelbytes für HDD und Floppy. Stage 2
berechnet SHA-256 mit festen Puffern im selben begrenzten Lesedurchlauf wie die
nur noch diagnostische CRC32. Danach prüft es vor ELF-Parsing und Kernelstart
RSA-2048-PSS/SHA-256 gemäß RFC 8017 mit MGF1-SHA-256, exakt 32 Byte Salt und
dem fest einkompilierten Research-Modulus für Exponent 65537. Der eingecheckte
private Research-Schlüssel ist ausdrücklich kein Produktions-Secret;
Release-Policies müssen ihn ablehnen. Damit ist das Kernelartefakt relativ zur
ausgewählten Stage-2-Vertrauensgrenze authentifiziert. Stage 1 und Stage 2
selbst sind auf dem beschreibbaren Medium jedoch nicht durch einen
unveränderlichen Firmwareanker geschützt; daraus folgen weder Secure Boot,
Anti-Rollback noch eine physische Plattformqualifikation.

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
`SEND`, `RECEIVE` und `CONTROL`. Der Erzeuger erhält alle drei Rechte.
Syscall 55 delegiert einen Endpoint explizit an die atomar aufgelöste aktuelle
Generation einer Ziel-PID. Die delegierten Rechte müssen eine nichtleere
Teilmenge der Quellrechte sein; `CONTROL` ist grundsätzlich nicht delegierbar.
Spawn vererbt keine IPC-Autorität mehr. Mehrparteienrouting ist bewusst noch
kein Bestandteil von v1.

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

Endpoint-, Queue- und autoritative globale Capability-Steuerdaten liegen nun
als feste Primary/Shadow-`critical_object`s mit SECDED, CRC, Version, Sequenz
und semantischen Validatoren vor. Eine Nachricht wird einschließlich
Absenderidentität explizit auf drei Blöcke von höchstens 64 Byte verteilt.
Einzelbitfehler werden korrigiert und begrenzt gezählt. Sind beide Kopien
unbrauchbar, wird der Endpoint quarantänisiert, beide Warterichtungen werden
geweckt und `IPC_EINTEGRITY` zurückgegeben, bevor Rechte oder Payload
veröffentlicht werden. Prozesslokale Handle-Slots sind nur Selektoren; jede
Verwendung muss einen geschützten, holder- und generationsgebundenen globalen
Capability-Record auflösen.

Ein Taskslot, ein Prozessslot und ein Admission-Budget von 32 physischen
Frames bleiben für einen expliziten Supervisor-Spawn reserviert. Normale
Ring-3-Spawns werden vor Prozesspublikation beziehungsweise Taskanlage
abgewiesen, sobald sie diese Reserve berühren würden. Der Supervisorpfad ist
separat benannt und darf genau diese statisch begrenzte Restartkapazität
verwenden; fehlgeschlagene Erzeugung läuft durch die bestehenden vollständigen
Rollbackpfade.

S0.4a führt drei statische Schedulingklassen ein: Kernel-Safety, überwachte
Services und Ambient-Tasks. Die Auswahl ist ein heapfreier fester Zyklus
`Safety, Safety, Service, Ambient`. Jede Klasse führt einen eigenen
Round-Robin-Cursor und überspringt blockierte Tasks in höchstens `MAX_TASKS`
Schritten. Dadurch bleiben sowohl das Verhältnis 2:1:1 als auch Fortschritt
innerhalb jeder laufbereiten Klasse unabhängig von Taskslot-Reuse erhalten.
Damit ist die Auswahl begrenzt und reproduzierbar, aber noch keine harte
Echtzeitgarantie.

S0.4b legt darüber absolute, an der monotonen PIT-Zeit ausgerichtete
100-ms-Fenster. Safety erhält 60 ms, Service 25 ms und Ambient 15 ms. Verbrauch
wird ausschließlich an Schedulergrenzen abgerechnet; lange nicht
präemptierbare Abschnitte und vollständig übersprungene Fenster erhöhen den
Überlastzähler ohne nachträgliche unbeschränkte Schleife. Nach Budgetverbrauch
ist die Klasse bis zur nächsten Fenstergrenze gedrosselt, während der
Kernelkontext für Diagnose und Recovery weiterläuft. Eine rückläufige
Clocksource sperrt alle Klassen fail-closed. Priority Inheritance sowie
vollständige WCET-, Speicher- und Queue-Nachweise folgen in S0.4c.

S0.4c-1 ergänzt Priority Inheritance ausschließlich für IPC-Wartebeziehungen
mit eindeutigem Gegenprozess. Der Wartende speichert Taskslot und
Prozessgeneration des Owners; der Scheduler berechnet die effektiven Klassen
in einer über `MAX_TASKS` begrenzten Fixpunktfolge und unterstützt damit auch
transitive Ketten. Wakeup, Timeout, Cancel und Exit entfernen die Beziehung.
Eine Klassenanhebung verändert keine Basisautorität und wird bei der
CPU-Fensterabrechnung der effektiven Klasse belastet. Spinlocks und
Präemptions-Guards bleiben ausgeschlossen, weil sie nach dem
Synchronisationsvertrag keine blockierenden Abschnitte enthalten dürfen.

S0.4c-2a macht die bereits statischen Kernkapazitäten prüfbar. Das versionierte
Register `safety/resource_budgets.toml` verweist für Taskslots, Kernelstack,
IPC-Endpunkte, Capability- und Nachrichtengrenzen sowie Storage-Requests auf
das jeweils autoritative C-Makro und mindestens einen Test. Der Validator
akzeptiert ausschließlich begrenzte ganzzahlige Präprozessorausdrücke und
bricht bei Drift oder fehlender Traceability ab. Das Register steht bewusst
auf `partial`: Laufzeit-High-Water-Marken, vollständige Speicherbudgets und
WCET-Messungen auf den ausgewählten Zielplattformen sind noch nicht erbracht.

S0.4c-2b1 ergänzt diesen statischen Vertrag um Laufzeitbeobachtung für IPC und
Storage-Requests. Versionierte Diagnosestrukturen führen aktive und maximale
Belegung sowie saturierende Ablehnungszähler unter dem bereits vorhandenen
Subsystem-Lock. Sie allokieren nicht und beeinflussen keine
Autoritätsentscheidung. Verhaltenstests erzwingen Endpunkt-, Capability-,
Nachrichten-, Client- und globale Storagegrenzen und belegen, dass Cleanup die
aktive Belegung auf null zurückführt, ohne den historischen Höchststand zu
verlieren. Weitere Queue-Metriken bleiben S0.4c-2b2.

S0.4c-2b2a versioniert die bestehende Speicherdiagnostik append-only auf v2.
Der 88-Byte-v1-Präfix bleibt im Syscall explizit verhandelbar; v2 hängt
historische Frame-/Heap-Belegung und saturierende Allokationsfehler an. Die
Zähler werden unter Frame- beziehungsweise Heap-Lock aktualisiert. Der
Ring-3-Gast prüft, dass Peaks nie unter der aktuellen Belegung liegen, nach
Freigabe erhalten bleiben und die Framebilanz wieder stimmt. Weil ein alter
`process.o` beim ersten inkrementellen ABI-Build den erweiterten Kerneloutput
in einen zu kleinen Stackslot kopierte, ist die Buildkette nun ebenfalls Teil
des Vertrags: explizite `.d`-Dateien für jedes C-Objekt werden vor jedem Link
vollständig validiert. Headeränderungen können damit keinen stillen
Mischbuild mehr erzeugen.

S0.4c-2b2b1 stellt die Taskslot-Auslastung über append-only Syscall 84 bereit.
Die 32-Byte-v1-Antwort wird unter IRQ-Schutz aus höchstens `MAX_TASKS` Slots
gebildet und enthält Kapazität, aktuelle/maximale Belegung, Supervisor-Reserve
und saturierende Ablehnungen. Der Messpfad allokiert nicht und verändert außer
den monotonen Diagnosezählern keinen Schedulerzustand.

S0.4c-2b2b2 hält Allokationsfehler aus Produktionsimages heraus und aktiviert
sie nur mit `REIST_MEMORY_FAULT_INJECTION`. Zwei statische Countdowns erzwingen
Heap- oder Frame-ENOMEM an einem vorgegebenen Allokationspunkt. Der Boottest
prüft eine unveränderte Heapbelegung sowie den vollständigen Rollback einer
nach der ersten Seite gescheiterten Kernelstack-Allokation. Nach Disarm muss
derselbe Stackslot wieder vollständig nutzbar sein; erst dann wird der
Erfolgsmarker ausgegeben.

S0.4c-2c1 ergänzt den Zig-Referenzbuild um einen getrennten GCC-Analysecompile
mit `-fstack-usage` und `-fcallgraph-info=su`. Für jedes der 75 C-Objekte muss
ein gepaartes Stack-/Callgraph-Artefakt existieren. Der begrenzte Validator
verlangt statische lokale Frames bis höchstens 4096 Byte und einen azyklischen
direkten Callgraph. Als erste konkrete Korrektur wurde der rekursive PCI-
Bridge-/Multifunktionsscan durch die ohnehin vollständig begrenzten Schleifen
über 256 Busse, 32 Slots und acht Funktionen ersetzt. Diese Evidenz ersetzt
noch keine Zielhardware-WCET-Budgets.

S0.4c-2c2a erweitert diese Evidenz um kumulative Budgets für Legacy-/Scheduler-
IRQ- und CPU-Exception-Einstiege. `safety/stack_budgets.json` ist der prüfbare
Vertrag: Er bindet jeden registrierten IRQ-/Exception-Handler sowie die im
User-Exception-Exit erreichbaren VFS-Callbacks an den indirekten
Dispatcherpfad und weist konservative Reserven für Assembly-, Validator- und
Fence-Callbacks aus. Der Validator vergleicht die Inventare mit den
Produktionsquellen und scheitert bei Drift, unbekannten indirekten Zielen,
fehlenden Kosten oder Überschreitung. Aktuell sind 1.744/7.168 Byte im Legacy-
IRQ-Pfad, 720/4.096 Byte im Scheduler-IRQ-Pfad und 2.000/7.168 Byte im CPU-
Exception-Pfad belegt. S0.4c-2c2b1 erweitert denselben Vertrag auf INT 0x80:
Der vollständige Syscallpfad belegt einschließlich einer konservativen
128-Byte-Reserve für Privilegwechsel, Registerframe und Assemblyausrichtung
6.880/7.168 Byte. Sämtliche VFS-Operationstabellen und die indirekten EXT2-
Verzeichnisbesucher werden gegen die Produktionsquellen inventarisiert. Die
verbleibenden 1.024 Byte des realen 8-KiB-Taskstacks sind nicht Teil des
zulässigen Budgets. WCET-Baselines der ausgewählten Plattformen folgen
getrennt.

S0.4c-2c2b2a ergänzt dafür eine bewusst nicht steuernde Laufzeitdiagnostik.
Der Scheduler misst mit serialisiertem `RDTSC` nur seine begrenzte
Entscheidungsarbeit und beendet jede Probe vor einem Kontextwechsel. Der
append-only Syscall 116 misst ausschließlich seinen eigenen nicht blockierenden
Diagnosepfad; Warte-, Schlaf- und Off-CPU-Zeit kann deshalb nicht in die Werte
eingehen. Eine feste 72-Byte-v1-Struktur enthält Frequenz, Samplezahl, Summe,
Maximum und Zeitquellenanomalien. Alle Zähler sind saturierend, werden unter
IRQ-Schutz aktualisiert und beeinflussen weder Scheduling noch Autorität.
Der überwachte Probe-Dienst fordert nach mindestens 64 Samples genau einen
normalisierten, generationsgeprüften Supervisor-Marker an. Messfehler
unterdrücken die Evidenz, beenden aber nicht den Essential Service.
`safety/wcet_budgets.json` begrenzt QEMU und
VMware jeweils auf 10 ms und verlangt null Zeitquellenanomalien. Das ist eine
empirische Emulator-Regressionsgrenze, keine physische WCET-, Zertifizierungs-
oder Zielhardwareaussage; die Hardwaremessung bleibt eine manuelle Abnahme.
Die Abnahme vom 23. August 2026 erfasste bei null Anomalien auf QEMU maximal
2.193.758 Zyklen (rund 0,613 ms) für die Schedulerentscheidung und 365.795
Zyklen (rund 0,102 ms) für INT 0x80. VMware erfasste maximal 163.214 Zyklen
(rund 0,051 ms) beziehungsweise 107.643 Zyklen (rund 0,034 ms). Beide liegen
unter der 10-ms-Regressionsgrenze; die Werte sind nicht zwischen Emulatoren
vergleichbar und bleiben nicht-authoritative Beobachtungen.

Jeder Prozess trägt ein versioniertes Domänenprofil mit einem vollständigen
Bitinventar der gegenwärtigen Syscalls. Normale Programme erhalten explizit
das Kompatibilitätsprofil. Das Supervisor-Profil `PROBE` beginnt dagegen bei
Default-Deny und erlaubt ausschließlich Exit, Identität, Yield/Sleep,
monotone Diagnosezeit, Memory-Statistik und die für den kontrollierten Kanal
benötigten IPC-Operationen. Der Dispatcher prüft das Profil zentral vor jeder
User-Speicherausgabe und jedem Seiteneffekt. Ring-0-Operationen besitzen einen
separaten Trusted-Pfad. `kill` ist auch im Kompatibilitätsprofil nur für ein
Kind erlaubt, dessen gespeicherte Eltern-PID und Elterngeneration mit dem
Aufrufer übereinstimmen.

S0.3b ergänzt diese Basis um eine überwachte, neu startbare Ring-3-Probe. Der
Health-Kanal ist generationgebunden; Crash, Hang und ungültige Antwort führen
innerhalb endlicher Deadlines über Fence, Revoke/Reap, Restart und Selbsttest
zur Reintegrationsentscheidung. Das beweist Prozessisolation und Recovery des
Probekanals, aber noch keine vom modularen Monolithen unabhängige Kernel-, CPU-
oder RAM-Fehlerdomäne.

S0.3c beginnt mit einem echten, wenn auch noch unkritischen Dienst: Der nach
der Fault-Injection gesund reintegrierte Prozess beantwortet begrenzte
Diagnoseanfragen über IPC. Syscall 57 ist ein schmaler Service-Directory-
Einstieg. Er prüft Zielpuffer und Dienstzustand vor jeder Veröffentlichung,
bindet die Besitzeridentität an PID plus Generation und delegiert an genau
einen Client ausschließlich `SEND|RECEIVE`, niemals `CONTROL`. Der Dienst
arbeitet mit 40-ms-Receive- und 100-ms-Send-Deadlines. Damit ist erstmals eine
reale Funktion hinter der restartbaren Ring-3-Grenze erreichbar; Kernelzeit,
Netzwerk, Storage und GUI bleiben noch Ring-0-/Kompatibilitätspfade.

S0.3c-2 ergänzt mit Syscall 58 die explizite Freigabe einer delegierten
Capability. Sie ist von der Endpoint-Zerstörung durch den Besitzer getrennt,
entfernt den generation-gebundenen Clientdatensatz atomar und weckt begrenzt
wartende Peers. Ein Client kann dadurch verbinden, arbeiten, freigeben und
erneut verbinden, ohne Capability-Slots dauerhaft zu verbrauchen.

S0.3c-3a verlagert als erstes Netzwerkinkrement die begrenzte Klassifikation
eines Ethernet-Headers in den Ring-3-Dienst. Das `NET1`-Protokoll akzeptiert
höchstens eine IPC-Nachricht, prüft vor jedem Zugriff die Mindestlänge und
klassifiziert ARP, IPv4 oder andere EtherTypes ohne Heap und ohne Ausgabe.
Ein synthetischer ARP-Frame und der Marker `NETWORK_PARSER_OK` liefern einen
deterministischen Gastnachweis. Der reale NIC-RX-/TX-Pfad bleibt vorerst im
Kernel; dieses Inkrement ist daher bewusst noch keine vollständige
Netzwerkdienstmigration.

S0.3c-3b bindet den echten RX-Pfad an diese Grenze an. `netdev` kopiert nach
der validierten Geräteannahme ausschließlich den 14-Byte-Ethernet-Header in
eine feste `NET1`-IPC-Nachricht. Der Trusted-Ingress blockiert nie, allokiert
nicht und verwirft bei voller Queue oder fehlendem Client. Er verwendet die
generation-gebundene Identität des einzigen delegierten Peers als Absender;
damit kann nur der Dienstbesitzer den Header empfangen und eine Antwort nur an
diesen Peer richten. Die bisherige Kernelverarbeitung bleibt während dieser
Transportstufe noch aktiv und wird erst nach einem realen NIC-Laufzeitnachweis
für den ausgewählten Pakettyp entfernt.

S0.3c-3c liefert den realen NIC-Nachweis. Nur die generation-geprüfte gesunde
Probedomäne darf über Syscall 59 höchstens alle 250 ms einen festen
ARP-Gateway-Probe auslösen; beliebige Frames oder Ziele sind nicht möglich.
Der Dienst unterscheidet synthetische `NET1`- von echten `NETR`-Headern und
meldet den Handoff erst nach einem tatsächlich empfangenen ARP-Header. Der
QEMU-Runner kann `NETWORK_HANDOFF_OK` verpflichtend verlangen. Ohne NIC liefert
der Dienst einen definierten `REIST_NET_UNAVAILABLE`-Degradationsstatus.
Der 10-ms-Supervisor-Worker führt außerdem den begrenzten Netzwerk-Bottom-Half
aus; RX-Fortschritt hängt damit nicht mehr von zufälligen Shell-Kommandos ab.

S0.3c-3d überträgt für genau den ausstehenden, rate-limitierten ARP-Probe die
Verarbeitungsautorität: Nimmt die generation-gebundene IPC-Queue den Header an,
wird dieses Frame nicht zusätzlich in die Kernel-Netstack-Queue gestellt. Bei
fehlendem Dienst, falschem EtherType oder Queue-Druck bleibt der bestehende
Kernelpfad zuständig. Fence/Restart löscht den Pending-Zustand, sodass keine
alte Probeautorität auf eine neue Dienstgeneration übergehen kann.

S0.3c-3f sättigt nach einem begrenzten Handshake deterministisch alle vier
IPC-Slots. Der folgende echte ARP-Reply kann dann nicht übernommen werden,
verbraucht die einmalige Probeautorität und fällt in den Kernelpfad zurück.
Vier anschließend korrekt beantwortete Lastnachrichten belegen, dass Queue,
Dienst und Kernelpfad nach dem Druckfenster weiter Fortschritt machen.

S0.3c-3g ergänzt oberhalb des IPC-v1-Transports einen festen Acht-Byte-
Dienstheader (`RQ1`/`RS1` plus 32-Bit-Anfrage-ID). Eine gültige Antwort muss
sowohl zur generation-codierten Endpoint-Capability als auch zur aktuellen,
von Null verschiedenen Anfrage-ID passen. IDs laufen nicht still auf Null
über. Der Gast weist die Ablehnung einer absichtlich falschen Antwort-ID nach,
bevor eine korrekt korrelierte Anfrage weiter Fortschritt macht.

S0.3c-3h erweitert den exklusiven Ingress auf den festen 42-Byte-
Ethernet/ARP-Header. Hardwaretyp, Protokolltyp, Adresslängen und Opcode werden
für diesen autoritativen Pfad nur im restartbaren Ring-3-Dienst validiert.
Strukturell ungültige ARP-Frames erzeugen weder Klassifikation noch Antwort;
der Kernel erhält weiterhin nur Frames, deren Übergabe vorher abgelehnt wurde.

S0.3c-3i bindet die ARP-Antwort an den beim Probe-Start eingefrorenen
Kontext: Gateway-IP, lokale IP und lokale MAC. Der Ring-3-Validator vergleicht
zusätzlich Ethernet- und ARP-Absender/Ziel miteinander und verlangt Opcode
Reply. Der 60-Byte-Ingress bleibt fest begrenzt; eine falsche Identität führt
zu keiner Antwort oder Zustandsveröffentlichung.

S0.3c-3j ersetzt den bloßen Pending-Zustand durch eine monotone Probe-ID.
Syscall 60 liefert sie dem Dienst, der feste Ingress trägt sie an Offset 60,
und ein einmaliger Supervisor-Bericht bestätigt die übereinstimmende ID vor
dem Headerbericht. ID-Erschöpfung ist fail-closed; Fence und Queue-Fallback
widerrufen aktive bzw. zugestellte IDs. Der bestehende Syscall 59 bleibt als
kompatibler Wrapper bestehen.

S0.3c-3k modelliert die Probe-Autorität als feste Zustandsmaschine aus
`next_id`, `active_id` und absoluter monotoner Deadline. Begin, Take, Expire
und Cancel sind allokationsfrei und besitzen keine Schleifen. Nach 250 ms wird
die aktive ID im Supervisor-Worker widerrufen; Deadline- und ID-Überlauf
sättigen beziehungsweise scheitern endgültig statt wiederverwendet zu werden.

S0.3c-3l führt getrennte saturierende Diagnosezähler für Deadline-Ablauf,
Queue-Fallback und semantische Ablehnung. Die Zähler beeinflussen keine
Autoritätsentscheidung, werden unter der kurzen Supervisor-Sperre aktualisiert
und kein Update erfolgt im Hard-IRQ; `UINT32_MAX` bleibt dauerhaft gesättigt.

S0.3c-3m exportiert die Zähler ausschließlich lesend über Syscall 61. Die
versionierte 24-Byte-Struktur wird erst nach vollständiger Userbereichs-,
Versions- und Größenprüfung gefüllt; `reserved` ist Null. Normale Prozesse
besitzen keine Reset-Autorität. Der reale Queue-Druck-Gasttest fordert einen
monoton gestiegenen Fallback-Zähler.

S0.3c-3n speichert diese Statistik als Critical Object mit ECC/CRC-geschützter
Primär- und Schattenkopie. Eine gültige Replica rekonstruiert einen beschädigten
Snapshot. Sind beide Kopien ungültig, endet die Abfrage vor der Userkopie mit
`-84`; das System erfindet weder Nullstände noch scheinbar plausible Zähler.

S0.3c-3o wendet denselben Vertrag auf die kurzlebige Probe-Autorität an. Die
monotone ID-Sequenz, aktive ID und absolute Deadline werden nur als validierter
Snapshot verändert. Nicht korrigierbare Korruption kann deshalb weder eine
Probe erzeugen noch verbrauchen; der Worker fordert stattdessen Isolation an.

S0.3c-3p schützt den nachfolgenden Identitätskontext ebenfalls redundant.
Gateway, lokale IP/MAC und die tatsächlich zugestellte ID werden gemeinsam
publiziert und verbraucht. Der Ring-3-Dienst kann dadurch weder eine veraltete
ID noch eine Identität aus einem teilweise beschädigten Snapshot bestätigen.

S0.3c-3q entfernt die letzten direkten Laufzeitentscheidungen aus der
Probe-Domäne. Prozessidentität, Endpoint, Supervisor-Handle, Health/Fence,
Launch-Zähler und Rate-Limit-Zeit liegen in einem validierten Control-Snapshot.
Ist keine Replica lesbar, bleiben Connect, Report und Handoff geschlossen; der
Worker aktiviert zusätzlich den globalen Output-Fence.

S0.3c-3r korreliert die drei geschützten Objekte zusätzlich durch die monotone
Probe-ID als Transaktionsepoche. Selbst drei CRC-/ECC-gültige Kopien dürfen nur
gemeinsam verwendet werden, wenn Control, Autorität und Identitätskontext
dieselbe Epoche tragen. Der Vergleich und die jeweilige Zustandsmutation liegen
unter einer gemeinsamen kurzen IRQ-off-Sperre; IPC und Gerätetransaktionen
bleiben außerhalb dieser Sperre.

S0.3c-4a führt den ersten mutierenden Netzwerk-Mediator ein. Ring 3 besitzt
weiterhin weder Cache-, Treiber- noch DMA-Zugriff, sondern übergibt über Syscall
62 genau `{version,size,probe_id,ip,mac}`. Der Kernel vergleicht die Struktur
mit Prozessgeneration, Control-Epoche und dem roh aus dem geschützten Ingress
gesicherten Kandidaten. Erst nach dem atomaren Autoritätsverbrauch darf der
feste ARP-Cache aktualisiert werden.

S0.3c-4b trennt diesen vermittelten Zustand vom ungeschützten Legacy-Cache.
Der feste Cache besitzt 32 statische Slots; jeder Slot speichert IP/MAC,
Quellepoche, Zustand und absolute monotone 30-s-Deadline redundant als
versioniertes Critical Object. Korrigierbare Einzelkopiefehler werden beim
Lesen rekonstruiert. Sind beide Kopien unlesbar, ist ein Sperreintrag
abgelaufen oder die Kapazität erschöpft, führt der Pfad fail-closed weder zu
Verdrängung noch zum Rückfall
auf eine möglicherweise unvalidierte Legacy-Bindung. Der Legacy-Pfad bleibt
nur für IP-Adressen zulässig, für die nie eine vermittelte
Vertrauensentscheidung existierte. Die nächste Stufe widerruft diese Slots
zusätzlich anhand der Dienstgeneration beim Fence/Restart.

S0.3c-4c ergänzt deshalb neben der Probe-Transaktionsepoche die vollständige
Identität aus PID und Prozessgeneration des produzierenden Dienstes. Der Fence
widerruft vor dem Terminate alle gültigen Slots genau dieser Identität zu
dauerhaften Sperreinträgen; gleiche Generationen anderer Prozessslots bleiben
unberührt.
Ein hardwareunabhängig initialisierter, einmal pro Sekunde begrenzter Scrub
liest exakt 32 Critical Objects, publiziert Ablauf und repariert eine gültige
Replica. Doppelkorruption verhindert weitere Lookups und isoliert die
Probedomäne beziehungsweise aktiviert ohne laufende Domäne den globalen
Output-Fence. Der RTL8139-Crashgast verlangt den Widerruf vor der erfolgreichen
Reintegration der Ersatzgeneration.

S0.3c-5a entfernt die erste verbliebene passive Vertrauensentscheidung aus
dem Ring-0-Netzstack. Die konfigurierte Gateway-IP ist für den Legacy-ARP-
Cache grundsätzlich gesperrt; dies gilt sowohl für ARP-Absender als auch für
das frühere implizite Lernen aus IPv4-Quell-MACs. Beim Publizieren einer
manuellen oder per DHCP erhaltenen Route wird eine vorherige Legacy-Bindung
gelöscht. Eine Gateway-MAC erhält erst durch den geschützten, generation- und
epochengebundenen Ring-3-Mediator Autorität. Lokale Nicht-Gateway-Peers bleiben
vorübergehend kompatibel und werden in S0.3c-5b in denselben Dienstvertrag
überführt.

S0.3c-5b1 entfernt zusätzlich den lokalen ARP-Responder aus dem allgemeinen
Ring-0-Paketparser. Ein Request an die eigene IP wird ausschließlich als
festes `NETQ`-Objekt an die gesunde, generationgeprüfte Ring-3-Domäne
publiziert. Deren Parser validiert die vollständige Ethernet-/ARP-Identität.
Syscall 63 akzeptiert danach nur `{version,size,request_id,target_ip,mac}` und
vergleicht diese Daten mit einer redundanten Critical-Object-Kopie sowie einer
250 ms begrenzten, an die konkrete Prozessgeneration gebundenen
Einmalautorität. Autorität und Kontext werden vor dem Gerätesend verbraucht.
Bei Queue-Druck, Deadline oder Sendefehler bleibt das System fail-closed; es
existiert kein Ring-0-Antwortfallback.

S0.3c-5b2a schließt den realen RX-Nachweis. Ein hostseitiger QEMU-Socket hängt
mit Slirp und RTL8139 an einem Hub und injiziert nach einem Gastmarker einen
vollständigen Broadcast-ARP-Request. Retries sind auf drei begrenzt und werden
erst nach ausbleibender Queue-Bestätigung ausgelöst, sodass nie mehrere
Einmalautorisierungen konkurrieren. Der Runtime-Vertrag verlangt Queue,
Ring-3-Entscheidung, echten NIC-Send und unabhängigen Gastfortschritt in dieser
Reihenfolge. Request-ID und Prozessgeneration bleiben dabei getrennte
monotone Namensräume. S0.3c-5b2b übernimmt auch die ausgehende Auflösung:
Cache-Misses erzeugen eine feste `NETA`-Nachricht; Zieladresse, Request-ID,
Probe-ID und Dienstgeneration liegen in einem geschützten
250-ms-Einmalvertrag. Nur der
verifizierte Dienst darf über Syscall 64 den echten Request auslösen. Der
RTL8139-Lauf prüft den resultierenden Ethernet-Frame am QEMU-Socket. Damit ist
der lokale ARP-Request-/Reply-Pfad vermittelt; ICMP und die DHCP-
Konfigurationsentscheidung folgen über denselben eng begrenzten Vertrag.

S0.3c-5c entfernt auch den autonomen ICMP-Echo-Responder aus Ring 0. In dieser
Zwischenstufe prüfte der Kernel noch Ethernet-, IPv4- und ICMP-Grenzen sowie
beide Prüfsummen und veröffentlichte einen gültigen Request nur als festes
`NETI`-Objekt an die gesunde Ring-3-Domäne. Der geschützte Kontext umfasst
Request-ID, Dienstgeneration, Quell-IP/-MAC, Identifier, Sequenz und maximal
32 Payloadbytes. Eine absolute 250-ms-Einmalautorität wird vor dem einzigen
Sendepunkt zusammen mit dem Kontext verbraucht. Syscall 72 ist ausschließlich
im Default-Deny-Profil des Dienstes freigegeben; normale Prozesse erhalten
keine Antwortautorität und Fehler reaktivieren keinen Kernel-Fallback. Der
RTL8139-Runtime-Test injiziert den Request und validiert den tatsächlich am
QEMU-Socket ausgegebenen Echo-Reply einschließlich Checksumme.

S0.3c-5d1 entfernt anschließend die direkte DHCP-Lease-Publikation aus dem
Ring-0-Transport. Der Kernel validiert das empfangene ACK und schützt
`{request_id, ip, netmask, gateway, dns, lease_seconds}` redundant, mutiert den aktiven
Netzwerkzustand aber erst nach einer zweiten semantischen Prüfung durch den
gesunden Ring-3-Dienst. Ein eigener Kernel-zu-Endpoint-Owner-Ingress mit der
reservierten Absenderidentität `(0,0)` transportiert das feste 28-Byte-`NETD`-
Objekt ohne ambienten Client und ohne künstliche Prozessidentität. Syscall 73
ist ausschließlich im Dienstprofil freigegeben und verbraucht eine an
Prozessgeneration und Request-ID gebundene 1-s-Einmalautorität. Kontext und
Autorität verschwinden vor der eigentlichen Konfigurationsmutation. Der
Bootpfad wartet höchstens 10 s auf einen gesunden Dienst und höchstens 1,5 s
auf den Commit; andernfalls bleibt das Interface definiert unkonfiguriert.
Der echte RTL8139-Nachweis fordert die Vermittlungsmarker vor `BOOT_OK`.
UDP-Transport, Lease-Erneuerung und Rebind bilden S0.3c-5d2.

S0.3c-5d2a zieht einen ersten eng begrenzten UDP-Datenschritt über dieselbe
Grenze: Nur Port 9000, maximal 32 Byte und Datagramme mit vorhandener gültiger
Prüfsumme werden als festes `NETU`-Objekt angenommen. Quell-IP/-MAC, beide
Ports und Payload werden redundant geschützt und an eine 250-ms-
Einmalautorität der aktuellen Dienstgeneration gebunden. Ring 3 prüft die
gesamte Struktur erneut; Syscall 74 verbraucht Kontext und Autorität, bevor
der Kernel genau eine Antwort mit vertauschten Ports sendet. Queue-Druck,
Deadline, Integritätsfehler oder ungültige Semantik erzeugen keine Antwort und
reaktivieren keinen Kernelpfad. Der RTL8139-Test validiert den tatsächlich am
QEMU-Socket beobachteten Frame samt UDP-Prüfsumme. Dies ist noch keine Socket-
ABI. S0.3c-5d2b2a erweitert den Dienstpfad inzwischen um vier statische,
generationgebundene Port-Bindings; S0.3c-5d2b2b schließt die zeitgebundene
Lease-Erneuerung ab.

S0.3c-5d2b1 ergänzt die zeitliche Autoritätsgrenze. Der ACK muss eine
Leasezeit zwischen 60 Sekunden und sieben Tagen enthalten; Transport,
geschützter Vorschlag und Ring-3-Dienst validieren denselben Wert. Nach dem
Commit liegt `{process_generation, ip, lease_seconds, deadline_ms}` als
Primary/Shadow-Critical-Object vor. Der Supervisor-Worker prüft die absolute
monotone Deadline in seinem festen 10-ms-Raster. Bei Ablauf werden IP,
Netzmaske, Gateway, DNS und die zugehörige Gateway-Bindung entzogen, ohne
Renewal im Kernel zu simulieren. Korruption und Dienst-Fence räumen dieselbe
Autorität fail-closed. Ein ausschließlich im Testbuild aktives 2500-ms-Limit
beweist den Ablauf mit RTL8139 und weiter betriebsfähiger Shell. Automatisches
Der nachfolgende Ring-3-Automat S0.3c-5d2b2b erneuert diese Lease vor Ablauf.

S0.3c-5d2b2a verwendet je Binding einen redundanten Descriptor, eine eigene
Einmalautorität und einen eigenen geschützten Datagrammkontext. Handles tragen
Slot und 24-Bit-Generation; zusätzlich muss die aktuelle Prozessgeneration des
Netzdienstes übereinstimmen. Doppelte Ports, erschöpfte Slots, stale Handles,
Antworten nach 250 ms und Antworten nach Fence werden vor dem NIC-Sendepunkt
abgewiesen. Der Kernel akzeptiert ausschließlich gültige UDP-Prüfsummen und
höchstens 32 Byte, übergibt Handle und vollständigen Peer-Kontext als `NETV`
an Ring 3 und verbraucht Autorität und Kontext vor dem einzigen Sendepunkt.
Port 9000 bleibt kompatibel, Port 9001 dient als realer Mehrbinding-Nachweis.
Anwendungs-Sockets und unbeschränkte Port-/Pufferzahlen gehören weiterhin
nicht zu diesem Safety-Dienstvertrag.

S0.3c-5d2b2b verschiebt die Renew-/Rebind-Entscheidung in einen reinen,
heapfreien Ring-3-Zustandsautomaten. Ein nach jedem Commit zugestelltes
`NETL`-Objekt enthält IP, T1, T2, Ablaufintervall, Operation und Request-ID.
Der Automat verwendet absolute 64-Bit-Monotonzeit, unternimmt höchstens drei
Versuche je Phase und führt pro Aufruf höchstens einen Zustandsübergang aus.
Syscall 78 validiert eine versionierte 16-Byte-Anfrage und stößt genau einen
broadcast DHCPREQUEST an; er wartet weder auf Netzwerk noch auf Timer.

Die zugehörige Kerneltransaktion ist an Dienstgeneration, erwartete IP,
Operation, monotone Transaktions-ID und eine 1,5-s-Deadline gebunden. Diese
Metadaten und die Einmalautorität liegen redundant geschützt vor. Der
Supervisor-Worker prüft pro Durchlauf höchstens ein DHCP-Reply. Ein gültiges
ACK passiert erneut den bestehenden geschützten Ring-3-Commit; NAK, Timeout,
Fence und Neustart löschen Autorität und Kontext fail-closed. Das v1-Profil
sendet Renew und Rebind bewusst als Broadcast und hält deshalb keine weitere
ungeschützte Serveradresse im Kernel. Ein realer RTL8139-Lauf mit verkürzter
Testlease bestätigt `DHCP_RENEW_REQUESTED -> DHCP_RENEWED`. Damit ist
S0.3c-5d2 geschlossen; die verbliebene Ring-0-Protokollzustands- und allgemeine
Socket-Demultiplexlogik bildet S0.3c-5e.

S0.3c-5e1 führt dafür einen vollständigen, aber noch parallelen RX-Handoff ein.
Neben den bestehenden DHCP-, Legacy- und Monitorqueues besitzt `netdev` eine
eigene statische Queue mit acht Slots zu je 1518 Byte. IRQ-/Poll-Produzenten
kopieren Frames begrenzt und verwerfen bei Druck, statt zu warten. Syscall 79
ist nur für die aktuelle gesunde Dienstgeneration freigegeben, validiert den
gesamten 1536-Byte-Ausgabebereich vor dem Dequeue und liefert leer sofort
`EAGAIN`. Ein Restart setzt den Queue-Lesepunkt auf den aktuellen Schreibpunkt
und verhindert damit Übergaben aus einer alten Generation.

Der Ring-3-Dienst prüft Version, Strukturgröße, Framegrenzen, Padding und
EtherType erneut. Der Kernel veröffentlicht die einmalige Diagnosebestätigung
erst nach erfolgreichem Copy-out; der folgende Dienstreport muss PID,
Generation und EtherType exakt treffen und verbraucht sie. Der RTL8139-Smoke
verlangt den Marker `REIST_NETWORK FRAME_HANDOFF`. Dieser Zwischenstand belegte
zunächst nur den Transport. Die nachfolgenden S0.3c-5e2-Pakete übernahmen
IPv4-/UDP-/DHCP-Zustand und entfernten den parallelen Ring-0-Demux erst nach
funktional äquivalenten Druck-, Restart- und Fault-Injection-Tests.

S0.3c-5e2a setzt auf diesem Transport einen ersten begrenzten Ring-3-Parser
auf. Der heapfreie IPv4-v1-Code akzeptiert nur Ethernet-II/IPv4 bis 1518 Byte,
IHL 20 bis 60 Byte, konsistente Gesamtlängen, TTL ungleich null und eine
gültige Headerprüfsumme. IPv4-Fragmente werden in v1 vollständig verworfen.
Das 28-Byte-Ergebnis enthält ausschließlich validierte Offsets, Längen,
Adressen und Protokoll. Der Supervisor bindet den Diagnosemarker
`REIST_NETWORK IPV4_PARSED_RING3` an eine tatsächlich an dieselbe PID und
Generation gelieferte IPv4-Frame-Berechtigung und akzeptiert nur ICMP oder
UDP. Diese Korrelation ist Nachweis-, nicht Ausgabeberechtigung; Ring-0-Demux
und Protokollzustand bleiben bis S0.3c-5e2b aktiv.

S0.3c-5e2b1 ergänzt darauf UDP-v1. Der Parser ruft den vollständigen IPv4-
Validator erneut auf, verlangt ein nichtnull Portpaar, exakt acht Byte plus
Nutzlast als UDP-Länge und eine verpflichtende Prüfsumme über IPv4-
Pseudoheader, UDP-Header und höchstens 1476 Nutzdatenbytes. Das feste
20-Byte-Ergebnis enthält nur Ports, Datagrammlänge, validierten Payloadoffset,
Payloadlänge und Prüfsumme. Eine getrennte PID-/Generationsberechtigung bindet
`REIST_NETWORK UDP_PARSED_RING3` an ein tatsächlich geliefertes IPv4/UDP-
Frame. Sie autorisiert weder Demultiplex noch Antwort; diese Zustände wechseln
erst in S0.3c-5e2b2 aus Ring 0.

S0.3c-5e2b2a macht den ersten UDP-Datenpfad exklusiv dienstbesessen. Nach dem
Copy-out veröffentlicht der Supervisor für höchstens 250 ms nur CRC32,
Framegröße und Dienstgeneration als geschütztes Lieferobjekt. Der Ring-3-
Parser reicht über Syscall 80 einen festen 40-Byte-Entscheid zurück. Kernel und
SDK prüfen Version, Größe, Userbereiche, Frame-CRC, Dienstgeneration, lokale
Zieladresse, Quellidentität und das aktive Binding, bevor eine bereits
vorhandene UDP-Antwortautorität erzeugt wird. Copy-out-Fehler widerrufen diese
Autorität. Ungültige, zu große oder ungebundene UDP-Frames werden mit einem
kanonischen Drop-Entscheid konsumiert. `netstack` veröffentlicht Datagramme an
aktiven Dienstports nicht zusätzlich über den Legacy-Pfad; andere Ports und
DHCP bleiben bis S0.3c-5e2b2b unverändert. Der RTL8139-Nachweis verlangt
`UDP_INGRESS_RING3` und die vermittelte Echoantwort bis `TEST_OK`.

S0.3c-5e2b2b1 ergänzt den DHCP-v1-Shadow-Parser direkt im Ring-3-Dienst. Er
akzeptiert höchstens 548 Byte DHCP-Nutzlast und nur BOOTREPLY über UDP 67 nach
68. Ethernet/IPv4-Grenzen, Fragmentfreiheit, BOOTP-Hardwaretyp und -länge,
Transaktions-ID, Client-MAC und Magic-Cookie werden vor der Optionsauswertung
geprüft. Die Optionsschleife ist durch die validierte UDP-Länge begrenzt,
verlangt END und lehnt abgeschnittene oder doppelte kritische Optionen ab.
Nur OFFER, ACK und NAK sind zulässig. IPv4 erlaubt für UDP eine Nullprüfsumme;
der DHCP-Parser akzeptiert sie ausschließlich in diesem eng begrenzten Pfad
und markiert sie im Ergebnis, während jede vorhandene Prüfsumme korrekt sein
muss. Das feste 52-Byte-Ergebnis enthält nur validierte Werte und Offsets.

Der Supervisor erkennt einen strukturellen 67→68-Kandidaten und veröffentlicht
dazu eine PID-/Generations- und Frame-CRC-gebundene Diagnoseberechtigung. Erst
der erfolgreiche Ring-3-Parser kann sie verbrauchen und
`DHCP_PARSED_RING3` auslösen. S0.3c-5e2b2b2 bindet dieses validierte Ergebnis
anschließend an die geschützte Boot- oder Renewal-Transaktion; ohne passende
Autorität entsteht weder Lease- noch Ausgabeautorität.

S0.3c-5e2b2b2a verschiebt Renewal/Rebind bereits aus diesem Parallelpfad. Der
append-only Syscall 81 übernimmt ein festes 52-Byte-Ergebnis und prüft vor
jeder Mutation ABI, Nachrichtentyp, Optionsmenge, lokale Client-MAC,
Dienstidentität und -generation, Frame-CRC, 250-ms-Lieferdeadline und die
geschützt gespeicherte DHCP-Transaktions-ID. Die historisch auf x86 in
Hostreihenfolge übertragene XID wird an dieser Grenze explizit in ihren
Wire-Wert überführt; eine stille Endian-Annahme ist damit ausgeschlossen.
ACK muss Netzmaske, Gateway, DNS und Lease enthalten und darf nur die bereits
geleaste Adresse erneuern. NAK löscht die geschützte Lease über den vorhandenen
Fail-Closed-Pfad.

Ein erfolgreicher Dienstentscheid verbraucht zunächst das
CRC-/generationgebundene Lieferobjekt und beendet dann den Transportzustand,
bevor die bestehende geschützte Commit-Sequenz startet. Der reale RTL8139-
Nachweis erreicht `DHCP_RENEW_INGRESS_RING3` vor `DHCP_RENEWED`.

S0.3c-5e2b2b2b1 verschiebt auch die Bootentscheidung in den Dienst. Der
append-only Syscall 82 darf nur von der aktuellen gesunden Dienstgeneration
eine geschützte, absolut auf 1.500 ms begrenzte Transaktion eröffnen. Ring 0
sendet pro Übergang genau ein DISCOVER- beziehungsweise REQUEST-Frame und
pollt weder Netz noch Timer. OFFER und ACK gelangen über den bestehenden
Frame-Handoff zum heapfreien Ring-3-Parser; der Supervisor bindet das Ergebnis
an Dienstgeneration, Frame-CRC, Client-MAC, XID, Server-ID, angebotene Adresse
und Lieferdeadline. Erst ein passendes ACK darf die bestehende geschützte
Lease-Commit-Sequenz auslösen. NAK oder Timeout löschen die Bootautorität. Der
Dienst versucht den Ablauf höchstens dreimal, der Kernel wartet insgesamt
höchstens sechs Sekunden und setzt den Boot ohne IP fort. Der RTL8139-Nachweis
bestätigt die vollständige Reihenfolge bis `BOOT_OK`.

S0.3c-5e2b2b2b2 entfernt die früheren synchronen Parserroutinen, die dedizierte
Vier-Slot-DHCP-Queue und den Supervisor-Poller vollständig. Eingehende DHCP-
Frames gelangen ausschließlich über die feste Service-Frame-Queue zum Ring-3-
Parser; Ring 0 sendet nur die einzeln autorisierten Transportframes und
committet ein vollständig korreliertes Ergebnis. Reale RTL8139-Läufe belegen
Boot und Renewal nach dem Rückbau; der Bootlauf umfasst zusätzlich Dienst-
Crash, Restart und Queue-Druck. Der anschließende Rückbau entfernt auch den
allgemeinen Ring-0-UDP-Parser, dessen Legacy-Einspeisung und die unbenutzte
direkte Echo-Sendehilfe. Der Kernel verwirft UDP-Eingang fail-closed; nur
`supervisor_network_udp_ingress` darf nach CRC-, Generations-, Deadline- und
Binding-Prüfung eine Antwortautorität erzeugen. Zu diesem Zwischenstand blieb
der Ring-0-IPv4/ICMP-Fallback außerhalb des gesunden Dienstpfads offen; das
nachfolgende S0.3c-5e2c bereitete und vollzog dessen kontrollierten Rückbau.

S0.3c-5e2c bereitet dessen kontrollierten Rückbau vor. Ein separater,
heapfreier Ring-3-Parser akzeptiert nur vollständig vom IPv4-v1-Parser
validierte ICMP-Echo-Requests und -Replies mit Code null. Er prüft die gesamte
ICMP-Nutzlast einschließlich ungerader Länge und veröffentlicht ausschließlich
ein festes 28-Byte-Ergebnis mit validierten Adressen, Offsets, Identifier und
Sequenz. Der Supervisor bindet `ICMP_PARSED_RING3` an die tatsächlich
ausgelieferte PID, Prozessgeneration und Frame-CRC. Dieser Shadow-Nachweis
besitzt bewusst keine Autorität. S0.3c-5e2d schließt den ICMP-Rückbau: Für
jeden rohen ICMP-Kandidaten erzeugt Ring 0 ein redundanzgeschütztes Ticket mit
Dienstgeneration, Frame-CRC und absoluter 250-ms-Deadline. Der append-only
Syscall 83 verbraucht es genau einmal und akzeptiert ausschließlich kanonisches
`DROP`, `ECHO_REQUEST` oder `ECHO_REPLY`. Erst der Request darf die bereits
geschützte `NETI`-Einmalautorität erzeugen; ein Reply darf nur die exakt
erwartete Ping-Identität abschließen. Der alte Ring-0-ICMP-Parser ist entfernt
und sein Eingang fällt geschlossen aus. Der RTL8139-Nachweis umfasst Parser,
Ingress und den wirklich ausgesendeten vermittelten Reply. S0.3c-5e2e entfernt
danach `handle_ip_packet` vollständig. S0.3c-5f entfernt zusätzlich die gesamte
Legacy-RX-Queue, `netstack_process_packet`, den Ring-0-ARP-Parser und den
ungeschützten ARP-Cache. Der statische Ring-3-Frame-Handoff ist damit die
einzige ARP-/IPv4-Eingangsentscheidung. Die parallele Monitorqueue ist
ausdrücklich autoritätslos. Ausgehende Auflösung liest ausschließlich den
`critical_object`-geschützten ARP-Cache; ein Routenwechsel widerruft betroffene
Gateway-Bindungen vor der neuen Konfigurationspublikation. Damit ist S0.3c-5
abgeschlossen.

S0.3c-6a härtet vor der Prozessmigration die bestehende persistente
Fehlerdomäne: Jede Storage-Schreiboperation und jede VFS-Mutation hat genau
einen geschützt gespeicherten Aktivzustand und eine saturierend berechnete
absolute Deadline. Reentranz oder Parallelüberlappung wird vor Seiteneffekten
abgewiesen; inkonsistente Übergänge fencen den Blockpfad beziehungsweise
schalten das Dateisystem read-only. Das folgende S0.3c-6b ersetzt direkte
Aufrufe schrittweise durch versioniertes IPC mit statischen Request-Pools.
S0.3c-6b stellt dafür den gemeinsamen Dataplane bereit: acht feste Slots,
24-Bit-Generationshandles, versionierte Block- und VFS-Operationen sowie
maximal 512 Byte Nutzdaten. Payloads liegen als CRC-geschützte Primär- und
Schattenkopie vor; Requestmetadaten und die einzige autorisierte
Dienstidentität sind `critical_object`-geschützt. Der kleine IPC-Control-Plane
transportiert künftig nur Handles und Status. Claim/Complete/Collect prüfen
Client- und Dienstgeneration atomar; Exit-Cleanup widerruft idempotent.

S0.3c-6c verbindet diesen Dataplane mit `STORAGE.PRG`. Das Prozessprofil ist
Default-Deny und erlaubt nur Bind, Claim, kernelvermitteltes Block-Read und
Complete neben den elementaren Zeit-/Exit-Syscalls. Die Dienstidentität und
der Restartzustand sind redundant geschützt; Start ist auf eine Sekunde und
Recovery auf drei Neustarts begrenzt. Danach fencet der Supervisor
Blockschreiben und VFS-Mutationen. Clients dürfen höchstens zwei Requests mit
maximal fünf Sekunden Laufzeit halten. Der erste reale End-to-End-Pfad liest
Sektor 0 einer erkannten ATA-Ressource und liefert ihn generationssicher an
den Client zurück; Schreib- und VFS-Ausführung bleiben bis S0.3c-6d gesperrt.

S0.3c-6d1 prüft den Restartpfad mit einem separaten, nicht auslieferbaren
Fault-Injection-Build. Nach einem erfolgreichen ATA-Sektorread beendet der
Kernel genau die erste Storage-Dienstgeneration, bevor sie Daten kopieren oder
den Request abschließen kann. Prozess-Cleanup widerruft den beanspruchten Slot;
der Supervisor bindet ausschließlich die neue Generation. Der Client darf den
stalen Handle nicht weiterverwenden und wiederholt genau einmal innerhalb
einer Zwei-Sekunden-Grenze. Erst ein erneut validierter MBR schließt den Gate.
Der normale Build enthält weder Trigger noch zusätzliche Autorität. Der
persistente Power-Loss-Pfad bleibt 6d3.

S0.3c-6d2 begrenzt jeden vermittelten ATA-Read auf zwei Versuche. Nach zwei
Fehlern wird die Ressourcen-ID in der `critical_object`-geschützten
Dienstkontrolle quarantänisiert. Der auslösende Request endet mit `-EIO`;
weitere Requests gegen diese Ressource werden ohne Hardwarekontakt mit
`-EHOSTDOWN` abgeschlossen. Die Quarantäne überlebt Dienstneustarts innerhalb
desselben Boots und kann daher nicht durch einen Generationstausch umgangen
werden. Ein separater Build injiziert den Fehler genau einmal; Produktion
enthält nur Retry, Quarantäne und Fehlerpropagation. In S0.3c-6d2 war eine
kontrollierte Requalifizierung noch nicht erlaubt; S0.3c-6e ergänzt sie unten
zusammen mit dem verifizierten Medien-Selbsttest.

S0.3c-6d3 koppelt persistente Journal-Recovery an die Dienst-Reintegration.
Der Testdatenträger enthält eine ACTIVE-Transaktion, zwei bereits veränderte
Zielsektoren, redundante Undo-Daten und eine absichtlich beschädigte primäre
Headerkopie. Boot-Recovery muss die gültige Kopie wählen, beide Zielsektoren
restaurieren sowie Primär- und Spiegelheader identisch CLEAN schreiben. Danach
wartet der Harness auf die vollständige Supervisor-Probe-Sequenz und verlangt
vom neu gebundenen Storage-Dienst einen echten MBR-Read. Die Servicekontrolle
besitzt dafür einen expliziten Aktivierungszustand: `poll()` darf einen noch
nicht gestarteten Dienst nicht als Ausfall behandeln. Alle Read/Repair/Update-
Operationen derselben redundanten Kontrollinstanz sind lokal IRQ-serialisiert,
damit Worker und Bind-Syscall keine Kopien gegeneinander überschreiben.

S0.3c-6e erweitert den Wiederanlauf auf die physische Medienressource. ATA und
FDD erhalten beim Boot einen `critical_object`-geschützten Fingerprint aus
Controllerlage, Geometrie beziehungsweise Modell/Kapazität und Bootsektor-CRC.
Nach einem I/O-Ausfall bleibt das Medium quarantänisiert. Der Supervisor prüft
es mit monoton begrenztem Backoff erneut, verifiziert die Geräteidentität und
verlangt zwei identische frische Bootsektor-Reads. Nur danach wird ein reiner
Lesefehler automatisch nach `ONLINE_RW` reintegriert. Ein unklar beendeter
Schreibzugriff setzt das Medium dagegen auf `ONLINE_RO` und hält das globale
Storage-/VFS-Schreib-Fence geschlossen. Es gibt keine blinde Wiederholung.

Dieser Vertrag gilt für alle heutigen und künftigen persistenten Medien. Die
Zustandsfolge lautet `ONLINE_RW -> QUARANTINED -> PROBING -> ONLINE_RW` bei
einem verifizierten reinen Lesefehler und `... -> ONLINE_RO` bei unklarem
Schreibabschluss. Automatische Probes besitzen ein festes Versuchslimit; nach
dessen Erschöpfung endet `PROBING` diagnostizierbar in `ONLINE_RO` oder
`OFFLINE`, ohne weitere automatische Hardwarezugriffe. S0.3c-6f1 bis
S0.3c-6f4 liefern für explizit markierte
REIST-FAT12-Medien Undo-Journal, Remap, kritische Replikate und geordnete
Dateitransaktionen. S0.3c-6f5 ergänzt die deterministische Persistenz-
Fehlermatrix. S0.3c-hw11 begrenzt die physische SATA-Hotplug-Reintegration und
ergänzt deterministische QEMU-AHCI-Backend-Fault-Injection. S0.3c-admin1
ergänzt capability-gebundene Storage-Administration; S0.3c-admin2 ergänzt
statische Komponenten-Lifecycle-Steuerung. S0.3c-layout1 ordnet ausführbare
Systemprogramme in einer begrenzten, kleingeschriebenen Hierarchie.
S0.3c-storage2 ergänzt die laufende Provisionierung: Auf einem leeren,
ungeschützten ATA-/AHCI-Medium wird eine ausgerichtete MBR-Partition erst nach
Flush und Readback als Child veröffentlicht. FAT32-Quickformat invalidiert den
alten Bootsektor, leert beide FATs in festen Chunks und publiziert den gültigen
BPB erst am Ende. Fullformat scannt danach alle Datencluster in endlichen
Requests unter einer aus der Mediengröße abgeleiteten monotonen Gesamtfrist.
Ein dreifach reproduzierbarer Zielsektorfehler bei stabilen primären und
Backup-Bootsektoren wird in beiden FATs als `0x0FFFFFF7` markiert. Kontroll-,
Flush- oder Transportfehler quarantänisieren das Medium. Nach Erfolg wird der
doppelt gelesene Boot-Fingerprint geschützt übernommen; Root- und gemountete
Ressourcen sind davon ausgeschlossen. Der
medienunabhängige Nachweis für EXT2, fremde FAT-Volumes und künftige Backends
bleibt offen. Erst ein nachgewiesenes
Recoveryprotokoll darf ein Medium nach unklarem Schreibabschluss wieder
`ONLINE_RW` schalten. Bei SATA umfasst dies COMRESET, IDENTIFY mit unverändertem
Modell und unveränderter Kapazität, zwei frische Fingerprint-Reads,
Undo-Journal-Recovery und einen weiteren frischen Read-Selbsttest vor der
Fence-Freigabe.

Der FDD-Pfad behandelt zusätzlich echtes Wechselmedien-Hotplug. Jeder
fehlgeschlagene normale FAT12-Read meldet die zugehörige Ressourcen-ID und
beendet nachfolgende Zugriffe vor Hardwarekontakt. Die kontrollierte Probe ist
der einzige Bypass: Sie setzt den FDC vollständig zurück, leert die
Reset-Interrupts, programmiert Datenrate und `SPECIFY` neu, kalibriert das
Laufwerk und führt erst dann die zwei frischen Reads aus. Ein QEMU-QMP-Gate
liest `HOTPLUG.TXT`, wirft A: im laufenden Gast aus, beobachtet Quarantäne,
legt dasselbe FAT12-Image wieder ein und verlangt `RESOURCE_REINTEGRATED_RW`
sowie eine erneut erfolgreiche Datei-Lektüre. Das bestehende Mount bleibt nur
für das wiedererkannte Medium nutzbar; ein abweichender Boot-Fingerprint bleibt
quarantänisiert. Stärkere Ganzmedien-Identität und kontrolliertes
Cache-Invalidieren/Remount bei extern veränderten, aber bootsektorgleichen
Medien gehören zum noch offenen FAT12-Maintenance-Abschluss.

### Administrative Maintenance

Manuelle Administration verwendet dieselben Sicherheitsgrenzen wie ein
Fehlerpfad, besitzt aber einen expliziten, autorisierten Ausgangszustand. Die
Befehle `/sbin/devctl.prg`, `/sbin/mount.prg`, `/sbin/umount.prg`,
`/sbin/svcctl.prg` und `/sbin/chkdsk.prg`
erhalten keine direkten Port-, DMA- oder Treiberzeiger. Sie senden versionierte
Requests an eine default-deny Kernel-Schnittstelle und benötigen für
Storage-Mutationen ein generations- und mediengebundenes Maintenance-Lease.

`CHKDSK.PRG` läuft in einem eigenen Default-deny-Wartungsprofil. Dieses Profil
darf VFS-Inhalte lesen sowie versionierte Check-/Repair-Aufträge senden, aber
keine Blöcke, Controller, Ports oder DMA-Ressourcen direkt ansprechen. Der
Storage-Dienst wählt bei FAT12-Spiegelschäden nur dann eine Quelle, wenn genau
eine Kopie alle BPB- und FAT12-Bereichsinvarianten erfüllt. Vor dem ersten
Write erwirbt er das Lease atomar für die aktuell qualifizierte
Medienidentität, erzwingt Unmount/Handle-Freiheit und wiederholt die Diagnose.
Jeder alte Zielsektor wird im vorhandenen Undo-Journal erfasst; widersprüchliche
gültige Spiegel werden nicht automatisch aufgelöst.

Die nachgelagerte Clusterprüfung verwendet feste Arrays für maximal 4085
FAT12-Cluster, eine Queue für 256 Unterverzeichnisse sowie je 128 konservative
Überlängen- und Kurzdateikandidaten. Jeder Chain-Walk ist durch die validierte
Clusterzahl begrenzt. Loops, Crosslinks, Orphans, ungültige Directory-Felder
oder erschöpfte Kapazität verhindern jede Mutation. Eine normal
EOC-terminierte, eindeutig besessene reguläre Dateikette darf nach expliziter
Bestätigung entweder auf die aus der Dateigröße berechnete Kettenlänge gekürzt
oder bei einer zu großen Directory-Dateigröße auf ihre tatsächlich lesbare
Kettenkapazität begrenzt werden. Der Kurzdateipfad erfindet keine Cluster und
akzeptiert nur die exakte Gesamtdiagnose `CHAIN_SHORT`; Startcluster null sowie
freie, bad oder ungültige Links werden abgelehnt. Vor den FAT-Writes werden
alle betroffenen Sektoren beider Spiegel, vor Directory-Writes alle höchstens
64 betroffenen Directory-Sektoren im Undo-Journal gesichert. Erfolg setzt
jeweils einen vollständig sauberen Rescan voraus.
Eine separate bestätigte Reclaim-Operation darf bei der exakten
Gesamtdiagnose `ORPHAN_CLUSTER` alle allokierten Cluster mit Owner null in
beiden FATs freigeben. Sie verändert weder `0xFF7`-Bad-Cluster noch erreichbare
Ketten, journalisiert jeden geänderten FAT-Sektor und behauptet keine
Datenrettung: Unerreichbare Inhalte werden verworfen, nicht geraten oder mit
einem erfundenen Eigentümer verbunden.
Reguläre Dateikettenschleifen besitzen einen eigenen bestätigten Pfad. Er ist
nur bei der exakten Gesamtdiagnose `CHAIN_LOOP` zulässig und nur, wenn jede
Schleife vor ihrer ersten Wiederholung mindestens den aus der Dateigröße
berechneten eindeutigen Präfix enthält. Dieser Präfix wird in fester
generation-spezifischer Markierung gehalten, sein letzter Cluster auf EOC
gesetzt und der folgende Suffix nur bis vor einen markierten Präfixcluster
freigegeben. Directory-Loops, kurze Loops und gemischte Diagnosen bleiben
fail-closed. Beide FATs werden journalisiert und ein sauberer Vollscan bleibt
Abschlussbedingung.
Loopende Unterverzeichnisse werden nicht mehr von der Inhaltsdiagnose
ausgenommen: Eine generation-spezifische Markierung lässt jeden eindeutigen
Directory-Cluster genau einmal lesen und stoppt vor der Wiederholung. Der
separate bestätigte Directory-Loop-Pfad ist nur zulässig, wenn danach die
Gesamtdiagnose exakt `CHAIN_LOOP` lautet und jede Schleife ein Verzeichnis ist.
Er validiert erneut, dass der letzte eindeutige Cluster auf den markierten
Präfix zurückzeigt, und ersetzt ausschließlich diesen Rücksprung durch EOC.
Directory-Daten und Clusterallokationen bleiben unverändert; beide FAT-Writes
folgen demselben Undo-Journal- und Vollscanvertrag.
Für reguläre Ketten mit der exakten Mischdiagnose
`CHAIN_LOOP|CHAIN_SHORT` gibt es eine kombinierte Transaktion. Sie ist nur
zulässig, wenn jede Loop- und Short-Meldung denselben festen Kandidaten
abbildet. Der letzte eindeutige Cluster wird nach Rücksprungvalidierung EOC,
alle eindeutigen Cluster bleiben allokiert und der 32-Bit-Directory-Size wird
auf deren vollständige Clusterkapazität reduziert. Alle betroffenen Sektoren
beider FATs und alle eindeutigen Directory-Sektoren werden vor dem ersten
Write gemeinsam undo-journalisiert. Der anschließende Vollscan muss ohne Loop,
Short oder Orphan enden.
Crosslinks werden zusätzlich mit zwei festen Clusterzählern klassifiziert:
Gesamtreferenzen und Pflichtreferenzen innerhalb deklarierter regulärer
Dateilängen beziehungsweise vollständiger Directory-Ketten. Die bestätigte
Crosslink-Reparatur ist nur bei der exakten Diagnose
`CHAIN_CROSSLINK|CHAIN_EXCESS` zulässig und verweigert jeden Cluster mit mehr
als einer Pflichtreferenz. Dadurch darf sie ausschließlich überlange
Dateitails am Sollende trennen, Cluster mit genau einer Pflichtreferenz
erhalten und Cluster ohne Pflichtreferenz freigeben. Die Entscheidung ist
unabhängig davon, welche Directory-Datei beim Scan zuerst Owner wurde.
Echte Pflicht-Crosslinks regulärer Dateien besitzen einen getrennten
Copy-on-Write-Pfad. Er akzeptiert nur die exakte Diagnose `CHAIN_CROSSLINK`,
wenn feste Dateideskriptoren sämtliche mehrfach benötigten Cluster ohne
Directory-Beteiligung vollständig erklären. Die erste Scanreihenfolge-Datei
bleibt kanonisch; jede spätere Kollision wird in höchstens 48 verschiedene freie
Cluster kopiert. Das 64-Einträge-Undo-Journal erfasst vor jeder Mutation alle
Ziel-Datensektoren, geänderten Sektoren beider FATs und eindeutigen Directory-
Sektoren. Verifizierte Datenwrites gehen der FAT-Publikation voraus; zuletzt
wird ausschließlich der niedrige Startcluster der geklonten Datei ersetzt.
Nur danach unreferenzierte Originalcluster werden freigegeben. Fehlender
Freiraum, Kandidaten-/Journalerschöpfung, Directory-Crosslinks oder jede
zusätzliche Diagnose bleiben fail-closed. Erfolg verlangt einen sauberen
Vollscan vor Journal-`CLEAN`.
Ein weiterer getrennter Pfad deckt ausschließlich Same-Parent-Crosslinks
strikt leerer Unterverzeichnisse ab. Ein fester Kandidat bindet Parent-Sektor,
Parent-Offset, Parent-Cluster und den genau einen EOC-terminierten
Directory-Cluster. Der Inhalt muss mit gültigem `.` und `..` beginnen und am
dritten Slot enden. Die erste Scanreihenfolge-Instanz bleibt kanonisch; jede
weitere erhält einen eigenen freien Cluster. In dessen verifizierter Kopie
ändert sich nur der niedrige Self-Cluster von `.`, anschließend werden beide
FATs und zuletzt ausschließlich der niedrige Startcluster des gebundenen
Parent-Eintrags publiziert. Alle Ziel-, FAT- und Parent-Sektoren liegen vor dem
ersten Write im Undo-Journal. Unterschiedliche Parents, nichtleere oder
mehrclusterige Verzeichnisse, reguläre Dateibeteiligung und Mischdiagnosen
bleiben fail-closed, weil sie rekursive Topologieklonung erfordern.
Der append-only Nachfolgepfad
`--repair-directory-topology --confirm` vermeidet rekursive Klone: Für eine
vollständig attribuierte Aliasgruppe bleibt genau der Parent-Eintrag kanonisch,
der zum validierten `..`-Parent des gemeinsamen Verzeichnisses passt;
Same-Parent-Aliase verwenden deterministisch den ersten Eintrag. Alle weiteren
Aliase werden nach erneuter CRC32-Identitätsprüfung als gelöscht markiert. Nach
Microsoft-FAT/VFAT-Terminologie streng gebundene LFN-Slots werden über
Ordinalfolge, Short-Name-Prüfsumme, Typ und Nullcluster validiert und in
derselben Transaktion entfernt. Damit bleiben die gemeinsame nichtleere oder
mehrclusterige Verzeichniskette, Unterinhalte und FAT unverändert. Sämtliche
betroffenen Parent-Sektoren liegen vor der ersten Mutation im festen
Undo-Journal; ein sauberer Vollscan ist Voraussetzung für Journal-`CLEAN`.
Fehlende oder mehrdeutige `..`-Zuordnung, überlappende Teilketten,
reguläre Dateibeteiligung, fremde Diagnosen und Kapazitätserschöpfung bleiben
vor Seiteneffekten gesperrt.
Der FAT12-Persistenzabschluss verwendet das bestehende Undo-Journal v2 und die
append-only Remaptabelle v1. `CHKDSK --record-bad-sector` reicht nur Resource
und Sektornummer über Operation 30 ein; Blockzugriff bleibt ausschließlich im
überwachten Storage-Dienst. Zulässig sind nur FAT-/Root-Metadatensektoren eines
markierten REIST-Mediums. Zwei identische Quellreads und ein verifizierter
Ersatzwrite gehen der Tabellenpublikation voraus; anschließend werden zwei
CRC-Header mit höherer Sequenz geschrieben und erneut geladen. Tabelle und
Arbeitsspeicher sind auf acht reservierte Spares begrenzt. Unbekannte
Persistenzversionen, I/O-/Integritätsfehler und Kapazitätsende führen über den
Kernel-Completion-Kontext zur resourceweiten Read-only-Sperre. Weder
transparentes Remapping beliebiger Nutzdaten noch Rekonstruktion eines nicht
lesbaren Sektors wird behauptet.
Für die Datenrettung ergänzt `--salvage-orphans --confirm` den weiterhin
separaten destruktiven Reclaim-Pfad. Der Storage-Dienst bildet aus allen
unerreichbaren, normal EOC-terminierten FAT12-Clustern feste Kettendeskriptoren.
Jeder Nichtkopf muss genau eine eingehende Orphan-Referenz besitzen; Schleifen,
Konvergenzen, Übergänge in erreichbare oder defekte Cluster und unvollständige
Abdeckung werden vor Writes abgelehnt. Ein validiertes `FOUND.000` im Root wird
wiederverwendet oder mit begrenzt zugewiesenen Directory-Clustern erzeugt und
bei Bedarf erweitert. Jede Kette erscheint als `FILEnnnn.CHK`; die vier Ziffern
sind der ursprüngliche FAT12-Startcluster und dienen als standardnaher
Herkunftsnachweis. Die Dateigröße umfasst bewusst die vollständige
Clusterallokation, weil die ursprüngliche Nutzlänge nicht mehr beweisbar ist.
Neue Directory-Daten, beide FAT-Spiegel und sämtliche Root-/Directory-Sektoren
liegen vor der ersten Mutation gemeinsam im Undo-Journal. Die Orphan-Ketten
selbst werden weder umgeschrieben noch freigegeben; Erfolg verlangt Readback,
Flush und einen sauberen Vollscan.
Ein Unterverzeichniseintrag mit gültigen Attributen, null im hohen
Clusterwort, gültigem Startcluster und unzulässiger Größe ungleich null bleibt
für die Inhaltsdiagnose traversierbar. Nur bei der exakten Gesamtdiagnose
`DIRECTORY_INVALID` und vollständiger Kandidatenabdeckung darf
`--repair-dir-size --confirm` das 32-Bit-Größenfeld auf den FAT-konformen Wert
null setzen. Attribute, Name, Startcluster, FAT und Daten bleiben unverändert;
jeder betroffene Directory-Sektor liegt vor dem Write im Undo-Journal und ein
sauberer Vollscan schließt die Transaktion ab.
Ein ansonsten gültiger Volume-Label-Eintrag mit einem reservierten niedrigen
Startcluster oder Größenfeld ungleich null erhält einen getrennten bestätigten
Pfad. `--repair-volume-label --confirm` verlangt ebenfalls die exakte
Gesamtdiagnose `DIRECTORY_INVALID` und vollständige Kandidatenabdeckung. Nach
erneuter Feldvalidierung im geleasten Sektor werden ausschließlich Startcluster
und Größe auf null gesetzt; Labelname, Attribute, FAT und Daten bleiben
unverändert. Undo-Journal, verifizierter Write und sauberer Vollscan sind auch
hier Abschlussbedingungen.
Reguläre Dateien der Größe null mit dennoch zugewiesener Kette werden als
`CHAIN_EXCESS|DIRECTORY_INVALID` diagnostiziert. Der bestätigte
`--repair-zero-files`-Pfad akzeptiert nur diese exakte Gesamtdiagnose, eine
vollständige feste Kandidatenmenge und normal EOC-terminierte Cluster mit genau
einer Referenz und keiner Pflichtreferenz. Nach gemeinsamer Undo-
Journalisierung beider FAT-Spiegel und aller Directory-Sektoren werden nur
diese Cluster freigegeben und der niedrige Startcluster nullgesetzt. Der
frühere Inhalt gilt ausdrücklich als verworfen; Crosslinks, Loops und
gemischte Diagnosen bleiben fail-closed.
Der inverse eindeutige Fall besitzt einen getrennten Directory-only-Pfad:
Eine reguläre Datei mit positiver Größe und Startcluster null hat keine lesbare
Kette. `--repair-zero-start --confirm` akzeptiert ausschließlich die exakte
Gesamtdiagnose `CHAIN_SHORT`, wenn jede Short-Meldung einen solchen festen
Kandidaten besitzt. Nach Lease-Revalidierung und Undo-Journalisierung wird nur
das 32-Bit-Größenfeld nullgesetzt. FAT-Spiegel, Startcluster, Name, Attribute,
Zeitfelder und Datenbereiche bleiben unverändert; ein sauberer Vollscan ist
weiterhin Abschlussbedingung.
Die feste Directory-Queue trägt zusätzlich den Parent-Cluster. Dadurch werden
nur exakt leerzeichenaufgefüllte `.`- und `..`-Einträge als Sonderfälle
erkannt und gegen aktuellen beziehungsweise übergeordneten Cluster geprüft;
Dot-Einträge im Root oder falsche Beziehungen bleiben unreparierbar. Bei einer
reinen `DIRECTORY_INVALID`-Diagnose aus ausschließlich korrekten Dot-Verweisen
mit Größe ungleich null darf `--repair-dot-size --confirm` nach Lease-
Revalidierung und Undo-Journalisierung nur deren 32-Bit-Größenfelder
nullsetzen. Namen, Attribute, Clusterfelder, FAT und Daten bleiben unverändert.
Der komplementäre `--repair-dot-cluster --confirm`-Pfad akzeptiert nur exakte
Dot-Einträge außerhalb des Root mit Größe null und einem falschen niedrigen
Startcluster. Die feste Scanbeziehung bestimmt den Ersatz eindeutig als Self-
oder Parent-Cluster. Nach erneutem Kandidatenabgleich und Undo-
Journalisierung wird ausschließlich dieses 16-Bit-Feld ersetzt. Kombinierte
Größen-/Clusterfehler, Root-Dot-Einträge und jede weitere Diagnose bleiben
fail-closed.

`device down` bedeutet Quiesce, Fence, begrenztes Drain, Unmount abhängiger
Volumes und anschließenden Zustand `ADMIN_DOWN`; Kernelcode wird dabei nicht
dynamisch entladen. `device up` requalifiziert Transport und Identität, führt
einen Selbsttest aus und publiziert die Ressource erst danach. `umount` sperrt
neue Opens vor der Prüfung vorhandener Handles; `mount` validiert Resource,
Dateisystemtyp, Pfad und Parent-/Child-Topologie vor jeder Veröffentlichung.
Das laufende Root-Volume sowie Scheduler, Uhr, Interruptkern und dessen
Storage-Elternressource sind aus dem laufenden System nicht deaktivierbar. Die
beim Start bestimmte Root-/Parent-Maske liegt in integritätsgeschützten
Metadaten und bleibt daher auch dann gesperrt, wenn der Root-Mount nach einem
Medienausfall nicht mehr inventarisierbar ist.

Damit ein plötzlicher Verlust des Root-Datenträgers nicht zugleich die
Administrationsfähigkeit entfernt, lädt der Kernel beim Boot ein festes
Rescue-Allowlist-Abbild in RAM: `/bin/shell.prg`, `/sbin/devctl.prg`,
`/sbin/mount.prg`, `/sbin/umount.prg`, `/sbin/svcctl.prg`,
`/libexec/reist/storage.prg`, `/libexec/reist/reist.prg`, `/sbin/drives.prg`,
`/bin/ls.prg`, `/bin/cat.prg` und `/sbin/chkdsk.prg`. Jedes Image ist auf
96 KiB, der gesamte statische Pool auf 224 KiB begrenzt. Die größere
Einzelgrenze nimmt den gewachsenen, weiterhin isolierten Storage-Dienst auf;
die seitenbündige Gesamtgrenze nimmt die vollständige Allowlist auf und bleibt
fest. Die Programme werden vor der Aufnahme als
MYPR-Abbild validiert und vor jedem Start erneut gegen CRC und redundant
geschützte Offset-/Größenmetadaten geprüft. Nur die exakten kanonischen Pfade
dürfen diesen read-only Fallback verwenden. Eine feste vollständige
Legacy-Tabelle übersetzt alte Root-Pfade ohne ein zweites Programmbild; eine
basename-basierte Suche verleiht keine Autorität. Das ist
kein Abbild veränderlicher Nutzdaten und keine unabhängige Redundanz: RAM,
Kernel und CPU bleiben eine gemeinsame Fehlerdomäne.

Systemdienste und Treiber werden nur über eine feste Registry mit stabilen IDs
und statischen Abhängigkeiten administrierbar. `down`, `up` und `restart`
besitzen je eine monotone Gesamtdauer, generation-scoped Revocation und einen
terminalen Fail-Closed-Zustand. Nicht registrierte Komponenten lehnen die
Operation ab; ein dynamischer universeller Treiber-Unloader ist ausdrücklich
nicht Teil der Architektur. Syscall 91 ist ausschließlich dem default-deny
Profil von `SVCCTL.PRG` erlaubt. Der Netzwerkdienst muss vor seinem Treiber
beendet und nach ihm gestartet werden; der Storage-Dienst bleibt an Uhr und
geschützten Root-Storage gebunden. Generationen laufen bei Erschöpfung nicht
um, sondern sperren weitere Übergänge fail-closed.

### Standby-Handover-Protokoll

S0.3c-7a definiert den plattformneutralen Sicherheitskern für zwei Kanäle.
Sein fester Status enthält `active_node`, `standby_node`, Lease-Dauer und
-Deadline, eine 64-Bit-Epoche, die extern bestätigte Fence-Epoche sowie eine
monotone Transitionssequenz. Der Status liegt redundant mit ECC, CRC und
semantischem Validator vor. Ein Standby darf nur übernehmen, wenn

1. die beobachtete Epoche noch exakt aktuell ist,
2. die Lease des Active abgelaufen ist,
3. ein unabhängiger Kanal das Fence genau dieser Epoche bestätigt hat und
4. Epoche sowie Sequenz noch erhöht werden können.

Der Rollenwechsel tauscht die IDs, erhöht die Epoche, löscht die alte
Fence-Bestätigung und eröffnet eine neue begrenzte Lease. Damit kann ein alter
Active weder verlängern noch mit einem alten Fence erneut übernehmen. Der
Protokollkern allokiert nicht und führt keine I/O aus. Die derzeitige
Implementierung ist absichtlich noch nicht im Bootpfad aktiviert: Ein lokaler
RAM-Datensatz wäre keine unabhängige Fehlerdomäne und dürfte keinen realen
Ausgang freigeben. S0.3c-7b muss Transport, rücklesbares Interlock und eigene
Zeit-/Stromversorgung bereitstellen; erst S0.3c-7c darf reale Übernahme und
Reintegration behaupten.

S0.3c-7b1 ersetzt direkte Fence-Bestätigung durch einen fest gebundenen
Backendvertrag. Das Backend muss das Fence für `(active_node, epoch)` getrennt
anfordern und später exakt für dasselbe Paar rücklesbar bestätigen. Ohne
Backend verweigert der Handover-Kern seine Initialisierung. Da ein externer
Transport warten oder I/O ausführen kann, werden Callbacks außerhalb der
kurzen IRQ-gesperrten Kontrollsektion aufgerufen. Vor Veröffentlichung der
Bestätigung liest der Kern den geschützten Status erneut und vergleicht
Active-ID, Epoche, Lease und Transitionssequenz mit dem Snapshot. Jede
zwischenzeitliche Änderung verwirft das Readback. Diese Schnittstelle ist nur
die sichere Aufnahme für S0.3c-7b2; ein Host-Fake ist kein unabhängiges
Interlock und wird nicht als Failover-Nachweis gezählt.

S0.3c-7b2a ergänzt ein ausführbares Referenzbackend über den dedizierten
COM2-UART. Request und Ack sind feste 24-Byte-Frames mit Magic, Version, Typ,
Länge, Active-ID, 64-Bit-Epoche und CRC32. Der Kernel akzeptiert nur den
vollständigen Ack für das zuletzt gesendete Tupel; Sende- und Empfangsloops
enden jeweils an einer monotonen 1-s-Deadline. Im isolierten QEMU-Profil läuft
der bestätigende Supervisor als eigener Hostprozess und der Gast führt nach
Leaseablauf einen echten Rollenwechsel aus. Dieser Test trennt Softwareprozess
und Transportkanal von COM1, qualifiziert aber keine elektrische
Unabhängigkeit. S0.3c-7b2b benötigt weiterhin ein Zielhardware-Interlock mit
eigener Stromversorgung und Zeitbasis.

S0.3c-7c1 erweitert das Referenzprofil auf zwei gleichzeitig laufende
QEMU-Prozesse. Das Active-Image sendet einen CRC-geschützten Epoch-Snapshot.
Das Standby-Image kündigt zuerst mit einem eigenen `READY`-Frame an, dass sein
UART empfangsbereit ist; erst dann leitet der Host den Snapshot weiter. Diese
Flusskontrolle ist notwendig, weil ein UART-FIFO keinen vollständigen
24-Byte-Frame vor Initialisierung puffern muss. Nach Leaseablauf stoppt der
Host den Active-Prozess und prüft dessen Ende, bevor er den Fence-Ack an den
Standby sendet. Der übernehmende Gast muss anschließend den kompletten
Supervisor-, Dienst- und Ring-3-Smoke bestehen. Das ist ein realer
Zwei-Prozess-Failover-Test, aber weder kontinuierliche Dienstzustandsreplikation
noch ein Nachweis unabhängiger Hardware oder kontrollierter Reintegration.

S0.3c-7c2a ergänzt eine geschützte Referenz-Zustandsmaschine. Ihr fester
40-Byte-Dienstzustand bindet Quelle, Dienst-ID, Epoche, 64-Bit-Sequenz und Wert;
Primär- und Shadow-Kopie werden durch ECC, CRC und Invarianten geprüft. Der
Transport kapselt ihn in einen versionierten 52-Byte-CRC-Frame. Innerhalb einer
Epoche wird ausschließlich `sequence + 1` angenommen. Replays, Lücken,
Quellenwechsel und Epochenwechsel werden vor jeder Veröffentlichung verworfen.
Nach einem bestätigten Takeover erhöht der neue Active Epoche und Sequenz und
sendet den promovierten Zustand. Ein dritter QEMU-Kanal wird erst danach
gestartet, übernimmt diesen Snapshot und bleibt nach expliziten Negativtests
ohne Lease-/Takeover-Autorität gefenceter Standby. Der Host prüft gleichzeitig,
dass der neue Active bis zum vollständigen Ring-3-Smoke weiterläuft. Diese
Referenz beweist Protokollreihenfolge und kontrollierten Prozess-Rejoin, nicht
Catch-up oder Ausgangsfreigabe eines realen Produktionsdienstes und keine
elektrisch unabhängige Hardware.

S0.3c-7c2b bindet die Referenz-Zustandsmaschine an einen realen
Produktionszustand: den CRC32-Fingerprint des erkannten ATA-Bootsektors mit
gültiger MBR-Signatur. Standby-Kanäle setzen vor dem Dateisystem-Mount ein
eigenes reversibles Storage-Handover-Gate. Dieses Gate liegt zusätzlich zu den
permanenten Fatal-/Integritäts-Fences und darf diese niemals zurücksetzen.
`storage_write_begin()` verweigert deshalb jede ATA- oder FDD-Mutation, solange
der Kanal nicht autorisiert wurde. Der Catch-up umfasst genau drei streng
sequenzierte Frames; jeder wird gegen den lokalen Datenträger geprüft. Eine
Freigabe ist nur nach Rollenwechsel und erfolgreicher Publikation des
promovierten Zustands möglich und führt den lokalen Fingerprint-Selbsttest
erneut aus. Der Ring-3-Gasttest beweist danach mit echten VFS-Schreibvorgängen,
dass der neue Active arbeitsfähig ist. Ein reparierter dritter Kanal übernimmt
den Zustand, behält das Gate aber bis zu einem späteren autorisierten Takeover.
Damit ist die reversible Freigabe enger als ein allgemeines „Unfence“: weder
Fatal- noch Integritäts-Fences können zur Laufzeit aufgehoben werden.

S0.3c-3e injiziert nach einem erfolgreichen echten Handoff einen Dienstcrash,
während eine weitere Probe aussteht. Der Fence löscht Pending-Autorität und
alte Endpoint-Generation; der Client beobachtet den Kanalabbruch, verbindet
sich innerhalb einer festen 2-s-Grenze mit der Ersatzgeneration und bestätigt
erneut `DIAG -> REIST_DIAG_OK`. Der strikte NIC-Smoke verlangt sowohl
`SERVICE_CRASH_RECOVERED` als auch `NETWORK_RECOVERY_OK`.
Die Laufzeitabnahme verwendet für die vorangehende Basisfolge den kumulativen
Marker `RECOVERY_SEQUENCE_OK`, der erst nach der vierten selbstgetesteten
Generation entsteht; einzelne konkurrierende Diagnosezeilen sind kein Gate.

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

S0.2a friert dafür den
[External Safety Monitor Contract](EXTERNAL_SAFETY_MONITOR_CONTRACT.md) und
das maschinenprüfbare Profil `safety/external_safety_monitor.toml` ein. Dessen
Status ist bewusst `unbound`: Erst ein konkret identifiziertes Gerät mit
eigener Strom- und Zeitbasis, unabhängigem Zielreset, latched Safe-State-
Ausgang, separatem elektrischem Sense-Readback und bestandener physischer
Fault-Injection-Kampagne darf `qualified` werden. Der vorhandene IB700-Lauf
bleibt Emulatornachweis und wird nicht als physische Unabhängigkeit gewertet.
Die automatisierte S0.2-Abnahme endet deshalb an der expliziten QEMU/VMware-
Systemgrenze: QEMU prüft den emulierten Watchdog, VMware einen begrenzten Boot
des frisch erzeugten disponiblen Build-Pakets mit fail-closed fehlendem externem Backend,
Probe-Recovery und Ring-3-Shell. Reale Hardware und elektrische Nachweise
bleiben manuelle Nutzerevidenz ohne automatischen Zielhardware-Claim.

Als erste konkrete S0.3-Sperre besitzt der Kernel eine statische, begrenzte
Output-Fence-Registry. Ein Fatalereignis verriegelt sie vor jeder Diagnose und
vor dem Watchdog-Handover dauerhaft bis zum Neustart. Der Netzwerk-Hook sperrt
alle Software-TX-Pfade und deaktiviert best-effort die Sender von E1000,
RTL8139, RTL8168/8111G und NE2000; Wiederholung ist wirkungslos und benötigt weder Heap noch
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
die vorhandenen Sender und liest bei E1000, RTL8139, RTL8168/8111G und NE2000 die relevanten
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
nachfolgende Journaltransaktion. Markierte REIST-FAT12-Medien besitzen einen
eigenen begrenzten Transaktionspfad; EXT2 und fremde Medien besitzen diese
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

Der Desktop-Papierkorb respektiert diese Grenze: Nutzdaten werden durch genau
einen Same-Directory-Rename auf einen reservierten 8.3-Namen im bisherigen
Elternverzeichnis veröffentlicht. Ein zentraler, leerer Katalogmarker unter
`/trash/files` ist keine zweite Datenkopie; die versionierten Metadaten führen
Original- und Storage-Pfad zusammen. Explorer blendet ausschließlich die
vollständige reservierte Namensklasse aus. Dadurch führt ein Papierkorb-Drop
weder einen Cross-Directory-Rename noch einen Copy/Delete-Fallback aus.
Wiederherstellung akzeptiert ausschließlich den aktuellen Katalogmarker und
ein exakt geparstes Version-2-Metadatum. Original- und reservierter Storage-
Pfad müssen kanonisch sein und dasselbe Elternverzeichnis besitzen; ein
belegtes Originalziel wird niemals ersetzt. Erst der erfolgreiche atomare
Rename veröffentlicht die Nutzdaten wieder, danach werden Marker und Metadaten
entfernt. Endgültiges Leeren folgt denselben Metadatenbindungen und läuft erst
nach expliziter Bestätigung mit festen Katalog-, Tiefen- und Objektbudgets;
Kapazitätserschöpfung meldet einen sichtbaren Teilfortschritt statt ungebunden
weiterzulaufen.

FAT32-Namen bilden eine begrenzte Veröffentlichungseinheit aus höchstens 20
absteigenden VFAT-LFN-Slots und genau einem checksum-gebundenen 8.3-Eintrag.
Lookup und `readdir` akzeptieren den langen Namen nur bei gültiger Reihenfolge,
Slotanzahl, Attributbelegung, Startcluster-Nullfeld und Prüfsumme; andernfalls
ist ausschließlich der 8.3-Alias sichtbar. Schreibpfade erzeugen eindeutige
`~n`-Aliase und schreiben den Kurzeintrag als letzten Publikationsanker. Die
aktuelle Pfad-ABI bildet druckbares ASCII auf UTF-16LE ab; nicht unterstützte
Unicode-Namen erhalten keine falsch dekodierte Autorität. LFN-Rename auf einen
freien Namen ist transaktional geklammert, LFN-Replace eines existierenden
Ziels bleibt bis zu einem atomaren variablen Slot-Replace fail-closed gesperrt.

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
4. Die umgesetzte S0.3b-Probedomäne als Referenz für begrenzte Recovery nutzen.
5. Mit S0.3c GUI und Netzwerk, danach Dateisystem und komplexe Treiber aus
   Ring 0 lösen und als echte überwachte Dienste betreiben.
6. Deterministische Ressourcenreservierung und transaktionalen Zustand
   einführen.
7. Signierte A/B-Images, Boot-Failover und unabhängigen Standby-Kanal ergänzen.
8. Erst danach x86-64 und zusätzliche Schutzmechanismen als kontrollierte
   Plattformmigration qualifizieren.
