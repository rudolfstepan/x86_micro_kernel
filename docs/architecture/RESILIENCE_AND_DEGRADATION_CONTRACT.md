# REIST Resilienzversprechen und Degradierungsvertrag

Stand: normativer Zielvertrag, 20. August 2026

Dieses Dokument legt systemweit fest, was Resilienz in REIST OS bedeutet, wie
Komponentenfehler eingegrenzt werden und welcher terminale Zustand nach einem
erschöpften Recoverybudget folgt. Subsystemverträge dürfen diese Regeln
verschärfen, aber nicht abschwächen.

Der aktuelle 32-Bit-Kernel befindet sich noch auf dem Weg vom modularen
Monolithen zu diesem Microkernel-Modell. Der Vertrag ist daher Ziel- und
Abnahmevorgabe, keine Behauptung, dass bereits jeder vorhandene Treiber die
beschriebene Isolation nachgewiesen hat.

## Resilienzversprechen

Für Fehler innerhalb einer deklarierten Ring-3-Fehlerdomäne verspricht REIST:

1. Der Fehler wird durch Exception, Validierung, Quote oder Fortschrittsfrist
   erkannt.
2. Die betroffene Generation wird isoliert und ihre Ausgaben werden vor der
   Recovery eingezäunt.
3. Capabilities, IRQ-Zustellung, IPC-Endpunkte und Ressourcenbesitz dieser
   Generation werden idempotent entzogen.
4. Microkernel und unabhängige Fehlerdomänen behalten nachweisbar Fortschritt.
5. Recovery läuft nur innerhalb fester Zeit-, Versuchs-, CPU- und
   Speicherbudgets.
6. Ein Ersatz wird erst nach Selbsttest, Abhängigkeitsprüfung und Erzeugung
   einer frischen Generation veröffentlicht.
7. Ein erschöpftes Budget führt in den profildefinierten degradierten oder
   sicheren Zustand und niemals in eine endlose Neustartschleife.

Ein Prozessabsturz ist damit ein erwartetes Komponentenereignis und kein Grund
für Kernel-Panic oder vollständigen Rechnerneustart. Dies gilt für Programme,
Systemdienste, Dateisysteme, Protokollstacks, GUI-Komponenten und Gerätetreiber,
sobald deren Ring-3-Migration die vorgeschriebenen Fault-Injection-Tests
bestanden hat.

## Geltungsbereich und ehrliche Grenzen

Das Versprechen umfasst Softwarefehler und begrenzte Gerätefehler, für welche
das gewählte Plattformprofil einen nachgewiesenen Isolationsmechanismus besitzt.
Es macht gemeinsam genutzte CPU, RAM, Stromversorgung, Takt oder Busse nicht zu
unabhängiger Redundanz. Unbekannte Kernelkorruption, Double Fault oder Verlust
einer gemeinsamen Hardwarebasis machen den aktuellen Rechnerkanal
unvertrauenswürdig und führen über Fencing und Watchdog in den kontrollierten
Recoverypfad.

Ring 3 allein begrenzt beliebiges DMA nicht. Eine qualifizierte IOMMU muss das
Gerät auf seine zugewiesene Domäne beschränken. Ohne IOMMU muss der Microkernel
jede DMA-Abbildung und jeden Deskriptor, der Systemspeicher erreichen kann,
besitzen und validieren; andernfalls lehnt das Assurance-Profil dieses Gerät
ab. REIST behauptet keine Treiberisolation, solange diese Bedingung unbewiesen
ist.

## Komponentenvertrag

Jede überwachte Komponente besitzt ein unveränderliches Startprofil mit:

- stabiler Komponenten- und Dienst-ID,
- Prozessgeneration und Capability-Satz,
- expliziten Abhängigkeiten und Startreihenfolge,
- CPU-, Speicher-, Thread-, Handle-, Queue- und I/O-Quoten,
- Fortschrittsfrist sowie Health- und Selbsttestprotokoll,
- Neustartanzahl, gesamter Recoveryfrist und optionalem Backoff,
- Kritikalitätsklasse, Fallback und terminaler Degradierungsstufe,
- Fencing- und Gerätereset-Aktionen,
- reservierten Ressourcen für mindestens einen überwachten Neustart.

Normale Prozesse können diese Restartreserve nicht verbrauchen. Verspätete
Nachrichten, IRQ-Benachrichtigungen und Handles einer alten Generation bleiben
ungültig, auch wenn IDs oder Speicheradressen erneut verwendet werden.

## Health- und Lebenszyklus

Der gemeinsame Zustandswortschatz lautet:

```text
STARTING -> HEALTHY -> DEGRADED
                  \-> ISOLATED -> RECOVERING -> HEALTHY
                                      \-> FAILED -> SAFE_STATE
```

`UNKNOWN` wird wie `FAILED` behandelt. Ein Heartbeat allein beweist keine
Gesundheit; der Supervisor verlangt zusätzlich begrenzte Fortschrittsmarken,
gültige Ergebnisse, Quoteneinhaltung und gesunde Abhängigkeiten.

Crash, User-Exception, Hang, ungültige Antwort, Quotenverletzung und
Gerätetimeout starten dieselbe Recoverytransaktion:

```text
erkennen
-> Komponentengeneration isolieren
-> gefährliche oder extern sichtbare Ausgaben einzäunen
-> Capabilities entziehen und IRQ-/IPC-Publikation stoppen
-> Prozess beenden und begrenzte Ressourcen zurückholen
-> Gerät zurücksetzen oder Dienst neu erzeugen
-> Selbsttest ausführen
-> Abhängigkeiten und Zustand validieren
-> frische Generation veröffentlichen
```

