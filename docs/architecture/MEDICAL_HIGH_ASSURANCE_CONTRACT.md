# Medical Reference Profile für den REIST High-Assurance Core

Produktname: **REIST OS — Resilient Execution, Isolation and Stability
Technology**

Stand: 16. August 2026

Status: optionales Referenzprofil; nur für ausdrücklich ausgewählte
medizinische Zielsysteme verbindlich

> **Safety-Hinweis:** Das aktuelle OS ist ein Forschungsprototyp. Es ist nicht
> zertifiziert, nicht klinisch validiert und darf nicht für Diagnose,
> Behandlung, Überwachung oder andere klinische Entscheidungen eingesetzt
> werden. Laufzeitreparatur wird vorrangig durch Isolation, Supervisor,
> Failover und transaktionale A/B-Updates erreicht. Kernel-Livepatching bleibt
> eine streng begrenzte, gesondert nachzuweisende Ausnahme.

## Zweck und klare Abgrenzung

Dieses Dokument ist ein **optionales Safety Profile** auf Basis des
[generischen REIST-Core-Vertrags](HIGH_ASSURANCE_CORE_CONTRACT.md). Es bestimmt
nicht die Identität oder die allgemeine Roadmap von REIST OS. Seine
medizinischen Anforderungen werden erst verbindlich, wenn ein konkretes
Zielsystem dieses Profil auswählt und Intended Purpose, Systemgrenze und
Risikoanalyse festlegt.

Dieses Dokument richtet die weitere Entwicklung des Betriebssystems auf einen
medizinischen High-Assurance-Einsatz aus. Bis die produktbezogene
Software-Sicherheitsklasse feststeht, gilt intern konservativ ein
**IEC-62304-Class-C-äquivalentes Entwicklungsrigor-Ziel** für Kernel und
Plattform. Das ist eine Hausregel für Architektur und Evidenz, keine
Klassifizierung, Zertifizierung oder Konformitätsaussage.

Das Betriebssystem ist damit **weder zertifiziert noch als Medizinprodukt
freigegeben**. Eine Eignung, Klassifizierung oder Zulassung kann nur für eine
klar abgegrenzte Produktkonfiguration, ihren bestimmungsgemäßen Gebrauch, ihre
Hardware, ihre Betriebsumgebung und ihre vollständige Nachweisführung bewertet
werden. Bestehender Code erfüllt diesen Vertrag nicht automatisch.

Die Schlüsselwörter **MUSS**, **DARF NICHT**, **SOLL** und **KANN** sind
normativ. Ein Abweichen von MUSS oder DARF NICHT erfordert vorab eine
dokumentierte Gefährdungsanalyse, eine mindestens gleichwertige Maßnahme,
unabhängige Freigabe und zugeordnete Verifikationsevidenz.

## Rangfolge der Entwicklungsziele

Bei einem Zielkonflikt gilt, vorbehaltlich der funktionsbezogenen
Gefährdungsanalyse:

1. Gefährdung von Patienten, Bedienern und Dritten verhindern
2. zeitliche und inhaltliche Korrektheit sicherheitsrelevanter Funktionen
3. Datenintegrität und kontrollierte Zustandsübergänge erhalten
4. Verfügbarkeit, Fehlercontainment und Wiederherstellbarkeit erhalten
5. Security und Datenschutz als Voraussetzungen für Safety durchsetzen
6. Funktionalität, Bedienkomfort, Durchsatz und mittlere Geschwindigkeit

Ein Security-Mechanismus darf eine Safety-Funktion nicht unbewertet
blockieren. Eine Safety-Ausnahme darf umgekehrt Security nicht stillschweigend
umgehen. Jeder Konflikt MUSS im Hazard- und Threat-Modell aufgelöst werden.

Leitentscheidung: **Redundanz, Isolation und nachweisbar begrenztes Verhalten
haben Vorrang vor maximaler Geschwindigkeit oder Ressourcenauslastung.**

## Geltungsbereich und Systemgrenze

Vor einer Safety-Bewertung MUSS eine versionierte Systemgrenze festgelegt
werden. Sie umfasst mindestens:

- bestimmungsgemäßen Gebrauch und vorhersehbaren Fehlgebrauch,
- sicherheitsrelevante Funktionen und ausdrücklich nicht sicherheitsrelevante
  Funktionen,
- unterstützte CPU, Chipsätze, Speicher, Massenspeicher, Timer, Netzwerke,
  Ein-/Ausgabegeräte und Firmwarestände,
- minimale und maximale Last, erforderliche Betriebsdauer und Wartungsfenster,
- externe Sensoren, Aktoren, Bedienoberflächen und Kommunikationspartner,
- Energieversorgung, Temperatur, elektromagnetische und sonstige
  Umgebungsannahmen,
- Vertrauensgrenzen, Angreifermodell und physische Zugriffsmöglichkeiten,
- Verantwortlichkeiten zwischen OS, Applikation, Hardware, Hersteller,
  Betreiber und Service.

Nicht bewertete Hardware, Treiber, Konfigurationen oder Buildvarianten DÜRFEN
NICHT als freigegebene Produktkonfiguration ausgegeben werden. Optionale
Komponenten bleiben standardmäßig deaktiviert, bis ihre Abhängigkeiten,
Fehlerbilder und Nachweise vollständig sind.

## Stabile Kernanforderungen

Diese IDs bilden den ersten Traceability-Anker. Ihre Bedeutung darf nicht
stillschweigend geändert werden; eine materielle Änderung erhält eine neue ID
und eine dokumentierte Auswirkungsanalyse.

