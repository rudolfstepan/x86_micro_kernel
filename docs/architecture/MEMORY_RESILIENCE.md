# Anforderung: Softwarebasierte Memory Resilience

## Zielsetzung

REIST OS soll untersuchen, ob Arbeitsspeicher auf Betriebssystemebene redundant verwaltet werden kann, sodass der vollständige Ausfall eines DIMMs, Memory-Ranks oder Memory-Channels nicht zwangsläufig zum Ausfall des Betriebssystems führt.

Das Konzept soll klassische ECC-Verfahren **nicht ersetzen**, sondern um eine zusätzliche Resilienzebene ergänzen.

Zielarchitektur:

$$
\boxed{\text{ECC} + \text{Memory Mirroring} + \text{Degraded Operation} + \text{Recovery}}
$$

## Verbindlicher erster Forschungs-PoC

Der erste Schritt ist ausdrücklich **kein transparentes Spiegeln beliebiger
virtueller Prozessseiten**. Ein x86-Seitentabelleneintrag verweist auf genau
einen physischen Frame; normale CPU-Schreibzugriffe können deshalb nicht durch
gewöhnliches Paging gleichzeitig in zwei Frames veröffentlicht werden.

Der erste PoC führt stattdessen ein explizites, kernel-eigenes
`resilient_page`-Objekt ein:

* feste Kapazität von höchstens vier Objekten,
* exakt 4096 Nutzbytes pro Objekt,
* zwei als A und B bezeichnete **simulierte** Fehlerdomänen,
* zwei Copy-on-write-Bänke pro Domäne,
* generationsgebundene Metadaten, CRC32 und semantische Validierung,
* transaktionale Schreib-, Lese-, Scrub-, Fehlerinjektions- und
  Rebuild-Operationen,
* Zustände `HEALTHY`, `DEGRADED`, `REBUILDING` und `FAILED`,
* feste Arbeits- und Wiederaufbaugrenzen ohne Heap, VFS, blockierendes Warten
  oder DMA im Laufzeitpfad.

Eine Schreibtransaktion bereitet die inaktive Bank jeder erreichbaren Replica
vollständig vor und prüft ihre CRC, bevor eine neue Generation veröffentlicht
wird. Ein Fehler vor dem Commit lässt die zuvor veröffentlichte Generation
lesbar. Widersprüchliche gleich alte Kopien oder zwei ungültige Replicas werden
nicht geraten, sondern liefern `FAILED` beziehungsweise einen nicht
korrigierbaren Fehler.

Die Bezeichnungen A und B beweisen zunächst keine Zuordnung zu verschiedenen
DIMMs, Ranks oder Channels. Adress-Interleaving und herstellerspezifisches
IMC-Hashing werden erst in einem eigenen Hardwarepaket untersucht.

### Implementierungsstand R1.2a und R1.2b

Der feste Objektvertrag ist implementiert und hostseitig geprüft. Vier Slots,
drei simulierte Domänen und je zwei 4096-Byte-Bänke liegen vollständig in
statischem Kernelstorage. Handles sind generationsgebunden; Metadaten werden
durch `critical_object` geschützt. Der Fault-Test deckt Unterbrechungen vor
dem Commit, Domänenverlust nach dem Commit, CRC-Verlust, widersprüchliche
gleichgenerationige Replicas, Doppelverlust und Rebuild-Unterbrechung ab.

R1.2b führt dieselbe Kampagne als compile-time-only Bootprobe im echten
Kernelimage aus. Der Gast hat Commit-Generation 2, degradierte committed Daten,
ein unabhängiges Objekt und den HEALTHY-Rebuild in die feste simulierte Domäne
C geordnet validiert und danach `BOOT_OK`, Userspace sowie `TEST_OK` erreicht.
Alle Targets überspringen den Desktop-Autostart und erreichen zuerst die
Ring-3-Shell. Das Memory-Resilience-Profil benötigt dafür keinen Sonderweg;
`DESKTOP` wird ausschließlich durch einen ausdrücklichen Benutzerbefehl
gestartet.

Damit ist eine im laufenden REIST-Kernel ausgeführte **simulierte** Degradation
nachgewiesen. Physische DIMM-, Rank-, Channel-, IMC-, MCE- oder EDAC-Eigenschaften
sind weiterhin weder geprüft noch behauptet.

## Funktionale Anforderungen

### 1. Physische Speicheridentifikation

Das Betriebssystem soll soweit von der Hardware unterstützt ermitteln können:

* installierte DIMMs
* Memory Channels
* Ranks
* physische Adressbereiche
* Zuordnung physischer Adressen zu DIMMs, Ranks und Channels
* verwendetes Memory Interleaving

Es ist zu untersuchen, welche Informationen CPU, IMC, Firmware/ACPI, SMBIOS und gegebenenfalls Chipsatz bereitstellen.

### 2. Redundante Speicherverwaltung

Für resiliente Speicherbereiche soll eine logische Page zwei physisch voneinander unabhängigen Pages zugeordnet werden können:

$$
P_L \rightarrow (P_A,P_B)
$$

Dabei sollen \(P_A\) und \(P_B\) möglichst auf unterschiedlichen physischen Fehlerdomänen liegen, vorzugsweise auf unterschiedlichen DIMMs oder Memory-Channels.

Ein einzelner Hardwareausfall darf damit nicht beide Kopien gleichzeitig betreffen.

