# REIST High-Assurance Core Contract

Stand: 13. August 2026

REIST OS ist ein **High-Assurance Research Operating System for
Fault-Tolerant and Fail-Operational Computing**. Dieser Vertrag definiert die
produkt- und branchenunabhängigen Regeln des generischen Kerns. Er ist keine
Zertifizierung und keine Behauptung, dass der heutige Forschungsstand bereits
fail-operational ist.

## Architekturmodell

```text
REIST OS
├── Generic High-Assurance Core
├── Medical reference profile
├── Spacecraft reference profile
├── Industrial-control reference profile
└── Experimental FPGA profile
```

Profile dürfen Anforderungen verschärfen, aber keine Kerninvariante lockern.
Jedes konkrete System legt sein Einsatzprofil, seine Essential Functions,
Gefahren, sicheren und degradierten Zustände sowie FTTI, RTO und RPO separat
fest. Anforderungen aus Medizin, Raumfahrt oder Industrie gelten nur, wenn
das entsprechende Profil für ein konkretes Zielsystem ausgewählt wurde.

## Verbindliche Kernprinzipien

1. **Fehler als Normalfall:** Jeder externe Eingang, jedes Gerät und jeder
   Dienst kann ausfallen, hängen oder inkonsistente Daten liefern.
2. **Begrenzung vor Reparatur:** `Detect -> Contain -> Recover -> Validate ->
   Reintegrate`; gelingt dies nicht innerhalb des Budgets, folgt `Degrade ->
   Safe State -> Controlled Restart`.
3. **Unabhängige Fehlerdomänen:** Redundanz auf demselben Kernel, derselben CPU,
   demselben RAM oder derselben Versorgung ist keine unabhängige Redundanz.
4. **Fail-closed Steuerung:** Gefährliche Ausgänge werden vor Diagnose,
   Restart oder Reintegration gesperrt und rücklesbar verifiziert.
5. **Gebundene Laufzeit:** Safety-Pfade besitzen feste Speicher-, CPU-, Queue-,
   Lock-, I/O- und Zeitbudgets; unbegrenztes Warten ist verboten.
6. **Keine Fortsetzung nach unbekannter Kernkorruption:** Ein beschädigter
   Rechnerkanal wird eingezäunt und ersetzt oder kontrolliert neu gestartet.
7. **Nachweisbarkeit:** Anforderung, Gefahr, Kontrolle, Code, Test und Ergebnis
   sind bidirektional rückverfolgbar; bekannte Abweichungen bleiben sichtbar.
8. **Langlebigkeit:** Formate und ABI sind versioniert; Updates sind signiert,
   atomar, stromausfallsicher und rückfallfähig; Zeit- und Zählerüberläufe
   werden über die vorgesehene Lebensdauer geprüft.

## Gefahrenregister und Traceability

Das maschinenlesbare Register [`safety/hazards.toml`](../../safety/hazards.toml)
verwendet Schema v1. Jeder Eintrag besitzt eine eindeutige ID, Schweregrad,
positive FTTI, definierten sicheren Zustand, konkrete Kontrollen,
Verifikationspfade und sichtbar verbleibendes Restrisiko. Der Validator
`scripts/validate_hazard_register.py` lehnt unbekannte Versionen, doppelte IDs,
unsichere Pfade und fehlende Evidenz fail-closed ab. Der Registerstatus bleibt
`partial`, bis alle Kern-, Geräte- und ausgewählten Profilgefahren erfasst und
die Testergebnisse automatisiert an eine Release-Baseline gebunden sind.

## Ressourcenregister