| ID | Verbindliche Kernanforderung |
|---|---|
| MHA-SCP-001 | Intended Purpose, Systemgrenze, Produktkonfiguration und Ausschlüsse MÜSSEN versioniert sein. |
| MHA-RSK-001 | Jede Safety-Funktion MUSS mit Hazard, Kontrolle, Restrisiko und Evidenz rückverfolgbar sein. |
| MHA-FTI-001 | Sicherer/degradierter Zustand und FTTI MÜSSEN pro gefährlicher Situation messbar definiert sein. |
| MHA-FCR-001 | Ein Einzelfehler DARF sich nicht unkontrolliert über seine Fehlereindämmungsregion ausbreiten. |
| MHA-STK-001 | Jeder Stack MUSS ein statisches Budget, Laufzeitüberwachung und nicht gemappte Schutzbereiche besitzen. |
| MHA-EXC-001 | Kritische CPU-Fehler MÜSSEN über einen unabhängigen, vorallokierten Notfallpfad eskalieren können. |
| MHA-PAN-001 | Ein Fatalpfad MUSS zeitlich begrenzt Ausgänge sichern, Crashdaten erfassen und Failover/Reset auslösen. |
| MHA-WDG-001 | Ein unabhängiger Watchdog MUSS Fortschritt statt bloßer Aktivität überwachen und ab Reset wirksam sein. |
| MHA-RED-001 | Erforderliche Verfügbarkeit MUSS durch analysierte Redundanz, Fencing und Common-Cause-Kontrollen entstehen. |
| MHA-RES-001 | Safety-Funktionen MÜSSEN garantierte CPU-, Speicher-, Queue-, Timer- und I/O-Ressourcen besitzen. |
| MHA-DAT-001 | Sicherheitsrelevante Daten MÜSSEN Ende-zu-Ende auf Integrität, Reihenfolge, Quelle und Aktualität geprüft werden. |
| MHA-PER-001 | Persistente Zustandsänderungen MÜSSEN Stromausfall und Teilwrites ohne stille Korruption überstehen. |
| MHA-UPD-001 | Updates MÜSSEN authentisiert, atomar, stromausfallfest, rückrollbar und redundanzbewahrend sein. |
| MHA-OBS-001 | Diagnose MUSS begrenzt und ausfallsicher sein und DARF Safety-Ressourcen nicht blockieren. |
| MHA-VER-001 | Anforderung, Risiko, Design, Code, Test und Ergebnis MÜSSEN bidirektional rückverfolgbar sein. |
| MHA-LIF-001 | Toolchain, Schlüssel, Hardware, Datenformate und Support MÜSSEN über die vorgesehene Lebensdauer beherrscht werden. |

## Verbindliche Systeminvarianten

Für jede freigegebene Konfiguration MÜSSEN folgende Invarianten nachweisbar
sein:

- Ein Fehler wird innerhalb seiner analysierten Fehlereindämmungsregion
  erkannt und breitet sich nicht unkontrolliert auf andere Safety-Funktionen
  aus.
- Eine Safety-Funktion erreicht entweder rechtzeitig ein korrektes Ergebnis,
  wechselt rechtzeitig auf eine qualifizierte redundante Instanz oder nimmt
  innerhalb ihrer FTTI den definierten sicheren Zustand ein.
- Veraltete, unvollständige oder korrupte Daten werden niemals stillschweigend
  als gültige aktuelle Daten ausgegeben.
- Ein unbekannter Gesundheitszustand wird nicht als gesund interpretiert.
- Diagnose, Logging, Desktop und Komfortfunktionen können reservierte
  Ressourcen einer Safety-Funktion nicht verbrauchen.
- Ein Update, eine Reparatur oder ein Neustart kann nicht alle notwendigen
  redundanten Instanzen gleichzeitig außer Betrieb nehmen.
- Jede automatische Wiederherstellung ist begrenzt, beobachtbar, auditierbar
  und besitzt einen definierten Abbruch in einen sicheren Zustand.
- Ein Systemneustart gilt nur dann als Safety-Maßnahme, wenn der dadurch
  entstehende Ausgangszustand und die gesamte Wiederanlaufzeit validiert sind.

## Hazard, sicherer Zustand und FTTI

### Hazard-Register

Jede sicherheitsrelevante Funktion MUSS vor ihrer Implementierung einen
eindeutig identifizierten Eintrag im Hazard-Register besitzen. Der Eintrag
enthält mindestens:

- gefährliche Situation, Ursachen, auslösende Ereignisse und Auswirkungen,
- betroffene Systemgrenze und Betriebsphase,
- Fehlerannahmen einschließlich Mehrfach- und latenter Fehler,
- normalen, degradierten und sicheren Zustand,
- Fault Tolerant Time Interval (FTTI) mit Begründung,
- Erkennungs-, Isolations-, Umschalt- und Stabilisierungsstrategie,
- Safety-Anforderungen und ihre Zuordnung zu Architektur und Code,
- angenommene Diagnoseabdeckung sowie verbleibendes Restrisiko,
- konkrete Verifikationsmethode, Akzeptanzkriterium, Evidenz und Eigentümer,
- Status, offene Annahmen und Freigabeentscheidung.

Hazards DÜRFEN NICHT ausschließlich als Softwarecrash beschrieben werden.
Falsche Werte, falsche Reihenfolge, verspätete Werte, Wiederholungen,
Auslassungen, unerkannte Datenalterung, Ressourcenerschöpfung und plausible,
aber fehlerhafte Ausgaben MÜSSEN betrachtet werden.

### Sicherer und degradierter Zustand

Der sichere Zustand MUSS pro Funktion und Kontext definiert werden. Er kann
Weiterbetrieb mit reduzierter Leistung, Übergabe an eine redundante Einheit,
Einfrieren eines validierten Ausgangs oder kontrolliertes Abschalten bedeuten.
„Aus“, „Neustart“ und „letzter Wert“ sind nicht ohne funktionsbezogenen
Nachweis sichere Zustände.

Ein degradierter Zustand MUSS sichtbar sein und besitzt:

- einen klar begrenzten Funktionsumfang,
- eine maximale zulässige Dauer,
- konservative Ausgangs- und Ressourcenlimits,
- eine Eskalation bei weiterem Fehler oder Zeitüberschreitung,
- eine Bediener- oder Systemmeldung, die nicht als Normalbetrieb missverstanden
  werden kann.

### Zeitbudget

Für jede zeitkritische Fehlerreaktion MUSS gelten:

```text
T_erkennen + T_bewerten + T_isolieren + T_umschalten
  + T_stabilisieren + Sicherheitsmarge < FTTI
```

Alle Summanden MÜSSEN unter Worst-Case-Last und relevanten Fehlerszenarien
gemessen oder konservativ begründet werden. Durchschnittswerte sind kein
Abnahmenachweis. Die Zeitquelle selbst MUSS überwacht werden; ein stehender,
springender oder driftender Timer darf die FTTI-Überwachung nicht unwirksam
machen.

## Architektur für Fehlereindämmung

### Fehlereindämmungsregionen

Kernel, Treiber, Dateisysteme, Netzwerk, Update, Diagnose, GUI und
Safety-Applikationen MÜSSEN als explizite Fehlereindämmungsregionen modelliert
werden. Wo Hardware und MMU es erlauben, gelten mindestens:

- getrennte Adressräume und minimale Zugriffsrechte,
- versionierte, validierte IPC-Nachrichten statt gemeinsamer veränderlicher
  Speicherbereiche,
- CPU-, Speicher-, Queue-, Handle- und I/O-Quoten pro Dienst,
- kontrollierter Geräte- und DMA-Zugriff; unbeschränkter DMA ist für
  nicht vertrauenswürdige Komponenten unzulässig,
