# External Safety Monitor Contract

Stand: 23. August 2026. Status: **automatisierte S0.2-QEMU/VMware-Baseline
abgenommen; physisches Profil unbound**.

## Zweck und Abgrenzung

Dieser Vertrag friert die Schnittstelle und die physische Abnahme für den noch
fehlenden unabhängigen Zielhardware-Watchdog ein. Er qualifiziert kein Gerät.
Das aktuelle Profil
[`safety/external_safety_monitor.toml`](../../safety/external_safety_monitor.toml)
steht deshalb auf `unbound`.

Die automatisierte Projektabnahme ist bewusst auf QEMU und VMware begrenzt.
Sie prüft Stack-/Exception-Containment, den emulierten IB700-Watchdog sowie
VMware-Boot und überwachte Diensterholung mit festen Fristen. Reale Hardware
prüft der Benutzer manuell. Diese Trennung schließt S0.2 für die automatisierte
Forschungsbaseline, qualifiziert aber weder Monitor noch Zielhardware.

Der QEMU-IB700-Watchdog, Hostmodelle, ein zweiter Hostprozess, ein
CPU-interner Timer sowie ein Watchdog im gleichen ungeprüften Strom- oder
Taktdomänenfehler sind wichtige Entwicklungsnachweise, aber kein Beleg für die
hier verlangte physische Unabhängigkeit. Auch ein gesendetes Kommando oder die
Bestätigung „Watchdog armed“ beweist keinen sicheren Ausgangszustand.

## Systemgrenze

Ein später ausgewählter Monitor muss außerhalb der zu überwachenden
CPU-/Kernel-Fehlerdomäne liegen und mindestens besitzen:

- eigene, nachgewiesene Strom- und Zeitbasis;
- einen vom Zielprozessor unabhängigen Reset-Ausgang;
- einen latched, bei Monitor-Stromverlust sicheren Interlock-Ausgang;
- eine getrennte elektrische Rückleseprüfung des tatsächlichen
  Interlock-Zustands;
- einen festen Transport mit begrenztem Frameformat und festen Fristen.

Das Interlock muss die vom konkreten Gefahrenmodell benannten gefährlichen
Ausgänge elektrisch sperren. Netzwerk-TX- oder Storage-Softwarelatches allein
sind kein Ersatz. `HALT` ist erst zulässig, wenn dieser unabhängige Kanal den
sicheren Zustand nachweislich hält.

## Zustandsmodell

```text
UNBOUND -> SELECTED -> QUALIFIED
   ^           |            |
   +-----------+------------+
        Änderung oder Evidenzverlust
```

- `unbound`: nur der unveränderliche Vertrag ist vorhanden; keine
  Hardwareaussage.
- `selected`: Ziel, Monitor, Firmware und Transport sind eindeutig gebunden;
  die vollständige physische Kampagne fehlt noch.
- `qualified`: alle maschinengeprüften Identitäten, Unabhängigkeitsmerkmale,
  Fristen und gehashten physischen Szenarien sind vorhanden und bestanden.

Eine Hardware- oder Firmwareänderung erzeugt eine neue Profilidentität und
setzt den Status mindestens auf `selected` zurück. Evidenz wird nie still auf
eine andere Revision übertragen.

## FTTI- und Reaktionsvertrag

Für die generische Forschungsbaseline gilt derzeit eine FTTI von 1.000 ms.
Die maschinenlesbare Aufteilung ist:

| Anteil | Maximum |
|---|---:|
| Heartbeatverlust erkennen | 300 ms |
| Fence anwenden | 100 ms |
| elektrischen Zustand rücklesen | 100 ms |
| Zielreset anfordern/bestätigen | 500 ms |

Die Summe darf die Gefahr-FTTI nicht überschreiten. Der Zielkanal sendet einen
Heartbeat höchstens alle 100 ms und ausschließlich nach nachgewiesenem
Scheduler-/Systemfortschritt. Timerinterrupts allein gelten nicht als
Fortschritt. Bei Heartbeat- oder Transportverlust gilt:

```text
latch safe outputs -> verify electrical readback -> assert target reset
-> preserve fence -> withhold reintegration authority
```

Readback- oder Resetfehler öffnen das Fence niemals. Nach unbekannter
Kernelkorruption findet keine In-Place-Reparatur statt.