Das versionierte Register
[`safety/resource_budgets.toml`](../../safety/resource_budgets.toml) bindet
statische Ressourcenobergrenzen an ihre autoritativen C-Makros und konkrete
Verifikationstests. `scripts/validate_resource_budgets.py` wertet keine
beliebigen Ausdrücke aus, sondern nur einen begrenzten ganzzahligen
Konstantenvertrag. Quellcode-Drift, doppelte IDs oder Symbole, Pfadflucht und
fehlende Evidenz führen zum Fehler. Der Status bleibt `partial`, bis zusätzlich
Laufzeit-High-Water-Marken aller kritischen Pools sowie Zielhardware-WCET-,
Callgraph- und vollständige Speicherbudgets nachgewiesen sind. IPC und der
Storage-Request-Pool liefern bereits versionierte, saturierende Belegungs- und
Erschöpfungsdiagnostik. Die Memory-Statistik v2 ergänzt Frame-/Heap-Peaks und
saturierende Allokationsfehler bei kompatiblem v1-Präfix. Taskslots besitzen
eine versionierte, allokationsfreie Momentaufnahme mit Kapazität, Supervisor-
Reserve, aktueller/maximaler Belegung und saturierenden Ablehnungen; sie scannt
höchstens `MAX_TASKS` Einträge. Deterministische Heap-/Frame-ENOMEM-Injection
ist ausschließlich in einem getrennten Testimage enthalten. Sie prüft
fehlgeschlagene Erst- und Teilallokationen auf exakte Rückgewinnung und darf in
keinem Produktionsprofil aktiv sein.

Headerabhängigkeiten sind Teil der Build-Evidenz. Jeder Kernel-C-Compile muss
eine explizite Dependency-Datei erzeugen; fehlende, falsche oder nicht zum
Quellobjekt gehörende Evidenz verhindert den Link. Damit darf eine ABI- oder
Strukturänderung keinen inkrementellen Mischbuild aus alten und neuen
Objektlayouts erzeugen.

## Ziel-Failure-Domains

Der aktuelle Kernel ist noch ein modularer Monolith. Das zentrale
Architektur-Gate ist daher nicht ein weiterer Gerätetreiber, sondern die
Verkleinerung der privilegierten Trusted Computing Base:

```text
Heute                         Ziel

kernel.bin                    REIST Safety Kernel
├── Scheduler                 ├── MMU / Scheduler / IPC
├── Memory                    ├── Capabilities
├── VFS / Filesystems         └── Supervisor primitives
├── Network                         ^       ^       ^
├── Drivers                         IPC     IPC     IPC
└── Supervisor                       |       |       |
                                  Storage Network Drivers
                                   Domain  Domain  Domain
```

Dateisystem, Netzwerk, GUI und komplexe Treiber müssen schrittweise in eigene
Adressräume mit Capability-beschränktem IPC, festen Quoten, Health-Vertrag,
Restartbudget und validierter Reintegration migrieren. Erst danach kann REIST
nachweisen, dass die absichtliche Zerstörung eines Dienstes seine Failure
Domain nicht verlässt.

## Quantitative Leitfrage

Für jede Release-Baseline wird nicht nur gefragt, ob Tests bestehen, sondern:

> Wie viel von REIST OS kann gezielt gestört oder zerstört werden, bevor die
> für das gewählte Profil definierten Essential Functions verloren gehen?

Die Antwort wird mit Fault-Injection, Fehlerabdeckung, FTTI, maximaler
Degradationsdauer, Recovery-Erfolgsrate und Common-Cause-Analyse gemessen.

## Safety Profiles

- Das **Medical reference profile** ordnet den Core-Regeln medizinische
  Safety-, Security-, QMS- und regulatorische Evidenz zu. Es ist optional und
  keine klinische Freigabe.
- Das **Spacecraft reference profile** ergänzt Strahlungs-/SEU-Modelle,
  autonome Recovery, Telemetrie, lange Kommunikationsausfälle und
  missionsphasenspezifische Safe Modes.
- Das **Industrial-control reference profile** ergänzt deterministische I/O,
  Anlagen-Interlocks, funktionale Sicherheit und kontrollierte Degradation.
- Das **Experimental FPGA profile** dient reproduzierbaren Hardware-Faults,
  diversitären Watchdogs, ECC-Injektion und beobachtbaren Recovery-Versuchen.

Profile werden in eigenen Dokumenten gepflegt. Keine Profilklassifikation
wird automatisch auf eine andere Branche oder ein anderes Produkt übertragen.