- begrenzte Interruptarbeit und verlagerte, budgetierte Folgeverarbeitung,
- Seitenschutz, Stack-Guardpages und Write-Xor-Execute, soweit die Plattform
  dies unterstützt,
- überprüfte Eingabegrenzen an jeder Vertrauens- und Prozessgrenze.

Fehleranfällige oder komplexe Treiber und Protokolle SOLLEN aus dem
privilegierten Kernel herausgelöst werden. Der privilegierte Kern MUSS klein,
statisch analysierbar und auf Mechanismen beschränkt bleiben, die Isolation,
Zeit, IPC, Speicher und kontrollierte Recovery ermöglichen.

### Zustandsmodell

Für überwachte Komponenten gilt mindestens dieses Zustandsmodell:

```text
STARTING -> HEALTHY -> DEGRADED -> RECOVERING -> HEALTHY
                    \              |
                     +----------> SAFE_STATE

UNKNOWN wird wie UNHEALTHY behandelt.
```

Jeder Übergang MUSS atomar, protokolliert und gegen veraltete Ereignisse
geschützt sein. Eine Komponente darf erst nach Selbsttest, Zustandsvalidierung
und expliziter Freigabe wieder eingegliedert werden.

### Nebenläufigkeit und Speicher

- Lockreihenfolgen, Besitz und IRQ-/Präemptionsregeln MÜSSEN dokumentiert und
  maschinell oder durch Tests überprüft werden.
- Blockierzeiten MÜSSEN begrenzt sein. Priority Inversion MUSS durch ein
  geeignetes, analysiertes Protokoll verhindert oder begrenzt werden.
- Use-after-free, doppelte Freigabe, Integerüberlauf, Bereichsverletzung und
  undefiniertes C-Verhalten MÜSSEN durch Design, Compilerprüfungen,
  Laufzeitschutz und Tests adressiert werden.
- In Safety-Pfaden SOLLEN nach der Initialisierung reservierte Pools statt
  unbeschränkter Heapallokation verwendet werden.
- Speicherfehler, Lecks und Fragmentierung MÜSSEN als Langzeit- und
  Fehlerfälle getestet werden. Canaries allein sind kein ausreichender
  Speicherschutz.

### Exceptions, Stacküberlauf und Kernel-Panic

Ein Taskfehler oder Stacküberlauf DARF NICHT automatisch einen
unkontrollierten Systemcrash auslösen. Die Architektur MUSS strikt zwischen
einem **eindämmbaren Fehler** und einer **möglichen globalen Korruption**
unterscheiden:

- Eindämmbare Fehler umfassen beispielsweise eine User-Exception, die
  Verletzung einer Task-Guardpage, einen isolierten Dienstabsturz oder eine
  lokale Quotenüberschreitung bei weiterhin vertrauenswürdigem Kernelzustand.
  Sie MÜSSEN auf die betroffene Fehlereindämmungsregion begrenzt werden. Der
  Kernel entzieht ihr Ausgaberechte, räumt Ressourcen kontrolliert auf und
  lässt Supervisor, Restart oder Failover innerhalb der FTTI übernehmen.
- Potenziell fatale Fehler umfassen beispielsweise beschädigte
  Kernelkontrollstrukturen, einen Kernel-Stacküberlauf, Double Fault,
  inkonsistente Seitentabellen oder einen nicht mehr vertrauenswürdigen CPU-
  beziehungsweise RAM-Zustand. Ungeprüft weiterzulaufen ist in diesem Fall
  verboten. Die Reaktion MUSS in einen vorab validierten Fail-safe-Pfad führen.
- Ist die Klassifizierung nicht sicher möglich, MUSS konservativ die fatale
  Behandlung gewählt werden.

Jeder User- und Kernel-Taskstack MUSS echte nicht gemappte Guardpages besitzen;
Canaries sind nur zusätzliche Diagnose. Die i386-Exceptionarchitektur MUSS für
Stackfehler einen vom normalen Taskstack unabhängigen Notfallpfad verwenden.
Insbesondere benötigt `#DF` einen dedizierten, vorinitialisierten
Emergency-Stack, zum Beispiel über einen validierten Task-Gate-/TSS-Pfad.
Relevante weitere kritische Exceptions MÜSSEN ohne Vertrauen in den
beschädigten normalen Stack erfassbar sein.

Eine einzelne Guardpage ist kein vollständiger Schutz gegen große
Stack-Sprünge. Kernel-VLAs und `alloca` sind unzulässig; Stackverbrauch MUSS
aus Compilerartefakten und Callgraphen begrenzt werden. Mehrere Guardpages oder
verifizierte Stack-Probes sind einzusetzen, wenn ein Sprung die Schutzseite
sonst überspringen könnte.

Der Exception-/Panic-Notfallpfad MUSS:

- ohne Heapallokation, blockierende Locks, Dateisystem oder komplexe Treiber
  auskommen,
- rekursionssicher und zeitlich begrenzt sein,
- Ausgänge sperren oder an einen unabhängigen sicheren Kanal übergeben,
- einen minimalen Crashrecord über einen reservierten Pfad sichern, soweit
  dies ohne zusätzliche Gefährdung möglich ist,
- eine redundante Instanz beziehungsweise einen separaten Safety-Controller
  benachrichtigen,
- abschließend einen validierten Hardwarewatchdog-, Reset- oder Haltpfad
  auslösen, bevor die FTTI überschritten wird.

Auch der Emergency-Diagnosekanal besitzt ein kurzes festes Pollbudget. Ein
defekter UART, Bildschirm oder Logpuffer darf den Fatalpfad niemals festhalten.

Eine Kernel-Panic ist damit die letzte Containmentmaßnahme, keine normale
Fehlerbehandlung und keine Laufzeitreparatur. Ihre sichtbare Wirkung MUSS
deterministisch und fail-safe sein; ein schwarzer Bildschirm, eine endlose
Panic-Ausgabe oder unkontrolliertes Weiterarbeiten ist unzulässig.

Bei global korrupter CPU-, RAM-, Bus- oder Stromversorgungsbasis kann Software
allein keine Wiederherstellung garantieren. Wo die Gefährdungsanalyse solche
Fehler fordert, MÜSSEN unabhängige Hardwaremaßnahmen wie ECC, Lockstep,
separater Watchdog, redundanter Rechner oder externer Safety-Controller die
letzte Schutzebene bilden. Diese Grenze MUSS als Annahme und Restrisiko offen
dokumentiert und durch reale Fehlerfälle verifiziert werden.

## Determinismus und Ressourcenreservierung