Jeder Übergang besitzt eine monotone Frist und einen terminalen Fehler. Cleanup
und Fencing sind idempotent, sodass ein zweiter Fehler während der Recovery
keinen veralteten Zustand erneut autorisiert.

## Automatische und manuelle Recovery

Automatischer Neustart ist nur erlaubt, solange Versuchs- und Gesamtzeitbudget
des Profils verbleiben. Er bildet niemals eine unbegrenzte Crashschleife.

Manuelle Anforderungen `down`, `up` und `restart` durchlaufen dieselbe
Supervisortransaktion. Administration darf eine Komponente gezielt neu starten
oder isoliert lassen, aber Fencing, Generationsentzug, Abhängigkeitsprüfung,
Selbsttest und terminale Safety-Regel nicht umgehen. Manuelle Eingriffe sind
diagnostizierbar und löschen die automatische Fehlerhistorie nicht stillschweigend.

## Kritikalitätsklassen

Vor dem Start erhält jede Komponente genau eine Konsequenzklasse:

| Klasse | Beispiel | Recoverybudget erschöpft | Systemreaktion |
| --- | --- | --- | --- |
| Optional | Audioausgabe, Editor | Komponente bleibt isoliert | Weiterbetrieb mit Status `DEGRADED` |
| Erforderlich | primäre GUI, Netzwerkdienst | validierten Fallback aktivieren | nur im erklärten reduzierten Umfang weiterarbeiten |
| Essenziell | profilspezifische Safety-Funktion | lokaler Verlust nicht tolerierbar | gefährliche Ausgaben sperren und `SAFE_STATE` oder unabhängigen Handover auslösen |

Die Beispiele sind Standardwerte. Ein konkretes Einsatzprofil darf dasselbe
Subsystem anders klassifizieren.

Das erste [Audiosubsystem](AUDIO_SUBSYSTEM.md) ist eine konkrete optionale
Komponente: HDA-Treiber und PCM-Service sind getrennte Ring-3-Fehlerdomänen.
Nach erschöpftem Restartbudget werden Geräte- und Streamgenerationen entzogen;
Shell, Desktop und Storage laufen im Zustand `DEGRADED` weiter.

## Systemweite Degradierungsstufen

REIST verwendet eine monotone Leiter abnehmender Funktionalität:

| Stufe | Bedeutung | Zulässige Aktion |
| --- | --- | --- |
| `FULL` | alle ausgewählten Dienste gesund | Normalbetrieb |
| `DEGRADED` | optionale oder redundante Funktion verloren | mit expliziter Diagnose und begrenzter Recovery weiterarbeiten |
| `ESSENTIAL` | nur profildefinierte Essential Functions verfügbar | nicht essenzielle Arbeit ablehnen und Restartreserve schützen |
| `SAFE` | normaler Auftrag kann nicht sicher fortgesetzt werden | gefährliche Ausgaben sperren, Diagnose erhalten, kontrollierte Recovery abwarten |
| `HALT` | keine vertrauenswürdige lokale Ausführung verbleibt | nur wenn ein unabhängiger Kanal den sicheren Zustand bereits hält |

Der Übergang zu weniger Funktion verlangt keine optimistische Annahme. Die
Rückkehr Richtung `FULL` verlangt Selbsttest, Zustandsabgleich und ausdrückliche
Reintegration durch den Supervisor; verstrichene Zeit allein löscht keine
Degradation.

## Persistenter und extern sichtbarer Zustand

Ein Neustart darf Teilarbeit nicht in akzeptierte Arbeit verwandeln.
Persistenter oder gefährlicher Zustand folgt `alt -> Kandidat -> validieren ->
atomar committen -> neu`. Recovery verwendet die letzte validierte Generation
und protokolliert fehlgeschlagene Generation, Ursache, Fencing-Ergebnis,
Versuche und terminalen Zustand in begrenzter Diagnose.

Kann eine Ausgabe nicht entzogen oder rückgelesen werden, muss das Subsystem
diese Grenze deklarieren und in den sicheren Zustand eskalieren, statt eine
erfolgreiche Eingrenzung zu behaupten.

## Abnahmenachweis

Ein Subsystem erfüllt dieses Resilienzversprechen erst, wenn das Zielprofil
mindestens Folgendes demonstriert:

- absichtlicher Crash und ungültiger Speicherzugriff bleiben im Prozess,
- ein Hang wird über Fortschrittsfrist erkannt,
- ungültige Antwort und alte Generation werden abgewiesen,
- Ausgaben werden vor dem Neustart eingezäunt,
- Ressourcen werden auch nach partieller Initialisierung begrenzt freigegeben,
- Neustart gelingt aus reservierter Kapazität,
- fehlgeschlagener Selbsttest und erschöpftes Budget erreichen den erklärten
  terminalen Zustand,
- unabhängige Prozesse und Microkernel behalten während der Injection
  Fortschritt,
- Gerätetreiber besitzen IOMMU-Isolation oder einen dokumentierten Nachweis
  vollständig vermittelten DMAs.

Quellcodeplatzierung, Normalpfad-Unit-Tests und eine erfolgreiche Demo sind
notwendig, aber nicht ausreichend. Runtime-Fault-Injection und Evidenz auf dem
Zielprofil entscheiden, ob das Versprechen tatsächlich erfüllt ist.