## Framevertrag `reist.external-monitor/1`

Der konkrete Transport darf später ergänzt werden; die Semantik bleibt
append-only. Version 1 verwendet ausschließlich 32-Byte-Frames mit `RMON`-
Magic, Version, Typ, Größe, 64-Bit-Bootnonce, 64-Bit-Sequenz, Payload und
CRC32C. Zulässige Typen sind `ARM`, `HEARTBEAT`, `FENCE`, `STATUS` und
`RESET`.

Der Empfänger verwirft vor jeder Zustandsänderung:

- falsche Version, Größe, Magic oder CRC32C;
- unbekannte Typen;
- wiederholte oder nicht streng frische Sequenzen;
- Frames aus einer alten Bootnonce;
- Antworten, die nicht zur aktuellen Anforderung gehören.

CRC32C schützt gegen zufällige Übertragungsfehler und ist keine
Authentisierung gegen einen bösartigen Teilnehmer. Falls das konkrete
Einsatzprofil einen feindlichen Transport annimmt, muss der spätere
Zielhardwareadapter einen authentisierten, anti-replay-geschützten Transport
ergänzen und versionieren.

## Fence und Readback

Der Monitor trennt vier Aussagen strikt:

1. Fence-Anforderung empfangen.
2. Ausgangstreiber in Safe State geschaltet.
3. Safe State über einen unabhängigen elektrischen Sense-Pfad rückgelesen.
4. Zielreset ausgelöst beziehungsweise dessen Ausbleiben erkannt.

Nur Punkt 3 bestätigt das Fence. Ein Echo desselben Ausgangsregisters, eine
Softwarevariable, ein Transport-Ack oder ein Watchdogstatus erfüllen den
Readbackvertrag nicht. Das Fence bleibt bis Power-Cycle oder einem für das
gewählte Profil authentisierten Reset latched. Monitor-Strom- oder Taktverlust
muss die Ausgänge passiv in den definierten sicheren Zustand führen.

## Physische Abnahme

`qualified` verlangt mindestens drei Wiederholungen jedes Szenarios auf genau
der gebundenen Ziel-/Monitor-/Firmwarekombination:

- Heartbeatverlust und festhängende Ziel-CPU;
- Transportverlust;
- beschädigtes und wiederholtes Frame;
- defekter Fence-Treiber;
- Readback-Abweichung;
- fehlgeschlagener Zielreset;
- Verlust der Monitor-Stromdomäne.

Jedes Szenario besitzt einen menschenlesbaren Bericht und ein Rohlog im
Repository. Beide Dateien sind SHA-256-gebunden. Der Validator akzeptiert als
`kind` ausschließlich `physical-target`; Source-Pattern, Hostmodell und
Emulator-only-Evidenz können die Kampagne ergänzen, aber nie ersetzen.

## Manuelle Zielhardware-Integration

Erst nach Auswahl real verfügbarer Hardware werden Backend, Firmware,
Transport und Testadapter implementiert. Die manuelle Zielhardware-Abnahme
muss dann zusätzlich beweisen:

- Heartbeats sind an validierten Fortschritt gekoppelt und alle I/O-Wartepfade
  besitzen monotone Deadline plus feste Pollgrenze;
- der Fatalpfad benötigt weder Heap, VFS, formatierte Ausgabe noch
  blockierende Locks;
- ungültige oder stale Frames bewirken vor Seiteneffekten eine Ablehnung;
- Fence und Readback bleiben bei Wiederholung idempotent;
- QEMU IB700 bleibt als Emulatorprofil getrennt und wird nicht als
  Zielhardwareevidenz umetikettiert;
- Real-Hardware-Build, physische Kampagne, Crashrecord-Recovery und
  kontrollierte Reintegration bestehen auf dem gebundenen Ziel.

S0.2 ist damit ausschließlich für die automatisierte QEMU/VMware-
Forschungsbaseline abgeschlossen. Das Gesamtgate S0 bleibt wegen der
nachfolgenden S0.3c- bis S0.6-Arbeit offen. Ohne eine `qualified` gebundene
physische Kampagne gibt es weiterhin weder Zielhardwarefreigabe noch
Fail-operational-Claim.