Safety-Pfade MÜSSEN obere Grenzen für Ausführungszeit, Queuegröße,
Speicherbedarf, I/O-Latenz und Wiederholungen besitzen. Unbegrenzte Schleifen,
unbegrenzte Retries, unbegrenzte Warteschlangen sowie blockierende
Dateisystem- oder Netzwerkzugriffe sind dort unzulässig.

Für jede Safety-Funktion MÜSSEN reserviert und per Admission Control geschützt
werden:

- CPU-Budget und Priorität,
- physischer und virtueller Speicher,
- IPC- und Ereignisqueuekapazität,
- Timer und maximale Interruptlatenz,
- I/O-Bandbreite und persistenter Speicher,
- Ressourcen für Überwachung, Alarmierung und sicheren Zustandswechsel.

Überbuchung ist nur für nicht sicherheitsrelevante Last zulässig. Bei
Überlast werden zuerst Komfort-, Diagnose- und Hintergrundfunktionen
kontrolliert gedrosselt oder verworfen. Die Safety-Funktion selbst MUSS ihre
Reservierung behalten. Grenzwertverletzungen lösen einen vorhersagbaren,
getesteten Degradationspfad aus.

Scheduler- und I/O-Verhalten MÜSSEN unter Normalbetrieb, maximaler Last,
Interruptsturm, Gerätefehler und laufender Recovery vermessen werden.
Durchsatzmessungen ersetzen keine Worst-Case-Analyse.

Jede Hardwaretransaktion MUSS eine monotone absolute Deadline, einen
abbrechbaren Fehlerpfad und eine Reset-/Quarantänestrategie besitzen. Ein
hängender Controller, Interruptsturm oder Diagnoseport darf den Kernel nicht
unbegrenzt blockieren. Interruptquellen werden nur bei registriertem Besitzer
freigegeben und bei Budgetverletzung maskiert und isoliert.

## Redundanz und Common-Cause-Beherrschung

Redundanz MUSS aus der Gefährdungsanalyse abgeleitet und nicht pauschal mit
zwei identischen Kopien gleichgesetzt werden. Betrachtet werden MÜSSEN:

- räumliche Redundanz von Prozessen, Kernen, Geräten oder Rechnern,
- zeitliche Redundanz durch Wiederholung mit unabhängiger Plausibilisierung,
- Informationsredundanz durch Sequenznummern, Prüfcodes und unabhängige
  Konsistenzregeln,
- Pfadredundanz für Strom, Takt, Bus, Netzwerk, Speicher und Sensorik,
- funktionale oder technische Diversität, wenn identische Fehlerursachen
  nicht ausreichend ausgeschlossen werden können.

Redundante Einheiten MÜSSEN hinsichtlich gemeinsamer Stromversorgung,
Taktquelle, Firmware, Compiler, Bibliotheken, Konfiguration, Eingangsdaten,
Wartungsprozess und physischer Umgebung auf Common-Cause-Fehler untersucht
werden. Diversität MUSS gezielt ein anderes relevantes Fehlerbild adressieren;
bloße Varianten ohne Unabhängigkeitsnachweis zählen nicht als Diversität.

Aktiv/Standby-, Voting- oder Quorum-Verfahren MÜSSEN festlegen:

- wer zu jedem Zeitpunkt Ausgänge autorisieren darf,
- wie Split Brain durch Fencing, Epochen oder Leases verhindert wird,
- wie veraltete Replikate erkannt werden,
- wie Zustandsabgleich und Integrität geprüft werden,
- wie schnell Fehler erkannt und umgeschaltet werden,
- wie latente Fehler durch Selbst- und Proof-Tests sichtbar werden,
- unter welchen Bedingungen eine reparierte Einheit wieder teilnehmen darf.

Ein redundanzverminderter Betrieb MUSS zeitlich begrenzt und alarmiert sein.
Redundanz darf Fehler nicht dauerhaft verdecken oder ihre Reparatur
aufschieben.

## Laufzeit-Recovery ohne Funktionsbeeinträchtigung

### Überwachte, isolierte Dienste

Treiber und Dienste SOLLEN durch einen separaten Supervisor überwacht werden.
Der Supervisor MUSS unabhängig von der zu überwachenden Komponente planen,
Speicher reservieren und Fortschritt beurteilen können. Ein Heartbeat allein
beweist keine korrekte Funktion; zusätzlich sind Fortschrittsmarken,
Plausibilitätsprüfungen und Deadlineüberwachung erforderlich.

Ein Neustart eines Dienstes MUSS:

1. die fehlerhafte Instanz isolieren und ihre Ausgabe sperren,
2. offene I/O- und Ressourcenbesitze sicher abbrechen oder übertragen,
3. Zustand aus einer validierten Quelle wiederherstellen,
4. Selbsttest und Schnittstellenprüfung ausführen,
5. erst danach Ausgaben wieder freigeben.

Restart-Budgets, begrenztes Backoff und eine Eskalation bei Wiederholfehlern
sind verpflichtend. Endlosschleifen aus Absturz und Neustart sind verboten.
Operationen über Recovery-Grenzen MÜSSEN idempotent oder anhand dauerhafter
Identitäten als Duplikat erkennbar sein.

### Rolling Failover

Wartung oder Reparatur im laufenden Betrieb MUSS bevorzugt über redundante
Instanzen erfolgen:

1. Mindest-Redundanz und FTTI-Spielraum prüfen.
2. Zielinstanz entlasten und in einen quieszenten Zustand bringen.
3. autorisierte Ausgabe per Fencing an eine gesunde Instanz übergeben.
4. Zielinstanz reparieren oder aktualisieren.
5. Selbsttest, Datenabgleich und Beobachtungsphase durchführen.
6. Instanz kontrolliert reintegrieren; erst danach die nächste Instanz ändern.

Zu keinem Zeitpunkt dürfen alle Instanzen derselben notwendigen
Safety-Funktion gleichzeitig geändert, neugestartet oder dequalifiziert sein.

### A/B-Update und Rollback

Systemimages und kritische Dienste MÜSSEN transaktional aktualisierbar sein.
Eine A/B-Strategie umfasst mindestens signierte Images, Integritätsprüfung vor
Aktivierung, atomare Slotwahl, Boot-Erfolgsbestätigung, begrenzte Bootversuche,
bekannt gutes Recovery-Image sowie automatischen und manuell auslösbaren
Rollback. Stromverlust in jeder Updatephase MUSS getestet werden.

Persistente Datenformate und IPC-Protokolle MÜSSEN versioniert sein. Vor einem
Rollback ist die Rückwärtskompatibilität des Zustands nachzuweisen oder eine
verlustfreie, getestete Rückmigration bereitzustellen.