### 3. Schreibkonsistenz

Änderungen an resilientem Speicher müssen konsistent auf beide Kopien übertragen werden.

Zu untersuchen sind insbesondere:

* atomare bzw. definierte Update-Semantik
* CPU-Caches
* Cache-Kohärenz
* Memory Ordering
* SMP-Systeme
* Verhalten bei einem Fehler während eines Schreibvorgangs

Es darf kein Zustand entstehen, bei dem nach einem Fehler nicht bestimmt werden kann, welche Kopie gültig ist.

### 4. Fehlererkennung

Das System soll untersuchen und soweit technisch möglich erkennen:

* korrigierbare ECC-Fehler
* nicht korrigierbare ECC-Fehler
* fehlerhafte Pages
* Ausfall eines DIMMs
* Ausfall eines Ranks
* Ausfall eines Memory-Channels
* Machine-Check-Ereignisse
* nicht mehr zuverlässig erreichbare Speicherbereiche

Ein Fehler darf nach seiner Erkennung nicht zu weiteren unkontrollierten Zugriffen auf die betroffene Fehlerdomäne führen.

### 5. Degraded Mode

Nach dem Ausfall einer Spiegelhälfte soll REIST OS nach Möglichkeit mit der verbleibenden Kopie weiterarbeiten.

Der betroffene Speicher wird anschließend beispielsweise als

`HEALTHY → DEGRADED → FAILED / REBUILDING → HEALTHY`

verwaltet.

Der Ausfall einer Speicherkomponente soll somit nach Möglichkeit zu einer **Reduzierung der Redundanz statt zu einem Systemausfall** führen.

### 6. Recovery und Rebuild

Sofern Ersatzspeicher verfügbar ist, soll untersucht werden, ob eine verlorene Spiegelkopie zur Laufzeit neu aufgebaut werden kann.

Konzeptionell:

$$
(P_A,P_B)
\xrightarrow{P_B\;failed}
(P_A)
\xrightarrow{rebuild}
(P_A,P_C)
$$

Dabei muss das System während des Rebuilds weiterarbeiten können.

## Abgrenzung zu ECC

ECC behandelt primär Datenfehler innerhalb eines grundsätzlich erreichbaren Speichers.

Die hier untersuchte Architektur adressiert zusätzlich den **Verlust einer vollständigen Speicherkomponente bzw. Fehlerdomäne**.

Beide Mechanismen sollen sich ergänzen:

**ECC:** Erkennen und gegebenenfalls Korrigieren lokaler Speicherfehler.

**Memory Resilience:** Redundanz, Isolation, Failover und Weiterbetrieb bei größeren Speicherfehlern.

## Hardwareabhängigkeiten

Eine rein softwarebasierte Lösung kann keine vollständige Fehlertoleranz garantieren.

Insbesondere muss untersucht werden, ob der integrierte Memory Controller nach dem Ausfall eines DIMMs oder Channels weiterhin zuverlässig auf die verbleibenden Speicherbereiche zugreifen kann.

Falls ein Hardwarefehler den IMC selbst blockiert oder in einen undefinierten Zustand versetzt, kann das Betriebssystem den Fehler möglicherweise nicht mehr behandeln.

Diese Grenze zwischen **OS-seitiger Recovery und notwendiger Hardware-RAS-Unterstützung** ist expliziter Bestandteil der Untersuchung.

## Spätere physische PoC-Stufen

Auch die späteren physischen PoC-Stufen setzen zunächst keine Entfernung eines
DIMMs voraus.

Stattdessen:

1. Zwei nachweisbar getrennte physische Speicherbereiche bereitstellen.
2. Explizite resiliente Objekte redundant in beiden Bereichen ablegen.
3. Ausschließlich vermittelte Schreibzugriffe transaktional spiegeln.
4. Eine Fehlerdomäne softwareseitig als ausgefallen markieren.
5. Sämtliche weiteren Zugriffe auf die verbleibende Kopie umleiten.
6. Datenintegrität prüfen.
7. Weiterbetrieb nicht betroffener Kernel- und User-Space-Funktionen
   verifizieren.
8. Optional eine neue Spiegelkopie erzeugen und einen Rebuild durchführen.

Anschließend können gezielte Hardware-Fault-Injection-Tests folgen.

## Erfolgskriterium

Der erste PoC ist erfolgreich, wenn der simulierte vollständige Verlust genau
einer bezeichneten Fehlerdomäne

$$
\boxed{\text{nicht zum Ausfall von Kernel oder laufendem System führt}}
$$

und das betroffene Objekt automatisch in einen definierten **Degraded Mode**
wechselt. Die letzte vollständig veröffentlichte Generation muss aus der
verbleibenden redundanten Kopie korrekt lesbar sein und begrenzt auf eine
Ersatzdomäne aufgebaut werden können. Der übrige Allokator und unabhängige
Objekte müssen weiter funktionieren.

Dieser Erfolg beweist weder physische DIMM-/Rank-/Channel-Unabhängigkeit noch
Machine-Check-Recovery, DIMM-Hot-Removal, transparentes Prozessspeicher-
Mirroring oder einen fail-operationalen Gesamtbetrieb.

Langfristiges Ziel ist nicht lediglich die Korrektur einzelner Speicherfehler, sondern die Fähigkeit des Systems, **den Verlust einer kompletten Speicherkomponente als beherrschbaren Hardwarefehler zu behandeln**.