Der Watchdog- und Rollbackschutz MUSS bereits ab Reset wirksam sein. Stage 1,
Stage 2, Kernel-Handoff und frühe Initialisierung besitzen begrenzte Versuche
und dürfen bei defektem Image nicht dauerhaft in `halt` verbleiben. Zwei
authentisierte Slots, ein unveränderliches Recovery-Image und ein atomarer,
redundanter Boot-Health-Record bilden den Mindestpfad.

Der aktuelle Forschungsstand implementiert davon zwei authentisierte
HDD-Slots sowie einen redundant CRC-/sequenzgeschützten Boot-Control-Record
mit zwei begrenzten Pending-B-Versuchen. Nach `BOOT_OK` bestätigt ausschließlich
der generationgebundene Ring-3-Storage-Dienst die exakt gestartete Sequenz;
beide Kopien werden in fester Reihenfolge mit Barriere und Read-back
geschrieben. Bestätigtes B bleibt aktiv, und Stage 2 persistiert bei einem
späteren B-Prüffehler den A-Rollback vor dessen Ausführung. Dies ist noch kein
vollständiger Boot-Health-/Update-Nachweis: unabhängige Beobachtungsphase,
unveränderliches Recovery-Image, Updateverteilung, vollständige Power-Loss-
Evidenz auf Zielhardware und Anti-Rollback fehlen.

### Kernel-Livepatching

Kernel-Livepatching ist **kein regulärer Recovery- oder Updatepfad**. Der
bevorzugte Weg ist Failover auf eine redundante Einheit mit anschließendem
Offline-Austausch des vollständigen, reproduzierbaren Images.

Eine Ausnahme für einen Livepatch ist nur zulässig, wenn alle folgenden
Bedingungen erfüllt sind:

- vollständige Hazard-, Security- und Timing-Auswirkungsanalyse,
- vorab qualifizierter, atomarer Patchmechanismus mit sicherem Rollback,
- unveränderte Datenlayouts, ABI, Ressourcenverträge und Safety-Invarianten,
- definierter Quieszenzpunkt ohne aktive Ausführung des ersetzten Codes,
- identische Verifikation auf der exakten Zielkonfiguration,
- unabhängige Freigabe, signiertes Artefakt und vollständiger Audittrail,
- anschließende Aufnahme der Änderung in das dauerhafte Basisimage.

Änderungen an Scheduler, Interruptpfad, Speicherverwaltung, Trust Chain,
Synchronisationsmodell, persistentem Layout oder Safety-Zustandsautomaten
DÜRFEN NICHT als ad-hoc Livepatch eingespielt werden.

## Datenintegrität und Persistenz

Sicherheitsrelevante Daten MÜSSEN Ende-zu-Ende geschützt werden: von der
Quelle über IPC, Verarbeitung, Speicherung und Replikation bis zur Ausgabe.
Der Schutz umfasst mindestens:

- Version, Länge, Typ, Quelle, Sequenz und Gültigkeitszeitraum,
- Erkennung von Verlust, Wiederholung, Umordnung, Beschädigung und Alterung,
- atomare Datensätze oder transaktionales Journal für persistente Änderungen,
- explizite und überprüfte Flush-/Commit-Semantik,
- Prüfcodes gegen zufällige Fehler und kryptografische Authentizität an
  adversarialen Grenzen,
- konsistente Regeln für Replikationskonflikte; „letzter Schreiber gewinnt“
  ist ohne Safety-Nachweis unzulässig,
- periodische Integritätsprüfung, Scrubbing und getestete Wiederherstellung,
- Backups, deren Lesbarkeit und Restorepfad regelmäßig geprüft werden.

CRC oder Redundanz allein beweisen keine Authentizität. Fehlerhafte Daten
MÜSSEN markiert, isoliert und bis zur geklärten Quelle von Safety-Ausgaben
ausgeschlossen werden. Datenqualität, Messalter und Unsicherheit sind Teil des
Werts und DÜRFEN NICHT von ihm getrennt verloren gehen.

## Observability, Audit und Betriebsdiagnose

Das System MUSS Fehler erkennen und erklären können, ohne dafür seine
Safety-Funktion zu gefährden. Strukturierte Ereignisse enthalten mindestens
eine stabile Ereignis-ID, monotone Zeit, Komponente, Build- und
Konfigurationsidentität, Zustandsübergang, Ursache, Maßnahme und Ergebnis.

Verpflichtend zu beobachten sind:

- Deadlineverletzungen und Worst-Case-Latenzen,
- Ressourcenverbrauch, Reserven, Lecks und Queuehochwasserstände,
- Watchdog-, Selbsttest-, Failover- und Reintegrationsergebnisse,
- Integritäts-, Authentifizierungs- und Autorisierungsfehler,
- aktive Versionen, Updatezustand und Abweichung von der Sollkonfiguration,
- Redundanzgrad, Replikationsalter und isolierte Komponenten.

Logging MUSS gepuffert, begrenzt und priorisiert sein. Ein voller, langsamer
oder ausgefallener Logpfad darf Safety-Tasks nicht blockieren. Safety- und
Security-Ereignisse dürfen nicht durch Debugfluten verdrängt werden. Kritische
Auditdaten MÜSSEN manipulationsnachweisbar, zugriffsgeschützt und gemäß der
produktbezogenen Aufbewahrungsregel behandelbar sein.

Crashdumps und Diagnosedaten MÜSSEN Datenschutz, Speichergrenzen und
Geheimnisschutz berücksichtigen. Ein Diagnoseverlust wird sichtbar gemeldet;
er darf nicht als gesunder Zustand gelten.

## Safety-orientierte Bedienoberfläche

Ein Absturz, Deadlock oder Ressourcenfehler des grafischen Desktops DARF die
Safety-Funktion nicht anhalten. Alarmierung und sicherer Zustandswechsel
MÜSSEN unabhängig vom Komfort-UI möglich bleiben.

Die Bedienoberfläche MUSS:

- veraltete, simulierte, geschätzte und ungültige Werte eindeutig markieren,
- Degradation, Redundanzverlust und ausstehende Bestätigung sichtbar machen,
- kritische Eingaben gegen versehentliche Wiederholung und Mehrdeutigkeit
  schützen,
- den tatsächlichen Systemzustand anzeigen statt nur den angeforderten,
- kritische Aktionen, Bedieneridentität und Ergebnis auditieren,
- bei Grafikfehlern einen getesteten, ressourcenreservierten Ersatzkanal
  anbieten, sofern die Hazard-Analyse eine Bedienmöglichkeit fordert.

Optische Gestaltung oder flüssige Animation darf nie Vorrang vor
Alarmzustellung, Aktualitätsanzeige oder Bedienbarkeit unter Degradation haben.

## Security, Boot- und Updatekette

### Security by Design

Für jede freigegebene Konfiguration MUSS ein versioniertes Threat-Modell
existieren. Es umfasst mindestens Secure Boot und Update, lokale und entfernte
Schnittstellen, Diagnosezugänge, Identitäten, Schlüssel, Datenabfluss,
Ressourcenerschöpfung und manipulierte Peripherie.

Es gelten Least Privilege, Deny by Default, minimale Angriffsfläche,
vollständige Eingabevalidierung, getrennte Rollen und widerrufbare
Berechtigungen. Entwicklungs- und Wartungszugänge DÜRFEN in einem
Produktionsimage weder versteckt noch mit Standardzugangsdaten aktiv sein.

### Vertrauenswürdiger Start

Die Vertrauenskette MUSS – soweit die freigegebene Plattform dies technisch
ermöglicht – unveränderlichen Vertrauensankern folgend Bootstufen, Kernel,
Systemprogramme, Konfiguration und Recovery-Image authentisieren. Ein
Verifikationsfehler führt in einen definierten Recovery- oder sicheren Zustand
und darf nicht stillschweigend fortgesetzt werden.

Schlüssel müssen getrennt nach Entwicklung und Produktion verwaltet,
rotierbar, widerrufbar und gegen Verlust abgesichert sein. Rollbackschutz darf
eine autorisierte Notfallwiederherstellung nicht unmöglich machen; beide Pfade
MÜSSEN gemeinsam entworfen und getestet werden.

### Lieferkette und reproduzierbarer Build

Jedes Release MUSS besitzen:

- eindeutige Quell-, Konfigurations- und Artefaktidentitäten,
- eine vollständige SBOM einschließlich Compiler, Linker, Generatoren,
  Bibliotheken, Firmware und eingebetteter Binärartefakte,
- gepinnte und überprüfte Abhängigkeiten,
- einen dokumentierten, möglichst hermetischen und reproduzierbaren Build,
- Provenienz und Signaturen für freigegebene Artefakte,
- dokumentierte bekannte Schwachstellen und ihre Risikobehandlung,
- getrennte Rechte für Erzeugen, Prüfen und Signieren.

Eine unabhängige Reproduktion SOLL denselben relevanten Binärinhalt ergeben.
Nicht reproduzierbare Bestandteile benötigen eine begründete Ausnahme,
gesicherte Originalwerkzeuge und zusätzliche Integritätsnachweise.

## Langlebigkeit und Wartbarkeit

Der erwartete Produkt- und Supportzeitraum MUSS produktspezifisch festgelegt
werden. Für diesen Zeitraum sind vorzuhalten:

- archivierte Quellen, Buildrezepte, Werkzeuge, Abhängigkeiten und
  Konfigurationen,
- stabile, versionierte ABI-, IPC-, Boot- und Datenformate mit
  Migrationsstrategie,
- Hardwareabstraktion und ein Plan für Bauteil-, Firmware- und
  Toolchain-Abkündigungen,
- Schlüsselrotation, Recoverymedien und Wiederherstellungsanweisungen,
- Ersatzteil-, Medienalterungs-, Batteriesicherungs- und Datenscrubbingkonzept,
- Verfahren für Schwachstellenannahme, Feldbeobachtung und Sicherheitsupdates,
- regelmäßige Wiederaufbau-, Restore- und Hardwareersatzproben.

Zeitüberläufe, Zählerwrap, RTC-Verlust, Schaltsekunden, Zeitsynchronisations-
und lange Offlinephasen MÜSSEN entsprechend ihrer Relevanz getestet werden.
Ein Toolchain-, Hardware- oder Abhängigkeitswechsel ist eine kontrollierte
Änderung mit Requalifizierungsanalyse, kein rein technisches Upgrade.

## Verifikation und belastbare Evidenz

### Evidenzkette

Für jede Safety-Anforderung MUSS eine bidirektionale, versionierte
Rückverfolgbarkeit bestehen:

```text
Hazard -> Safety-Anforderung -> Architektur -> Implementierung
       -> Verifikation -> Ergebnis/Evidenz -> freigegebene Konfiguration
```

Tests ohne zugeordnete Anforderung und Anforderungen ohne bestandenes,
eindeutiges Akzeptanzkriterium sind kein Abschlussnachweis. Evidenzartefakte
MÜSSEN unveränderlich referenziert und mit Werkzeug-, Build-, Hardware- und
Konfigurationsversion gespeichert werden.

### Verifikationsumfang

Je nach Kritikalität sind mindestens anzuwenden:

- Review von Anforderungen, Architektur, Code und Testfällen,
- Compilerwarnungen als Fehler sowie statische Analyse nach dokumentiertem
  Regelwerk,
- Unit-, Komponenten-, Integrations-, System- und Hardware-in-the-Loop-Tests,
- Grenzwert-, Zustandsautomaten-, Nebenläufigkeits- und Fuzztests,
- Messung von Stack, Heap, Interruptlatenz, WCET-nahen Pfaden und Reserven,
- Abdeckungsanalyse mit vorab festgelegter Begründung und Schließung
  unerklärter Lücken,
- unabhängige Verifikation sicherheitskritischer Mechanismen,
- Prüfung, ob formale Spezifikation oder Modellprüfung für kleine kritische
  Zustandsautomaten, Protokolle und Invarianten erforderlich ist.

Eine hohe Codeabdeckung ersetzt weder richtige Anforderungen noch
Fehlerwirkungsanalyse. Flaky Tests DÜRFEN NICHT ignoriert oder blind wiederholt
werden; sie gelten bis zur Ursachenklärung als Fehler im Produkt oder
Testsystem.

### Fault Injection

Fault-Injection MUSS mindestens die als relevant identifizierten Fälle aus
folgenden Klassen abdecken:

- Speicherbitfehler, Allokationsfehler, Erschöpfung und Schutzverletzung,
- hängende, langsame, abstürzende oder fehlerhaft antwortende Dienste,
- verlorene, duplizierte, umgeordnete und beschädigte IPC-/Netzwerkdaten,
- Timerstillstand, Zeitsprung, Interruptverlust und Interruptsturm,
- Massenspeicherfehler, voller Datenträger und unterbrochener Schreibvorgang,
- Stromverlust und Brownout in Boot-, Commit-, Failover- und Updatephasen,
- inkonsistente Sensoren, Aktoren oder redundante Replikate,
- beschädigte, falsche, abgelaufene oder unvollständige Updates,
- Verlust einer Redundanzeinheit sowie Common-Cause-Szenarien.

Erfolgsmaß ist nicht nur das Erkennen des Fehlers, sondern das Einhalten der
FTTI, das Containment, der richtige Ausgang, der Audittrail und die
anschließende sichere Reintegration.

### Soak- und Dauertest

Dauertests MÜSSEN den vorgesehenen Betriebszeitraum, Spitzenlasten,
wiederholte Recovery, Ressourcenzyklen und Alterungseffekte mit einer
begründeten Sicherheitsmarge abbilden. Akzeptanzkriterien werden vor Testbeginn
festgelegt. Es dürfen keine unerklärte Drift, Lecks, Deadlineverletzungen,
stille Datenabweichungen oder ungeplanten Resets verbleiben.

## Änderungssteuerung und Incident-Verfahren

Jede Änderung an Quellcode, Toolchain, Konfiguration, Hardwareannahme,
Testsystem oder Dokumentation MUSS vor dem Merge klassifiziert werden. Die
Änderung enthält:

- Zweck und betroffene Anforderungen/Hazards/Threats,
- Safety-, Security-, Timing-, Ressourcen- und Kompatibilitätsauswirkung,
- Rückwärts-, Rollback- und Datenmigrationsbetrachtung,
- erforderliche Regression und neue Fault-Injection-Fälle,
- Reviewer und erforderliche unabhängige Freigabe,
- aktualisierte Traceability und reproduzierbare Evidenz.

Notfalländerungen dürfen den Mindestschutz aus Authentisierung, Integrität,
Vier-Augen-Freigabe, begrenztem Rollout, Monitoring und Rollback nicht
umgehen. Nach jeder Störung werden Ursache, Erkennungsverzug,
Containmentwirkung, Recovery, mögliche Common-Cause-Folgen und Prävention
ohne Schuldzuweisung analysiert. Korrekturen MÜSSEN in Anforderungen, Tests
und Hazard-/Threat-Register zurückfließen.

## Definition of Done

Eine sicherheitsrelevante Änderung ist nur „fertig“, wenn alle zutreffenden
Punkte nachweislich erfüllt sind:

- [ ] Systemgrenze, Zweck und Kritikalität sind festgelegt.
- [ ] Hazard- und Threat-Einträge einschließlich sicherem Zustand und FTTI
      sind aktualisiert.
- [ ] Anforderungen besitzen eindeutige, messbare Akzeptanzkriterien.
- [ ] Fault-Containment-, Ressourcen-, Timing- und Recoveryverträge sind
      dokumentiert.
- [ ] Fehler- und Rollbackpfade wurden ebenso wie der Normalpfad implementiert.
- [ ] Alle externen Eingaben, Größen, Zeiten, Zustände und Rechte werden
      validiert.
- [ ] Code-, Architektur- und Testreview sind abgeschlossen; erforderliche
      Unabhängigkeit ist dokumentiert.
- [ ] Statische Analyse, Build, Unit-, Integrations- und Systemregression sind
      ohne ungeklärte Abweichung bestanden.
- [ ] Relevante Fault-Injection-, Power-Loss-, Überlast-, Failover- und
      Reintegrationstests sind bestanden.
- [ ] Worst-Case-Zeiten und Ressourcenreserven liegen innerhalb des Vertrags.
- [ ] Logs, Alarme, Audit und Datenschutz sind geprüft.
- [ ] SBOM, reproduzierbarer Build, Signatur und Artefaktprovenienz sind
      aktualisiert.
- [ ] Traceability ist vollständig und die Evidenz unveränderlich archiviert.
- [ ] Offene Restrisiken sind ausdrücklich akzeptiert; es gibt keine
      stillschweigend vertagten Safety-Fehler.

Ein Release ist zusätzlich nur freigabefähig, wenn die exakte
Produktkonfiguration, Recoverymedien, Betriebsanweisungen und Rollbackprobe
bestanden sind. „Funktioniert in QEMU“ oder „Tests grün“ allein genügt nicht.

## Verbotene Muster

Folgende Muster sind in Safety-relevanten Komponenten unzulässig:

- ungeprüfte Rückgabewerte, Pointer, Längen, Integeroperationen oder
  Hardwarezustände,
- stiller Fallback, der ungültige oder veraltete Daten als korrekt darstellt,
- unbegrenzte Schleifen, Retries, Warteschlangen, Locks oder Allokationen,
- verdeckte globale veränderliche Zustände und undokumentierte
  Lockreihenfolgen,
- Fehlerbehandlung nur durch Panic, Reset oder Watchdog ohne nachgewiesenen
  sicheren Ausgang,
- identische Redundanz ohne Common-Cause-Betrachtung,
- Heartbeat gleichzusetzen mit funktionaler Korrektheit,
- automatische gleichzeitige Updates aller redundanten Einheiten,
- Reparatur oder Reintegration ohne Fencing, Selbsttest und Zustandsprüfung,
- unsignierte Images, ungepinnte Buildabhängigkeiten oder nicht
  nachvollziehbare Binärartefakte,
- Debug-Backdoors, Standardpasswörter oder produktive Sicherheitsumgehungen,
- dynamische Konfigurationsänderungen ohne Authentisierung, Validierung,
  Audit und Rollback,
- direkte unbeschränkte Hardware-, DMA- oder Kernelspeicherzugriffe aus
  nicht minimalen Komponenten,
- Behandlung von Diagnoseverlust, Redundanzverlust oder unbekanntem Zustand
  als Normalbetrieb,
- Abnahme aufgrund von Mittelwerten, Einzelboots oder ausschließlich
  emulatorbasierten Tests.

## Phasenweiser Übergang des bestehenden OS

Die Umstellung erfolgt in kontrollierten Gates. Security, Traceability und
Hazardpflege beginnen in Phase 0 und werden nicht bis zum Ende verschoben.

### Phase 0 – Baseline und Safety Scope

- exakten Istzustand, Annahmen und bekannte Lücken inventarisieren,
- Produkt-/Systemgrenze und erste Safety-Funktionen festlegen,
- Hazard-, Threat-, Anforderungs- und Evidenzregister einführen,
- reproduzierbare Referenzbuilds und unveränderliche Testartefakte schaffen,
- Safety-Claims bis zum Nachweis ausdrücklich sperren.

Gate: Jede neue Arbeit ist einem Risiko, einer Anforderung und einem Testziel
zuordenbar.

### Phase 1 – Fail-closed Fundament

- Boot-, Speicher-, Prozess-, Syscall- und Persistenzpfade vollständig
  validieren,
- Guardpages, Schutzrechte, Stack-/Heapgrenzen und kontrollierte Panicpfade
  stärken,
- unabhängigen Hardwarewatchdog, Crashrecord und Boot-Selbsttests einführen,
- Fehlercodes und Gesundheitszustände vereinheitlichen.

Gate: Fehler führen reproduzierbar zu Containment oder definiertem sicheren
Zustand statt zu stiller Korruption.

### Phase 2 – Determinismus und Reservationsmodell

- prioritäts- und zeitbewussten Schedulervertrag festlegen,
- CPU-, Speicher-, IPC- und I/O-Reservierungen mit Admission Control bauen,
- ungebundene Operationen aus Safety-Pfaden entfernen,
- Worst-Case-Latenzen und Degradationsgrenzen messen.

Gate: Ressourcen- und Zeitverträge bleiben auch bei Überlast und injizierten
Fehlern eingehalten.

### Phase 3 – Isolierte Dienste und Supervision

- komplexe Treiber und Dienste in eigene Fehlereindämmungsregionen verlagern,
- versionierte IPC, Capability-/Rechtemodell und Quoten etablieren,
- Supervisor, Restart-Budgets, Zustandsprüfung und sicheren Ersatzkanal
  implementieren,
- GUI und Diagnose vollständig von Safety-Ausführung entkoppeln.

Gate: Der Ausfall eines einzelnen isolierbaren Dienstes wird innerhalb der
FTTI beherrscht, ohne unkontrollierte Auswirkung auf andere Funktionen.

### Phase 4 – Redundanz und unterbrechungsarme Wartung

- funktionsbezogene Redundanz und Common-Cause-Gegenmaßnahmen implementieren,
- Fencing, Failover, Replikationsprüfung und sichere Reintegration bauen,
- transaktionales A/B-Update, Recovery-Image und Rolling Update einführen,
- Power-Loss- und Split-Brain-Szenarien vollständig testen.

Gate: Reparatur und Update erhalten die erforderliche Safety-Funktion und
Mindest-Redundanz durchgehend.

### Phase 5 – Supply Chain und Lebenszyklus

- vollständige Vertrauenskette, Schlüsselverwaltung und signierte Releases,
- SBOM, gepinnte Toolchain und unabhängigen reproduzierbaren Build,
- Langzeitarchiv, Hardwaremigration, Feldmonitoring und Restoreproben,
- geregeltes Vulnerability- und Incident-Management.

Gate: Jedes Feldartefakt ist authentisch, reproduzierbar, inventarisiert und
kontrolliert wiederherstellbar.

### Phase 6 – Systemnachweis und externe Einordnung

- vollständige Traceability und Safety-/Security-Argumentation schließen,
- Fault-Injection-, Soak-, HIL- und reale Hardwarematrix abnehmen,
- Betriebs-, Wartungs-, Update- und Notfallverfahren validieren,
- unabhängige fachliche und regulatorische Prüfung der konkreten
  Produktkonfiguration durchführen.

Gate: Erst nach erfolgreicher externer Einordnung darf eine Eignungs- oder
Konformitätsaussage für das konkrete Medizinprodukt erwogen werden.

## Informative regulatorische Ausgangsbasis

Diese Liste dient ausschließlich der Planung. Anwendbarkeit, Ausgabe,
Produktklasse, nationale Umsetzung sowie Kollateral- und Particular Standards
MÜSSEN für den konkreten Intended Purpose durch Regulatory Affairs und die
zuständigen externen Stellen bestätigt werden.

- [IEC 62304:2006+A1:2015](https://webstore.iec.ch/en/publication/22794)
  beschreibt den Software-Lebenszyklus für Medizinproduktsoftware. Die
  Software-Sicherheitsklasse folgt aus Produktkontext und Risikoanalyse; sie
  ist weder EU-MDR-Klasse noch FDA-Geräteklasse oder SIL.
- [ISO 14971:2019](https://www.iso.org/standard/72704.html) ist die zentrale
  Basis für kontinuierliches Medizinprodukt-Risikomanagement über den gesamten
  Lebenszyklus.
- [IEC 81001-5-1:2021](https://webstore.iec.ch/en/publication/63293) mit der
  Interpretation von 2025 adressiert den Security-Lebenszyklus von
  Gesundheitssoftware. Security wird als Voraussetzung für Safety behandelt.
- Die [EU-MDR 2017/745](https://eur-lex.europa.eu/eli/reg/2017/745/oj) verlangt
  bei anwendbaren Produkten unter anderem kontinuierliches Risikomanagement,
  Software-Lebenszyklus, Informationssicherheit sowie Verifikation und
  Validierung. Ein generisches OS ist nicht allein durch dieses Zielbild ein
  Medizinprodukt.
- Die seit 2. Februar 2026 wirksame
  [FDA Quality Management System Regulation](https://www.fda.gov/medical-devices/postmarket-requirements-devices/quality-management-system-regulation-qmsr)
  inkorporiert ISO 13485:2016 für betroffene Hersteller fertiger Geräte.
- Die aktuelle
  [FDA-Cybersecurity-Guidance vom Februar 2026](https://www.fda.gov/regulatory-information/search-fda-guidance-documents/cybersecurity-medical-devices-quality-management-system-considerations-and-content-premarket)
  behandelt Secure Product Development, Updates, Schwachstellenmanagement und
  SBOM über den Produktlebenszyklus; gesetzliche Pflichten für Cyber Devices
  sind zusätzlich produktbezogen zu prüfen.
- [IEC 60601-1 Ed. 3.2](https://webstore.iec.ch/en/publication/67497) und
  zutreffende Kollateral-/Particular Standards sind relevant, wenn das
  Endprodukt ein medizinisches elektrisches Gerät oder System ist. Der
  sichere, degradierte oder fail-operationale Zustand folgt aus wesentlicher
  Leistung und Gefahrenanalyse, nicht aus einer pauschalen Forderung an das OS.
- Für sicherheitsbezogene Bedienung und Alarme sind je Produkt insbesondere
  [IEC 62366-1](https://webstore.iec.ch/en/publication/21863) und gegebenenfalls
  IEC 60601-1-8 zu bewerten.

IEC 62304, ISO 14971, IEC 81001-5-1, ISO 13485 und IEC 60601-1 decken
unterschiedliche Ebenen ab. Keine einzelne Norm und kein grüner Testlauf
beweist die Sicherheit oder Zulässigkeit des vollständigen Geräts.

## Normative Einordnung wird separat validiert

Die Zuordnung dieses Vertrags zu anwendbaren medizinischen, funktionalen
Safety-, Security-, Qualitätsmanagement-, Usability- und
Risikomanagementnormen wird separat durch qualifizierte Fachstellen validiert.
Dieses Dokument erfindet keine Normanforderungen, ersetzt keine regulatorische
Beratung und behauptet keine Konformität. Normfassungen, regionale
Anwendbarkeit, Produktklassifizierung, geforderte Unabhängigkeit und konkrete
Nachweisformate MÜSSEN vor einer Produktfreigabe ausdrücklich bestätigt und
versioniert werden.
